// ---------------------------------------------------------------------------
// Virtual desktops (public COM + explorer registry — no internal shell GUIDs)
// ---------------------------------------------------------------------------

void CancelMinFocusTimer();
void CancelPreviewMinFocusTimer();
void EnsurePendingAppTimer();
void EnsurePendingPreviewTimer();
void OnMinFocusTimerElapsed(MinFocusConfirmMode mode = MinFocusConfirmMode::FromTimer);
void OnPreviewMinFocusTimerElapsed(
    MinFocusConfirmMode mode = MinFocusConfirmMode::FromTimer);
void RequestApplyVisuals();
void RequestApplyVisualsDebounced();
void RequestApplyPreviewVisuals();
void RefreshButtonHighlight(FrameworkElement button);
void ScheduleRefreshAllHighlights(FrameworkElement dispatcherAnchor);
void RecomputeRanksForDesktopLocked(DesktopRecencyState& desk);

struct IconPanelLayoutWatch {
    winrt::weak_ref<FrameworkElement> panel;
    winrt::event_token sizeChanged{};
    TaskbarEdge lastEdge = TaskbarEdge::Bottom;
    bool haveEdge = false;
};
std::mutex g_layoutWatchMutex;
std::unordered_map<void*, IconPanelLayoutWatch> g_layoutWatches;
thread_local int g_iconPanelRelayoutDepth = 0;
thread_local ULONGLONG g_lastFullRefreshTick = 0;

std::wstring GuidToLogString(const GUID& id) {
    wchar_t buf[64]{};
    if (StringFromGUID2(id, buf, ARRAYSIZE(buf)) > 0) {
        return buf;
    }
    return L"{?}";
}

IVirtualDesktopManager* EnsureVdm() {
    // CoCreate on the STA that will use it. UI threads must not create or
    // call this pointer — they read the cached desktop GUID instead.
    if (!OnHookThread()) {
        std::lock_guard<std::mutex> lock(g_vdmMutex);
        return g_vdm;
    }
    std::lock_guard<std::mutex> lock(g_vdmMutex);
    if (g_vdm) {
        return g_vdm;
    }
    IVirtualDesktopManager* vdm = nullptr;
    HRESULT hr = CoCreateInstance(kClsidVirtualDesktopManager, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&vdm));
    if (FAILED(hr) || !vdm) {
        Wh_Log(L"IVirtualDesktopManager create failed %08X", hr);
        return nullptr;
    }
    g_vdm = vdm;
    return g_vdm;
}

void ReleaseVdm() {
    if (!OnHookThread() && g_hookThreadId.load() != 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_vdmMutex);
    if (g_vdm) {
        g_vdm->Release();
        g_vdm = nullptr;
    }
}

bool TryGetWindowDesktopId(HWND hwnd, GUID* outId) {
    if (!hwnd || !outId) {
        return false;
    }
    IVirtualDesktopManager* vdm = EnsureVdm();
    if (!vdm) {
        return false;
    }
    GUID id{};
    HRESULT hr = vdm->GetWindowDesktopId(hwnd, &id);
    if (FAILED(hr) || InlineIsEqualGUID(id, GUID_NULL)) {
        return false;
    }
    *outId = id;
    return true;
}

bool ReadCurrentDesktopFromRegistry(GUID* outId) {
    if (!outId) {
        return false;
    }
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                      L"VirtualDesktops",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    GUID id{};
    DWORD size = sizeof(id);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(key, L"CurrentVirtualDesktop", nullptr, &type,
                               reinterpret_cast<LPBYTE>(&id), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_BINARY || size != sizeof(GUID) ||
        InlineIsEqualGUID(id, GUID_NULL)) {
        return false;
    }
    *outId = id;
    return true;
}

GUID ResolveCurrentDesktopId() {
    GUID fromReg{};
    if (ReadCurrentDesktopFromRegistry(&fromReg)) {
        return fromReg;
    }
    // Registry missing (rare). VDM is worker-thread only.
    GUID fromWnd{};
    if (OnHookThread() &&
        TryGetWindowDesktopId(GetForegroundWindow(), &fromWnd)) {
        return fromWnd;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_currentDesktopId;
}

// Desktop to credit a confirmed focus to (Fable review 2.3). The pending
// record keeps the registry's current desktop from when focus arrived, and
// the registry lags a switch by a moment. At confirm (after min-focus) the
// registry has caught up: if the window is on the current desktop (public
// IVirtualDesktopManager, worker thread), credit that one. GUID_NULL keeps
// the caller's default (current desktop).
GUID ConfirmDesktopId(const GUID& pendingDesktop, HWND hwnd) {
    GUID current{};
    if (!ReadCurrentDesktopFromRegistry(&current) ||
        InlineIsEqualGUID(current, pendingDesktop)) {
        return pendingDesktop;
    }
    IVirtualDesktopManager* vdm = EnsureVdm();
    BOOL onCurrent = FALSE;
    if (hwnd && vdm &&
        SUCCEEDED(vdm->IsWindowOnCurrentVirtualDesktop(hwnd, &onCurrent)) &&
        onCurrent) {
        return current;
    }
    return pendingDesktop;
}

// Returns true when the current-desktop id changed after the first successful
// read (i.e. a real switch, not startup). Safe to call without g_stateMutex.
bool RefreshCurrentDesktopId() {
    const GUID id = ResolveCurrentDesktopId();
    DeferredLogScope logScope;  // logs after unlock
    std::lock_guard<std::mutex> lock(g_stateMutex);
    const bool first = !g_haveCurrentDesktop;
    const bool changed = !InlineIsEqualGUID(id, g_currentDesktopId);
    g_currentDesktopId = id;
    g_haveCurrentDesktop = true;
    g_desktopMaps[id];
    if (changed) {
        LOG_DEFERRED(L"Current virtual desktop: %s (%zu tracked)",
               GuidToLogString(id).c_str(), g_desktopMaps.size());
    }
    return !first && changed;
}

// Requires g_stateMutex.
DesktopRecencyState& DeskStateLocked(const GUID& id) {
    return g_desktopMaps[id];
}

DesktopRecencyState& CurrentDeskLocked() {
    return g_desktopMaps[g_currentDesktopId];
}

void ClearButtonRunningGrace() {
    std::lock_guard<std::mutex> lock(g_buttonStateMutex);
    for (auto& [id, e] : g_buttonStates) {
        e.lastRunningTick = 0;
        e.observedRunning = false;
    }
}

// Requires g_stateMutex. True if pending was dropped.
bool DropPendingIfWrongDesktopLocked() {
    if (g_pendingFocus.valid &&
        !InlineIsEqualGUID(g_pendingFocus.desktopId, g_currentDesktopId)) {
        g_pendingFocus = {};
        return true;
    }
    return false;
}

// reason: what noticed it, for the log. The input-desktop event (lock
// screen, UAC) runs the same steps without a virtual-desktop change.
void OnVirtualDesktopSwitched(PCWSTR reason) {
    if (g_unloading.load()) {
        return;
    }
    ClearButtonRunningGrace();
    RefreshCurrentDesktopId();
    bool droppedPending = false;
    {
        DeferredLogScope logScope;  // logs after unlock
        std::lock_guard<std::mutex> lock(g_stateMutex);
        droppedPending = DropPendingIfWrongDesktopLocked();
        // Do not recompute here: grace was just cleared, so every path would
        // fail AppAppearsOnTaskbar. ApplyAllHighlights snapshots IsRunning
        // on the UI thread, then recomputes.
        LOG_DEFERRED(L"Desktop switch (%s): current %s droppedPending=%d",
               reason, GuidToLogString(g_currentDesktopId).c_str(),
               droppedPending ? 1 : 0);
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

