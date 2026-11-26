// ---------------------------------------------------------------------------
// Window-level recency (thumbnail previews)
// ---------------------------------------------------------------------------

void PruneWindowFocusMapLocked(DesktopRecencyState& desk);
void StampWindowRecencyLocked(DesktopRecencyState& desk,
                              HWND hwnd,
                              const std::wstring& processKey,
                              const std::wstring& windowTitle,
                              ULONGLONG now,
                              const std::wstring& appIdUpper = {},
                              const std::wstring& pathUpper = {});

void PruneWindowFocusMapLocked(DesktopRecencyState& desk) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG decayMs =
        DecayMsFromMinutes(SettingsSnap()->previewDecayMinutes);

    for (auto it = desk.windowFocusMap.begin();
         it != desk.windowFocusMap.end();) {
        if (!it->first || !IsWindow(it->first) ||
            !HwndMatchesStoredPid(it->first, it->second.pid)) {
            it = desk.windowFocusMap.erase(it);
            continue;
        }
        if (IsTickDecayed(it->second.lastConfirmedTick, decayMs, now)) {
            it = desk.windowFocusMap.erase(it);
            continue;
        }
        ++it;
    }
}

// Requires g_stateMutex.
void StampWindowRecencyLocked(DesktopRecencyState& desk,
                              HWND hwnd,
                              const std::wstring& processKey,
                              const std::wstring& windowTitle,
                              ULONGLONG now,
                              const std::wstring& appIdUpper,
                              const std::wstring& pathUpper) {
    if (!hwnd) {
        return;
    }
    const DWORD pid = PidFromHwnd(hwnd);
    if (!pid) {
        return;  // gone already: nothing could check a recycled handle later
    }
    auto it = desk.windowFocusMap.find(hwnd);
    if (it != desk.windowFocusMap.end() && it->second.pid != 0 &&
        it->second.pid != pid) {
        Wh_Log(L"HWND recycled, dropping stale preview recency: hwnd=%p "
               L"oldPid=%u newPid=%u",
               hwnd, it->second.pid, pid);
        desk.windowFocusMap.erase(it);
    }
    WindowFocusInfo& winfo = desk.windowFocusMap[hwnd];
    winfo.hwnd = hwnd;
    winfo.pid = pid;
    if (!processKey.empty()) {
        winfo.processKey = processKey;
    }
    if (!pathUpper.empty()) {
        winfo.pathUpper = pathUpper;
    }
    if (!windowTitle.empty()) {
        winfo.windowTitle = windowTitle;
    }
    if (!appIdUpper.empty()) {
        winfo.appIdUpper = appIdUpper;
    }
    winfo.lastConfirmedTick = now;
    winfo.confirmSeq = g_windowConfirmSeq.fetch_add(1) + 1;
    PruneWindowFocusMapLocked(desk);
}

// True if hwnd is still a non-decayed confirmed recent window on this desktop.
bool IsWindowRecentForPreviewLocked(DesktopRecencyState& desk,
                                    HWND hwnd,
                                    ULONGLONG* outTick = nullptr) {
    if (!hwnd) {
        return false;
    }
    auto it = desk.windowFocusMap.find(hwnd);
    if (it == desk.windowFocusMap.end() || it->second.lastConfirmedTick == 0) {
        return false;
    }
    if (!IsWindow(hwnd) || !HwndMatchesStoredPid(hwnd, it->second.pid)) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG decayMs =
        DecayMsFromMinutes(SettingsSnap()->previewDecayMinutes);
    if (IsTickDecayed(it->second.lastConfirmedTick, decayMs, now)) {
        return false;
    }
    if (outTick) {
        *outTick = it->second.lastConfirmedTick;
    }
    return true;
}

