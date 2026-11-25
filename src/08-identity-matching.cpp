// ---------------------------------------------------------------------------
// Button identity matching
// ---------------------------------------------------------------------------

using TaskListButton_get_IsRunning_t = HRESULT(WINAPI*)(void* pThis,
                                                        bool* running);
TaskListButton_get_IsRunning_t TaskListButton_get_IsRunning_Original;

bool TaskListButton_IsRunning(FrameworkElement taskListButtonElement) {
    if (!TaskListButton_get_IsRunning_Original || !taskListButtonElement) {
        return false;
    }
    bool isRunning = false;
    HRESULT hr = TaskListButton_get_IsRunning_Original(
        winrt::get_abi(
            taskListButtonElement.as<winrt::Windows::Foundation::IUnknown>()),
        &isRunning);
    return SUCCEEDED(hr) && isRunning;
}

// IsRunning, plus a short grace so Alt-Tab flicker does not drop glows.
// Running states read for the first time (new buttons) or again after a
// desktop / lock switch reset them. Summed per UI thread and logged once by
// the next full pass: a lock or unlock used to log one "Identity state"
// line per button (about 20 per switch, capture
// manual-20261125-211736-720767).
struct RunningReread {
    unsigned running = 0;
    unsigned stopped = 0;
};
thread_local RunningReread t_runningReread;

// Virtual-desktop switches clear observedRunning so pinned-not-running icons
// on another desktop never keep a highlight. Call this on the UI thread
// before AppAppearsOnTaskbar / rank eligibility.
bool ButtonCountsAsRunning(FrameworkElement button) {
    const bool running = TaskListButton_IsRunning(button);
    const ULONGLONG now = GetTickCount64();
    void* id = InspectableIdentity(button);
    bool grace = false;
    {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_buttonStateMutex);
        ButtonState* state = FindButtonStateLocked(button, false);
        if (!state) {
            return running;
        }
        auto& e = *state;
        if (e.runningUnknown) {
            e.runningUnknown = false;
            ++(running ? t_runningReread.running : t_runningReread.stopped);
        } else if (running != e.observedRunning) {
            LOG_DEFERRED(L"Identity state: button=%p running=%d->%d hwnd=%p pid=%u appId=%s",
                   id, e.observedRunning, running, e.sampleHwnd, e.samplePid,
                   e.appIdUpper.c_str());
        }
        if (running) {
            e.lastRunningTick = now;
            e.observedRunning = true;
            return true;
        }
        e.observedRunning = false;
        // Grace protects a transient state change, not a closed app. Check
        // the whole group: the sampled window might close before its siblings.
        bool liveWindow = HwndMatchesStoredPid(e.sampleHwnd, e.samplePid);
        for (const auto& window : e.groupWindows) {
            if (HwndMatchesStoredPid(window.hwnd, window.pid)) {
                liveWindow = true;
                break;
            }
        }
        if (!liveWindow) {
            e.lastRunningTick = 0;
        }
        grace = e.lastRunningTick != 0 &&
                now - e.lastRunningTick < kIsRunningGraceMs;
    }
    if (grace) {
        // A close animation can report not-running before HWND destruction.
        // Ensure another pass happens even if Explorer sends no further UVS.
        ScheduleRefreshAllHighlights(button);
    }
    // Grace preserves rank eligibility only. A stopped button must never
    // keep painting merely because its closing HWND is still alive.
    return false;
}

std::wstring GetButtonAutomationName(FrameworkElement button) {
    try {
        return std::wstring(
            Automation::AutomationProperties::GetName(button).c_str());
    } catch (...) {
        return {};
    }
}

// "Window: 0x31426" on an uncombined button (Never Combine) names its window,
// not an app; see GetButtonAutomationWindow.
bool IsWindowAutomationId(const std::wstring& id) {
    return id.size() > 7 && _wcsnicmp(id.c_str(), L"Window:", 7) == 0;
}

// Uncombined taskbar buttons (Never Combine / combine when full) carry their
// window in AutomationProperties.AutomationId: "Window: 0x<hwnd hex>"; group
// buttons carry "Appid: <AUMID>" (UWPSpy dump 2026-11-15, two VS Code
// windows). Explorer's identifier, not a display name. Callers must still
// check the HWND against the group's windows.
HWND GetButtonAutomationWindow(FrameworkElement button) {
    if (!button) {
        return nullptr;
    }
    try {
        std::wstring id =
            Automation::AutomationProperties::GetAutomationId(button).c_str();
        if (!IsWindowAutomationId(id)) {
            return nullptr;
        }
        const wchar_t* p = id.c_str() + 7;
        while (*p == L' ' || *p == L'\t') {
            ++p;
        }
        wchar_t* end = nullptr;
        const unsigned long long value = wcstoull(p, &end, 16);
        if (!value || end == p) {
            return nullptr;
        }
        HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(value));
        return IsWindow(hwnd) ? hwnd : nullptr;
    } catch (...) {
        return nullptr;
    }
}

// Explorer AppId equality, plus per-window slots for uncombined buttons.
// Same rank may bind many buttons (secondary taskbar / Never Combine).
bool IdentityMatchesRank(const ButtonIdentity& ident,
                         const AppFocusInfo& info) {
    // Per-window slot: only the button that lists exactly this window (an
    // uncombined button lists just its own). No AppId match, or every
    // button of the app would light up (Never Combine, 2026-11-15).
    if (info.perWindow) {
        if (!info.lastHwnd || !info.lastPid ||
            !HwndMatchesStoredPid(info.lastHwnd, info.lastPid)) {
            return false;
        }
        for (const auto& g : ident.groupWindows) {
            if (g.hwnd == info.lastHwnd && g.pid == info.lastPid) {
                return true;
            }
        }
        return ident.sampleHwnd == info.lastHwnd &&
               ident.samplePid == info.lastPid;
    }
    // One taskbar button = one app = one Explorer AppId.
    if (ident.appIdUpper.empty() ||
        AppIdFromAppKey(info.key) != ident.appIdUpper) {
        return false;
    }
    // Uncombined buttons (Never Combine) each stand for one window. An
    // app-level rank (window not confirmed on its own yet) lights only the
    // button of the app's last focused window, not every window of the app
    // (user report 2026-11-16: all Calculator buttons lit after switching
    // to Never Combine).
    if (ident.uncombined) {
        return info.lastHwnd && info.lastPid &&
               HwndMatchesStoredPid(info.lastHwnd, info.lastPid) &&
               ident.sampleHwnd == info.lastHwnd;
    }
    return true;
}

// Best rank for a single button (1-based), or 0.
int FindRankForButton(FrameworkElement button,
                      const std::vector<AppFocusInfo>& ranks) {
    if (!button || ranks.empty()) {
        return 0;
    }
    if (!ButtonCountsAsRunning(button)) {
        return 0;
    }

    const ButtonIdentity ident = GetCachedButtonIdentity(button);
    for (size_t i = 0; i < ranks.size(); i++) {
        if (IdentityMatchesRank(ident, ranks[i])) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

