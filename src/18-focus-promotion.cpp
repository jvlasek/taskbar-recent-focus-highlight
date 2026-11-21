// ---------------------------------------------------------------------------
// Focus promotion (min focus time)
// ---------------------------------------------------------------------------

void CancelMinFocusTimer() {
    DisarmHookTimer(kMinFocusTimerId);
}

void CancelPreviewMinFocusTimer() {
    DisarmHookTimer(kPreviewMinFocusTimerId);
}

// True if pending is still the focused app. May retarget to a new top-level
// window of the same PID. False if focus left the process.
bool StillPendingForeground(const PendingFocus& pending,
                            HWND* outHwnd,
                            std::wstring* outTitle) {
    // Same mapping as the focus path (child -> root, owned dialog -> owner),
    // so an owned dialog keeps its owner's pending focus alive. Raw HWND
    // here made a Lister dialog (no AUMID) look like another app after
    // 0.11.4/0.11.5, and its owner was never confirmed.
    HWND foreground = NormalizeFocusHwnd(GetForegroundWindow());
    HWND confirmHwnd = pending.hwnd;
    std::wstring title = pending.windowTitle;
    if (!foreground || foreground != pending.hwnd) {
        DWORD fgPid = 0;
        if (foreground) {
            GetWindowThreadProcessId(foreground, &fgPid);
        }
        if (!foreground || fgPid != pending.processId) {
            return false;
        }
        // Desktop/tray share explorer.exe with folder windows. Same PID is
        // not enough — those HWNDs must take the transient-wait path.
        if (IsTransientForeground(foreground)) {
            return false;
        }
        // Same PID is not the same app when Explorer puts the windows on
        // different taskbar buttons (UWP hosts; TC main window vs Lister).
        {
            const std::wstring fgId = LookupWindowAppId(foreground);
            std::wstring pendingId = AppIdFromAppKey(pending.key);
            if (pendingId.empty()) {
                pendingId = LookupWindowAppId(pending.hwnd);
            }
            if (fgId.empty() || fgId != pendingId) {
                return false;
            }
        }
        confirmHwnd = foreground;
        std::wstring t = GetWindowTitle(foreground);
        if (!t.empty()) {
            title = std::move(t);
        }
    }
    if (!confirmHwnd || !IsWindow(confirmHwnd)) {
        return false;
    }
    if (outHwnd) {
        *outHwnd = confirmHwnd;
    }
    if (outTitle) {
        *outTitle = std::move(title);
    }
    return true;
}

// The app and preview min-focus confirmations are one state machine (Fable
// review 4.1): the deadline, the still-foreground check with the bounded
// Alt-Tab grace, then a track-specific confirm. A track names its timer,
// its seconds, its start tick and its retry field.
struct MinFocusTrack {
    bool preview = false;
    UINT_PTR timerId = 0;
    int seconds = 0;
    ULONGLONG start = 0;
    ULONGLONG PendingFocus::*retryStart = nullptr;
    PCWSTR logName = L"";
};

MinFocusTrack AppTrack(const PendingFocus& p, const Settings& s) {
    return {false, kMinFocusTimerId, s.minFocusSeconds, p.focusStartTick,
            &PendingFocus::transientRetryStartTick, L"Min-focus timer"};
}

MinFocusTrack PreviewTrack(const PendingFocus& p, const Settings& s) {
    return {true, kPreviewMinFocusTimerId, s.previewMinFocusSeconds,
            p.previewStartTick ? p.previewStartTick : p.focusStartTick,
            &PendingFocus::previewTransientRetryStartTick,
            L"Preview min-focus"};
}

// Copies the pending focus. False if there is none, or if it is excluded
// (then it is dropped).
bool TakePendingForConfirm(PendingFocus& pending) {
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_pendingFocus.valid) {
            return false;
        }
        pending = g_pendingFocus;
    }
    std::wstring pendingAppId = pending.appIdUpper;
    if (pendingAppId.empty() && pending.hwnd) {
        pendingAppId = ExclusionAumid(pending.hwnd);
    }
    if (IsExcludedKey(pending.key, ToUpper(pending.displayName), pendingAppId,
                      pending.pathUpper)) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_pendingFocus.hwnd == pending.hwnd) {
            g_pendingFocus = {};
        }
        return false;
    }
    return true;
}

// Drops the pending focus (app track only: a preview track leaves the app
// timer running).
void DropPendingFor(const MinFocusTrack& track, const PendingFocus& pending) {
    if (track.preview) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_pendingFocus.hwnd == pending.hwnd) {
            g_pendingFocus = {};
        }
    }
    StopDecayTimerIfIdle();
}

// True when the track may confirm now, with the window to confirm (it may
// be another top-level window of the same app). Otherwise the deadline was
// re-armed, Alt-Tab grace continues, or the candidate was dropped.
bool MinFocusDeadlineReached(const MinFocusTrack& track,
                             const PendingFocus& pending,
                             MinFocusConfirmMode mode,
                             HWND* confirmHwnd,
                             std::wstring* title) {
    if (mode == MinFocusConfirmMode::FromTimer) {
        // KillTimer does not dequeue a WM_TIMER already posted.
        const ULONGLONG remaining =
            RemainingDeadlineMs(track.start, track.seconds, GetTickCount64());
        if (remaining > 0) {
            ArmHookTimer(track.timerId, remaining);
            return false;
        }
    }
    if (StillPendingForeground(pending, confirmHwnd, title)) {
        return true;
    }
    if (mode == MinFocusConfirmMode::FromTimer &&
        IsTransientForeground(GetForegroundWindow())) {
        // Alt-Tab / taskbar holds FG briefly: keep the candidate through a
        // bounded grace, then drop.
        ULONGLONG retryStart = pending.*track.retryStart;
        const bool firstWait = retryStart == 0;
        const ULONGLONG delay = TransientRetryDelayMs(
            track.start, track.seconds, GetTickCount64(), retryStart);
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (g_pendingFocus.valid && g_pendingFocus.hwnd == pending.hwnd) {
                g_pendingFocus.*track.retryStart = retryStart;
            }
        }
        if (delay == 0) {
            const ULONGLONG waited =
                retryStart ? GetTickCount64() - retryStart : 0;
            Wh_Log(L"%s: gave up waiting through transient FG for %s "
                   L"(after %llus)",
                   track.logName, pending.displayName.c_str(),
                   static_cast<unsigned long long>(waited / 1000));
            DropPendingFor(track, pending);
            return false;
        }
        ArmHookTimer(track.timerId, delay);
        // Only the start of a wait: it re-polls every 200 ms (bursts of 40
        // lines in capture manual-20261120-154409-086197). It ends in a
        // confirmation or the "gave up" line.
        if (!track.preview && firstWait) {
            Wh_Log(L"Min-focus timer: transient FG, waiting on %s",
                   pending.displayName.c_str());
        }
        return false;
    }
    if (!track.preview) {
        Wh_Log(L"Min-focus timer: focus left %s before confirmation",
               pending.displayName.c_str());
    }
    DropPendingFor(track, pending);
    return false;
}

void OnPreviewMinFocusTimerElapsed(MinFocusConfirmMode mode) {
    auto settings = SettingsSnap();
    if (!settings->previewHighlightEnabled || g_unloading.load()) {
        return;
    }

    PendingFocus pending;
    if (!TakePendingForConfirm(pending)) {
        return;
    }
    const MinFocusTrack track = PreviewTrack(pending, *settings);
    HWND confirmHwnd = nullptr;
    std::wstring title;
    if (!MinFocusDeadlineReached(track, pending, mode, &confirmHwnd, &title)) {
        return;  // re-armed, waiting through Alt-Tab, or focus left
    }

    std::wstring pendingAppId = ExclusionAumid(confirmHwnd);
    if (IsExcludedKey(pending.key, ToUpper(pending.displayName), pendingAppId,
                      pending.pathUpper)) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_pendingFocus.hwnd == pending.hwnd) {
            g_pendingFocus.previewConfirmed = true;
            if (g_pendingFocus.appConfirmed) {
                g_pendingFocus = {};
            }
        }
        return;
    }

    const ULONGLONG now = GetTickCount64();
    pending.desktopId = ConfirmDesktopId(pending.desktopId, confirmHwnd);
    {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& desk =
            InlineIsEqualGUID(pending.desktopId, GUID_NULL)
                ? CurrentDeskLocked()
                : DeskStateLocked(pending.desktopId);
        StampWindowRecencyLocked(desk, confirmHwnd, pending.key, title, now,
                                 pendingAppId, pending.pathUpper);
        if (g_pendingFocus.valid && g_pendingFocus.hwnd == pending.hwnd) {
            g_pendingFocus.previewConfirmed = true;
            if (g_pendingFocus.appConfirmed) {
                g_pendingFocus = {};
            }
        }
        LOG_DEFERRED(L"Preview focus confirmed: hwnd=%p %s title=\"%s\" (map=%zu "
               L"desktop=%s)",
               confirmHwnd, pending.displayName.c_str(), title.c_str(),
               desk.windowFocusMap.size(),
               GuidToLogString(pending.desktopId).c_str());
    }

    EnsureDecayTimerArmed();
    StopDecayTimerIfIdle();
    RequestApplyPreviewVisuals();
}


void OnMinFocusTimerElapsed(MinFocusConfirmMode mode) {
    auto settings = SettingsSnap();
    if (g_unloading.load()) {
        return;
    }
    PendingFocus pending;
    if (!TakePendingForConfirm(pending)) {
        return;
    }
    const MinFocusTrack track = AppTrack(pending, *settings);
    HWND confirmHwnd = nullptr;
    std::wstring title;
    if (!MinFocusDeadlineReached(track, pending, mode, &confirmHwnd, &title)) {
        return;  // re-armed, waiting through Alt-Tab, or dropped
    }
    pending.hwnd = confirmHwnd;
    if (!title.empty()) {
        pending.windowTitle = std::move(title);
    }

    const std::wstring appIdUpper =
        ExclusionAumid(pending.hwnd);
    if (IsExcludedKey(pending.key, ToUpper(pending.displayName), appIdUpper,
                      pending.pathUpper)) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_pendingFocus.hwnd == pending.hwnd) {
            g_pendingFocus = {};
        }
        return;
    }

    const ULONGLONG now = GetTickCount64();
    pending.desktopId = ConfirmDesktopId(pending.desktopId, pending.hwnd);
    {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& desk =
            InlineIsEqualGUID(pending.desktopId, GUID_NULL)
                ? CurrentDeskLocked()
                : DeskStateLocked(pending.desktopId);
        // The window record is the recency; the recompute below folds it
        // into its app once Explorer's AppId for the window is known.
        WindowAppFocus& w = desk.windowAppFocus[pending.hwnd];
        w.displayName = pending.displayName;
        if (!pending.windowTitle.empty()) {
            w.title = pending.windowTitle;
        }
        w.appIdUpper = appIdUpper;
        w.pathUpper = pending.pathUpper;
        w.pid = pending.processId ? pending.processId
                                  : PidFromHwnd(pending.hwnd);
        w.tick = now;

        // Preview min-focus is independent: do not stamp the current HWND
        // here. A window switch just before the app deadline must wait out
        // previewStartTick (clicks too, since 0.11.20).
        if (g_pendingFocus.valid &&
            (g_pendingFocus.hwnd == pending.hwnd ||
             g_pendingFocus.processId == pending.processId)) {
            g_pendingFocus.appConfirmed = true;
            if (!settings->previewHighlightEnabled ||
                g_pendingFocus.previewConfirmed) {
                g_pendingFocus = {};
            }
        }

        RecomputeRanksForDesktopLocked(desk);
        auto rec = desk.windowAppFocus.find(pending.hwnd);
        const std::wstring derived =
            rec == desk.windowAppFocus.end() || rec->second.key.empty()
                ? std::wstring(L"(no button yet)")
                : rec->second.key;
        LOG_DEFERRED(L"Confirmed focus: %s window=%p app=%s (apps=%zu, "
               L"windows=%zu, ranks=%zu, title=\"%s\" desktop=%s)",
               pending.displayName.c_str(), pending.hwnd, derived.c_str(),
               desk.appFocusMap.size(), desk.windowAppFocus.size(),
               desk.rankedApps.size(),
               pending.windowTitle.c_str(),
               GuidToLogString(pending.desktopId).c_str());
    }

    EnsureDecayTimerArmed();

    // The preview track may have given up during a long Alt-Tab while the
    // app track kept waiting; the window is in front again now, so let it
    // confirm too (its deadline has passed: FromTimer re-checks the
    // foreground). Otherwise this visit left no preview recency
    // (state-machines.md, observation 2).
    if (settings->previewHighlightEnabled) {
        bool previewPending = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            previewPending = g_pendingFocus.valid &&
                             g_pendingFocus.appConfirmed &&
                             !g_pendingFocus.previewConfirmed;
        }
        if (previewPending) {
            EnsurePendingPreviewTimer();
        }
    }

    // The UI pass re-binds ranks to buttons; a rank with no button paints
    // nothing but keeps its history.
    RequestApplyVisuals();
}

// Arms the track's timer for the rest of its deadline, or confirms now.
// An already due deadline confirms FromTimer, which still honors the
// Alt-Tab grace; Immediate would drop the candidate while the switcher
// holds foreground.
void ArmOrConfirmTrack(const MinFocusTrack& track,
                       void (*elapsed)(MinFocusConfirmMode)) {
    if (track.seconds <= 0) {
        elapsed(MinFocusConfirmMode::Immediate);
        return;
    }
    const ULONGLONG remaining =
        RemainingDeadlineMs(track.start, track.seconds, GetTickCount64());
    if (remaining == 0) {
        elapsed(MinFocusConfirmMode::FromTimer);
        return;
    }
    ArmHookTimer(track.timerId, remaining);
}

// Keep the app min-focus one-shot alive. A stale WM_TIMER KillTimer's the
// live timer; same-app FOREGROUND used to assume it was still running.
void EnsurePendingAppTimer() {
    PendingFocus pending;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_pendingFocus.valid || g_pendingFocus.appConfirmed) {
            return;
        }
        pending = g_pendingFocus;
    }
    ArmOrConfirmTrack(AppTrack(pending, *SettingsSnap()),
                      OnMinFocusTimerElapsed);
}

void EnsurePendingPreviewTimer() {
    PendingFocus pending;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_pendingFocus.valid || g_pendingFocus.previewConfirmed) {
            return;
        }
        pending = g_pendingFocus;
    }
    auto settings = SettingsSnap();
    if (!settings->previewHighlightEnabled) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_pendingFocus.valid &&
            g_pendingFocus.hwnd == pending.hwnd) {
            g_pendingFocus.previewConfirmed = true;
            if (g_pendingFocus.appConfirmed) {
                g_pendingFocus = {};
            }
        }
        CancelPreviewMinFocusTimer();
        return;
    }
    ArmOrConfirmTrack(PreviewTrack(pending, *settings),
                      OnPreviewMinFocusTimerElapsed);
}

void SchedulePreviewConfirm(bool windowAlreadyTracked) {
    if (!SettingsSnap()->previewHighlightEnabled) {
        return;
    }
    CancelPreviewMinFocusTimer();
    const int previewMin = (std::max)(0, SettingsSnap()->previewMinFocusSeconds);
    if (previewMin <= 0 || windowAlreadyTracked) {
        OnPreviewMinFocusTimerElapsed(MinFocusConfirmMode::Immediate);
        return;
    }
    ArmHookTimer(kPreviewMinFocusTimerId,
                 static_cast<ULONGLONG>(previewMin) * 1000ULL);
}

// ---------------------------------------------------------------------------
// Last use (0.12.1): decay counts from when an app was last *used*, not from
// when it was switched to. While a confirmed window is in front, the decay
// tick moves its recency forward to the last keyboard / mouse input
// (GetLastInputInfo, session-wide); when it loses the foreground it is
// stamped with the last input before that. Walking away stops the input
// time, so everything still ages out from the last input. Before, an app
// used without switching for longer than decayMinutes lost its highlight
// while in use (capture manual-20261120-071227: Windows Sandbox).
// Only a stretch in front in which the window was confirmed counts: a visit
// shorter than the minimum focus refreshes nothing, so it cannot jump ranks.
// Focus thread only.
// ---------------------------------------------------------------------------
HWND g_inFrontHwnd = nullptr;
ULONGLONG g_inFrontSince = 0;

// GetTickCount64 time of the last input in this session, 0 if unknown.
ULONGLONG LastInputTick64(ULONGLONG now) {
    LASTINPUTINFO lii{};
    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) {
        return 0;
    }
    // dwTime is GetTickCount (32-bit); the difference survives wrap.
    const ULONGLONG ago = static_cast<DWORD>(static_cast<DWORD>(now) -
                                              lii.dwTime);
    return ago < now ? now - ago : 0;
}

// What RefreshLastUseLocked moved (for the log).
constexpr unsigned kLastUseMovedApp = 1;
constexpr unsigned kLastUseMovedPreview = 2;

PCWSTR LastUseMovedName(unsigned moved) {
    switch (moved) {
        case kLastUseMovedApp:
            return L"app";
        case kLastUseMovedPreview:
            return L"preview";
        default:
            return L"app+preview";
    }
}

// Requires g_stateMutex. Moves hwnd's app and preview recency forward to
// `at`, only for records confirmed during the current stretch in front
// (tick >= since). Returns the kLastUseMoved* bits; 0 if nothing moved.
// The app record needs the app minimum focus in this stretch; the preview
// one only the preview minimum (often 0 s), so a short visit moves only
// "preview".
unsigned RefreshLastUseLocked(HWND hwnd, ULONGLONG since, ULONGLONG at) {
    unsigned moved = 0;
    for (auto& [id, desk] : g_desktopMaps) {
        (void)id;
        auto w = desk.windowAppFocus.find(hwnd);
        if (w != desk.windowAppFocus.end() && w->second.tick >= since &&
            w->second.tick < at && HwndMatchesStoredPid(hwnd, w->second.pid)) {
            w->second.tick = at;
            moved |= kLastUseMovedApp;
        }
        auto p = desk.windowFocusMap.find(hwnd);
        if (p != desk.windowFocusMap.end() &&
            p->second.lastConfirmedTick >= since &&
            p->second.lastConfirmedTick < at &&
            HwndMatchesStoredPid(hwnd, p->second.pid)) {
            p->second.lastConfirmedTick = at;
            moved |= kLastUseMovedPreview;
        }
    }
    return moved;
}

// A new foreground (any window, transient ones included): stamp the window
// that left with its last use, then start the new stretch.
void NoteForegroundForLastUse(HWND hwnd) {
    if (hwnd == g_inFrontHwnd) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_inFrontHwnd) {
        const ULONGLONG at = LastInputTick64(now);
        if (at) {
            DeferredLogScope logScope;  // logs after unlock
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (const unsigned moved = RefreshLastUseLocked(
                    g_inFrontHwnd, g_inFrontSince, at)) {
                LOG_DEFERRED(L"Last use: hwnd=%p left, used until %llus ago "
                             L"(%s)",
                             g_inFrontHwnd,
                             static_cast<unsigned long long>((now - at) / 1000),
                             LastUseMovedName(moved));
            }
        }
    }
    g_inFrontHwnd = hwnd;
    g_inFrontSince = now;
}

// Decay tick: the confirmed window in front was used up to the last input.
void RefreshInFrontLastUse() {
    HWND fg = NormalizeFocusHwnd(GetForegroundWindow());
    if (!fg || fg != g_inFrontHwnd) {
        return;  // a change the foreground handler has not seen yet
    }
    const ULONGLONG at = LastInputTick64(GetTickCount64());
    if (!at) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    RefreshLastUseLocked(fg, g_inFrontSince, at);
}

void HandleForegroundChanged(HWND hWnd) {
    if (g_unloading.load()) {
        return;
    }
    ProcessImagePathCacheScope pathCacheScope;

    hWnd = NormalizeFocusHwnd(hWnd);
    NoteForegroundForLastUse(hWnd);

    // Normally the registry watch on the focus thread reports switches.
    // Without it, read here, before the transient check: switching to an
    // empty desktop gives the desktop / taskbar foreground, which is
    // transient. Every reader that sees the GUID change runs the full
    // switch steps (state-machines.md, observation 3).
    if (!g_desktopKeyWatchActive && RefreshCurrentDesktopId()) {
        OnVirtualDesktopSwitched(L"foreground");
    }

    if (IsTransientForeground(hWnd)) {
        // Alt-Tab frame, taskbar, desktop, IME. Do not cancel min-focus.
        bool ranksNonEmpty = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            ranksNonEmpty = !CurrentDeskLocked().rankedApps.empty();
        }
        if (ranksNonEmpty) {
            RequestApplyVisualsDebounced();
        }
        return;
    }

    const std::optional<AppIdentity> resolved = ResolveAppIdentity(hWnd);
    if (!resolved) {
        CancelMinFocusTimer();
        CancelPreviewMinFocusTimer();
        bool ranksNonEmpty = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_pendingFocus = {};
            ranksNonEmpty = !CurrentDeskLocked().rankedApps.empty();
        }
        // Still repaint ranks — taskbar active states changed.
        if (ranksNonEmpty) {
            RequestApplyVisualsDebounced();
        }
        return;
    }

    const std::wstring& key = resolved->key;
    const std::wstring& displayName = resolved->displayName;
    const std::wstring& windowTitle = resolved->windowTitle;
    const std::wstring& appIdUpper = resolved->appIdUpper;
    const std::wstring& pathUpper = resolved->pathUpper;
    const DWORD processId = resolved->processId;
    if (resolved->noButton) {
        // Provisional key until a captured button lists this window: ask
        // for a full pass (with a repeater re-seed) so a button that exists
        // is captured now.
        ++g_buttonSeedGeneration;
        RequestApplyVisuals();
    }

    const ULONGLONG now = GetTickCount64();
    const int minSeconds = (std::max)(0, SettingsSnap()->minFocusSeconds);

    bool alreadyTracked = false;
    bool windowAlreadyTracked = false;
    bool ranksNonEmpty = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& desk = CurrentDeskLocked();
        ranksNonEmpty = !desk.rankedApps.empty();
        auto it = desk.appFocusMap.find(key);
        // Decay, not the 30 s prune, ends immediate re-focus. A tick that is
        // already past decayMinutes waits the minimum again.
        if (it != desk.appFocusMap.end() &&
            it->second.lastConfirmedFocusTick != 0) {
            const ULONGLONG decayMs =
                DecayMsFromMinutes(SettingsSnap()->decayMinutes);
            alreadyTracked = !IsTickDecayed(it->second.lastConfirmedFocusTick,
                                            decayMs, now);
        }
        // A window confirmed before whose button is not known yet is
        // tracked too (its record joins the app at the next recompute).
        if (!alreadyTracked) {
            auto w = desk.windowAppFocus.find(hWnd);
            if (w != desk.windowAppFocus.end() &&
                HwndMatchesStoredPid(hWnd, w->second.pid)) {
                alreadyTracked = !IsTickDecayed(
                    w->second.tick,
                    DecayMsFromMinutes(SettingsSnap()->decayMinutes), now);
            }
        }
        // Log title only. Identity is the AppId, not the window title.
        if (it != desk.appFocusMap.end() && !windowTitle.empty()) {
            it->second.lastWindowTitle = windowTitle;
        }
        // Same live-window rule as preview ranking: PID, liveness, decay.
        windowAlreadyTracked = IsWindowRecentForPreviewLocked(desk, hWnd);
        if (windowAlreadyTracked && !windowTitle.empty()) {
            desk.windowFocusMap[hWnd].windowTitle = windowTitle;
        }
    }

    // Always repaint existing ranks on any focus change (Alt-Tab must not
    // leave other ranked icons unstyled until the min-focus timer fires).
    // Debounce: transient FG + landed app would otherwise full-rebind twice.
    if (ranksNonEmpty || alreadyTracked) {
        RequestApplyVisualsDebounced();
    }

    bool sameAppPending = false;
    bool hwndChanged = false;
    GUID deskForLog{};
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        // Same app as the pending focus: same key, or the same window whose
        // button became known meanwhile (WINDOW:<hwnd> -> APPID:), which
        // must not restart the app wait (state-machines.md, observation 1).
        const bool sameApp =
            g_pendingFocus.key == key ||
            (g_pendingFocus.hwnd == hWnd &&
             g_pendingFocus.processId == processId &&
             g_pendingFocus.key.rfind(L"WINDOW:", 0) == 0);
        if (g_pendingFocus.valid && sameApp &&
            InlineIsEqualGUID(g_pendingFocus.desktopId, g_currentDesktopId)) {
            g_pendingFocus.key = key;
            hwndChanged = g_pendingFocus.hwnd != hWnd;
            g_pendingFocus.hwnd = hWnd;
            g_pendingFocus.processId = processId;
            g_pendingFocus.transientRetryStartTick = 0;
            g_pendingFocus.previewTransientRetryStartTick = 0;
            if (!windowTitle.empty()) {
                g_pendingFocus.windowTitle = windowTitle;
            }
            if (!appIdUpper.empty()) {
                g_pendingFocus.appIdUpper = appIdUpper;
            }
            if (!pathUpper.empty()) {
                g_pendingFocus.pathUpper = pathUpper;
            }
            if (hwndChanged) {
                g_pendingFocus.previewStartTick = now;
                g_pendingFocus.previewConfirmed = false;
            }
            sameAppPending = true;
        } else {
            PendingFocus next;
            next.hwnd = hWnd;
            next.processId = processId;
            next.key = key;
            next.displayName = displayName;
            next.windowTitle = windowTitle;
            next.appIdUpper = appIdUpper;
            next.pathUpper = pathUpper;
            next.focusStartTick = now;
            next.previewStartTick = now;
            next.desktopId = g_currentDesktopId;
            next.valid = true;
            g_pendingFocus = std::move(next);
        }
        deskForLog = g_pendingFocus.desktopId;
    }

    if (sameAppPending) {
        // Same app: keep app min-focus timer; re-schedule preview if HWND moved
        // between instances (multi-window VS Code / Terminal).
        if (hwndChanged) {
            SchedulePreviewConfirm(windowAlreadyTracked);
        } else {
            // WinEvent and shell activation can describe the same episode.
            // Preserve its deadline rather than restarting the preview wait.
            EnsurePendingPreviewTimer();
        }
        EnsurePendingAppTimer();
        return;
    }

    CancelMinFocusTimer();
    CancelPreviewMinFocusTimer();

    const bool skipMinFocus = ShouldSkipAppMinFocus(key, alreadyTracked);

    Wh_Log(L"Focus candidate: %s key=%s (minFocus=%ds, tracked=%d, skipMin=%d, "
           L"promote=%s, previewTracked=%d desktop=%s)",
           displayName.c_str(), key.c_str(), minSeconds,
           alreadyTracked ? 1 : 0, skipMinFocus ? 1 : 0,
           PromoteModeName(SettingsSnap()->promoteMode),
           windowAlreadyTracked ? 1 : 0, GuidToLogString(deskForLog).c_str());

    SchedulePreviewConfirm(windowAlreadyTracked);

    if (skipMinFocus) {
        // minFocus=0, or promoteMode allows instant re-focus (tracked map
        // and/or currently highlighted top-N).
        OnMinFocusTimerElapsed(MinFocusConfirmMode::Immediate);
        return;
    }

    ArmHookTimer(kMinFocusTimerId, static_cast<ULONGLONG>(minSeconds) * 1000ULL);
}

void OnDecayTimer() {
    if (g_unloading.load()) {
        return;
    }
    // A tick far past its interval: the machine slept (GetTickCount64 counts
    // sleep, so everything idle that long decays now) or the thread stalled.
    // Before, only the late "Decay recompute" line showed it.
    {
        const ULONGLONG now = GetTickCount64();
        if (g_lastDecayTick &&
            now - g_lastDecayTick > kDecayCheckIntervalMs + 60000) {
            Wh_Log(L"Decay tick late by %llu min (sleep or stall)",
                   static_cast<unsigned long long>(
                       (now - g_lastDecayTick - kDecayCheckIntervalMs) /
                       60000));
        }
        g_lastDecayTick = now;
    }
    std::vector<std::wstring> keysBefore;
    std::vector<std::wstring> keysAfter;
    size_t windowsBefore = 0;
    size_t windowsAfter = 0;
    if (RefreshCurrentDesktopId()) {
        OnVirtualDesktopSwitched(L"decay tick");  // it saw the switch first
    }
    RefreshInFrontLastUse();  // before decay: the app in use is not idle
    {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_stateMutex);
        const auto& beforeApps = CurrentDeskLocked().rankedApps;
        keysBefore.reserve(beforeApps.size());
        for (const auto& app : beforeApps) {
            keysBefore.push_back(app.key);
        }
        windowsBefore = CurrentDeskLocked().windowFocusMap.size();
        // Only the current desktop affects paint; the others are pruned
        // below in separate short holds (Fable review 2.5).
        RecomputeRanksForDesktopLocked(CurrentDeskLocked());
        PruneWindowFocusMapLocked(CurrentDeskLocked());
        const auto& afterApps = CurrentDeskLocked().rankedApps;
        keysAfter.reserve(afterApps.size());
        for (const auto& app : afterApps) {
            keysAfter.push_back(app.key);
        }
        windowsAfter = CurrentDeskLocked().windowFocusMap.size();
    }
    std::vector<GUID> otherDesktops;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (const auto& [id, desk] : g_desktopMaps) {
            (void)desk;
            if (!InlineIsEqualGUID(id, g_currentDesktopId)) {
                otherDesktops.push_back(id);
            }
        }
    }
    for (const GUID& id : otherDesktops) {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_desktopMaps.find(id);
        if (it == g_desktopMaps.end() ||
            InlineIsEqualGUID(id, g_currentDesktopId)) {
            continue;
        }
        RecomputeRanksForDesktopLocked(it->second);
        PruneWindowFocusMapLocked(it->second);
        if (it->second.appFocusMap.empty() &&
            it->second.windowAppFocus.empty() &&
            it->second.windowFocusMap.empty()) {
            g_desktopMaps.erase(it);
        }
    }
    // Same length can still be a different ladder ([A,B,C] -> [A,C,D]).
    // An empty list requests a few more sweeps so a missed clear can retry,
    // then stops: an idle desktop used to run a full pass on every decay
    // tick for hours (capture 2026-11-17, 0.11.39). Focus thread only.
    static int emptySweepsLeft = 0;
    if (keysBefore != keysAfter) {
        emptySweepsLeft = 2;
    }
    const bool retrySweep = keysAfter.empty() && emptySweepsLeft > 0;
    if (retrySweep && keysBefore == keysAfter) {
        --emptySweepsLeft;
    }
    if (keysBefore != keysAfter || retrySweep) {
        if (keysBefore != keysAfter) {
            Wh_Log(L"Decay recompute: ranks %zu -> %zu", keysBefore.size(),
                   keysAfter.size());
        }
        // Ensure overlays are stripped even if some weak refs are stale after
        // sleep — every live button will clear on next UpdateVisualStates too.
        g_pendingOverlaySweep = true;
        RequestApplyVisuals();
    }
    if (windowsAfter != windowsBefore) {
        Wh_Log(L"Preview decay: windows %zu -> %zu", windowsBefore,
               windowsAfter);
        RequestApplyPreviewVisuals();
    }
    StopDecayTimerIfIdle();
}

// HSHELL_WINDOWDESTROYED (Fable review 3.3): forget the window at once
// instead of on the next decay tick, and repaint so a closed app frees its
// icon slot and preview mark without waiting. App recency itself stays
// (other windows of the app, or re-open within decay). The handle is
// already invalid here, so it is matched by value only.
void HandleWindowDestroyed(HWND hwnd) {
    if (g_unloading.load() || !hwnd) {
        return;
    }
    bool known = false;
    bool previewChanged = false;
    bool droppedPending = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (auto& [id, desk] : g_desktopMaps) {
            (void)id;
            if (desk.windowFocusMap.erase(hwnd)) {
                known = true;
                previewChanged = true;
            }
            if (desk.windowAppFocus.erase(hwnd)) {
                known = true;
            }
            for (const auto& [key, info] : desk.appFocusMap) {
                (void)key;
                if (info.lastHwnd == hwnd) {
                    known = true;
                }
            }
        }
        if (g_pendingFocus.valid && g_pendingFocus.hwnd == hwnd) {
            g_pendingFocus = {};
            droppedPending = true;
            known = true;
        }
    }
    ForgetWindowAppId(hwnd);
    if (droppedPending) {
        CancelMinFocusTimer();
        CancelPreviewMinFocusTimer();
    }
    if (!known) {
        return;
    }
    // Debounced: the button's own running state settles a moment later.
    RequestApplyVisualsDebounced();
    if (previewChanged) {
        RequestApplyPreviewVisuals();
    }
}
