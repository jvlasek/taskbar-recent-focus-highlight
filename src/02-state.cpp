// ---------------------------------------------------------------------------
// Focus / recency state
// ---------------------------------------------------------------------------

struct AppFocusInfo {
    std::wstring key;          // APPID:<Explorer AppId> or provisional WINDOW:…
    std::wstring displayName;  // e.g. WindowsTerminal.exe
    std::wstring lastWindowTitle;  // unmatched-rank logs only
    std::wstring pathUpper;    // process image path (exclusion sweeps)
    std::wstring appIdUpper;   // window AppUserModelID when present
    HWND lastHwnd = nullptr;
    DWORD lastPid = 0;  // reject recycled lastHwnd (preview already does this)
    ULONGLONG lastConfirmedFocusTick = 0;
    // Rank unit is one window (lastHwnd), not the app: set on the per-window
    // slots RecomputeRanks makes for uncombined buttons (Never Combine).
    bool perWindow = false;
};

struct PendingFocus {
    HWND hwnd = nullptr;
    DWORD processId = 0;
    std::wstring key;
    std::wstring displayName;
    std::wstring windowTitle;
    std::wstring appIdUpper;  // window AUMID (exclusion only)
    std::wstring pathUpper;   // process image path (exclusion only)
    ULONGLONG focusStartTick = 0;
    ULONGLONG previewStartTick = 0;  // HWND-level; resets when instance changes
    // Post-deadline Alt-Tab / tray / IME wait. 0 = still inside min-focus.
    ULONGLONG transientRetryStartTick = 0;
    ULONGLONG previewTransientRetryStartTick = 0;
    GUID desktopId{};
    bool valid = false;
    bool appConfirmed = false;
    bool previewConfirmed = false;
    // App confirm found no AppId yet: one short wait for a re-seeded pass.
    bool appIdRetried = false;
};

// Per-window recency for multi-instance thumbnail previews (separate timers).
struct WindowFocusInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;              // reject recycled HWND with a new process
    std::wstring processKey;    // app key: APPID:<Explorer AppId>
    std::wstring pathUpper;     // process image path (exclusion sweeps)
    std::wstring windowTitle;   // log / exclude display; not an identity key
    std::wstring appIdUpper;    // Win32 window AUMID for exclusion sweeps
    ULONGLONG lastConfirmedTick = 0;
    ULONGLONG confirmSeq = 0;  // unique per confirm (breaks GetTickCount ties)
};

struct HwndHash {
    size_t operator()(HWND h) const noexcept {
        return std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(h));
    }
};

// Explorer's AppId per taskbar window, from the button captures (UI
// thread); read by the focus worker to key app recency. Upper case. Stored
// with the window's PID so a recycled handle does not inherit an AppId
// (Fable review 2.4). Pruned of dead windows when it grows, and on
// HSHELL_WINDOWDESTROYED.
struct WindowAppIdEntry {
    std::wstring appIdUpper;
    DWORD pid = 0;
};
std::mutex g_windowAppIdMutex;
std::unordered_map<HWND, WindowAppIdEntry, HwndHash> g_windowAppId;

DWORD WindowPid(HWND hwnd) {
    DWORD pid = 0;
    if (hwnd) {
        GetWindowThreadProcessId(hwnd, &pid);
    }
    return pid;
}

void PublishWindowAppIds(const std::vector<HWND>& windows,
                         const std::wstring& appIdUpper) {
    if (appIdUpper.empty() || windows.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_windowAppIdMutex);
    for (HWND h : windows) {
        g_windowAppId[h] = WindowAppIdEntry{appIdUpper, WindowPid(h)};
    }
    if (g_windowAppId.size() > 512) {
        for (auto it = g_windowAppId.begin(); it != g_windowAppId.end();) {
            it = IsWindow(it->first) ? std::next(it) : g_windowAppId.erase(it);
        }
    }
}

std::wstring LookupWindowAppId(HWND hwnd) {
    std::lock_guard<std::mutex> lock(g_windowAppIdMutex);
    auto it = g_windowAppId.find(hwnd);
    if (it == g_windowAppId.end()) {
        return {};
    }
    if (it->second.pid && WindowPid(hwnd) != it->second.pid) {
        g_windowAppId.erase(it);  // handle recycled into another process
        return {};
    }
    return it->second.appIdUpper;
}

void ForgetWindowAppId(HWND hwnd) {
    std::lock_guard<std::mutex> lock(g_windowAppIdMutex);
    g_windowAppId.erase(hwnd);
}

struct GuidHash {
    size_t operator()(const GUID& g) const noexcept {
        size_t h = 0;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&g);
        for (int i = 0; i < 16; ++i) {
            h = h * 131u + p[i];
        }
        return h;
    }
};

struct GuidEqual {
    bool operator()(const GUID& a, const GUID& b) const noexcept {
        return InlineIsEqualGUID(a, b) != 0;
    }
};

// App + window recency for one virtual desktop (session-wide, not per-monitor).
// App-level (min-focus) confirmation of one window. Only used to split an
// app into per-window rank slots when its windows have their own taskbar
// buttons ("Never combine" / "combine when full"): one button = one unit.
struct WindowAppFocus {
    std::wstring key;
    std::wstring title;
    DWORD pid = 0;
    ULONGLONG tick = 0;
};

// Windows that own a taskbar button inside a multi-window group (an
// uncombined button). Written by the UI capture, read by RecomputeRanks.
// Lock order: g_stateMutex, then g_uncombinedMutex (never the reverse).
std::mutex g_uncombinedMutex;
// Per UI thread (one per taskbar dispatcher), so one taskbar's full pass
// cannot drop another taskbar's windows; readers use the union.
std::unordered_map<DWORD, std::unordered_set<HWND, HwndHash>>
    g_uncombinedWindowsByThread;

struct DesktopRecencyState {
    std::unordered_map<HWND, WindowAppFocus, HwndHash> windowAppFocus;
    std::unordered_map<std::wstring, AppFocusInfo> appFocusMap;
    std::vector<AppFocusInfo> rankedApps;
    std::unordered_map<HWND, WindowFocusInfo, HwndHash> windowFocusMap;
};

std::mutex g_stateMutex;

// Log lines produced while g_stateMutex is held are queued per thread and
// written once the outermost DeferredLogScope ends, prefixed with the
// function that produced them (Wh_Log's own prefix shows the destructor). Declare the scope before
// the lock so it is destroyed after it: Wh_Log is OutputDebugString plus
// file I/O, and the UI thread waits on this lock. Without a scope, logs
// directly.
thread_local std::vector<std::wstring> t_deferredLogs;
thread_local int t_deferredLogDepth = 0;

struct DeferredLogScope {
    DeferredLogScope() { ++t_deferredLogDepth; }
    ~DeferredLogScope() {
        if (--t_deferredLogDepth != 0 || t_deferredLogs.empty()) {
            return;
        }
        std::vector<std::wstring> lines;
        lines.swap(t_deferredLogs);
        for (const auto& line : lines) {
            Wh_Log(L"%s", line.c_str());
        }
    }
    DeferredLogScope(const DeferredLogScope&) = delete;
    DeferredLogScope& operator=(const DeferredLogScope&) = delete;
};

#define LOG_DEFERRED(...)                                              \
    do {                                                               \
        if (t_deferredLogDepth == 0) {                                 \
            Wh_Log(__VA_ARGS__);                                       \
        } else {                                                       \
            wchar_t deferredLogBuf_[1024];                             \
            const int deferredLogPre_ = _snwprintf_s(                  \
                deferredLogBuf_, _countof(deferredLogBuf_), _TRUNCATE, \
                L"(%S) ", __func__);                                   \
            if (deferredLogPre_ > 0) {                                 \
                _snwprintf_s(deferredLogBuf_ + deferredLogPre_,        \
                             _countof(deferredLogBuf_) -               \
                                 deferredLogPre_,                      \
                             _TRUNCATE, __VA_ARGS__);                  \
            }                                                          \
            t_deferredLogs.emplace_back(deferredLogBuf_);              \
        }                                                              \
    } while (0)
std::unordered_map<GUID, DesktopRecencyState, GuidHash, GuidEqual> g_desktopMaps;
GUID g_currentDesktopId{};
bool g_haveCurrentDesktop = false;
PendingFocus g_pendingFocus;

std::atomic<ULONGLONG> g_windowConfirmSeq{0};

// Public IVirtualDesktopManager (fallback when registry current-desktop fails).
IVirtualDesktopManager* g_vdm = nullptr;
std::mutex g_vdmMutex;

// HWND plus the PID captured with it. A recycled handle must not match.
struct CapturedWindow {
    HWND hwnd = nullptr;
    DWORD pid = 0;
};

// One synchronous snapshot of a taskbar button (UI thread). Only value data
// is kept; no XAML or task-item pointers outlive the capture.
struct ButtonResolveData {
    void* buttonId = nullptr;  // opaque lookup key
    HWND hwnd = nullptr;       // first window of the button
    DWORD pid = 0;
    bool running = false;
    std::vector<CapturedWindow> windows;
    std::wstring explorerAppId;  // ITaskGroup/ITaskItem AppId (upper case)
    bool uncombined = false;     // Never Combine: stands for one window
};


struct ButtonPathCacheEntry {
    winrt::weak_ref<FrameworkElement> button;
    // Explorer's AppId for this button (ITaskGroup / ITaskItem get_AppId,
    // upper case), refreshed synchronously on every full pass.
    std::wstring appIdUpper;
    HWND sampleHwnd = nullptr;  // first window of the button
    DWORD samplePid = 0;
    std::vector<CapturedWindow> groupWindows;
    bool uncombined = false;  // own-window button (Never Combine)
    ULONGLONG lastRunningTick = 0;  // last time IsRunning was true (Alt-Tab grace)
    bool observedRunning = false;   // last UI snapshot; not a 400ms heartbeat
    // Last ApplyAllHighlights assignment: -1 unknown, 0 none, >0 1-based rank.
    int lastPaintRank = -1;
    uint32_t lastPaintSettingsGen = 0;
    uint32_t lastPaintAccent = 0;
    TaskbarEdge lastPaintEdge = TaskbarEdge::Bottom;
    int lastPaintBoxW = 0;
    int lastPaintBoxH = 0;
};
std::mutex g_buttonPathMutex;
std::unordered_map<void*, ButtonPathCacheEntry> g_buttonPathCache;
// Button identity lookups (Taskbar.View.dll symbols) are hooked and usable.
std::atomic<bool> g_taskbandResolveReady{false};

// Live thumbnail views for unload / re-apply while flyout is open.
std::mutex g_thumbViewsMutex;
std::vector<winrt::weak_ref<FrameworkElement>> g_trackedThumbViews;

std::atomic<bool> g_unloading{false};
std::atomic<bool> g_taskbarViewHookAttempted{false};
// After decay / empty ranks / desktop switch: ApplyAllHighlights must visit
// every button. UVS must not clear chrome just because this is set (flicker).
std::atomic<bool> g_pendingOverlaySweep{false};
std::atomic<bool> g_decayTimerArmed{false};
// Bumped when buttons the mod has not seen may exist (desktop switch, a
// focused window no captured button lists): the next full pass re-seeds
// every taskbar repeater (SeedButtonsFromRepeater_UIThread).
std::atomic<unsigned> g_buttonSeedGeneration{0};
// A desktop switch also re-seeds once more on the next debounced pass:
// Explorer realizes that desktop's buttons a moment after the switch.
std::atomic<bool> g_reseedOnDebouncedPass{false};
// Hook thread only. Skip re-arming so a hover storm cannot postpone forever.
bool g_fullRebindTimerArmed = false;

std::mutex g_winEventHookThreadMutex;
std::atomic<HANDLE> g_winEventHookThread{nullptr};
std::atomic<HWND> g_hookThreadHwnd{nullptr};
std::atomic<DWORD> g_hookThreadId{0};
HANDLE g_hookThreadReadyEvent = nullptr;

void SignalHookThreadReady() {
    if (g_hookThreadReadyEvent) {
        SetEvent(g_hookThreadReadyEvent);
    }
}

HWND HookThreadWindow() {
    return g_hookThreadHwnd.load(std::memory_order_acquire);
}

bool PostToHookThread(UINT msg, WPARAM wParam = 0, LPARAM lParam = 0) {
    HWND hwnd = HookThreadWindow();
    return hwnd && PostMessage(hwnd, msg, wParam, lParam);
}

bool OnHookThread() {
    DWORD id = g_hookThreadId.load(std::memory_order_acquire);
    return id != 0 && GetCurrentThreadId() == id;
}

// Canonical COM identity for hash maps. The weak_ref in the value is the
// liveness token; recycled pointers are dropped when the weak_ref dies.
void* InspectableIdentity(winrt::Windows::Foundation::IInspectable const& obj) {
    if (!obj) {
        return nullptr;
    }
    try {
        auto unk = obj.as<::IUnknown>();
        return winrt::get_abi(unk);
    } catch (...) {
        return nullptr;
    }
}

bool WeakIsSameElement(winrt::weak_ref<FrameworkElement> const& weak,
                       FrameworkElement expected) {
    if (!expected) {
        return false;
    }
    try {
        auto live = weak.get();
        return live && live == expected;
    } catch (...) {
        return false;
    }
}

// UI-thread tracking of task list buttons (weak refs, keyed by IUnknown*).
std::mutex g_buttonsMutex;
std::unordered_map<void*, winrt::weak_ref<FrameworkElement>> g_trackedButtons;

// Agile CoreDispatcher list — captured on the UI thread when we first see a
// button/thumb/panel. CollectUiDispatchers must not weak.get() XAML objects
// off the UI thread (last-ref ~FrameworkElement would run on the wrong thread).
// no_destroy: process teardown must not ~CoreDispatcher after XAML is gone.
std::mutex g_dispatchersMutex;
struct UiDispatcher {
    winrt::Windows::UI::Core::CoreDispatcher dispatcher{nullptr};
    winrt::handle thread;
    DWORD threadId;
};
[[clang::no_destroy]] std::optional<
    std::vector<std::shared_ptr<UiDispatcher>>>
    g_uiDispatchers{std::in_place};

bool RememberUiDispatcher(FrameworkElement el) {
    if (!el || g_unloading.load()) {
        return false;
    }
    try {
        auto dispatcher = el.Dispatcher();
        if (!dispatcher || !dispatcher.HasThreadAccess()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(g_dispatchersMutex);
        if (!g_uiDispatchers) {
            return false;
        }
        for (const auto& existing : *g_uiDispatchers) {
            if (existing->dispatcher == dispatcher) {
                return true;
            }
        }
        // Capture a real handle while on its owning thread. An ID can be
        // reused after termination; failure to open it later proves nothing.
        HANDLE thread = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                             GetCurrentProcess(), &thread, SYNCHRONIZE,
                             FALSE, 0)) {
            Wh_Log(L"Cannot capture UI thread: %u", GetLastError());
            return false;
        }
        winrt::handle ownedThread{thread};
        auto entry = std::make_shared<UiDispatcher>();
        entry->dispatcher = dispatcher;
        entry->thread = std::move(ownedThread);
        entry->threadId = GetCurrentThreadId();
        g_uiDispatchers->push_back(std::move(entry));
        return true;
    } catch (...) {
        return false;
    }
}

bool DispatcherTryRun(
    winrt::Windows::UI::Core::CoreDispatcher const& dispatcher,
    winrt::Windows::UI::Core::CoreDispatcherPriority prio,
    winrt::Windows::UI::Core::DispatchedHandler const& handler);

constexpr UINT WM_APP_FOREGROUND_CHANGED = WM_APP + 1;
constexpr UINT WM_APP_DESKTOP_SWITCHED = WM_APP + 2;
constexpr UINT WM_APP_SHUTDOWN = WM_APP + 3;
constexpr UINT WM_APP_REQUEST_APPLY_DEBOUNCED = WM_APP + 4;
constexpr UINT WM_APP_REFRESH_ACCENT = WM_APP + 5;
constexpr UINT WM_APP_SETTINGS_CHANGED = WM_APP + 6;

constexpr UINT_PTR kMinFocusTimerId = 1;
constexpr UINT_PTR kDecayTimerId = 2;
constexpr UINT_PTR kPreviewMinFocusTimerId = 3;
constexpr UINT_PTR kFullRebindTimerId = 4;
// Owned and accessed only by the focus thread.
UINT g_shellHookMessage = 0;
constexpr UINT kDecayCheckIntervalMs = 30 * 1000;
constexpr ULONGLONG kIsRunningGraceMs = 400;
// Full identity rebind (all buttons). UVS only re-paints the cached rank;
// siblings whose visuals Windows cleared without another UVS wait for this.
constexpr ULONGLONG kFullRebindDebounceMs = 300;
// After min-focus elapsed, poll this often while Alt-Tab / tray holds FG.
constexpr ULONGLONG kTransientRetryPollMs = 200;

// All glow layers live on our overlay (never BackgroundElement — hover/active
// storyboards own that and constantly wipe our styles). The mod writes no
// property of a native element (the icon size boost went in 0.11.38).
constexpr PCWSTR kGlowElementName = L"WhRecentFocusGlow";
constexpr PCWSTR kGlowLayerNames[] = {
    L"WhRecentFocusGlowL0",
    L"WhRecentFocusGlowL1",
};
constexpr int kGlowMaxLayers = 2;

// Intensity for a 0-based rank: the three configured values, then each
// further rank fades by a quarter (floor 10 %, never above rank 3) so ranks
// 4+ stay distinguishable (0.11.38; they used to repeat rank 3).
int RankIntensity(const int (&perRank)[3], int rankIdx) {
    if (rankIdx < 3) {
        return perRank[rankIdx < 0 ? 0 : rankIdx];
    }
    double v = perRank[2];
    for (int r = 3; r <= rankIdx; ++r) {
        v *= 0.75;
    }
    const int floor = (std::min)(perRank[2], 10);
    return (std::max)(floor, static_cast<int>(v + 0.5));
}
constexpr PCWSTR kBackgroundElementName = L"BackgroundElement";
// Thumbnail preview glow (own named overlays on TaskItemThumbnailView).
constexpr PCWSTR kThumbGlowElementName = L"WhRecentFocusThumbGlow";
constexpr PCWSTR kThumbGlowLayerNames[] = {
    L"WhRecentFocusThumbGlowL0",
    L"WhRecentFocusThumbGlowL1",
};
// Title-area overlays (separate so plate host can sit full-card).
constexpr PCWSTR kThumbTitleBgName = L"WhRecentFocusThumbTitleBg";
constexpr PCWSTR kThumbTitleBarName = L"WhRecentFocusThumbTitleBar";
// Marker: we tinted native BackgroundBorder. Tag holds the previous Brush
// (or is empty if the local value was unset) so Taskbar Styler survives clear.

