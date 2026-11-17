// ---------------------------------------------------------------------------
// Taskbar.View.dll hooks
// ---------------------------------------------------------------------------

using TaskListButton_UpdateVisualStates_t = void(WINAPI*)(void* pThis);
TaskListButton_UpdateVisualStates_t TaskListButton_UpdateVisualStates_Original;

FrameworkElement TaskListButtonElementFromThis(void* pThis) {
    FrameworkElement element = nullptr;
    // implementation* layout: IUnknown at pThis+3 (same as other taskbar mods).
    ((IUnknown*)pThis + 3)
        ->QueryInterface(winrt::guid_of<FrameworkElement>(),
                         winrt::put_abi(element));
    return element;
}

// Re-paint one button from the last full bind. Does not re-score identity —
// hover UpdateVisualStates would otherwise O(buttons×ranks) every mouse-over.
// Unranked button: an unknown identity needs a full pass; a known one is
// stored as rank 0 so hover repaints skip it.
void MarkButtonUnranked(FrameworkElement button) {
    if (GetCachedButtonIdentity(button).appIdUpper.empty()) {
        ScheduleRefreshAllHighlights(button);
    } else {
        SetCachedPaintState(button, 0, SettingsSnap()->generation);
    }
}

void RefreshButtonHighlight(FrameworkElement button) {
    if (!button || g_unloading.load()) {
        return;
    }

    // Explorer reuses a TaskListButton when the exe is replaced. The cached
    // paint rank still belongs to the old path if that HWND is gone or now
    // belongs to another process. PID only — do not OpenProcess from
    // UpdateVisualStates.
    // A grouped button whose sample window closed while siblings remain is
    // re-captured and re-ranked inline, without a visible off/on (Fable 2.2).
    auto sampleDead = [](const ButtonIdentity& ident) {
        return ident.sampleHwnd &&
               (!IsWindow(ident.sampleHwnd) ||
                (ident.samplePid &&
                 !HwndMatchesStoredPid(ident.sampleHwnd, ident.samplePid)));
    };
    if (TaskListButton_IsRunning(button) &&
        sampleDead(GetCachedButtonIdentity(button))) {
        EnsureButtonIdentity(button);
        if (sampleDead(GetCachedButtonIdentity(button))) {
            if (ButtonHasOurChrome(button)) {
                ClearButtonHighlight(button);
            }
            SetCachedPaintState(button, -1, SettingsSnap()->generation);
            ScheduleRefreshAllHighlights(button);
            return;
        }
    }

    // Closed / pinned-not-running: drop chrome here and rebind so the slot
    // frees. Read the rank first — clear stores 0.
    // Rank eligibility retains grace; visuals follow IsRunning immediately.
    if (!ButtonCountsAsRunning(button)) {
        const int cached = GetCachedPaintState(button).rank;
        if (ButtonHasOurChrome(button)) {
            ClearButtonHighlight(button);
        }
        if (cached > 0) {
            SetCachedPaintState(button, 0, SettingsSnap()->generation);
            ScheduleRefreshAllHighlights(button);
        } else if (cached < 0) {
            MarkButtonUnranked(button);
        }
        return;
    }

    // Sweep is ApplyAllHighlights. Clearing here blanks ranked icons until
    // that pass (desktop switch / decay flicker).
    int rank = GetCachedPaintState(button).rank;
    if (rank < 0) {
        std::vector<AppFocusInfo> ranks;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            ranks = CurrentDeskLocked().rankedApps;
        }
        if (ranks.empty()) {
            if (ButtonHasOurChrome(button)) {
                ClearButtonHighlight(button);
            } else {
                MarkButtonUnranked(button);
            }
            return;
        }
        rank = FindRankForButton(button, ranks);
    }

    if (rank > 0) {
        ApplyButtonHighlight(button, rank);
    } else if (ButtonHasOurChrome(button)) {
        ClearButtonHighlight(button);
    } else {
        MarkButtonUnranked(button);
    }
}

// Full identity rebind on the anchor's UI thread (Low priority, debounced).
// The anchor only selects the dispatcher. The queued bind still runs if
// that button is destroyed before Low priority.
void ScheduleRefreshAllHighlights(FrameworkElement dispatcherAnchor) {
    if (!dispatcherAnchor || g_unloading.load()) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastFullRefreshTick < kFullRebindDebounceMs &&
        g_lastFullRefreshTick != 0) {
        PostToHookThread(WM_APP_REQUEST_APPLY_DEBOUNCED);
        return;
    }
    try {
        auto dispatcher = dispatcherAnchor.Dispatcher();
        if (!dispatcher ||
            !QueueFullPass(
                dispatcher, GetCurrentThreadId(),
                winrt::Windows::UI::Core::CoreDispatcherPriority::Low)) {
            PostToHookThread(WM_APP_REQUEST_APPLY_DEBOUNCED);
            return;
        }
        g_lastFullRefreshTick = now;
    } catch (...) {
        PostToHookThread(WM_APP_REQUEST_APPLY_DEBOUNCED);
    }
}

void WINAPI TaskListButton_UpdateVisualStates_Hook(void* pThis) {
    TaskListButton_UpdateVisualStates_Original(pThis);

    if (g_unloading.load()) {
        return;
    }

    FrameworkElement button = TaskListButtonElementFromThis(pThis);
    if (!button) {
        return;
    }

    try {
        if (button.Name() != L"TaskListButton") {
            return;
        }
    } catch (...) {
        return;
    }

    TrackButton_UIThread(button);
    const bool seeded = SeedButtonsFromRepeater_UIThread(button);
    RefreshButtonHighlight(button);
    // Only an unknown rank (-1) or a pending sweep needs a full pass. Our
    // overlays are own elements that Explorer's hover states do not reset,
    // so a ranked button no longer re-binds every sibling (Fable review 5).
    const int rank = GetCachedPaintState(button).rank;
    if (seeded || rank < 0 || g_pendingOverlaySweep.load()) {
        ScheduleRefreshAllHighlights(button);
    }
}

// XAML thumbnail view template — apply preview glow when flyout builds items.
using TaskItemThumbnailView_OnApplyTemplate_t = void(WINAPI*)(void* pThis);
TaskItemThumbnailView_OnApplyTemplate_t
    TaskItemThumbnailView_OnApplyTemplate_Original;
void WINAPI TaskItemThumbnailView_OnApplyTemplate_Hook(void* pThis) {
    if (TaskItemThumbnailView_OnApplyTemplate_Original) {
        TaskItemThumbnailView_OnApplyTemplate_Original(pThis);
    }
    if (g_unloading.load() || !SettingsSnap()->previewHighlightEnabled) {
        return;
    }
    try {
        IUnknown* unknownPtr = *((IUnknown**)pThis + 1);
        if (!unknownPtr) {
            return;
        }
        FrameworkElement element = nullptr;
        unknownPtr->QueryInterface(winrt::guid_of<FrameworkElement>(),
                                   winrt::put_abi(element));
        if (element) {
            TrackThumbView_UIThread(element);
            SchedulePreviewFlyoutRefresh(element);
        }
    } catch (...) {
        HRESULT hr = winrt::to_hresult();
        Wh_Log(L"TaskItemThumbnailView_OnApplyTemplate error %08X", hr);
    }
}

// A card was bound to a (possibly different) window: realized, recycled by
// the ItemsRepeater, or rebound after another card was removed. OnApplyTemplate
// only runs on first realization, so without this a reused card kept the
// previous window's rank paint until the flyout reopened.
using TaskItemThumbnailView_put_ThumbnailSource_t = int32_t(WINAPI*)(void* pThis,
                                                                     void* source);
TaskItemThumbnailView_put_ThumbnailSource_t
    TaskItemThumbnailView_put_ThumbnailSource_Original;
int32_t WINAPI TaskItemThumbnailView_put_ThumbnailSource_Hook(void* pThis,
                                                              void* source) {
    const int32_t result =
        TaskItemThumbnailView_put_ThumbnailSource_Original(pThis, source);
    if (g_unloading.load() || !SettingsSnap()->previewHighlightEnabled) {
        return result;
    }
    try {
        FrameworkElement card = nullptr;
        static_cast<::IUnknown*>(pThis)->QueryInterface(
            winrt::guid_of<FrameworkElement>(), winrt::put_abi(card));
        if (card) {
            // Coalesced: a flyout binding N cards schedules one refresh.
            SchedulePreviewFlyoutRefresh(card);
        }
    } catch (...) {
    }
    return result;
}


using HoverFlyoutModel_TargetItemKey_t = void(WINAPI*)(void* pThis,
                                                       void* param1);
extern HoverFlyoutModel_TargetItemKey_t HoverFlyoutModel_TargetItemKey_Original;
void WINAPI HoverFlyoutModel_TargetItemKey_Hook(void* pThis, void* param1);

bool HookTaskbarViewDllSymbols(HMODULE module) {
    // Taskbar.View.dll, ExplorerExtensions.dll
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskListButton,struct winrt::Taskbar::ITaskListButton>::get_IsRunning(bool *))"},
            &TaskListButton_get_IsRunning_Original,
        },
        {
            {LR"(private: void __cdecl winrt::Taskbar::implementation::TaskListButton::UpdateVisualStates(void))"},
            &TaskListButton_UpdateVisualStates_Original,
            TaskListButton_UpdateVisualStates_Hook,
        },
        {
            {LR"(struct winrt::Taskbar::TaskListWindowViewModel __cdecl TryGetItemFromContainer<struct winrt::Taskbar::TaskListWindowViewModel>(struct winrt::Windows::UI::Xaml::UIElement const &))"},
            &TryGetItemFromContainer_TaskListWindowViewModel_Original,
        },
        {
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskListWindowViewModel,struct winrt::Taskbar::ITaskListWindowViewModel>::get_TaskItem(void * *))"},
            &TaskListWindowViewModel_get_TaskItem_Original,
        },
        {
            {LR"(struct winrt::Taskbar::TaskListGroupViewModel __cdecl TryGetItemFromContainer<struct winrt::Taskbar::TaskListGroupViewModel>(struct winrt::Windows::UI::Xaml::UIElement const &))"},
            &TryGetItemFromContainer_TaskListGroupViewModel_Original,
        },
        {
            {LR"(public: bool __cdecl winrt::Taskbar::implementation::TaskListGroupViewModel::IsMultiWindow(void)const )"},
            &TaskListGroupViewModel_IsMultiWindow_Original,
        },
        {
            {LR"(public: __cdecl winrt::impl::consume_WindowsUdk_UI_Shell_ITaskGroup<struct winrt::WindowsUdk::UI::Shell::ITaskGroup>::IsRunning(void)const )"},
            &ITaskGroup_IsRunning_Original,
            ITaskGroup_IsRunning_Hook,
        },
        {
            // Optional: XAML thumbnail flyout (Win11). Fail soft if missing.
            {LR"(public: void __cdecl winrt::Taskbar::implementation::TaskItemThumbnailView::OnApplyTemplate(void))"},
            &TaskItemThumbnailView_OnApplyTemplate_Original,
            TaskItemThumbnailView_OnApplyTemplate_Hook,
            true,
        },
        {
            // Optional: flyout retargeted to another button; clear and
            // repaint its cards (symbol as in taskbar-thumbnail-reorder).
            {LR"(private: void __cdecl winrt::Taskbar::implementation::HoverFlyoutModel::TargetItemKey(struct winrt::hstring const &))"},
            &HoverFlyoutModel_TargetItemKey_Original,
            HoverFlyoutModel_TargetItemKey_Hook,
            true,
        },
        {
            // Optional: card view model -> window (preview resolve pass 0).
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskItemThumbnailView,struct winrt::Taskbar::ITaskItemThumbnailView>::get_ThumbnailSource(void * *))"},
            &TaskItemThumbnailView_get_ThumbnailSource,
            nullptr,
            true,
        },
        {
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskItemThumbnailViewModel,struct winrt::Taskbar::ITaskItemThumbnailViewModel>::get_TaskItemThumbnail(void * *))"},
            &TaskItemThumbnailViewModel_get_TaskItemThumbnail,
            nullptr,
            true,
        },
        {
            // Optional: repaint when a card is rebound to another window.
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskItemThumbnailView,struct winrt::Taskbar::ITaskItemThumbnailView>::put_ThumbnailSource(void *))"},
            &TaskItemThumbnailView_put_ThumbnailSource_Original,
            TaskItemThumbnailView_put_ThumbnailSource_Hook,
            true,
        },
    };

    if (!HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks))) {
        Wh_Log(L"HookSymbols failed for Taskbar.View.dll");
        return false;
    }
    // Button identity (TaskListButton -> view model -> WindowsUdk TaskItem /
    // TaskGroup) lives here; before 0.11.20 this flag waited for taskbar.dll.
    g_taskbandResolveReady = true;

    Wh_Log(L"Preview resolve: view model %s, rebind repaint %s",
           (TaskItemThumbnailView_get_ThumbnailSource &&
            TaskItemThumbnailViewModel_get_TaskItemThumbnail)
               ? L"on"
               : L"unavailable",
           TaskItemThumbnailView_put_ThumbnailSource_Original ? L"on"
                                                              : L"unavailable");

    if (TaskItemThumbnailView_OnApplyTemplate_Original) {
        Wh_Log(L"Hooked Taskbar.View.dll symbols (identity + paint + thumbnails)");
    } else {
        Wh_Log(L"Hooked Taskbar.View.dll symbols (identity + paint; "
               L"thumbnail OnApplyTemplate unavailable)");
    }
    return true;
}

HoverFlyoutModel_TargetItemKey_t HoverFlyoutModel_TargetItemKey_Original;
void WINAPI HoverFlyoutModel_TargetItemKey_Hook(void* pThis, void* param1) {
    // Flyout retargeted to another button: clear and repaint (below).
    HoverFlyoutModel_TargetItemKey_Original(pThis, param1);
    if (g_unloading.load() || !SettingsSnap()->previewHighlightEnabled) {
        return;
    }
    // Repeater may not have swapped realized cards yet. Clear leftover chrome
    // now and paint at Low after layout so new HWNDs are not bound to the
    // previous app's views.
    try {
        // Explorer sets the same key repeatedly while one button's flyout
        // stays open; clearing every tracked card each time was the cost
        // (Fable review 5). Only a different button needs the clear.
        static thread_local std::wstring lastKey;
        std::wstring key;
        if (param1) {
            key = reinterpret_cast<const winrt::hstring*>(param1)->c_str();
        }
        if (key.empty() || key != lastKey) {
            ClearAllThumbnailHighlights_UIThread();
        }
        lastKey = std::move(key);
        FrameworkElement el = PickLiveFlyoutThumbOnThisDispatcher();
        if (el) {
            SchedulePreviewFlyoutRefresh(el);
        }
    } catch (...) {
    }
}

HMODULE GetTaskbarViewModuleHandle() {
    HMODULE module = GetModuleHandle(L"Taskbar.View.dll");
    if (!module) {
        module = GetModuleHandle(L"ExplorerExtensions.dll");
    }
    return module;
}

void HandleLoadedModuleIfTaskbarView(HMODULE module, LPCWSTR lpLibFileName) {
    if (g_taskbarViewHookAttempted.load() ||
        GetTaskbarViewModuleHandle() != module ||
        g_taskbarViewHookAttempted.exchange(true)) {
        return;
    }
    Wh_Log(L"Loaded %s", lpLibFileName);
    // Claim once before resolving symbols, including when resolution fails.
    if (HookTaskbarViewDllSymbols(module)) {
        Wh_ApplyHookOperations();
    }
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR lpLibFileName,
                                   HANDLE hFile,
                                   DWORD dwFlags) {
    HMODULE module = LoadLibraryExW_Original(lpLibFileName, hFile, dwFlags);
    if (module) {
        HandleLoadedModuleIfTaskbarView(module, lpLibFileName);
    }
    return module;
}

