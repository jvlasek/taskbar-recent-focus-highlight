// ---------------------------------------------------------------------------
// Windhawk entry points
// ---------------------------------------------------------------------------

BOOL Wh_ModInit() {
    Wh_Log(L"> init " WH_MOD_VERSION);

    g_unloading = false;
    LoadSettings(/*userSavedSettings=*/false);
    // highlightCount=0 with previews off used to return FALSE here. Windhawk
    // shows nothing for a failed init and then does not call
    // Wh_ModSettingsChanged, so turning a highlight back on did nothing
    // until the mod was toggled (Neon review 1.8.2). Now the mod loads and
    // paints nothing until a setting asks for it.
    {
        auto settings = SettingsSnap();
        if (settings->highlightCount <= 0 &&
            !settings->previewHighlightEnabled) {
            Wh_Log(L"Nothing to paint yet (highlightCount=0, previews off)");
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
        g_taskbarViewHooked = true;  // the focus thread starts tracking
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
    // The current foreground is posted by ActivateFocusTracking, once this
    // process is known to host the taskbar.
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
    size_t leftLayoutWatches = 0;
    size_t leftRepeaterWatches = 0;
    {
        std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
        leftLayoutWatches = g_layoutWatches.size();
    }
    {
        std::lock_guard<std::mutex> lock(g_repeaterWatchMutex);
        // Entries left here belong to repeaters still alive on a dispatcher
        // the drain did not reach.
        leftRepeaterWatches = g_repeaterWatches.size();
    }
    if (leftLayoutWatches) {
        Wh_Log(L"ERROR: %zu IconPanel SizeChanged watches not revoked",
               leftLayoutWatches);
    }
    if (leftRepeaterWatches) {
        Wh_Log(L"ERROR: %zu flyout ElementClearing watches not revoked",
               leftRepeaterWatches);
    }

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_desktopMaps.clear();
        g_currentDesktopId = {};
        g_haveCurrentDesktop = false;
        g_pendingFocus = {};
    }
    // The worker has exited; its last-use stretch goes with it.
    g_inFrontHwnd = nullptr;
    g_inFrontSince = 0;
    ReleaseVdm();
    {
        std::lock_guard<std::mutex> lock(g_windowAppIdMutex);
        g_windowAppId.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_uncombinedMutex);
        g_uncombinedWindowsByThread.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_fullPassQueuedMutex);
        g_fullPassQueuedThreads.clear();
    }
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

    // Newly excluded apps are dropped on the focus worker
    // (DropExcludedRecords): it owns the recency maps and is the STA
    // ExclusionAumid's SHGetPropertyStoreForWindow needs (Neon review 1.6).
    PostToHookThread(WM_APP_SETTINGS_CHANGED);
    RequestApplyVisuals();
    RequestApplyPreviewVisuals();
}
