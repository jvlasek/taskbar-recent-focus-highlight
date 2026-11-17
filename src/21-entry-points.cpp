// ---------------------------------------------------------------------------
// Windhawk entry points
// ---------------------------------------------------------------------------

BOOL Wh_ModInit() {
    Wh_Log(L"> init " WH_MOD_VERSION);

    g_unloading = false;
    LoadSettings(/*userSavedSettings=*/false);
    {
        auto settings = SettingsSnap();
        if (settings->highlightCount <= 0 &&
            !settings->previewHighlightEnabled) {
            Wh_Log(L"Nothing to paint (highlightCount=0, previews off) — "
                   L"skipping init");
            return FALSE;
        }
    }

    HMODULE kernelBaseModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelBaseModule) {
        Wh_Log(L"kernelbase.dll not loaded");
        return FALSE;
    }
    auto pKernelBaseLoadLibraryExW = (decltype(&LoadLibraryExW))GetProcAddress(
        kernelBaseModule, "LoadLibraryExW");
    if (!pKernelBaseLoadLibraryExW) {
        Wh_Log(L"LoadLibraryExW not found in kernelbase.dll");
        return FALSE;
    }
    WindhawkUtils::SetFunctionHook(pKernelBaseLoadLibraryExW,
                                   LoadLibraryExW_Hook,
                                   &LoadLibraryExW_Original);

    // No taskbar.dll hooks since 0.11.20: button identity, windows, previews
    // and focus all come from Taskbar.View.dll and public WindowsUdk
    // interfaces.
    if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
        if (!HookTaskbarViewDllSymbols(taskbarViewModule)) {
            Wh_Log(L"Taskbar.View hooks failed");
            return FALSE;
        }
        g_taskbarViewHookAttempted = true;
    } else {
        Wh_Log(L"Taskbar view module not loaded yet");
    }

    if (!StartWinEventHookThread()) {
        Wh_Log(L"Focus thread failed to start");
        return FALSE;
    }
    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");

    if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
        HandleLoadedModuleIfTaskbarView(taskbarViewModule, L"Taskbar view (after init)");
    }

    if (HWND fg = GetForegroundWindow()) {
        PostToHookThread(WM_APP_FOREGROUND_CHANGED,
                         reinterpret_cast<WPARAM>(fg));
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");
    g_unloading = true;
    g_taskbandResolveReady = false;

    // Stop the worker first so it cannot TryRunAsync after the UI drain.
    StopWinEventHookThread();

    if (!RunOnEachUiDispatcherAndWait([]() {
            ClearAllHighlights_UIThread();
            ClearAllThumbnailHighlights_UIThread();
            RevokeIconPanelLayoutWatchesOnThisDispatcher();
            RevokeRepeaterWatchesOnThisDispatcher();
        })) {
        Wh_Log(L"ERROR: UI cleanup did not finish on every dispatcher");
    }
    {
        std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
        if (!g_layoutWatches.empty()) {
            Wh_Log(L"ERROR: %zu IconPanel SizeChanged watches not revoked",
                   g_layoutWatches.size());
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_repeaterWatchMutex);
        // Entries left here belong to repeaters still alive on a dispatcher
        // the drain did not reach.
        if (!g_repeaterWatches.empty()) {
            Wh_Log(L"ERROR: %zu flyout ElementClearing watches not revoked",
                   g_repeaterWatches.size());
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_desktopMaps.clear();
        g_currentDesktopId = {};
        g_haveCurrentDesktop = false;
        g_pendingFocus = {};
    }
    ReleaseVdm();
    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        g_trackedButtons.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_buttonStateMutex);
        g_buttonStates.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
        g_trackedThumbViews.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_dispatchersMutex);
        g_uiDispatchers.reset();
    }
}

void Wh_ModSettingsChanged() {
    Wh_Log(L">");

    LoadSettings(/*userSavedSettings=*/true);

    std::vector<HWND> windowHwnds;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (auto& [id, desk] : g_desktopMaps) {
            (void)id;
            for (const auto& [hwnd, winfo] : desk.windowFocusMap) {
                (void)winfo;
                if (hwnd) {
                    windowHwnds.push_back(hwnd);
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
                if (IsExcludedKey(it->first, displayUpper,
                                  it->second.appIdUpper,
                                  it->second.pathUpper)) {
                    it = desk.appFocusMap.erase(it);
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
                } else {
                    ++it;
                }
            }
            PruneWindowFocusMapLocked(desk);
            RecomputeRanksForDesktopLocked(desk);
        }
    }

    PostToHookThread(WM_APP_SETTINGS_CHANGED);
    RequestApplyVisuals();
    RequestApplyPreviewVisuals();
}
