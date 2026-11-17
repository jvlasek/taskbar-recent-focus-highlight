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

void OnPreviewMinFocusTimerElapsed(MinFocusConfirmMode mode) {
    auto settings = SettingsSnap();
    if (!settings->previewHighlightEnabled || g_unloading.load()) {
        return;
    }

    PendingFocus pending;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_pendingFocus.valid) {
            return;
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
        return;
    }

    if (mode == MinFocusConfirmMode::FromTimer) {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG start = pending.previewStartTick
                                    ? pending.previewStartTick
                                    : pending.focusStartTick;
        const ULONGLONG remaining = RemainingDeadlineMs(
            start, settings->previewMinFocusSeconds, now);
        if (remaining > 0) {
            ArmHookTimer(kPreviewMinFocusTimerId, remaining);
            return;
        }
    }

    HWND confirmHwnd = nullptr;
    std::wstring title;
    if (!StillPendingForeground(pending, &confirmHwnd, &title)) {
        if (mode == MinFocusConfirmMode::FromTimer &&
            IsTransientForeground(GetForegroundWindow())) {
            const ULONGLONG start = pending.previewStartTick
                                        ? pending.previewStartTick
                                        : pending.focusStartTick;
            const ULONGLONG now = GetTickCount64();
            ULONGLONG retryStart = pending.previewTransientRetryStartTick;
            const ULONGLONG delay = TransientRetryDelayMs(
                start, settings->previewMinFocusSeconds, now, retryStart);
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                if (g_pendingFocus.valid &&
                    g_pendingFocus.hwnd == pending.hwnd) {
                    g_pendingFocus.previewTransientRetryStartTick = retryStart;
                }
            }
            if (delay == 0) {
                Wh_Log(L"Preview min-focus: gave up waiting through transient "
                       L"FG for %s",
                       pending.displayName.c_str());
                return;
            }
            ArmHookTimer(kPreviewMinFocusTimerId, delay);
        }
        // Focus left the app — drop only preview; app timer may still be pending.
        return;
    }

    pendingAppId = ExclusionAumid(confirmHwnd);
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
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_pendingFocus.valid) {
            return;
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
        return;
    }

    if (mode == MinFocusConfirmMode::FromTimer) {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG remaining = RemainingDeadlineMs(
            pending.focusStartTick, settings->minFocusSeconds, now);
        if (remaining > 0) {
            ArmHookTimer(kMinFocusTimerId, remaining);
            return;
        }
    }

    HWND confirmHwnd = nullptr;
    std::wstring title;
    if (!StillPendingForeground(pending, &confirmHwnd, &title)) {
        if (mode == MinFocusConfirmMode::FromTimer &&
            IsTransientForeground(GetForegroundWindow())) {
            // Alt-Tab / taskbar stole FG briefly. WndProc already KillTimer'd;
            // keep the candidate through a bounded grace, then drop.
            const ULONGLONG now = GetTickCount64();
            ULONGLONG retryStart = pending.transientRetryStartTick;
            const ULONGLONG delay = TransientRetryDelayMs(
                pending.focusStartTick, settings->minFocusSeconds, now,
                retryStart);
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                if (g_pendingFocus.valid &&
                    g_pendingFocus.hwnd == pending.hwnd) {
                    g_pendingFocus.transientRetryStartTick = retryStart;
                }
            }
            if (delay == 0) {
                Wh_Log(L"Min-focus timer: gave up waiting through transient "
                       L"FG for %s",
                       pending.displayName.c_str());
                {
                    std::lock_guard<std::mutex> lock(g_stateMutex);
                    if (g_pendingFocus.hwnd == pending.hwnd) {
                        g_pendingFocus = {};
                    }
                }
                StopDecayTimerIfIdle();
                return;
            }
            ArmHookTimer(kMinFocusTimerId, delay);
            Wh_Log(L"Min-focus timer: transient FG, still waiting on %s "
                   L"(%llums left)",
                   pending.displayName.c_str(),
                   static_cast<unsigned long long>(delay));
            return;
        }
        Wh_Log(L"Min-focus timer: focus left %s before confirmation",
               pending.displayName.c_str());
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (g_pendingFocus.hwnd == pending.hwnd) {
                g_pendingFocus = {};
            }
        }
        StopDecayTimerIfIdle();
        return;
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
    auto settings = SettingsSnap();
    if (settings->minFocusSeconds <= 0) {
        OnMinFocusTimerElapsed(MinFocusConfirmMode::Immediate);
        return;
    }
    const ULONGLONG remaining = RemainingDeadlineMs(
        pending.focusStartTick, settings->minFocusSeconds, GetTickCount64());
    if (remaining == 0) {
        // Already due. FromTimer still honors Alt-Tab grace; Immediate
        // would drop the candidate while the switcher holds foreground.
        OnMinFocusTimerElapsed(MinFocusConfirmMode::FromTimer);
        return;
    }
    ArmHookTimer(kMinFocusTimerId, remaining);
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
    const int previewMin = (std::max)(0, settings->previewMinFocusSeconds);
    if (previewMin <= 0) {
        OnPreviewMinFocusTimerElapsed(MinFocusConfirmMode::Immediate);
        return;
    }
    const ULONGLONG start = pending.previewStartTick
                                ? pending.previewStartTick
                                : pending.focusStartTick;
    const ULONGLONG remaining =
        RemainingDeadlineMs(start, previewMin, GetTickCount64());
    if (remaining == 0) {
        // Same as the app timer: a due deadline resumes grace handling.
        OnPreviewMinFocusTimerElapsed(MinFocusConfirmMode::FromTimer);
        return;
    }
    ArmHookTimer(kPreviewMinFocusTimerId, remaining);
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
        RequestApplyVisuals();
        RequestApplyPreviewVisuals();
    }

    std::wstring key;
    std::wstring displayName;
    std::wstring windowTitle;
    std::wstring appIdUpper;
    std::wstring pathUpper;
    DWORD processId = 0;
    bool noButton = false;
    if (!ResolveAppIdentity(hWnd, key, displayName, processId, &windowTitle,
                            &appIdUpper, &pathUpper, &noButton)) {
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

    if (noButton) {
        // Provisional key until a captured button lists this window: ask
        // for a full pass now so a button that exists is captured soon.
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
    // An empty list keeps requesting a sweep so a missed clear can retry.
    if (keysBefore != keysAfter || keysAfter.empty()) {
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

