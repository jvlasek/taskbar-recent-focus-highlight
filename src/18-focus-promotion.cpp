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
        const ULONGLONG delay = TransientRetryDelayMs(
            track.start, track.seconds, GetTickCount64(), retryStart);
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (g_pendingFocus.valid && g_pendingFocus.hwnd == pending.hwnd) {
                g_pendingFocus.*track.retryStart = retryStart;
            }
        }
        if (delay == 0) {
            Wh_Log(L"%s: gave up waiting through transient FG for %s",
                   track.logName, pending.displayName.c_str());
            DropPendingFor(track, pending);
            return false;
        }
        ArmHookTimer(track.timerId, delay);
        if (!track.preview) {
            Wh_Log(L"Min-focus timer: transient FG, still waiting on %s "
                   L"(%llums left)",
                   pending.displayName.c_str(),
                   static_cast<unsigned long long>(delay));
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

    // A window whose button was captured since focus arrived is recorded
    // under its AppId right away instead of a provisional key.
    if (IsProvisionalAppKey(pending.key)) {
        const std::wstring appId = LookupWindowAppId(pending.hwnd);
        if (!appId.empty()) {
            pending.key = std::wstring(L"APPID:") + appId;
        } else if (mode == MinFocusConfirmMode::FromTimer &&
                   !pending.appIdRetried) {
            // Its button may exist but not be tracked yet (newly realized on
            // this desktop): re-seed in a pass and confirm 300 ms later.
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                if (g_pendingFocus.valid && g_pendingFocus.hwnd == pending.hwnd) {
                    g_pendingFocus.appIdRetried = true;
                }
            }
            ++g_buttonSeedGeneration;
            RequestApplyVisuals();
            ArmHookTimer(kMinFocusTimerId, 300);
            return;
        }
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
        AppFocusInfo& info = desk.appFocusMap[pending.key];
        info.key = pending.key;
        info.displayName = pending.displayName;
        if (!pending.windowTitle.empty()) {
            info.lastWindowTitle = pending.windowTitle;
        }
        info.lastHwnd = pending.hwnd;
        info.lastPid = pending.processId ? pending.processId
                                         : PidFromHwnd(pending.hwnd);
        info.appIdUpper = appIdUpper;
        info.pathUpper = pending.pathUpper;
        info.lastConfirmedFocusTick = now;
        {
            WindowAppFocus& w = desk.windowAppFocus[pending.hwnd];
            w.key = pending.key;
            w.title = pending.windowTitle;
            w.pid = info.lastPid;
            w.tick = now;
        }

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
        LOG_DEFERRED(L"Confirmed focus: %s key=%s (map size=%zu, ranks=%zu, "
               L"title=\"%s\" desktop=%s)",
               pending.displayName.c_str(), pending.key.c_str(),
               desk.appFocusMap.size(), desk.rankedApps.size(),
               pending.windowTitle.c_str(),
               GuidToLogString(pending.desktopId).c_str());
    }

    EnsureDecayTimerArmed();

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

void HandleForegroundChanged(HWND hWnd) {
    if (g_unloading.load()) {
        return;
    }
    ProcessImagePathCacheScope pathCacheScope;

    hWnd = NormalizeFocusHwnd(hWnd);

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

    const bool desktopChanged = RefreshCurrentDesktopId();
    if (desktopChanged) {
        ClearButtonRunningGrace();
        bool droppedPending = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            droppedPending = DropPendingIfWrongDesktopLocked();
        }
        if (droppedPending) {
            CancelMinFocusTimer();
            CancelPreviewMinFocusTimer();
        }
        g_pendingOverlaySweep = true;
        ++g_buttonSeedGeneration;
        g_reseedOnDebouncedPass = true;
        RequestApplyVisualsDebounced();
        RequestApplyVisuals();
        RequestApplyPreviewVisuals();
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
        // Log title only. Identity is path / AUMID, not the window title.
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

        if (g_pendingFocus.valid && g_pendingFocus.key == key &&
            InlineIsEqualGUID(g_pendingFocus.desktopId, g_currentDesktopId)) {
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
    std::vector<std::wstring> keysBefore;
    std::vector<std::wstring> keysAfter;
    size_t windowsBefore = 0;
    size_t windowsAfter = 0;
    RefreshCurrentDesktopId();
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
