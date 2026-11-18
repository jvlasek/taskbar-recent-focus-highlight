// ---------------------------------------------------------------------------
// WinEvent hook thread
// ---------------------------------------------------------------------------

// Notification targets can already be stale when the focus worker receives
// them. Background creation/attention is identity information, not recency.
void HandleForegroundNotification(HWND target) {
    if (g_unloading.load() || !target) {
        return;
    }
    HWND foreground = NormalizeFocusHwnd(GetForegroundWindow());
    if (!foreground || NormalizeFocusHwnd(target) != foreground) {
        return;
    }
    HandleForegroundChanged(foreground);
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

LRESULT CALLBACK HookThreadWndProc(HWND hWnd,
                                   UINT msg,
                                   WPARAM wParam,
                                   LPARAM lParam) {
    if (g_shellHookMessage && msg == g_shellHookMessage) {
        HandleShellActivation(wParam, reinterpret_cast<HWND>(lParam));
        return 0;
    }
    switch (msg) {
        case WM_CLOSE:
            // This hidden notification window is owned by worker shutdown.
            return 0;
        case WM_APP_FOREGROUND_CHANGED:
            HandleForegroundNotification(reinterpret_cast<HWND>(wParam));
            return 0;
        case WM_APP_DESKTOP_SWITCHED:
            OnVirtualDesktopSwitched();
            return 0;
        case WM_APP_SHUTDOWN:
            PostQuitMessage(0);
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
            } else if (wParam == kDesktopRecheckTimerId) {
                KillTimer(hWnd, kDesktopRecheckTimerId);
                if (RefreshCurrentDesktopId()) {
                    OnVirtualDesktopSwitched();
                    g_desktopRechecksLeft = 0;
                } else if (--g_desktopRechecksLeft > 0) {
                    SetTimer(hWnd, kDesktopRecheckTimerId,
                             3 * kDesktopRecheckMs, nullptr);
                }
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
            KillTimer(hWnd, kDesktopRecheckTimerId);
            g_fullRebindTimerArmed = false;
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
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
    g_hookThreadHwnd.store(hwnd, std::memory_order_release);

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) {
        Wh_Log(L"Focus thread CoInitializeEx failed %08X", coHr);
    }
    SubscribeAccentChanges();
    RefreshCachedAccent();

    HWINEVENTHOOK hook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!hook) {
        Wh_Log(L"SetWinEventHook failed: %u", GetLastError());
        UnsubscribeAccentChanges();
        ReleaseVdm();
        g_hookThreadHwnd.store(nullptr, std::memory_order_release);
        if (shellHookRegistered)
            DeregisterShellHookWindow(hwnd);
        DestroyWindow(hwnd);
        g_shellHookMessage = 0;
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        g_hookThreadId.store(0, std::memory_order_release);
        if (SUCCEEDED(coHr)) {
            CoUninitialize();
        }
        SignalHookThreadReady();
        return 1;
    }
    Wh_Log(L"EVENT_SYSTEM_FOREGROUND hook installed");
    SignalHookThreadReady();

    HWINEVENTHOOK deskHook =
        SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH,
                        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!deskHook) {
        Wh_Log(L"EVENT_SYSTEM_DESKTOPSWITCH hook failed: %u", GetLastError());
    } else {
        Wh_Log(L"EVENT_SYSTEM_DESKTOPSWITCH hook installed");
    }

    RefreshCurrentDesktopId();

    if (HWND fg = GetForegroundWindow()) {
        PostMessage(hwnd, WM_APP_FOREGROUND_CHANGED,
                    reinterpret_cast<WPARAM>(fg), 0);
    }

    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, nullptr, 0, 0)) != 0) {
        if (bRet == -1) {
            break;
        }
        if (msg.message == WM_APP && msg.hwnd == nullptr) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hook) {
        UnhookWinEvent(hook);
    }
    if (deskHook) {
        UnhookWinEvent(deskHook);
    }

    ReleaseVdm();
    UnsubscribeAccentChanges();

    g_decayTimerArmed.store(false);
    g_hookThreadHwnd.store(nullptr, std::memory_order_release);
    if (shellHookRegistered)
        DeregisterShellHookWindow(hwnd);
    DestroyWindow(hwnd);
    g_shellHookMessage = 0;
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    g_hookThreadId.store(0, std::memory_order_release);

    if (SUCCEEDED(coHr)) {
        CoUninitialize();
    }

    Wh_Log(L"WinEvent hook thread exiting");
    return 0;
}

bool StartWinEventHookThread() {
    std::lock_guard<std::mutex> lock(g_winEventHookThreadMutex);
    if (g_winEventHookThread && HookThreadWindow()) {
        return true;
    }
    if (!g_hookThreadReadyEvent) {
        g_hookThreadReadyEvent =
            CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_hookThreadReadyEvent) {
            Wh_Log(L"CreateEventW failed for hook-thread ready: %u",
                   GetLastError());
            return false;
        }
    } else {
        ResetEvent(g_hookThreadReadyEvent);
    }
    HANDLE hThread =
        CreateThread(nullptr, 0, WinEventHookThread, nullptr, 0, nullptr);
    if (!hThread) {
        Wh_Log(L"CreateThread failed: %u", GetLastError());
        if (g_hookThreadReadyEvent) {
            CloseHandle(g_hookThreadReadyEvent);
            g_hookThreadReadyEvent = nullptr;
        }
        return false;
    }
    g_winEventHookThread = hThread;
    WaitForSingleObject(g_hookThreadReadyEvent, INFINITE);
    if (!HookThreadWindow()) {
        Wh_Log(L"Focus thread failed to create the message window");
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        g_winEventHookThread = nullptr;
        if (g_hookThreadReadyEvent) {
            CloseHandle(g_hookThreadReadyEvent);
            g_hookThreadReadyEvent = nullptr;
        }
        return false;
    }
    Wh_Log(L"WinEvent hook thread started");
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

