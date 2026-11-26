// ---------------------------------------------------------------------------
// WinEvent hook thread
// ---------------------------------------------------------------------------

// Focus thread only. False until the Taskbar.View hooks are in (see
// ActivateFocusTracking); a foreground posted before that is ignored.
bool g_focusTrackingActive = false;
void ActivateFocusTracking(HWND hwnd);

// Notification targets can already be stale when the focus worker receives
// them. Background creation/attention is identity information, not recency.
void HandleForegroundNotification(HWND target) {
    if (g_unloading.load() || !target || !g_focusTrackingActive) {
        return;
    }
    HWND foreground = NormalizeFocusHwnd(GetForegroundWindow());
    if (!foreground || NormalizeFocusHwnd(target) != foreground) {
        return;
    }
    HandleForegroundChanged(foreground);
}

// EVENT_SYSTEM_DESKTOPSWITCH fires on the way to the lock screen / UAC and
// back, and the old "lock / UAC" label read the same both ways. Names the
// input desktop now: "Default" means back on the user desktop; one this
// session may not open (the secure Winlogon desktop) is "not accessible".
std::wstring InputDesktopSwitchReason() {
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desk) {
        return L"input desktop: not accessible (lock / UAC)";
    }
    wchar_t name[64] = L"";
    if (!GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name),
                                   nullptr)) {
        name[0] = L'\0';
    }
    CloseDesktop(desk);
    return std::wstring(L"input desktop: ") + (name[0] ? name : L"?");
}

void HandleShellActivation(WPARAM code, HWND target) {
    if (code == HSHELL_WINDOWACTIVATED || code == HSHELL_RUDEAPPACTIVATED) {
        HandleForegroundNotification(target);
    } else if (code == HSHELL_WINDOWDESTROYED) {
        HandleWindowDestroyed(target);
    }
}

void CALLBACK WinEventProc(HWINEVENTHOOK /*hWinEventHook*/,
                           DWORD event,
                           HWND hWnd,
                           LONG idObject,
                           LONG /*idChild*/,
                           DWORD /*dwEventThread*/,
                           DWORD /*dwmsEventTime*/) {
    if (g_unloading.load()) {
        return;
    }
    if (event == EVENT_SYSTEM_DESKTOPSWITCH) {
        PostToHookThread(WM_APP_DESKTOP_SWITCHED);
        return;
    }
    if (event != EVENT_SYSTEM_FOREGROUND) {
        return;
    }
    if (idObject != OBJID_WINDOW || !hWnd) {
        return;
    }

    PostToHookThread(WM_APP_FOREGROUND_CHANGED, reinterpret_cast<WPARAM>(hWnd));
}

// After a settings change: drop the records of newly excluded apps on every
// desktop. Focus thread (WM_APP_SETTINGS_CHANGED): it owns the maps and is an
// STA, which ExclusionAumid (SHGetPropertyStoreForWindow) needs. Windhawk's
// settings thread gives no COM guarantee; an AUMID read failing there
// would let a newly excluded app keep its highlight.
void DropExcludedRecords() {
    size_t dropped = 0;
    std::unordered_set<HWND, HwndHash> windowHwnds;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (auto& [id, desk] : g_desktopMaps) {
            (void)id;
            for (const auto& [hwnd, winfo] : desk.windowFocusMap) {
                (void)winfo;
                if (hwnd) {
                    windowHwnds.insert(hwnd);
                }
            }
            for (const auto& [hwnd, w] : desk.windowAppFocus) {
                (void)w;
                if (hwnd) {
                    windowHwnds.insert(hwnd);
                }
            }
            for (const auto& [key, app] : desk.appFocusMap) {
                (void)key;
                if (app.lastHwnd && app.appIdUpper.empty() &&
                    HwndMatchesStoredPid(app.lastHwnd, app.lastPid)) {
                    windowHwnds.insert(app.lastHwnd);
                }
            }
        }
    }
    std::unordered_map<HWND, std::wstring, HwndHash> windowAppIds;
    for (HWND hwnd : windowHwnds) {
        std::wstring id = ExclusionAumid(hwnd);
        if (!id.empty()) {
            windowAppIds[hwnd] = std::move(id);
        }
    }

    {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (auto& [id, desk] : g_desktopMaps) {
            (void)id;
            for (auto it = desk.appFocusMap.begin();
                 it != desk.appFocusMap.end();) {
                std::wstring displayUpper = ToUpper(it->second.displayName);
                // No AUMID read at focus time (empty exclude list then):
                // the last window's, read now.
                std::wstring appIdUpper = it->second.appIdUpper;
                if (appIdUpper.empty()) {
                    auto found = windowAppIds.find(it->second.lastHwnd);
                    if (found != windowAppIds.end()) {
                        appIdUpper = found->second;
                    }
                }
                if (IsExcludedKey(it->first, displayUpper, appIdUpper,
                                  it->second.pathUpper)) {
                    it = desk.appFocusMap.erase(it);
                    ++dropped;
                } else {
                    ++it;
                }
            }
            // Window records too, or the next recompute would derive the
            // excluded app again.
            for (auto it = desk.windowAppFocus.begin();
                 it != desk.windowAppFocus.end();) {
                auto& w = it->second;
                if (w.appIdUpper.empty()) {
                    auto found = windowAppIds.find(it->first);
                    if (found != windowAppIds.end()) {
                        w.appIdUpper = found->second;
                    }
                }
                if (IsExcludedKey(w.key, ToUpper(w.displayName), w.appIdUpper,
                                  w.pathUpper)) {
                    it = desk.windowAppFocus.erase(it);
                    ++dropped;
                } else {
                    ++it;
                }
            }
            for (auto it = desk.windowFocusMap.begin();
                 it != desk.windowFocusMap.end();) {
                if (it->second.appIdUpper.empty()) {
                    auto found = windowAppIds.find(it->first);
                    if (found != windowAppIds.end()) {
                        it->second.appIdUpper = found->second;
                    }
                }
                std::wstring fileUpper =
                    ToUpper(FileNameFromPath(it->second.pathUpper));
                if (IsExcludedKey(it->second.processKey, fileUpper,
                                  it->second.appIdUpper,
                                  it->second.pathUpper)) {
                    it = desk.windowFocusMap.erase(it);
                    ++dropped;
                } else {
                    ++it;
                }
            }
            PruneWindowFocusMapLocked(desk);
            RecomputeRanksForDesktopLocked(desk);
        }
    }
    if (dropped) {
        Wh_Log(L"Settings: dropped %zu records of excluded apps", dropped);
        RequestApplyVisuals();
        RequestApplyPreviewVisuals();
    }
}

LRESULT CALLBACK HookThreadWndProc(HWND hWnd,
                                   UINT msg,
                                   WPARAM wParam,
                                   LPARAM lParam) {
    if (g_shellHookMessage && msg == g_shellHookMessage) {
        HandleShellActivation(wParam, reinterpret_cast<HWND>(lParam));
        return 0;
    }
    if (g_focusTrackingActive && !g_unloading.load() &&
        AccentChangeMessage(msg, lParam)) {
        SetTimer(hWnd, kAccentRefreshTimerId, kAccentRefreshDelayMs, nullptr);
        // Broadcasts: fall through to DefWindowProc.
    }
    switch (msg) {
        case WM_CLOSE:
            // This hidden notification window is owned by worker shutdown.
            return 0;
        case WM_APP_FOREGROUND_CHANGED:
            HandleForegroundNotification(reinterpret_cast<HWND>(wParam));
            return 0;
        case WM_APP_DESKTOP_SWITCHED:
            OnVirtualDesktopSwitched(InputDesktopSwitchReason().c_str());
            return 0;
        case WM_APP_SHUTDOWN:
            PostQuitMessage(0);
            return 0;
        case WM_APP_TASKBAR_READY:
            ActivateFocusTracking(hWnd);
            return 0;
        case WM_APP_REQUEST_APPLY_DEBOUNCED:
            if (!g_fullRebindTimerArmed) {
                g_fullRebindTimerArmed = true;
                SetTimer(hWnd, kFullRebindTimerId,
                         static_cast<UINT>(kFullRebindDebounceMs), nullptr);
            }
            return 0;
        case WM_APP_REFRESH_ACCENT:
            RefreshCachedAccent();
            RequestApplyVisuals();
            RequestApplyPreviewVisuals();
            return 0;
        case WM_APP_SETTINGS_CHANGED: {
            DropExcludedRecords();
            bool dropPending = false;
            bool keepPending = false;
            PendingFocus pendingSnap;
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                pendingSnap = g_pendingFocus;
            }
            std::wstring pendingAppId = pendingSnap.appIdUpper;
            if (pendingSnap.valid && pendingAppId.empty() && pendingSnap.hwnd) {
                pendingAppId = ExclusionAumid(pendingSnap.hwnd);
            }
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                if (g_pendingFocus.valid) {
                    const std::wstring aumid =
                        !g_pendingFocus.appIdUpper.empty()
                            ? g_pendingFocus.appIdUpper
                            : pendingAppId;
                    if (IsExcludedKey(g_pendingFocus.key,
                                      ToUpper(g_pendingFocus.displayName),
                                      aumid, g_pendingFocus.pathUpper)) {
                        g_pendingFocus = {};
                        dropPending = true;
                    } else {
                        keepPending = true;
                    }
                }
            }
            if (dropPending) {
                CancelMinFocusTimer();
                CancelPreviewMinFocusTimer();
            } else if (keepPending) {
                // Color/thickness/intensity must not disarm an allowed
                // candidate. Re-arm the time still left. A deadline that
                // has already elapsed resumes the timer path, including
                // Alt-Tab grace. KillTimer does not flush a queued WM_TIMER.
                EnsurePendingAppTimer();
                EnsurePendingPreviewTimer();
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == kMinFocusTimerId) {
                KillTimer(hWnd, kMinFocusTimerId);
                OnMinFocusTimerElapsed();
            } else if (wParam == kPreviewMinFocusTimerId) {
                KillTimer(hWnd, kPreviewMinFocusTimerId);
                OnPreviewMinFocusTimerElapsed();
            } else if (wParam == kDecayTimerId) {
                OnDecayTimer();

            } else if (wParam == kAccentRefreshTimerId) {
                KillTimer(hWnd, kAccentRefreshTimerId);
                PostMessage(hWnd, WM_APP_REFRESH_ACCENT, 0, 0);
            } else if (wParam == kFullRebindTimerId) {
                KillTimer(hWnd, kFullRebindTimerId);
                g_fullRebindTimerArmed = false;
                if (g_reseedOnDebouncedPass.exchange(false)) {
                    ++g_buttonSeedGeneration;
                }
                RequestApplyVisuals();
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hWnd, kMinFocusTimerId);
            KillTimer(hWnd, kPreviewMinFocusTimerId);
            KillTimer(hWnd, kDecayTimerId);
            KillTimer(hWnd, kFullRebindTimerId);
            KillTimer(hWnd, kAccentRefreshTimerId);
            g_fullRebindTimerArmed = false;
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Registry watch for virtual-desktop switches (see g_desktopKeyWatchActive).
// One-shot: re-armed after every signal, before the value is read, so no
// write between the two is missed. Closing the key ends the registration.
struct DesktopKeyWatch {
    HKEY key = nullptr;
    HANDLE event = nullptr;

    bool Arm() {
        return key && event &&
               RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
                                       event, TRUE) == ERROR_SUCCESS;
    }
    bool Open() {
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                          L"Explorer\\VirtualDesktops",
                          0, KEY_NOTIFY, &key) != ERROR_SUCCESS) {
            key = nullptr;
            return false;
        }
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return Arm();
    }
    void Close() {
        if (key) {
            RegCloseKey(key);
            key = nullptr;
        }
        if (event) {
            CloseHandle(event);
            event = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Focus tracking starts with the taskbar. Windhawk loads the mod into
// every explorer.exe, also folder-window and COM-server processes that never
// load Taskbar.View.dll. There the worker keeps only its hidden window and
// waits: no shell hook, no WinEvents, no accent refresh, no registry
// watch, and no focus logs (each such process would log the taskbar
// process's focus events again).
// Started by WM_APP_TASKBAR_READY once the Taskbar.View hooks are in, or at
// worker start when they already are. Focus thread only.
// ---------------------------------------------------------------------------
struct FocusTracking {
    bool shellHookRegistered = false;
    HWINEVENTHOOK foregroundHook = nullptr;
    HWINEVENTHOOK desktopHook = nullptr;
    DesktopKeyWatch desktopWatch;
};
FocusTracking g_focusTracking;

void ActivateFocusTracking(HWND hwnd) {
    if (g_focusTrackingActive || g_unloading.load()) {
        return;
    }
    g_focusTrackingActive = true;
    FocusTracking& t = g_focusTracking;

    // Shell notifications require a desktop window. Never show or activate it.
    g_shellHookMessage = RegisterWindowMessageW(L"SHELLHOOK");
    const bool shellHookRegistered =
        g_shellHookMessage && RegisterShellHookWindow(hwnd);
    if (!shellHookRegistered) {
        Wh_Log(L"Shell activation registration failed: %u; using foreground WinEvents only",
               GetLastError());
        g_shellHookMessage = 0;
    } else {
        Wh_Log(L"Shell activation notifications registered (no foreground polling)");
    }
    // Foreground WinEvents
    t.shellHookRegistered = shellHookRegistered;

    // Accent changes: AccentChangeMessage in HookThreadWndProc.
    RefreshCachedAccent();

    t.foregroundHook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!t.foregroundHook) {
        // Init is long over: log it, do not fail.
        if (shellHookRegistered) {
            Wh_Log(L"ERROR: SetWinEventHook failed: %u; focus from shell "
                   L"activations only",
                   GetLastError());
        } else {
            Wh_Log(L"ERROR: SetWinEventHook failed: %u; no focus "
                   L"notifications, nothing will be ranked",
                   GetLastError());
        }
    } else {
        Wh_Log(L"EVENT_SYSTEM_FOREGROUND hook installed");
    }

    t.desktopHook =
        SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH,
                        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!t.desktopHook) {
        Wh_Log(L"EVENT_SYSTEM_DESKTOPSWITCH hook failed: %u", GetLastError());
    } else {
        Wh_Log(L"EVENT_SYSTEM_DESKTOPSWITCH hook installed");
    }

    g_desktopKeyWatchActive = t.desktopWatch.Open();
    if (g_desktopKeyWatchActive) {
        Wh_Log(L"Virtual desktop registry watch installed");
    } else {
        Wh_Log(L"Virtual desktop registry watch failed: %u; reading the "
               L"desktop on each foreground change",
               GetLastError());
        t.desktopWatch.Close();
    }

    RefreshCurrentDesktopId();
    Wh_Log(L"Focus tracking started");

    if (HWND fg = GetForegroundWindow()) {
        PostMessage(hwnd, WM_APP_FOREGROUND_CHANGED,
                    reinterpret_cast<WPARAM>(fg), 0);
    }
}

// Worker exit. Safe when tracking never started.
void DeactivateFocusTracking(HWND hwnd) {
    FocusTracking& t = g_focusTracking;
    t.desktopWatch.Close();
    g_desktopKeyWatchActive = false;
    if (t.foregroundHook) {
        UnhookWinEvent(t.foregroundHook);
        t.foregroundHook = nullptr;
    }
    if (t.desktopHook) {
        UnhookWinEvent(t.desktopHook);
        t.desktopHook = nullptr;
    }
    if (t.shellHookRegistered) {
        DeregisterShellHookWindow(hwnd);
        t.shellHookRegistered = false;
    }
    g_shellHookMessage = 0;
    g_focusTrackingActive = false;
}

DWORD WINAPI WinEventHookThread(LPVOID /*param*/) {
    g_hookThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    g_decayTimerArmed.store(false);
    g_fullRebindTimerArmed = false;
    g_shellHookMessage = 0;

    HMODULE hMod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&HookThreadWndProc),
                            &hMod) ||
        !hMod) {
        Wh_Log(L"GetModuleHandleExW failed for message-window class: %u",
               GetLastError());
        g_hookThreadId.store(0, std::memory_order_release);
        SignalHookThreadReady();
        return 1;
    }

    WNDCLASSEXW wc{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = HookThreadWndProc,
        .hInstance = hMod,
        .lpszClassName = L"Windhawk_TaskbarRecentFocusHighlight_MsgWnd",
    };
    ATOM atom = RegisterClassExW(&wc);
    if (!atom) {
        Wh_Log(L"RegisterClassExW failed: %u", GetLastError());
        g_hookThreadId.store(0, std::memory_order_release);
        SignalHookThreadReady();
        return 1;
    }

    HWND hwnd =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                        L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                        nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        Wh_Log(L"Failed to create message window: %u", GetLastError());
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        g_hookThreadId.store(0, std::memory_order_release);
        SignalHookThreadReady();
        return 1;
    }
    g_hookThreadHwnd.store(hwnd, std::memory_order_release);

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) {
        Wh_Log(L"Focus thread CoInitializeEx failed %08X", coHr);
    }
    SignalHookThreadReady();

    // The window is published above, so a WM_APP_TASKBAR_READY posted from
    // now on arrives; the flag covers hooks installed before this point.
    if (g_taskbarViewHooked.load()) {
        ActivateFocusTracking(hwnd);
    } else {
        Wh_Log(L"Focus tracking waits for the taskbar (Taskbar.View.dll not "
               L"hooked in this process)");
    }

    // Messages (WinEvents, shell hook, timers, posted work) plus the
    // desktop key event.
    DesktopKeyWatch& desktopWatch = g_focusTracking.desktopWatch;
    bool quit = false;
    while (!quit) {
        const DWORD handles = desktopWatch.event ? 1 : 0;
        const DWORD r = MsgWaitForMultipleObjectsEx(
            handles, handles ? &desktopWatch.event : nullptr, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (handles && r == WAIT_OBJECT_0) {
            if (!desktopWatch.Arm()) {
                Wh_Log(L"Virtual desktop registry watch lost; reading the "
                       L"desktop on each foreground change");
                desktopWatch.Close();
                g_desktopKeyWatchActive = false;
            }
            if (!g_unloading.load() && RefreshCurrentDesktopId()) {
                OnVirtualDesktopSwitched(L"registry");
            }
            // Fall through to the messages: a burst of registry writes must
            // not hold WM_APP_SHUTDOWN in the queue.
        } else if (r != WAIT_OBJECT_0 + handles) {
            Wh_Log(L"Focus thread wait failed: %u", GetLastError());
            break;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT ||
                (msg.message == WM_APP && msg.hwnd == nullptr)) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    ReleaseVdm();
    g_decayTimerArmed.store(false);
    g_hookThreadHwnd.store(nullptr, std::memory_order_release);
    DeactivateFocusTracking(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_hookThreadId.store(0, std::memory_order_release);
    if (SUCCEEDED(coHr)) {
        CoUninitialize();
    }

    Wh_Log(L"WinEvent hook thread exiting");
    return 0;
}

bool StartWinEventHookThread() {
    DeferredLogScope logScope;  // logs after unlock
    std::lock_guard<std::mutex> lock(g_winEventHookThreadMutex);
    if (g_winEventHookThread && HookThreadWindow()) {
        return true;
    }
    if (!g_hookThreadReadyEvent) {
        g_hookThreadReadyEvent =
            CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_hookThreadReadyEvent) {
            const DWORD error = GetLastError();
            LOG_DEFERRED(L"CreateEventW failed for hook-thread ready: %u",
                         error);
            return false;
        }
    } else {
        ResetEvent(g_hookThreadReadyEvent);
    }
    HANDLE hThread =
        CreateThread(nullptr, 0, WinEventHookThread, nullptr, 0, nullptr);
    if (!hThread) {
        const DWORD error = GetLastError();
        LOG_DEFERRED(L"CreateThread failed: %u", error);
        if (g_hookThreadReadyEvent) {
            CloseHandle(g_hookThreadReadyEvent);
            g_hookThreadReadyEvent = nullptr;
        }
        return false;
    }
    g_winEventHookThread = hThread;
    WaitForSingleObject(g_hookThreadReadyEvent, INFINITE);
    if (!HookThreadWindow()) {
        LOG_DEFERRED(L"Focus thread failed to create the message window");
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        g_winEventHookThread = nullptr;
        if (g_hookThreadReadyEvent) {
            CloseHandle(g_hookThreadReadyEvent);
            g_hookThreadReadyEvent = nullptr;
        }
        return false;
    }
    LOG_DEFERRED(L"WinEvent hook thread started");
    return true;
}

void StopWinEventHookThread() {
    HANDLE hThread = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_winEventHookThreadMutex);
        hThread = g_winEventHookThread.exchange(nullptr);
    }
    if (!hThread) {
        return;
    }

    HWND hwnd = HookThreadWindow();
    if (hwnd) {
        PostMessage(hwnd, WM_APP_SHUTDOWN, 0, 0);
    }
    DWORD threadId = GetThreadId(hThread);
    if (threadId) {
        PostThreadMessage(threadId, WM_APP, 0, 0);
    }
    const DWORD w = WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    if (g_hookThreadReadyEvent) {
        CloseHandle(g_hookThreadReadyEvent);
        g_hookThreadReadyEvent = nullptr;
    }
    if (w != WAIT_OBJECT_0) {
        Wh_Log(L"ERROR: focus thread wait failed (%u)", w);
    } else {
        Wh_Log(L"WinEvent hook thread stopped");
    }
}

