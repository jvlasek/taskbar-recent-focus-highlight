// ---------------------------------------------------------------------------
// Button → task item / task group — adapted from taskbar-volume-control-per-app
// ---------------------------------------------------------------------------

thread_local bool g_captureTaskGroup = false;
thread_local void* g_capturedTaskGroup = nullptr;

using TryGetItemFromContainer_TaskListWindowViewModel_t =
    void*(WINAPI*)(void** output, UIElement* container);
TryGetItemFromContainer_TaskListWindowViewModel_t
    TryGetItemFromContainer_TaskListWindowViewModel_Original;

// ---------------------------------------------------------------------------
// WindowsUdk.UI.Shell public interfaces: task groups and task items
// ---------------------------------------------------------------------------
// Layouts from windowsudk.winmd (C:\Windows\SystemApps\
// MicrosoftWindows.UndockedDevKit_cw5n1h2txyewy, build 26100-era, read
// 2026-11-15). Only the slots up to the last method we call are declared;
// earlier slots are placeholders whose parameter types do not matter because
// we never call them. A changed interface has a new IID, so QueryInterface
// fails and callers fall back.

constexpr GUID IID_Wudk_ITaskItem = {
    0xB081D9D6, 0x9B45, 0x5363, {0x8A, 0x4D, 0x85, 0x4A, 0xDD, 0x6A, 0xBE, 0x7E}};
constexpr GUID IID_Wudk_ITaskGroup = {
    0xA152E779, 0x93DF, 0x5BE2, {0xAF, 0x2C, 0xD3, 0x42, 0x45, 0x2B, 0x0C, 0xE0}};

struct WudkITaskItemAbi : ::IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AppId(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UniqueId(UINT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_WindowId(UINT64* value) = 0;
};

struct WudkITaskGroupAbi : ::IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AppId(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UniqueId(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayName(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AccessibleName(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Icon(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OverlayIcon(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OverlayIconDescription(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Badge(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRequestingAttention(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Progress(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_VisualState(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_VisualState(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Bounds(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(void*) = 0;
    // IVectorView<WindowsUdk.UI.Shell.TaskItem>
    virtual HRESULT STDMETHODCALLTYPE get_Items(::IInspectable** value) = 0;
};

// Windows.Foundation.Collections.IVectorView<T> for a runtime-class T: the
// element comes back as its default interface pointer. The parameterized IID
// is not needed: get_Items already returns this interface.
struct WudkIVectorViewAbi : ::IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, ::IInspectable** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
};

HWND HwndFromUdkTaskItem(::IUnknown* taskItem) {
    if (!taskItem) {
        return nullptr;
    }
    winrt::com_ptr<WudkITaskItemAbi> item;
    UINT64 windowId = 0;
    if (FAILED(taskItem->QueryInterface(IID_Wudk_ITaskItem, item.put_void())) ||
        FAILED(item->get_WindowId(&windowId))) {
        return nullptr;
    }
    HWND hwnd = reinterpret_cast<HWND>(windowId);
    return IsWindow(hwnd) ? hwnd : nullptr;
}

// Explorer's AppId (the key it groups taskbar buttons by): ITaskItem /
// ITaskGroup get_AppId, slot 0 of both public interfaces. Upper case.
std::wstring UdkTaskItemAppId(::IUnknown* taskItem) {
    if (!taskItem) {
        return {};
    }
    try {
        winrt::com_ptr<WudkITaskItemAbi> item;
        winrt::hstring id;
        if (FAILED(taskItem->QueryInterface(IID_Wudk_ITaskItem,
                                            item.put_void())) ||
            FAILED(item->get_AppId(reinterpret_cast<HSTRING*>(
                winrt::put_abi(id))))) {
            return {};
        }
        return ToUpper(std::wstring(id));
    } catch (...) {
        return {};
    }
}

std::wstring UdkTaskGroupAppId(void* udkTaskGroup) {
    if (!udkTaskGroup) {
        return {};
    }
    try {
        winrt::com_ptr<WudkITaskGroupAbi> group;
        winrt::hstring id;
        if (FAILED(static_cast<::IUnknown*>(udkTaskGroup)->QueryInterface(
                IID_Wudk_ITaskGroup, group.put_void())) ||
            FAILED(group->get_AppId(reinterpret_cast<HSTRING*>(
                winrt::put_abi(id))))) {
            return {};
        }
        return ToUpper(std::wstring(id));
    } catch (...) {
        return {};
    }
}

// Public replacement for CTaskGroup's private item array: the group's
// Items -> each item's WindowId. Returns false if the interfaces are not
// available (caller keeps the old path).
bool CollectUdkGroupWindows(void* udkTaskGroup, std::vector<HWND>& out) {
    out.clear();
    if (!udkTaskGroup) {
        return false;
    }
    try {
        winrt::com_ptr<WudkITaskGroupAbi> group;
        if (FAILED(static_cast<::IUnknown*>(udkTaskGroup)->QueryInterface(
                IID_Wudk_ITaskGroup, group.put_void()))) {
            return false;
        }
        winrt::com_ptr<::IInspectable> itemsObject;
        if (FAILED(group->get_Items(itemsObject.put())) || !itemsObject) {
            return false;
        }
        auto* items = reinterpret_cast<WudkIVectorViewAbi*>(itemsObject.get());
        UINT32 size = 0;
        if (FAILED(items->get_Size(&size))) {
            return false;
        }
        for (UINT32 i = 0; i < size && i < 256; ++i) {
            winrt::com_ptr<::IInspectable> element;
            if (FAILED(items->GetAt(i, element.put())) || !element) {
                continue;
            }
            if (HWND hwnd = HwndFromUdkTaskItem(element.get())) {
                if (std::find(out.begin(), out.end(), hwnd) == out.end()) {
                    out.push_back(hwnd);
                }
            }
        }
        return true;
    } catch (...) {
        out.clear();
        return false;
    }
}

using TaskListWindowViewModel_get_TaskItem_t = int(WINAPI*)(void* pThis,
                                                            void** taskItem);
TaskListWindowViewModel_get_TaskItem_t
    TaskListWindowViewModel_get_TaskItem_Original;

using TryGetItemFromContainer_TaskListGroupViewModel_t =
    void*(WINAPI*)(void** output, UIElement* container);
TryGetItemFromContainer_TaskListGroupViewModel_t
    TryGetItemFromContainer_TaskListGroupViewModel_Original;

using TaskListGroupViewModel_IsMultiWindow_t = bool(WINAPI*)(void* pThis);
TaskListGroupViewModel_IsMultiWindow_t
    TaskListGroupViewModel_IsMultiWindow_Original;

using ITaskGroup_IsRunning_t = bool(WINAPI*)(void* pThis);
ITaskGroup_IsRunning_t ITaskGroup_IsRunning_Original;
bool WINAPI ITaskGroup_IsRunning_Hook(void* pThis) {
    if (g_captureTaskGroup) {
        g_capturedTaskGroup = *(void**)pThis;
        return false;
    }
    return ITaskGroup_IsRunning_Original
               ? ITaskGroup_IsRunning_Original(pThis)
               : false;
}

winrt::com_ptr<IUnknown> GetWindowsUdkTaskItemFromTaskListButton(
    UIElement element) {
    if (!TryGetItemFromContainer_TaskListWindowViewModel_Original ||
        !TaskListWindowViewModel_get_TaskItem_Original) {
        return nullptr;
    }
    winrt::com_ptr<IUnknown> windowViewModel;
    TryGetItemFromContainer_TaskListWindowViewModel_Original(
        windowViewModel.put_void(), &element);
    if (!windowViewModel) {
        return nullptr;
    }
    winrt::com_ptr<IUnknown> windowsUdkTaskItem;
    TaskListWindowViewModel_get_TaskItem_Original(
        windowViewModel.get(), windowsUdkTaskItem.put_void());
    return windowsUdkTaskItem;
}

void* GetWindowsUdkTaskGroupFromTaskListButton(UIElement element) {
    if (!TryGetItemFromContainer_TaskListGroupViewModel_Original ||
        !TaskListGroupViewModel_IsMultiWindow_Original) {
        return nullptr;
    }
    winrt::com_ptr<IUnknown> groupViewModel;
    TryGetItemFromContainer_TaskListGroupViewModel_Original(
        groupViewModel.put_void(), &element);
    if (!groupViewModel) {
        return nullptr;
    }
    g_capturedTaskGroup = nullptr;
    g_captureTaskGroup = true;
    TaskListGroupViewModel_IsMultiWindow_Original((void**)groupViewModel.get() -
                                                  1);
    g_captureTaskGroup = false;
    // If the IsMultiWindow -> ITaskGroup::IsRunning path changes in
    // Explorer, no group is ever captured and a grouped app's second window
    // is never ranked. Say so once (Fable review 3.1). Single misses are
    // normal (some view models do not ask their group, 0.11.42 log), so only
    // many misses with no capture at all count.
    static std::atomic<unsigned> captureHits{0}, captureMisses{0};
    static std::atomic<bool> loggedCaptureMiss{false};
    if (g_capturedTaskGroup) {
        ++captureHits;
    } else if (++captureMisses >= 20 && captureHits.load() == 0 &&
               !loggedCaptureMiss.exchange(true)) {
        Wh_Log(L"WARNING: task group capture never succeeded (%u misses): "
               L"IsMultiWindow / ITaskGroup::IsRunning changed?",
               captureMisses.load());
    }
    return g_capturedTaskGroup;
}

// Synchronous snapshot of the button's Explorer AppId and window list (UI
// thread, a few COM calls).
ButtonResolveData CaptureButtonResolveData(FrameworkElement button) {
    ButtonResolveData data;
    data.buttonId = InspectableIdentity(button);
    data.running = TaskListButton_IsRunning(button);
    DWORD pid = 0;
    HWND hwnd = nullptr;
    std::vector<CapturedWindow> groupWindows;
    auto rememberWindow = [&](HWND h) {
        if (!h || !IsWindow(h)) {
            return;
        }
        for (const auto& g : groupWindows) {
            if (g.hwnd == h) {
                return;
            }
        }
        groupWindows.push_back(CapturedWindow{h, PidFromHwnd(h)});
    };
    std::vector<HWND> udkWindows;
    try {
        UIElement el = button.as<UIElement>();
        // Public path: the button's TaskItem (single window) and its
        // TaskGroup's Items, each -> ITaskItem.WindowId. UWP gives the
        // ApplicationFrameWindow, the HWND focus tracking uses.
        auto udkItem = GetWindowsUdkTaskItemFromTaskListButton(el);
        HWND ownWindow = nullptr;
        std::wstring itemAppId = UdkTaskItemAppId(udkItem.get());
        std::wstring groupAppId;
        if (udkItem) {
            if (HWND h = HwndFromUdkTaskItem(udkItem.get())) {
                ownWindow = h;
                udkWindows.push_back(h);
            }
        }
        void* windowsUdkTaskGroup =
            GetWindowsUdkTaskGroupFromTaskListButton(el);
        if (windowsUdkTaskGroup) {
            groupAppId = UdkTaskGroupAppId(windowsUdkTaskGroup);
            std::vector<HWND> groupHwnds;
            if (CollectUdkGroupWindows(windowsUdkTaskGroup, groupHwnds)) {
                for (HWND h : groupHwnds) {
                    if (std::find(udkWindows.begin(), udkWindows.end(), h) ==
                        udkWindows.end()) {
                        udkWindows.push_back(h);
                    }
                }
            }
        }
        // Uncombined button (Never combine / combine when full): it stands
        // for one window. Explorer marks it with AutomationId
        // "Window: 0x<hwnd>" (combined buttons: "Appid: …") and binds it to
        // that window's own TaskItem, without a group view model (0.11.14
        // diag: windowVM=1 groupVM=0, one window). Require both to agree.
        // (0.11.11-0.11.13 wrongly required a group of 2+ windows here.)
        bool uncombined = false;
        if (HWND idWindow = GetButtonAutomationWindow(button)) {
            if (ownWindow ? ownWindow == idWindow
                          : std::find(udkWindows.begin(), udkWindows.end(),
                                      idWindow) != udkWindows.end()) {
                ownWindow = idWindow;
                uncombined = true;
            }
        }
        {
            bool changed = false;
            std::lock_guard<std::mutex> lock(g_uncombinedMutex);
            if (uncombined) {
                changed = g_uncombinedWindowsByThread[GetCurrentThreadId()]
                              .insert(ownWindow)
                              .second;
            } else if (ownWindow) {
                changed = g_uncombinedWindowsByThread[GetCurrentThreadId()]
                              .erase(ownWindow) > 0;
            }
            if (changed) {
                Wh_Log(L"Button windows: button=%p own=%p -> %s",
                       data.buttonId, ownWindow,
                       uncombined ? L"uncombined (own slot)" : L"combined");
            }
        }
        if (uncombined) {
            udkWindows.assign(1, ownWindow);
        }
        data.uncombined = uncombined;
        data.explorerAppId = !groupAppId.empty() ? groupAppId : itemAppId;
        if (!groupAppId.empty() && !itemAppId.empty() &&
            groupAppId != itemAppId) {
            Wh_Log(L"AppId shadow: button=%p group AppId %s != item AppId %s",
                   data.buttonId, groupAppId.c_str(), itemAppId.c_str());
        }
        PublishWindowAppIds(udkWindows, data.explorerAppId);
        if (!udkWindows.empty()) {
            for (HWND h : udkWindows) {
                rememberWindow(h);
            }
            if (!groupWindows.empty()) {
                hwnd = groupWindows.front().hwnd;
                GetWindowThreadProcessId(hwnd, &pid);
            }
        }
    } catch (...) {
        pid = 0;
        hwnd = nullptr;
        groupWindows.clear();
    }

    data.hwnd = hwnd;
    data.pid = hwnd ? PidFromHwnd(hwnd) : pid;
    data.windows = std::move(groupWindows);
    return data;
}

// Button identity = Explorer's AppId (the key Explorer groups taskbar
// buttons by) plus the button's window set, read synchronously on the UI
// thread from the button's public WindowsUdk task item / group (a few COM
// calls, ~10 us). Replaces the 0.10.3-0.11.31 asynchronous resolver (worker
// process-path/AUMID lookups with serials, deadlines and rechecks). A
// changed AppId or window set invalidates the cached paint rank; a button
// Explorer recycles for another app simply reports the new AppId.
std::wstring EnsureButtonIdentity(FrameworkElement button) {
    if (!button || !g_taskbandResolveReady.load() || g_unloading.load()) {
        return {};
    }
    void* id = InspectableIdentity(button);
    if (!id) {
        return {};
    }
    ButtonResolveData data = CaptureButtonResolveData(button);
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    auto it = g_buttonPathCache.find(id);
    if (it != g_buttonPathCache.end() &&
        !WeakIsSameElement(it->second.button, button)) {
        g_buttonPathCache.erase(it);
    }
    auto& e = g_buttonPathCache[id];
    if (!e.button) {
        e.button = winrt::make_weak(button);
    }
    const bool windowsChanged =
        e.groupWindows.size() != data.windows.size() ||
        !std::equal(e.groupWindows.begin(), e.groupWindows.end(),
                    data.windows.begin(),
                    [](const CapturedWindow& a, const CapturedWindow& b) {
                        return a.hwnd == b.hwnd && a.pid == b.pid;
                    });
    if (e.appIdUpper != data.explorerAppId) {
        Wh_Log(L"Button identity: button=%p appId=%s (was %s) windows=%zu "
               L"running=%d",
               id, data.explorerAppId.c_str(), e.appIdUpper.c_str(),
               data.windows.size(), data.running);
        e.lastPaintRank = -1;
    } else if (windowsChanged) {
        e.lastPaintRank = -1;
    }
    if (e.uncombined != data.uncombined) {
        e.lastPaintRank = -1;
    }
    e.appIdUpper = data.explorerAppId;
    e.uncombined = data.uncombined;
    e.sampleHwnd = data.hwnd;
    e.samplePid = data.pid;
    e.groupWindows = std::move(data.windows);
    return e.appIdUpper;
}

ButtonIdentity GetCachedButtonIdentity(FrameworkElement button) {
    ButtonIdentity out;
    if (!button) {
        return out;
    }
    void* id = InspectableIdentity(button);
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    auto it = id ? g_buttonPathCache.find(id) : g_buttonPathCache.end();
    if (it == g_buttonPathCache.end()) {
        return out;
    }
    if (!WeakIsSameElement(it->second.button, button)) {
        g_buttonPathCache.erase(it);
        return out;
    }
    const auto& e = it->second;
    out.appIdUpper = e.appIdUpper;
    out.uncombined = e.uncombined;
    out.sampleHwnd = e.sampleHwnd;
    out.samplePid = e.samplePid;
    out.groupWindows = e.groupWindows;
    return out;
}

PaintCacheState GetCachedPaintState(FrameworkElement button) {
    PaintCacheState out;
    if (!button) {
        return out;
    }
    void* id = InspectableIdentity(button);
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    auto it = id ? g_buttonPathCache.find(id) : g_buttonPathCache.end();
    if (it == g_buttonPathCache.end()) {
        return out;
    }
    if (!WeakIsSameElement(it->second.button, button)) {
        g_buttonPathCache.erase(it);
        return out;
    }
    out.rank = it->second.lastPaintRank;
    out.settingsGen = it->second.lastPaintSettingsGen;
    out.accent = it->second.lastPaintAccent;
    out.edge = it->second.lastPaintEdge;
    out.boxW = it->second.lastPaintBoxW;
    out.boxH = it->second.lastPaintBoxH;
    return out;
}

void SetCachedPaintState(FrameworkElement button,
                         int rank,
                         uint32_t gen,
                         TaskbarEdge edge,
                         int boxW,
                         int boxH) {
    if (!button) {
        return;
    }
    void* id = InspectableIdentity(button);
    if (!id) {
        return;
    }
    const uint32_t accent = g_cachedAccent.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    auto it = g_buttonPathCache.find(id);
    if (it != g_buttonPathCache.end() &&
        !WeakIsSameElement(it->second.button, button)) {
        g_buttonPathCache.erase(it);
        it = g_buttonPathCache.end();
    }
    if (it == g_buttonPathCache.end()) {
        ButtonPathCacheEntry stub;
        stub.button = winrt::make_weak(button);
        stub.lastPaintRank = rank;
        stub.lastPaintSettingsGen = gen;
        stub.lastPaintAccent = accent;
        stub.lastPaintEdge = edge;
        stub.lastPaintBoxW = boxW;
        stub.lastPaintBoxH = boxH;
        g_buttonPathCache.emplace(id, std::move(stub));
        return;
    }
    it->second.lastPaintRank = rank;
    it->second.lastPaintSettingsGen = gen;
    it->second.lastPaintAccent = accent;
    it->second.lastPaintEdge = edge;
    it->second.lastPaintBoxW = boxW;
    it->second.lastPaintBoxH = boxH;
}

template <typename Fn>
void ForEachLiveElementOnThisDispatcher(
    const std::vector<winrt::weak_ref<FrameworkElement>>& weaks,
    Fn&& fn) {
    for (auto& weak : weaks) {
        FrameworkElement el = nullptr;
        try {
            el = weak.get();
        } catch (...) {
            continue;
        }
        if (!el) {
            continue;
        }
        try {
            auto dispatcher = el.Dispatcher();
            if (!dispatcher || !dispatcher.HasThreadAccess()) {
                continue;
            }
        } catch (...) {
            continue;
        }
        fn(el);
    }
}

// ItemsRepeater keeps recycled TaskListButtons parented, parked off-screen
// (about -10000,-10000), with their last AutomationId / name (UWPSpy capture
// uwspy-nevercombine). Their cached identity can still match a rank, so a
// full rebind used to "bind" one rank to every parked copy. GetElementIndex
// is -1 for an element the repeater does not currently show. No repeater
// ancestor (unknown layout): treat as shown, as before.
bool IsRealizedTaskbarButton(FrameworkElement const& button) {
    try {
        DependencyObject child = button;
        DependencyObject cur = Media::VisualTreeHelper::GetParent(button);
        for (int depth = 0; cur && depth < 8; ++depth) {
            if (auto repeater =
                    cur.try_as<winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater>()) {
                auto element = child.try_as<UIElement>();
                return element && repeater.GetElementIndex(element) >= 0;
            }
            child = cur;
            cur = Media::VisualTreeHelper::GetParent(cur);
        }
    } catch (...) {
    }
    return true;
}

// Explorer reports a TaskListButton to the mod only when that button's
// visual state changes, so after loading into a running Explorer most
// buttons were unknown until hovered (TODO 2026-11-17). The first button
// seen on a taskbar seeds every button its ItemsRepeater currently shows
// (Fable review 3.2, option 1). Once per repeater; buttons realized later
// report themselves. Returns true if new buttons were tracked.
winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater ButtonRepeater(
    FrameworkElement const& button) {
    try {
        DependencyObject cur = Media::VisualTreeHelper::GetParent(button);
        for (int depth = 0; cur && depth < 8; ++depth) {
            if (auto repeater = cur.try_as<
                    winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater>()) {
                return repeater;
            }
            cur = Media::VisualTreeHelper::GetParent(cur);
        }
    } catch (...) {
    }
    return nullptr;
}

// Tracks every TaskListButton the repeater currently shows. Returns the
// number tracked.
int SeedRepeater_UIThread(
    winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater const& repeater) {
    int tracked = 0;
    try {
        auto source = repeater.ItemsSourceView();
        const int count = source ? source.Count() : 0;
        for (int i = 0; i < count; ++i) {
            auto el = repeater.TryGetElement(i).try_as<FrameworkElement>();
            if (!el) {
                continue;
            }
            FrameworkElement btn =
                el.Name() == L"TaskListButton"
                    ? el
                    : FindDescendantByName(el, L"TaskListButton");
            if (btn) {
                TrackButton_UIThread(btn);
                ++tracked;
            }
        }
    } catch (...) {
    }
    return tracked;
}

bool SeedButtonsFromRepeater_UIThread(FrameworkElement const& button) {
    static thread_local std::unordered_set<void*> seededRepeaters;
    auto repeater = ButtonRepeater(button);
    if (!repeater ||
        !seededRepeaters.insert(InspectableIdentity(repeater)).second) {
        return false;
    }
    const int tracked = SeedRepeater_UIThread(repeater);
    Wh_Log(L"Seeded %d taskbar buttons from their repeater", tracked);
    return tracked > 0;
}

std::vector<FrameworkElement> CollectLiveButtonsOnThisDispatcher() {
    PruneTrackedButtons_UIThread();
    PruneDeadButtonPathCache_UIThread();
    std::vector<winrt::weak_ref<FrameworkElement>> buttons;
    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        buttons.reserve(g_trackedButtons.size());
        for (auto& [id, weak] : g_trackedButtons) {
            buttons.push_back(weak);
        }
    }
    std::vector<FrameworkElement> live;
    live.reserve(buttons.size());
    ForEachLiveElementOnThisDispatcher(
        buttons, [&](FrameworkElement b) { live.push_back(b); });
    return live;
}

// A button Explorer recycled (parked off-screen): it shows nothing, so it
// stops counting as running for ranking. Returns its last painted rank
// (0 = already clear) and marks it clear.
int ParkButtonState(FrameworkElement button) {
    void* id = InspectableIdentity(button);
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    auto it = id ? g_buttonPathCache.find(id) : g_buttonPathCache.end();
    if (it == g_buttonPathCache.end() ||
        !WeakIsSameElement(it->second.button, button)) {
        return -1;
    }
    auto& e = it->second;
    e.observedRunning = false;
    e.lastRunningTick = 0;
    const int prior = e.lastPaintRank;
    e.lastPaintRank = 0;
    return prior;
}

void ApplyAllHighlights_UIThread() {
    g_lastFullRefreshTick = GetTickCount64();

    // Re-seed after a desktop switch or an unknown focused window: that
    // desktop's buttons may be newly realized and not have reported yet.
    {
        static thread_local unsigned seenGeneration = 0;
        const unsigned gen = g_buttonSeedGeneration.load();
        if (gen != seenGeneration) {
            seenGeneration = gen;
            std::unordered_set<void*> repeaters;  // one walk per taskbar
            for (auto& b : CollectLiveButtonsOnThisDispatcher()) {
                auto repeater = ButtonRepeater(b);
                if (repeater &&
                    repeaters.insert(InspectableIdentity(repeater)).second) {
                    SeedRepeater_UIThread(repeater);
                }
            }
        }
    }
    std::vector<FrameworkElement> live = CollectLiveButtonsOnThisDispatcher();
    size_t parked = 0;
    live.erase(std::remove_if(live.begin(), live.end(),
                              [&](FrameworkElement const& b) {
                                  if (IsRealizedTaskbarButton(b)) {
                                      return false;
                                  }
                                  // Parked: no longer running for ranking,
                                  // and cleared once (not every pass).
                                  if (ParkButtonState(b) != 0) {
                                      ClearButtonHighlight(b);
                                  }
                                  ++parked;
                                  return true;
                              }),
               live.end());

    // Snapshot IsRunning before eligibility. Empty ranks used to return
    // here and never refresh lastRunningTick / observedRunning, so idle
    // decay and desktop-switch clears could not recover.
    std::vector<std::wstring> buttonAppIds(live.size());
    std::vector<ButtonIdentity> idents(live.size());
    std::vector<char> running(live.size(), 0);
    // Bottom <-> top moves keep the panel size (no SizeChanged), so the pass
    // refreshes the edge; once per pass, not per button (Fable review 5).
    std::optional<TaskbarEdge> passEdge;
    for (size_t bi = 0; bi < live.size(); ++bi) {
        if (auto ip = GetIconPanel(live[bi])) {
            RefreshCachedTaskbarEdge(ip, passEdge);
        }
        buttonAppIds[bi] = EnsureButtonIdentity(live[bi]);
        running[bi] = ButtonCountsAsRunning(live[bi]) ? 1 : 0;
        if (running[bi]) {
            idents[bi] = GetCachedButtonIdentity(live[bi]);
        }
    }
    // The shown buttons are authoritative for which windows have their own
    // slot: when Never Combine is switched off, no button is uncombined any
    // more and per-window ranks merge back into one app rank (0.11.35).
    // Captures add windows as they appear; this pass also drops them.
    {
        std::unordered_set<HWND, HwndHash> shownUncombined;
        for (size_t bi = 0; bi < live.size(); ++bi) {
            if (running[bi] && idents[bi].uncombined && idents[bi].sampleHwnd) {
                shownUncombined.insert(idents[bi].sampleHwnd);
            }
        }
        std::lock_guard<std::mutex> lock(g_uncombinedMutex);
        auto& mine = g_uncombinedWindowsByThread[GetCurrentThreadId()];
        if (shownUncombined != mine) {
            Wh_Log(L"Uncombined windows (this taskbar): %zu -> %zu",
                   mine.size(), shownUncombined.size());
            mine = std::move(shownUncombined);
        }
    }

    std::vector<AppFocusInfo> ranks;
    GUID deskId{};
    {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_stateMutex);
        RecomputeRanksForDesktopLocked(CurrentDeskLocked());
        ranks = CurrentDeskLocked().rankedApps;
        deskId = g_currentDesktopId;
    }

    Wh_Log(L"ApplyAllHighlights: %zu tracked buttons (+%zu recycled, "
           L"skipped), %zu ranks desktop=%s",
           live.size(), parked, ranks.size(), GuidToLogString(deskId).c_str());
    for (size_t i = 0; i < ranks.size(); ++i) {
        if (ranks[i].perWindow) {
            Wh_Log(L"  rank list[%zu]: %s window %p \"%s\"", i + 1,
                   ranks[i].displayName.c_str(), ranks[i].lastHwnd,
                   ranks[i].lastWindowTitle.c_str());
        } else {
            Wh_Log(L"  rank list[%zu]: %s", i + 1, ranks[i].displayName.c_str());
        }
    }

    if (g_unloading.load() || ranks.empty()) {
        for (auto& button : live) {
            ClearButtonHighlight(button);
        }
        g_pendingOverlaySweep = false;
        if (ranks.empty()) {
            Wh_Log(L"ApplyAllHighlights: no ranks — cleared overlays on %zu "
                   L"tracked buttons",
                   live.size());
        }
        return;
    }

    // Explorer AppId (plus per-window slots). One rank may bind many buttons
    // (secondary taskbar / Never Combine); a button takes its best rank.
    std::vector<bool> rankTaken(ranks.size(), false);
    for (size_t bi = 0; bi < live.size(); ++bi) {
        int rank = 0;
        for (size_t ri = 0; running[bi] && ri < ranks.size(); ++ri) {
            if (IdentityMatchesRank(idents[bi], ranks[ri])) {
                rank = static_cast<int>(ri) + 1;
                rankTaken[ri] = true;
                Wh_Log(L"  bind rank %zu %s -> \"%s\" (appId=%s)", ri + 1,
                       ranks[ri].displayName.c_str(),
                       GetButtonAutomationName(live[bi]).c_str(),
                       buttonAppIds[bi].c_str());
                break;
            }
        }
        if (rank > 0) {
            ApplyButtonHighlight(live[bi], rank);
        } else {
            ClearButtonHighlight(live[bi]);
        }
    }

    // Sweep completed for all currently tracked buttons.
    if (g_pendingOverlaySweep.load()) {
        g_pendingOverlaySweep = false;
        Wh_Log(L"Overlay sweep finished (%zu buttons cleared/updated)",
               live.size());
    }

    // A rank without a button is logged only: it is in the list because a
    // running button with its AppId existed a moment ago (AppAppearsOnTaskbar
    // in the recompute above), so this is a transient (closing, recycling).
    for (size_t ri = 0; ri < ranks.size(); ++ri) {
        if (!rankTaken[ri]) {
            Wh_Log(L"  UNMATCHED rank %zu: %s (title=\"%s\")", ri + 1,
                   ranks[ri].displayName.c_str(),
                   ranks[ri].lastWindowTitle.c_str());
        }
    }
}

void ClearAllHighlights_UIThread() {
    for (auto& button : CollectLiveButtonsOnThisDispatcher()) {
        ClearButtonHighlight(button);
    }
}

