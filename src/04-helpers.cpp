// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ULONGLONG DecayMsFromMinutes(int minutes) {
    if (minutes <= 0) {
        return 0;
    }
    return static_cast<ULONGLONG>(minutes) * 60ULL * 1000ULL;
}

bool IsTickDecayed(ULONGLONG tick, ULONGLONG decayMs, ULONGLONG now) {
    return decayMs > 0 && tick != 0 && now - tick > decayMs;
}

// KillTimer does not dequeue a WM_TIMER already posted. Re-check this
// candidate's start tick and arm the remainder instead of confirming early.
ULONGLONG RemainingDeadlineMs(ULONGLONG startTick, int seconds, ULONGLONG now) {
    if (seconds <= 0) {
        return 0;
    }
    const ULONGLONG need = static_cast<ULONGLONG>(seconds) * 1000ULL;
    if (now < startTick) {
        return need;
    }
    const ULONGLONG elapsed = now - startTick;
    if (elapsed >= need) {
        return 0;
    }
    return need - elapsed;
}

// Extra wait after the min-focus deadline while Alt-Tab / tray / IME holds
// FG (the landed app often sends no second FOREGROUND). Same duration as
// the original wait, at least 2s, then drop the candidate.
ULONGLONG TransientGraceMs(int minFocusSeconds) {
    const ULONGLONG wait =
        static_cast<ULONGLONG>((std::max)(0, minFocusSeconds)) * 1000ULL;
    return (std::max)(2000ULL, wait);
}

// Remaining delay while a transient window holds FG. 0 = give up.
// Updates retryStartTick (0 until the original deadline has passed).
ULONGLONG TransientRetryDelayMs(ULONGLONG startTick,
                                int minFocusSeconds,
                                ULONGLONG now,
                                ULONGLONG& retryStartTick) {
    const ULONGLONG remaining =
        RemainingDeadlineMs(startTick, minFocusSeconds, now);
    if (remaining > 0) {
        retryStartTick = 0;
        return remaining;
    }
    if (retryStartTick == 0) {
        retryStartTick = now;
    }
    if (now - retryStartTick >= TransientGraceMs(minFocusSeconds)) {
        return 0;
    }
    return kTransientRetryPollMs;
}

UINT ClampWinTimerMs(ULONGLONG ms) {
    constexpr ULONGLONG kMax = 0x7FFFFFFFULL;
    if (ms == 0) {
        return 1;
    }
    if (ms > kMax) {
        return static_cast<UINT>(kMax);
    }
    return static_cast<UINT>(ms);
}

void ArmHookTimer(UINT_PTR id, ULONGLONG ms) {
    HWND hwnd = HookThreadWindow();
    if (hwnd) {
        SetTimer(hwnd, id, ClampWinTimerMs(ms), nullptr);
    }
}

void DisarmHookTimer(UINT_PTR id) {
    HWND hwnd = HookThreadWindow();
    if (hwnd) {
        KillTimer(hwnd, id);
    }
}

// Requires g_stateMutex.
bool RecencyMapsEmptyLocked() {
    for (const auto& [id, desk] : g_desktopMaps) {
        (void)id;
        if (!desk.appFocusMap.empty() || !desk.windowAppFocus.empty() ||
            !desk.windowFocusMap.empty()) {
            return false;
        }
    }
    return true;
}

void EnsureDecayTimerArmed() {
    if (!OnHookThread()) {
        return;
    }
    if (g_decayTimerArmed.exchange(true)) {
        return;
    }
    ArmHookTimer(kDecayTimerId, kDecayCheckIntervalMs);
    Wh_Log(L"Decay timer armed");
}

void StopDecayTimerIfIdle() {
    if (!OnHookThread()) {
        return;
    }
    bool idle = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        idle = RecencyMapsEmptyLocked() && !g_pendingFocus.valid;
    }
    if (idle && g_decayTimerArmed.exchange(false)) {
        DisarmHookTimer(kDecayTimerId);
        Wh_Log(L"Decay timer stopped (no tracked apps/windows)");
    }
}

std::wstring ToUpper(std::wstring s) {
    if (!s.empty()) {
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, s.data(),
                      static_cast<int>(s.length()), s.data(),
                      static_cast<int>(s.length()), nullptr, nullptr, 0);
    }
    return s;
}

thread_local DWORD g_imagePathCachePid = 0;
thread_local std::wstring g_imagePathCache;
thread_local int g_imagePathCacheScope = 0;

struct ProcessImagePathCacheScope {
    ProcessImagePathCacheScope() { ++g_imagePathCacheScope; }
    ~ProcessImagePathCacheScope() {
        if (--g_imagePathCacheScope <= 0) {
            g_imagePathCacheScope = 0;
            g_imagePathCachePid = 0;
            std::wstring().swap(g_imagePathCache);
        }
    }
};

std::wstring GetProcessImagePath(DWORD processId) {
    if (g_imagePathCacheScope > 0 && processId != 0 &&
        processId == g_imagePathCachePid) {
        return g_imagePathCache;
    }
    std::wstring path;
    HANDLE hProcess =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) {
        return path;
    }

    auto remember = [&]() {
        if (g_imagePathCacheScope > 0 && processId != 0) {
            g_imagePathCachePid = processId;
            g_imagePathCache = path;
        }
    };

    DWORD size = MAX_PATH;
    for (int attempt = 0; attempt < 8; ++attempt) {
        path.assign(size, L'\0');
        DWORD n = size;
        if (QueryFullProcessImageName(hProcess, 0, path.data(), &n)) {
            path.resize(n);
            CloseHandle(hProcess);
            remember();
            return path;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            path.clear();
            break;
        }
        // Failure does not report the required size — double until the cap.
        if (size >= 32768) {
            path.clear();
            break;
        }
        size = (size > 16384) ? 32768 : size * 2;
    }
    CloseHandle(hProcess);
    remember();
    return path;
}

DWORD PidFromHwnd(HWND hwnd) {
    DWORD pid = 0;
    if (hwnd) {
        GetWindowThreadProcessId(hwnd, &pid);
    }
    return pid;
}

// False when the handle was recycled into a different process.
bool HwndMatchesStoredPid(HWND hwnd, DWORD storedPid) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    if (!storedPid) {
        return true;
    }
    const DWORD pid = PidFromHwnd(hwnd);
    // pid 0: window is gone / inaccessible — not a match (handle recycle).
    return pid != 0 && pid == storedPid;
}

std::wstring FileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

bool IsOwnExplorerProcess(DWORD processId) {
    return processId == GetCurrentProcessId();
}

std::wstring AppIdFromAppKey(const std::wstring& key);
std::wstring CanonicalAppId(std::wstring id);

bool IsExcludedKey(const std::wstring& keyUpper,
                   const std::wstring& displayNameUpper,
                   const std::wstring& appIdUpper = {},
                   const std::wstring& pathUpper = {}) {
    auto settings = SettingsSnap();
    const auto& excluded = settings->excludedPrograms;
    if (excluded.empty()) {
        return false;
    }
    if (excluded.contains(keyUpper)) {
        return true;
    }
    if (!displayNameUpper.empty() && excluded.contains(displayNameUpper)) {
        return true;
    }
    // App keys are APPID:<Explorer AppId>; the process path comes separately.
    const std::wstring& path = pathUpper;
    if (!path.empty() && excluded.contains(path)) {
        return true;
    }
    if (!path.empty()) {
        std::wstring fileUpper = ToUpper(FileNameFromPath(path));
        if (!fileUpper.empty() && excluded.contains(fileUpper)) {
            return true;
        }
    }
    const std::wstring keyAppId = AppIdFromAppKey(keyUpper);
    if (!keyAppId.empty() && excluded.contains(keyAppId)) {
        return true;
    }
    // Win32 windows can have a path key and a separate window AUMID.
    if (!appIdUpper.empty()) {
        if (excluded.contains(appIdUpper)) {
            return true;
        }
        const std::wstring canonical = CanonicalAppId(appIdUpper);
        if (!canonical.empty() && excluded.contains(canonical)) {
            return true;
        }
    }
    return false;
}

HWND NormalizeFocusHwnd(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) {
        return nullptr;
    }
    if (GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD) {
        HWND root = GetAncestor(hWnd, GA_ROOT);
        if (root && IsWindow(root)) {
            hWnd = root;
        }
    }
    // Owned windows (dialogs such as Lister's Find box) have no taskbar
    // button of their own; Explorer reports their owner as the active item
    // (shadow log 2026-11-15: #32770 owned by TLister). Credit the owner, or
    // a Lister dialog — no AUMID, so path-keyed — counted as Total Commander.
    // Same process only; WS_EX_APPWINDOW asks for its own button, keep it.
    // Climb GW_OWNER step by step and keep the highest *visible* owner:
    // GA_ROOTOWNER would end at a hidden application window (Delphi's
    // TApplication above Lister's TSEARCHTEXT find box, 0.11.6 capture), and
    // that hidden window has no button. Stop early at an owner that asks for
    // its own button (WS_EX_APPWINDOW).
    if (!(GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_APPWINDOW)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hWnd, &pid);
        HWND best = nullptr;
        HWND owner = GetWindow(hWnd, GW_OWNER);
        for (int depth = 0; owner && depth < 8; ++depth) {
            DWORD ownerPid = 0;
            GetWindowThreadProcessId(owner, &ownerPid);
            if (!pid || ownerPid != pid) {
                break;
            }
            if (IsWindowVisible(owner)) {
                best = owner;
                if (GetWindowLong(owner, GWL_EXSTYLE) & WS_EX_APPWINDOW) {
                    break;
                }
            }
            owner = GetWindow(owner, GW_OWNER);
        }
        if (best && IsWindow(best)) {
            return best;
        }
    }
    return hWnd;
}

std::wstring GetWindowClassName(HWND hWnd) {
    WCHAR buf[256]{};
    if (!hWnd || !GetClassNameW(hWnd, buf, ARRAYSIZE(buf))) {
        return {};
    }
    return buf;
}

std::wstring GetWindowAppUserModelId(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) {
        return {};
    }
    IPropertyStore* store = nullptr;
    HRESULT hr = SHGetPropertyStoreForWindow(hWnd, IID_IPropertyStore,
                                             reinterpret_cast<void**>(&store));
    if (FAILED(hr) || !store) {
        return {};
    }
    PROPVARIANT pv;
    PropVariantInit(&pv);
    hr = store->GetValue(PKEY_AppUserModel_ID, &pv);
    std::wstring id;
    if (SUCCEEDED(hr) && pv.vt == VT_LPWSTR && pv.pwszVal && pv.pwszVal[0]) {
        id = pv.pwszVal;
    }
    PropVariantClear(&pv);
    store->Release();
    return id;
}

// The window AUMID is used only for the exclude list since 0.11.33, and
// SHGetPropertyStoreForWindow is cross-process: skip it when the list is
// empty (the default).
std::wstring ExclusionAumid(HWND hWnd) {
    if (SettingsSnap()->excludedPrograms.empty()) {
        return {};
    }
    return ToUpper(GetWindowAppUserModelId(hWnd));
}

std::wstring StripAppIdPrefix(std::wstring id) {
    // AutomationId looks like "Appid: com.squirrel.Discord.Discord"
    constexpr wchar_t kPref[] = L"Appid:";
    if (id.size() > 6 && _wcsnicmp(id.c_str(), kPref, 6) == 0) {
        id.erase(0, 6);
        while (!id.empty() && (id.front() == L' ' || id.front() == L'\t')) {
            id.erase(id.begin());
        }
    }
    return id;
}

std::wstring CanonicalAppId(std::wstring id) {
    return ToUpper(StripAppIdPrefix(std::move(id)));
}

bool IsUwpHostFileName(const std::wstring& fileNameUpper) {
    return fileNameUpper == L"APPLICATIONFRAMEHOST.EXE" ||
           fileNameUpper == L"WWAHOST.EXE";
}

// Microsoft.WindowsCalculator_8wekyb3d8bbwe!App → Microsoft.WindowsCalculator
std::wstring DisplayNameFromAppId(const std::wstring& appIdUpper) {
    std::wstring s = CanonicalAppId(appIdUpper);
    auto bang = s.find(L'!');
    if (bang != std::wstring::npos) {
        s.resize(bang);
    }
    auto us = s.rfind(L'_');
    if (us != std::wstring::npos && us > 0) {
        s.resize(us);
    }
    return s.empty() ? appIdUpper : s;
}

std::wstring AppIdFromAppKey(const std::wstring& key) {
    if (key.rfind(L"APPID:", 0) != 0) {
        return {};
    }
    return key.substr(6);
}

// Pending-focus identity of a window not (yet) listed by any captured
// taskbar button. Never stored in the recency maps.
std::wstring ProvisionalAppKey(HWND hwnd) {
    wchar_t buf[40];
    swprintf_s(buf, L"WINDOW:%p", hwnd);
    return buf;
}

std::wstring GetWindowTitle(HWND hWnd);

bool ShouldIgnoreHwnd(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) {
        return true;
    }
    if (GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD) {
        return true;
    }
    if (!IsWindowVisible(hWnd)) {
        return true;
    }

    WCHAR className[256]{};
    if (!GetClassName(hWnd, className, ARRAYSIZE(className))) {
        return true;
    }

    static const PCWSTR kIgnoredClasses[] = {
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"Shell_TrayWndDummy",
        L"Progman",
        L"WorkerW",
        L"XamlExplorerHostIslandWindow",
        L"ForegroundStaging",
        L"MultitaskingViewFrame",
        L"TaskListThumbnailWnd",
        L"Windows.Internal.Shell.TabProxyWindow",
        L"NotifyIconOverflowWindow",
        L"tooltips_class32",
    };

    for (auto ignored : kIgnoredClasses) {
        if (_wcsicmp(className, ignored) == 0) {
            return true;
        }
    }

    LONG exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        // Ignore typical tool popups, but keep sizable titled windows.
        // Total Commander Lister (and similar viewers) can be tool-styled
        // yet still appear as grouped taskbar thumbnails.
        if (GetWindowTitle(hWnd).empty()) {
            return true;
        }
        RECT rc{};
        if (!GetWindowRect(hWnd, &rc) || (rc.right - rc.left) < 200 ||
            (rc.bottom - rc.top) < 150) {
            return true;
        }
    }

    return false;
}

bool IsShellHostFileName(const std::wstring& fileNameUpper) {
    // ShellExperienceHost: jump lists, Action Center, clock/calendar flyout
    // (2026-11-15: "Jump List for Total Commander" cancelled TC's min-focus).
    // StartMenuExperienceHost was misspelled before 0.11.5 and never matched.
    return fileNameUpper == L"EXPLORER.EXE" ||
           fileNameUpper == L"SEARCHHOST.EXE" ||
           fileNameUpper == L"STARTMENUEXPERIENCEHOST.EXE" ||
           fileNameUpper == L"SHELLEXPERIENCEHOST.EXE" ||
           fileNameUpper == L"SHELLHOST.EXE" ||
           fileNameUpper == L"TEXTINPUTHOST.EXE";
}

// Ordinary File Explorer windows. Same process as the taskbar, but they are
// a real app with a taskbar button — not Alt-Tab / desktop / tray.
bool IsExplorerFolderWindowClass(const std::wstring& classUpper) {
    return classUpper == L"CABINETWCLASS" || classUpper == L"EXPLOREWCLASS";
}

// Alt-Tab UI, taskbar, desktop, IME: not a real app switch. Must not cancel
// an in-flight min-focus candidate — the landed app often sends no second
// FOREGROUND after these. Folder windows are a leave (and can be ranked).
bool IsTransientForeground(HWND hWnd) {
    hWnd = NormalizeFocusHwnd(hWnd);
    if (ShouldIgnoreHwnd(hWnd)) {
        return true;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(hWnd, &processId);
    if (!processId) {
        return true;
    }
    const std::wstring classUpper = ToUpper(GetWindowClassName(hWnd));
    if (IsExplorerFolderWindowClass(classUpper)) {
        return false;
    }
    if (IsOwnExplorerProcess(processId)) {
        return true;
    }
    return IsShellHostFileName(
        ToUpper(FileNameFromPath(GetProcessImagePath(processId))));
}

std::wstring GetWindowTitle(HWND hWnd) {
    // GetWindowTextW sends WM_GETTEXT to same-process windows, so a hung
    // Explorer folder can stall our worker and its shutdown join. Read stored
    // text without messaging; missing text is acceptable for this label.
    wchar_t buf[512]{};
    int n = InternalGetWindowText(hWnd, buf, ARRAYSIZE(buf));
    if (n <= 0) {
        return {};
    }
    return std::wstring(buf, static_cast<size_t>(n));
}

// App key = APPID:<Explorer AppId> of the taskbar button that lists this
// window (0.11.33; was the window AUMID or process path). A window that no
// captured button lists yet (just opened, or the mod just loaded and
// Explorer has not reported its buttons) gets a per-window key WINDOW:<hwnd>
// (noButton set). That key only identifies the pending focus; recency is
// recorded per window and joins its app once a button lists the window
// (DeriveAppsFromWindowsLocked). A window that never gets a button (tray
// widget) is never ranked and decays.
struct AppIdentity {
    std::wstring key;          // APPID:<AppId>, or provisional WINDOW:<hwnd>
    std::wstring displayName;  // logs / exclude list
    std::wstring windowTitle;
    std::wstring appIdUpper;   // window AUMID (exclude list only)
    std::wstring pathUpper;    // process image path (exclude list only)
    DWORD processId = 0;
    bool noButton = false;     // provisional key
};

std::optional<AppIdentity> ResolveAppIdentity(HWND hWnd) {
    hWnd = NormalizeFocusHwnd(hWnd);
    if (ShouldIgnoreHwnd(hWnd)) {
        return std::nullopt;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hWnd, &processId);
    if (!processId) {
        return std::nullopt;
    }

    const std::wstring classUpper = ToUpper(GetWindowClassName(hWnd));
    const bool explorerFolder = IsExplorerFolderWindowClass(classUpper);
    if (IsOwnExplorerProcess(processId) && !explorerFolder) {
        return std::nullopt;
    }

    std::wstring path = GetProcessImagePath(processId);
    if (path.empty()) {
        return std::nullopt;
    }

    std::wstring pathUpper = ToUpper(path);
    std::wstring fileName = FileNameFromPath(path);
    std::wstring fileNameUpper = ToUpper(fileName);

    if (IsShellHostFileName(fileNameUpper) && !explorerFolder) {
        return std::nullopt;
    }

    std::wstring appIdUpper = ExclusionAumid(hWnd);
    const std::wstring explorerAppId = LookupWindowAppId(hWnd);
    const std::wstring title = GetWindowTitle(hWnd);
    std::wstring displayName = fileName;
    if (IsUwpHostFileName(fileNameUpper)) {
        // Prefer "Settings" over WINDOWS.IMMERSIVECONTROLPANEL for logs /
        // exclude. Key remains APPID:… .
        if (!title.empty() && title.size() <= 80 &&
            title.find(L'\\') == std::wstring::npos &&
            title.find(L'/') == std::wstring::npos) {
            displayName = title;
        } else {
            displayName = DisplayNameFromAppId(explorerAppId);
        }
    }

    AppIdentity id;
    id.noButton = explorerAppId.empty();
    id.key = id.noButton ? ProvisionalAppKey(hWnd)
                         : std::wstring(L"APPID:") + explorerAppId;
    // Exclude list: exe name, path, window AUMID or Explorer AppId.
    if (IsExcludedKey(id.key, ToUpper(displayName), appIdUpper, pathUpper)) {
        Wh_Log(L"Excluded: %s", displayName.c_str());
        return std::nullopt;
    }
    id.displayName = std::move(displayName);
    id.windowTitle = title;
    id.appIdUpper = std::move(appIdUpper);
    id.pathUpper = std::move(pathUpper);
    id.processId = processId;
    return id;
}

FrameworkElement FindChildByName(FrameworkElement element, PCWSTR name) {
    if (!element) {
        return nullptr;
    }

    int childrenCount = Media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < childrenCount; i++) {
        auto child = Media::VisualTreeHelper::GetChild(element, i)
                         .try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        if (child.Name() == name) {
            return child;
        }
    }
    return nullptr;
}

FrameworkElement FindDescendantByName(FrameworkElement element, PCWSTR name) {
    if (!element) {
        return nullptr;
    }
    if (element.Name() == name) {
        return element;
    }

    int childrenCount = Media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < childrenCount; i++) {
        auto child = Media::VisualTreeHelper::GetChild(element, i)
                         .try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        if (auto found = FindDescendantByName(child, name)) {
            return found;
        }
    }
    return nullptr;
}

