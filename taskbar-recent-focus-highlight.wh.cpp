// ==WindhawkMod==
// @id              taskbar-recent-focus-highlight
// @name            Taskbar Recent Focus Highlight
// @description     Visually highlight the most recently focused running apps on the taskbar
// @version         0.8.13
// @author          Jakub Vlášek
// @github          https://github.com/jvlasek
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -lole32 -loleaut32 -lruntimeobject -lpropsys -luuid -lshell32
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.

// ==WindhawkModReadme==
/*
# Taskbar Recent Focus Highlight

Visually highlights the most recently used running applications on the taskbar
for faster context switching. Optionally glows the **most recent window** inside
multi-window thumbnail previews (e.g. several VS Code or Terminal instances).

## How it works

The mod tracks window focus (`EVENT_SYSTEM_FOREGROUND`) and keeps a ranked list
of the apps you actually stayed on (after a configurable minimum focus time).
The top N **running** taskbar buttons receive a highlight (frame, full plate,
left bar, or bottom running bar) and optional icon scale. App↔button binding
uses a process-path cache resolved from the taskband (same approach as
taskbar-volume-control-per-app), with name matching as fallback.

Separately, it tracks **per-window** recency (own min-focus / decay). When you
hover a combined taskbar icon and the flyout shows 2+ thumbnails, the most
recent window’s preview is marked (title bar, soft title tint, whole plate, or
ring — configurable). Single-window flyouts are left alone.

## Tips for testing

1. Compile and enable the mod in Windhawk (injects into `explorer.exe`).
2. Optionally enable **Debug logging**.
3. Focus apps for at least the minimum focus time (default 8s).
4. Ranked apps should show a highlight on their taskbar buttons (default: left
   bar); rank 1 is strongest.
5. Brief Alt+Tab under the minimum focus time should not change ranks.
6. Open 2+ windows of one app, focus one, hover the icon — that preview glows.
7. Disable the mod or toggle Enabled off to clear highlights.
8. Multi-monitor: the same rank should appear on every taskbar that shows
   that app. Combined-icon flyouts should mark the recent window even when
   titles differ (Total Commander Lister, etc.). Lister has its own taskbar
   icon — focusing it should highlight Lister, not Total Commander.

See the repo README.md for full settings and architecture notes.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
# Windhawk shows settings in this list order (no section headers — $group is
# not allowed by the settings schema). Names use [General] / [Icons] /
# [Previews] / [Advanced] prefixes so groups stay obvious in a flat list.

# --- General ---
- enabled: true
  $name: "[General] Enabled"
  $description: Master toggle for all highlighting (icons and previews)
- highlightCount: 3
  $name: "[General] Number of highlighted apps"
  $description: How many recent apps to boost on the taskbar (1–6 recommended)
- minFocusSeconds: 8
  $name: "[General] Minimum focus time (seconds)"
  $description: >-
    Only count an app as recent if it stays focused at least this long.
    Filters Alt+Tab noise. (Preview windows use a separate timer under Previews.)
    See “When to skip min-focus” for re-focus of apps already in the list.
- promoteMode: immediateTracked
  $name: "[General] When to skip min-focus"
  $description: >-
    Controls instant promotion when you re-focus an app (confirmed apps still
    become rank 1 once promoted — this only skips the wait timer).

    Immediate if in recency map (default): any app still in the map (even if
    not currently highlighted) promotes immediately.

    Immediate only if highlighted: instant only when the app is already in the
    top-N glow set; rank 4+ and new apps wait the full min-focus time.

    Always wait: every app focus (including re-focus) waits min-focus seconds.
  $options:
  - immediateTracked: Immediate if still in recency map (default)
  - immediateTopN: Immediate only if already highlighted (top N)
  - alwaysWait: Always wait min-focus time
- decayMinutes: 30
  $name: "[General] Decay time (minutes)"
  $description: >-
    Time after last focus before an app drops out of the taskbar highlight
    list. (Preview windows use a separate decay under Previews.)
- requireTaskbarButton: true
  $name: "[General] Only apps on the taskbar"
  $description: >-
    Ignore tray-only / tool windows that take focus but have no taskbar button
    (e.g. desktop widgets that open a popup then hide to the tray).
- excludedPrograms: [""]
  $name: "[General] Exclude list"
  $description: >-
    Apps that should never be highlighted (icons or previews). Entries can be
    process names, paths or application IDs, for example:

    mspaint.exe

    C:\Windows\System32\notepad.exe

    Microsoft.WindowsCalculator_8wekyb3d8bbwe!App

# --- Taskbar icons ---
- glowStyle: leftBar
  $name: "[Icons] Highlight style"
  $description: >-
    How ranked apps look on the taskbar. Left bar = vertical pill. Frame/Full =
    rounded rectangle. Bottom bar = our own underline (does not restyle the
    native running indicator permanently).
  $options:
  - leftBar: Left vertical bar
  - frame: Frame (hollow rounded rectangle)
  - full: Full (filled rounded rectangle)
  - bottomBar: Bottom bar (under icon)
- glowColor: accent
  $name: "[Icons] Glow color"
  $description: Base color for icon highlights (and previews)
  $options:
  - accent: System accent color
  - green: Green
  - blue: Blue
  - orange: Orange
  - white: White
  - custom: Custom (see custom glow color)
- customGlowColor: "#00C853"
  $name: "[Icons] Custom glow color"
  $description: Used when glow color is Custom (hex, e.g. #00C853)
- glowIntensityRank1: 100
  $name: "[Icons] Intensity rank 1"
  $description: Strength for the most recent app (0–100)
- glowIntensityRank2: 70
  $name: "[Icons] Intensity rank 2"
  $description: Strength for the 2nd most recent app (0–100)
- glowIntensityRank3: 45
  $name: "[Icons] Intensity rank 3"
  $description: Strength for the 3rd most recent app (0–100); also used for ranks 4+
- glowThickness: 3
  $name: "[Icons] Thickness (px)"
  $description: >-
    Frame/Full border width, or bar thickness (1–16). For left/bottom bars this
    is the bar’s short dimension.
- glowRoundness: 28
  $name: "[Icons] Roundness (%)"
  $description: >-
    Corner radius for Frame/Full (0 = square, ~25–35 = Win11, 50 ≈ pill).
    Left bar uses this for pill rounding; bottom bar ignores it.
- glowSize: 92
  $name: "[Icons] Size (%)"
  $description: >-
    Frame/Full: box size vs icon panel (≤100). Left bar: bar height %. Bottom
    bar: indicator length % of icon width (try 70–100).
- glowLayers: 2
  $name: "[Icons] Layers"
  $description: >-
    Frame/Full: nested frames (1–3). Left bar: soft outer glow layers. Bottom
    bar: ignored.
- glowFillOpacity: 40
  $name: "[Icons] Fill opacity"
  $description: >-
    0–100. Plate fill for Full; solid bar opacity for Left/Bottom. Frame uses
    stroke only. (Thumbnail tints use [Previews] Tint opacity.)
- sizeBoostRank1: 10
  $name: "[Icons] Size boost rank 1 (%)"
  $description: Subtle icon scale for rank 1 (0 = disabled)
- sizeBoostRank2: 6
  $name: "[Icons] Size boost rank 2 (%)"
  $description: Subtle icon scale for rank 2 (0 = disabled)
- sizeBoostRank3: 3
  $name: "[Icons] Size boost rank 3 (%)"
  $description: Subtle icon scale for rank 3 (0 = disabled)

# --- Thumbnail previews ---
- previewHighlightEnabled: true
  $name: "[Previews] Highlight recent window"
  $description: >-
    When hovering a multi-window taskbar icon, mark the thumbnail of the most
    recently focused window. Single-window flyouts are never highlighted.
- previewStyle: titleBar
  $name: "[Previews] Highlight style"
  $description: >-
    How to mark the recent window. Title bar = thin line under the title.
    Title background = soft wash behind the title. Plate = tint the whole card.
    Ring = hollow border around the card.
  $options:
  - titleBar: Bar under window title
  - titleBg: Title background tint
  - plate: Whole preview plate
  - ring: Ring / frame
- previewFillOpacity: 40
  $name: "[Previews] Tint opacity"
  $description: >-
    0–100. Strength of title-background wash and whole-preview plate. Title bar
    line uses full accent and ignores this. Independent of [Icons] Fill opacity.
- previewMinFocusSeconds: 1
  $name: "[Previews] Minimum focus (seconds)"
  $description: >-
    How long a window must stay focused before it counts as the “recent”
    preview. Separate from app ranking min focus (0 = immediate).
- previewDecayMinutes: 15
  $name: "[Previews] Decay (minutes)"
  $description: >-
    Drop a window from preview recency after this idle time (0 = never).
    Separate from app ranking decay.

# --- Advanced ---
- glowDebugLog: false
  $name: "[Advanced] Debug log (verbose)"
  $description: >-
    Logs glow metrics, path binds, and preview resolve details. Leave off for
    normal use; turn on when diagnosing matches.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <commctrl.h>
#include <initguid.h>
#include <propkey.h>
#include <propsys.h>
#include <psapi.h>
#include <shobjidl.h>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace winrt::Windows::UI::Xaml;

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

enum class GlowColorMode {
    Accent,
    Green,
    Blue,
    Orange,
    White,
    Custom,
};

enum class GlowStyle {
    Frame,      // hollow rounded rectangle
    Full,       // filled rounded rectangle
    LeftBar,    // vertical bar on the left of the icon
    BottomBar,  // native RunningIndicator, colored + elongated
};

// When re-focus may skip the app min-focus timer (see HandleForegroundChanged).
enum class PromoteMode {
    ImmediateTracked,  // any app still in g_appFocusMap (current default)
    ImmediateTopN,     // only if currently in g_rankedApps (highlighted)
    AlwaysWait,        // always use minFocusSeconds
};

// Thumbnail flyout highlight (independent of icon GlowStyle).
enum class PreviewStyle {
    Ring,     // hollow frame around the whole preview (placeholder)
    TitleBg,  // tint behind the window title text
    Plate,    // tint whole preview card (hover-like plate)
    TitleBar, // thin bar under the title, above the thumbnail image
};

struct {
    bool enabled = true;
    int highlightCount = 3;
    int minFocusSeconds = 8;
    PromoteMode promoteMode = PromoteMode::ImmediateTracked;
    GlowColorMode glowColor = GlowColorMode::Accent;
    std::wstring customGlowColor = L"#00C853";
    int glowIntensity[3] = {100, 70, 45};
    int sizeBoostPercent[3] = {10, 6, 3};
    GlowStyle glowStyle = GlowStyle::LeftBar;
    int glowThickness = 3;      // px
    int glowRoundness = 28;     // % of glow box
    int glowSize = 92;          // % of icon panel (clamped to fit)
    int glowLayers = 2;         // 1–3
    int glowFillOpacity = 40;   // % for Full / left / bottom icon styles
    int previewFillOpacity = 40;  // % for thumbnail plate / titleBg only
    bool glowDebugLog = false;
    int decayMinutes = 30;
    bool requireTaskbarButton = true;  // skip tray-only focus targets
    bool previewHighlightEnabled = true;
    int previewMinFocusSeconds = 1;
    int previewDecayMinutes = 15;
    PreviewStyle previewStyle = PreviewStyle::TitleBar;
    std::unordered_set<std::wstring> excludedPrograms;
} g_settings;

// ---------------------------------------------------------------------------
// Focus / recency state
// ---------------------------------------------------------------------------

struct AppFocusInfo {
    std::wstring key;          // APPID:… or PATH|CLS:class or bare path
    std::wstring displayName;  // e.g. WindowsTerminal.exe
    std::wstring lastWindowTitle;  // from focused HWND (helps button match)
    std::wstring classUpper;   // focused window class (TLister vs TTOTAL_CMD)
    std::wstring appIdUpper;   // window AppUserModelID when present
    HWND lastHwnd = nullptr;
    ULONGLONG lastConfirmedFocusTick = 0;
    // True once we saw a TaskListButton for this path (path cache / bind).
    bool seenOnTaskbar = false;
};

struct PendingFocus {
    HWND hwnd = nullptr;
    DWORD processId = 0;
    std::wstring key;
    std::wstring displayName;
    std::wstring windowTitle;
    ULONGLONG focusStartTick = 0;
    bool valid = false;
};

std::mutex g_stateMutex;
std::unordered_map<std::wstring, AppFocusInfo> g_appFocusMap;
PendingFocus g_pendingFocus;
std::vector<AppFocusInfo> g_rankedApps;

// process key (UPPER path) -> last seen AutomationProperties.Name of its button
std::unordered_map<std::wstring, std::wstring> g_keyToAutomationName;

// Per-window recency for multi-instance thumbnail previews (separate timers).
struct WindowFocusInfo {
    HWND hwnd = nullptr;
    std::wstring processKey;    // UPPER path
    std::wstring windowTitle;   // fallback match
    ULONGLONG lastConfirmedTick = 0;
    ULONGLONG confirmSeq = 0;  // unique per confirm (breaks GetTickCount ties)
};
std::atomic<ULONGLONG> g_windowConfirmSeq{0};
struct HwndHash {
    size_t operator()(HWND h) const noexcept {
        return std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(h));
    }
};
std::unordered_map<HWND, WindowFocusInfo, HwndHash> g_windowFocusMap;

// Option C: long-lived button → process path cache (resolve rarely, paint often).
struct ButtonPathCacheEntry {
    winrt::weak_ref<FrameworkElement> button;
    std::wstring pathUpper;  // empty if resolve failed / not yet tried
    std::wstring appIdUpper;
    std::wstring classUpper;
    std::wstring autoIdUpper;  // AutomationId, often "APPID: …"
    DWORD pid = 0;
    HWND sampleHwnd = nullptr;  // sample from resolve; preview uses g_windowFocusMap
    std::vector<HWND> groupHwnds;
    bool resolveAttempted = false;
    ULONGLONG lastResolveTick = 0;
};
std::mutex g_buttonPathMutex;
std::vector<ButtonPathCacheEntry> g_buttonPathCache;
std::atomic<bool> g_taskbandResolveReady{false};

// XAML TaskItemThumbnail (model) → native task item (optional hooks).
struct ThumbnailTaskItemMapping {
    winrt::weak_ref<winrt::Windows::Foundation::IInspectable> thumbnail;
    void* taskGroup = nullptr;
    void* taskItem = nullptr;
    HWND hwnd = nullptr;  // resolved at map time (stable for same-title windows)
};
std::mutex g_thumbnailMapMutex;
std::vector<ThumbnailTaskItemMapping> g_thumbnailTaskItemMapping;
std::atomic<bool> g_previewHooksReady{false};

// Live thumbnail views for unload / re-apply while flyout is open.
std::mutex g_thumbViewsMutex;
std::vector<winrt::weak_ref<FrameworkElement>> g_trackedThumbViews;

std::atomic<bool> g_unloading{false};
std::atomic<bool> g_taskbarViewDllLoaded{false};
std::atomic<bool> g_taskbarDllHooked{false};
// After decay / empty ranks, force-clear overlays on next button touch if
// RequestApplyVisuals couldn't run (sleep/wake, no dispatcher yet).
std::atomic<bool> g_pendingOverlaySweep{false};

std::mutex g_winEventHookThreadMutex;
std::atomic<HANDLE> g_winEventHookThread{nullptr};
HWND g_hookThreadHwnd = nullptr;

// UI-thread tracking of task list buttons (weak refs).
std::mutex g_buttonsMutex;
std::vector<winrt::weak_ref<FrameworkElement>> g_trackedButtons;
winrt::weak_ref<FrameworkElement> g_dispatcherAnchor;

constexpr UINT WM_APP_FOREGROUND_CHANGED = WM_APP + 1;
constexpr UINT WM_APP_REQUEST_APPLY = WM_APP + 2;
constexpr UINT WM_APP_REQUEST_PREVIEW_APPLY = WM_APP + 3;
constexpr UINT_PTR kMinFocusTimerId = 1;
constexpr UINT_PTR kDecayTimerId = 2;
constexpr UINT_PTR kPreviewMinFocusTimerId = 3;
constexpr UINT kDecayCheckIntervalMs = 30 * 1000;

// All glow layers live on our overlay (never BackgroundElement — hover/active
// storyboards own that and constantly wipe our styles).
constexpr PCWSTR kGlowElementName = L"WhRecentFocusGlow";
constexpr PCWSTR kGlowLayerNames[] = {
    L"WhRecentFocusGlowL0",
    L"WhRecentFocusGlowL1",
    L"WhRecentFocusGlowL2",
};
constexpr int kGlowMaxLayers = 3;
constexpr PCWSTR kBackgroundElementName = L"BackgroundElement";
// Present only while bottomBar style is applied to this button.
constexpr PCWSTR kBottomBarMarkerName = L"WhRecentFocusBottomBar";
// Thumbnail preview glow (own named overlays on TaskItemThumbnailView).
constexpr PCWSTR kThumbGlowElementName = L"WhRecentFocusThumbGlow";
constexpr PCWSTR kThumbGlowLayerNames[] = {
    L"WhRecentFocusThumbGlowL0",
    L"WhRecentFocusThumbGlowL1",
};
// Title-area overlays (separate so plate/ring host can sit full-card).
constexpr PCWSTR kThumbTitleBgName = L"WhRecentFocusThumbTitleBg";
constexpr PCWSTR kThumbTitleBarName = L"WhRecentFocusThumbTitleBar";
// Marker: we set local Background on BackgroundBorder / title TextBlock.
constexpr PCWSTR kThumbNativeStyleMarker = L"WhRecentFocusThumbNative";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::wstring ToUpper(std::wstring s) {
    if (!s.empty()) {
        LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, s.data(),
                      static_cast<int>(s.length()), s.data(),
                      static_cast<int>(s.length()), nullptr, nullptr, 0);
    }
    return s;
}

// Keep A–Z / 0–9 only, uppercased — for fuzzy exe ↔ automation-name match.
std::wstring AlnumUpper(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t ch : s) {
        if (ch >= L'a' && ch <= L'z') {
            out.push_back(static_cast<wchar_t>(ch - L'a' + L'A'));
        } else if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) {
            out.push_back(ch);
        }
    }
    return out;
}

std::wstring GetProcessImagePath(DWORD processId) {
    std::wstring path;
    HANDLE hProcess =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) {
        return path;
    }

    WCHAR buffer[MAX_PATH];
    DWORD size = ARRAYSIZE(buffer);
    if (QueryFullProcessImageName(hProcess, 0, buffer, &size)) {
        path.assign(buffer, size);
    }
    CloseHandle(hProcess);
    return path;
}

std::wstring FileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::wstring StripExtension(std::wstring name) {
    size_t pos = name.find_last_of(L'.');
    if (pos != std::wstring::npos && pos > 0) {
        name.resize(pos);
    }
    return name;
}

bool IsOwnExplorerProcess(DWORD processId) {
    return processId == GetCurrentProcessId();
}

std::wstring PathFromAppKey(const std::wstring& key);
std::wstring AppIdFromAppKey(const std::wstring& key);
std::wstring ClassFromAppKey(const std::wstring& key);

bool IsExcludedKey(const std::wstring& keyUpper,
                   const std::wstring& displayNameUpper) {
    if (g_settings.excludedPrograms.empty()) {
        return false;
    }
    if (g_settings.excludedPrograms.contains(keyUpper)) {
        return true;
    }
    if (!displayNameUpper.empty() &&
        g_settings.excludedPrograms.contains(displayNameUpper)) {
        return true;
    }
    const std::wstring path = PathFromAppKey(keyUpper);
    if (!path.empty() && g_settings.excludedPrograms.contains(path)) {
        return true;
    }
    if (!path.empty()) {
        std::wstring fileUpper = ToUpper(FileNameFromPath(path));
        if (!fileUpper.empty() &&
            g_settings.excludedPrograms.contains(fileUpper)) {
            return true;
        }
    }
    const std::wstring appId = AppIdFromAppKey(keyUpper);
    if (!appId.empty() && g_settings.excludedPrograms.contains(appId)) {
        return true;
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
            return root;
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

std::wstring MakeAppKey(const std::wstring& pathUpper,
                        const std::wstring& /*appIdUpper*/,
                        const std::wstring& /*classUpper*/) {
    // Two one-way streets (same as volume-per-app):
    //   focus HWND → PID → image path
    //   TaskListButton → taskband HWND → PID → image path
    // Join key is the path. Class/AppId are scoring hints only (TC vs Lister).
    // Do not put AppId or class in the key: Windhawk's editor is VSCodium.exe
    // with AppId RAMENSOFTWARE.WINDHAWK — that is still one taskbar button.
    return pathUpper;
}

std::wstring PathFromAppKey(const std::wstring& key) {
    if (key.rfind(L"APPID:", 0) == 0) {
        return {};
    }
    auto pos = key.find(L"|CLS:");
    if (pos != std::wstring::npos) {
        return key.substr(0, pos);
    }
    return key;
}

std::wstring ClassFromAppKey(const std::wstring& key) {
    auto pos = key.find(L"|CLS:");
    if (pos == std::wstring::npos) {
        return {};
    }
    return key.substr(pos + 5);
}

std::wstring AppIdFromAppKey(const std::wstring& key) {
    if (key.rfind(L"APPID:", 0) != 0) {
        return {};
    }
    std::wstring rest = key.substr(6);
    auto pos = rest.find(L"|CLS:");
    if (pos != std::wstring::npos) {
        rest.resize(pos);
    }
    return rest;
}

bool SamePidAndClass(HWND a, HWND b) {
    if (!a || !b || !IsWindow(a) || !IsWindow(b)) {
        return false;
    }
    DWORD pa = 0, pb = 0;
    GetWindowThreadProcessId(a, &pa);
    GetWindowThreadProcessId(b, &pb);
    if (!pa || pa != pb) {
        return false;
    }
    return ToUpper(GetWindowClassName(a)) == ToUpper(GetWindowClassName(b));
}

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
        if (GetWindowTextLengthW(hWnd) <= 0) {
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

std::wstring GetWindowTitle(HWND hWnd) {
    wchar_t buf[512];
    int n = GetWindowTextW(hWnd, buf, ARRAYSIZE(buf));
    if (n <= 0) {
        return {};
    }
    return std::wstring(buf, static_cast<size_t>(n));
}

bool ResolveAppIdentity(HWND hWnd,
                        std::wstring& outKey,
                        std::wstring& outDisplayName,
                        DWORD& outProcessId,
                        std::wstring* outWindowTitle = nullptr) {
    outKey.clear();
    outDisplayName.clear();
    outProcessId = 0;
    if (outWindowTitle) {
        outWindowTitle->clear();
    }

    hWnd = NormalizeFocusHwnd(hWnd);
    if (ShouldIgnoreHwnd(hWnd)) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hWnd, &processId);
    if (!processId || IsOwnExplorerProcess(processId)) {
        return false;
    }

    std::wstring path = GetProcessImagePath(processId);
    if (path.empty()) {
        return false;
    }

    std::wstring pathUpper = ToUpper(path);
    std::wstring fileName = FileNameFromPath(path);
    std::wstring fileNameUpper = ToUpper(fileName);

    if (fileNameUpper == L"EXPLORER.EXE" ||
        fileNameUpper == L"APPLICATIONFRAMEHOST.EXE" ||
        fileNameUpper == L"SEARCHHOST.EXE" ||
        fileNameUpper == L"STARTMENUXPERIENCEHOST.EXE" ||
        fileNameUpper == L"SHELLHOST.EXE" ||
        fileNameUpper == L"TEXTINPUTHOST.EXE") {
        return false;
    }

    std::wstring appIdUpper = ToUpper(GetWindowAppUserModelId(hWnd));
    std::wstring classUpper = ToUpper(GetWindowClassName(hWnd));

    if (IsExcludedKey(pathUpper, fileNameUpper) ||
        (!appIdUpper.empty() &&
         g_settings.excludedPrograms.contains(appIdUpper))) {
        Wh_Log(L"Excluded: %s", fileName.c_str());
        return false;
    }

    // One process can own several taskbar icons (Total Commander vs Lister).
    // Prefer the shell's grouping id, then path+class so they stay distinct.
    outKey = MakeAppKey(pathUpper, appIdUpper, classUpper);
    outDisplayName = fileName;
    outProcessId = processId;
    if (outWindowTitle) {
        *outWindowTitle = GetWindowTitle(hWnd);
    }
    return true;
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

// ---------------------------------------------------------------------------
// Ranking
// ---------------------------------------------------------------------------

// True if any cached TaskListButton resolved to this process path (or same
// file name). Call from UI thread after EnsureButtonPathCached, or any thread
// if only reading the path cache.
bool PathAppearsOnTaskbar(const std::wstring& keyOrPath,
                          const std::wstring& displayName) {
    const std::wstring pathUpper = PathFromAppKey(keyOrPath);
    std::wstring fileUpper = ToUpper(displayName);
    if (fileUpper.empty() && !pathUpper.empty()) {
        fileUpper = ToUpper(FileNameFromPath(pathUpper));
    }
    if (pathUpper.empty() && fileUpper.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    for (const auto& e : g_buttonPathCache) {
        if (e.pathUpper.empty()) {
            continue;
        }
        // Class is for *which* button to highlight, not whether the app
        // exists on the taskbar (TC + Lister share a path, different class).
        if (!pathUpper.empty() && e.pathUpper == pathUpper) {
            return true;
        }
        if (!fileUpper.empty() &&
            ToUpper(FileNameFromPath(e.pathUpper)) == fileUpper) {
            return true;
        }
    }
    return false;
}

// Same process path, two taskbar icons (TOTALCMD64 → Total Commander + Lister).
bool PathHasSplitTaskbarButtons(const std::wstring& pathUpper) {
    if (pathUpper.empty()) {
        return false;
    }
    const std::wstring fileUpper = ToUpper(FileNameFromPath(pathUpper));
    std::wstring firstClass;
    bool sawLister = false;
    bool sawOther = false;
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    for (const auto& e : g_buttonPathCache) {
        if (e.pathUpper.empty()) {
            continue;
        }
        if (e.pathUpper != pathUpper &&
            ToUpper(FileNameFromPath(e.pathUpper)) != fileUpper) {
            continue;
        }
        if (e.classUpper.find(L"LISTER") != std::wstring::npos) {
            sawLister = true;
        } else if (!e.classUpper.empty()) {
            sawOther = true;
        }
        if (firstClass.empty()) {
            firstClass = e.classUpper;
        } else if (!e.classUpper.empty() && e.classUpper != firstClass) {
            return true;
        }
    }
    return sawLister && sawOther;
}

void RecomputeRanksLocked() {
    g_rankedApps.clear();

    if (!g_settings.enabled || g_unloading.load()) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG decayMs =
        g_settings.decayMinutes > 0
            ? static_cast<ULONGLONG>(g_settings.decayMinutes) * 60ULL * 1000ULL
            : 0;

    std::vector<AppFocusInfo> candidates;
    candidates.reserve(g_appFocusMap.size());

    for (auto it = g_appFocusMap.begin(); it != g_appFocusMap.end();) {
        auto& info = it->second;
        if (info.lastConfirmedFocusTick == 0) {
            ++it;
            continue;
        }
        if (decayMs > 0 && now - info.lastConfirmedFocusTick > decayMs) {
            Wh_Log(L"Decayed: %s", info.displayName.c_str());
            g_keyToAutomationName.erase(it->first);
            it = g_appFocusMap.erase(it);
            continue;
        }
        // Tray-only / no taskbar button: keep optional history but never rank.
        if (g_settings.requireTaskbarButton && !info.seenOnTaskbar) {
            // Refresh from path cache if buttons resolved since last time.
            if (PathAppearsOnTaskbar(info.key, info.displayName)) {
                info.seenOnTaskbar = true;
            } else {
                ++it;
                continue;
            }
        }
        candidates.push_back(info);
        ++it;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const AppFocusInfo& a, const AppFocusInfo& b) {
                  return a.lastConfirmedFocusTick > b.lastConfirmedFocusTick;
              });

    const int limit = (std::max)(0, g_settings.highlightCount);
    if (static_cast<int>(candidates.size()) > limit) {
        candidates.resize(static_cast<size_t>(limit));
    }

    g_rankedApps = std::move(candidates);
}

// ---------------------------------------------------------------------------
// Window-level recency (thumbnail previews)
// ---------------------------------------------------------------------------

void PruneWindowFocusMapLocked() {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG decayMs =
        g_settings.previewDecayMinutes > 0
            ? static_cast<ULONGLONG>(g_settings.previewDecayMinutes) * 60ULL *
                  1000ULL
            : 0;

    for (auto it = g_windowFocusMap.begin(); it != g_windowFocusMap.end();) {
        if (!it->first || !IsWindow(it->first)) {
            it = g_windowFocusMap.erase(it);
            continue;
        }
        if (decayMs > 0 && it->second.lastConfirmedTick > 0 &&
            now - it->second.lastConfirmedTick > decayMs) {
            it = g_windowFocusMap.erase(it);
            continue;
        }
        ++it;
    }
}

// True if hwnd is still a non-decayed confirmed recent window.
bool IsWindowRecentForPreviewLocked(HWND hwnd, ULONGLONG* outTick = nullptr) {
    if (!hwnd) {
        return false;
    }
    auto it = g_windowFocusMap.find(hwnd);
    if (it == g_windowFocusMap.end() || it->second.lastConfirmedTick == 0) {
        return false;
    }
    if (!IsWindow(hwnd)) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG decayMs =
        g_settings.previewDecayMinutes > 0
            ? static_cast<ULONGLONG>(g_settings.previewDecayMinutes) * 60ULL *
                  1000ULL
            : 0;
    if (decayMs > 0 && now - it->second.lastConfirmedTick > decayMs) {
        return false;
    }
    if (outTick) {
        *outTick = it->second.lastConfirmedTick;
    }
    return true;
}

// Snapshot of recent windows for UI matching (copy under lock).
std::vector<WindowFocusInfo> CopyRecentWindowsForPreview() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    PruneWindowFocusMapLocked();
    std::vector<WindowFocusInfo> out;
    out.reserve(g_windowFocusMap.size());
    for (const auto& [hwnd, info] : g_windowFocusMap) {
        if (IsWindowRecentForPreviewLocked(hwnd)) {
            out.push_back(info);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const WindowFocusInfo& a, const WindowFocusInfo& b) {
                  return a.lastConfirmedTick > b.lastConfirmedTick;
              });
    return out;
}

// ---------------------------------------------------------------------------
// Color / style helpers (UI thread)
// ---------------------------------------------------------------------------

bool ParseHexColor(const std::wstring& text, winrt::Windows::UI::Color& out) {
    std::wstring s = text;
    if (!s.empty() && s[0] == L'#') {
        s.erase(0, 1);
    }
    // Allow 0x prefix
    if (s.size() >= 2 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
        s.erase(0, 2);
    }

    auto hexVal = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') {
            return c - L'0';
        }
        if (c >= L'a' && c <= L'f') {
            return c - L'a' + 10;
        }
        if (c >= L'A' && c <= L'F') {
            return c - L'A' + 10;
        }
        return -1;
    };

    auto readByte = [&](size_t i) -> int {
        int hi = hexVal(s[i]);
        int lo = hexVal(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        return (hi << 4) | lo;
    };

    out.A = 255;
    if (s.size() == 8) {
        int a = readByte(0), r = readByte(2), g = readByte(4), b = readByte(6);
        if (a < 0 || r < 0 || g < 0 || b < 0) {
            return false;
        }
        out.A = static_cast<uint8_t>(a);
        out.R = static_cast<uint8_t>(r);
        out.G = static_cast<uint8_t>(g);
        out.B = static_cast<uint8_t>(b);
        return true;
    }
    if (s.size() == 6) {
        int r = readByte(0), g = readByte(2), b = readByte(4);
        if (r < 0 || g < 0 || b < 0) {
            return false;
        }
        out.R = static_cast<uint8_t>(r);
        out.G = static_cast<uint8_t>(g);
        out.B = static_cast<uint8_t>(b);
        return true;
    }
    return false;
}

winrt::Windows::UI::Color ResolveGlowBaseColor() {
    winrt::Windows::UI::Color c{255, 0, 200, 83};  // default green-ish

    switch (g_settings.glowColor) {
        case GlowColorMode::Accent:
            try {
                winrt::Windows::UI::ViewManagement::UISettings uiSettings;
                c = uiSettings.GetColorValue(
                    winrt::Windows::UI::ViewManagement::UIColorType::Accent);
                c.A = 255;
            } catch (...) {
                c = {255, 0, 120, 215};
            }
            break;
        case GlowColorMode::Green:
            c = {255, 0, 200, 83};
            break;
        case GlowColorMode::Blue:
            c = {255, 30, 144, 255};
            break;
        case GlowColorMode::Orange:
            c = {255, 255, 140, 0};
            break;
        case GlowColorMode::White:
            c = {255, 255, 255, 255};
            break;
        case GlowColorMode::Custom:
            if (!ParseHexColor(g_settings.customGlowColor, c)) {
                c = {255, 0, 200, 83};
            }
            break;
    }
    return c;
}

int RankIntensity(int rankZeroBased) {
    int idx = rankZeroBased < 3 ? rankZeroBased : 2;
    return g_settings.glowIntensity[idx];
}

int RankSizeBoost(int rankZeroBased) {
    int idx = rankZeroBased < 3 ? rankZeroBased : 2;
    return g_settings.sizeBoostPercent[idx];
}

// Defined with option-C resolve stack (button → process path).
std::wstring EnsureButtonPathCached(FrameworkElement button, bool force);
std::wstring GetCachedButtonPath(FrameworkElement button);
struct ButtonIdentity {
    std::wstring pathUpper;
    std::wstring appIdUpper;
    std::wstring classUpper;
    std::wstring autoIdUpper;
    DWORD pid = 0;
    HWND sampleHwnd = nullptr;
    std::vector<HWND> groupHwnds;
};
ButtonIdentity GetCachedButtonIdentity(FrameworkElement button);
bool RunOnUiThread(const winrt::Windows::UI::Core::DispatchedHandler& handler);
void RequestApplyPreviewVisuals();
void RefreshThumbnailFlyout_UIThread(FrameworkElement anyThumb);

// ---------------------------------------------------------------------------
// Button identity matching
// ---------------------------------------------------------------------------

using TaskListButton_get_IsRunning_t = HRESULT(WINAPI*)(void* pThis,
                                                        bool* running);
TaskListButton_get_IsRunning_t TaskListButton_get_IsRunning_Original;

bool TaskListButton_IsRunning(FrameworkElement taskListButtonElement) {
    if (!TaskListButton_get_IsRunning_Original || !taskListButtonElement) {
        return false;
    }
    bool isRunning = false;
    HRESULT hr = TaskListButton_get_IsRunning_Original(
        winrt::get_abi(
            taskListButtonElement.as<winrt::Windows::Foundation::IUnknown>()),
        &isRunning);
    return SUCCEEDED(hr) && isRunning;
}

std::wstring GetButtonAutomationName(FrameworkElement button) {
    try {
        return std::wstring(
            Automation::AutomationProperties::GetName(button).c_str());
    } catch (...) {
        return {};
    }
}

std::wstring GetButtonAutomationAppId(FrameworkElement button) {
    if (!button) {
        return {};
    }
    try {
        std::wstring id =
            Automation::AutomationProperties::GetAutomationId(button).c_str();
        return ToUpper(StripAppIdPrefix(std::move(id)));
    } catch (...) {
        return {};
    }
}

bool IsVisualStateActive(FrameworkElement root) {
    if (!root) {
        return false;
    }

    try {
        auto groups = VisualStateManager::GetVisualStateGroups(root);
        for (auto group : groups) {
            auto current = group.CurrentState();
            if (!current) {
                continue;
            }
            std::wstring name(current.Name().c_str());
            // "InactivePointerOver".find("Active") is a hit — that made every
            // hovered running button look focused (TC stole Lister's glow).
            if (name.rfind(L"Inactive", 0) == 0) {
                continue;
            }
            if (name.rfind(L"Active", 0) == 0) {
                return true;
            }
        }
    } catch (...) {
    }

    // Also check children that host state groups (IconPanel).
    try {
        int n = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < n; i++) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (child && IsVisualStateActive(child)) {
                return true;
            }
        }
    } catch (...) {
    }

    return false;
}

// Taskbar names look like "App - 2 running windows pinned" — strip that noise.
// Do NOT cut at the first " - ": Lister titles are
// "Lister - [C:\\path\\file.txt] - 3 running windows and 1 group".
std::wstring NormalizeAutomationName(std::wstring name) {
    auto isRunningCountSuffix = [](std::wstring_view tail) -> bool {
        size_t i = 0;
        while (i < tail.size() && tail[i] == L' ') {
            ++i;
        }
        if (i >= tail.size() || tail[i] < L'0' || tail[i] > L'9') {
            return false;
        }
        while (i < tail.size() && tail[i] >= L'0' && tail[i] <= L'9') {
            ++i;
        }
        if (i >= tail.size() || tail[i] != L' ') {
            return false;
        }
        ++i;
        constexpr wchar_t kRun[] = L"running";
        if (i + 7 > tail.size()) {
            return false;
        }
        for (int k = 0; k < 7; ++k) {
            wchar_t c = tail[i + static_cast<size_t>(k)];
            if (c >= L'A' && c <= L'Z') {
                c = static_cast<wchar_t>(c - L'A' + L'a');
            }
            if (c != kRun[k]) {
                return false;
            }
        }
        return true;
    };

    size_t cut = std::wstring::npos;
    for (size_t search = 0; search + 3 < name.size();) {
        const auto pos = name.find(L" - ", search);
        if (pos == std::wstring::npos) {
            break;
        }
        if (isRunningCountSuffix(std::wstring_view(name).substr(pos + 3))) {
            cut = pos;
            break;
        }
        search = pos + 3;
    }
    if (cut != std::wstring::npos) {
        name.resize(cut);
    }
    // Trailing " pinned"
    constexpr wchar_t kPinned[] = L" pinned";
    if (name.size() > 7) {
        auto off = name.size() - 7;
        if (_wcsicmp(name.c_str() + off, kPinned) == 0) {
            name.resize(off);
        }
    }
    // Trim spaces
    while (!name.empty() && name.back() == L' ') {
        name.pop_back();
    }
    return name;
}

std::wstring ExtractBracketedPath(const std::wstring& s) {
    const auto open = s.find(L'[');
    const auto close = s.rfind(L']');
    if (open == std::wstring::npos || close == std::wstring::npos ||
        close <= open + 1) {
        return {};
    }
    return s.substr(open + 1, close - open - 1);
}

// Strip marketing suffixes so WINDOWSTERMINAL ≈ TERMINALPREVIEW → TERMINAL.
std::wstring StripProductNoise(std::wstring alnumUpper) {
    static const wchar_t* kNoise[] = {L"PREVIEW", L"BETA",   L"PORTABLE",
                                      L"CANARY",  L"INSIDER", L"NIGHTLY"};
    for (auto noise : kNoise) {
        for (;;) {
            auto pos = alnumUpper.find(noise);
            if (pos == std::wstring::npos) {
                break;
            }
            alnumUpper.erase(pos, wcslen(noise));
        }
    }
    return alnumUpper;
}

// True if every char of `needle` appears in order inside `haystack`
// (not necessarily contiguous). TOTALCMD64 ⊂ TOTALCOMMANDER64BIT.
bool IsSubsequence(const std::wstring& needle, const std::wstring& haystack) {
    if (needle.empty()) {
        return false;
    }
    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); ++i) {
        if (haystack[i] == needle[j]) {
            ++j;
        }
    }
    return j == needle.size();
}

// Initials of title words: "Total Commander 64 bit" → TC64B
std::wstring InitialsAlnum(const std::wstring& automationName) {
    std::wstring title = NormalizeAutomationName(automationName);
    std::wstring out;
    bool atWord = true;
    for (wchar_t ch : title) {
        if (ch == L' ' || ch == L'-' || ch == L'_' || ch == L'.') {
            atWord = true;
            continue;
        }
        if (atWord) {
            if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'0' && ch <= L'9')) {
                if (ch >= L'a' && ch <= L'z') {
                    ch = static_cast<wchar_t>(ch - L'a' + L'A');
                }
                out.push_back(ch);
            }
            atWord = false;
        }
    }
    return out;
}

// Higher = better. 0 = no match.
// Important: VSCodium must NOT match "Visual Studio Code" (initials VSC alone
// used to fire). Prefer longer/more specific matches; callers assign 1:1.
int ScoreExeToAutomationName(const std::wstring& displayName,
                             const std::wstring& automationName) {
    if (displayName.empty() || automationName.empty()) {
        return 0;
    }

    std::wstring exeAlnum = AlnumUpper(StripExtension(displayName));
    std::wstring autoAlnum = AlnumUpper(NormalizeAutomationName(automationName));
    if (exeAlnum.empty() || autoAlnum.empty()) {
        return 0;
    }

    if (exeAlnum == autoAlnum) {
        return 100;
    }

    // Contiguous containment (CODE ⊂ VISUALSTUDIOCODE, STEAM ⊂ STEAM…).
    if (exeAlnum.size() >= 4 &&
        autoAlnum.find(exeAlnum) != std::wstring::npos) {
        return 92;
    }
    if (autoAlnum.size() >= 4 &&
        exeAlnum.find(autoAlnum) != std::wstring::npos) {
        // Prefer when title is a large part of the exe (STEAM vs STEAMWEBHELPER).
        int cover = static_cast<int>(autoAlnum.size() * 100 / exeAlnum.size());
        return 80 + (std::min)(12, cover / 10);
    }

    std::wstring exeN = StripProductNoise(exeAlnum);
    std::wstring autoN = StripProductNoise(autoAlnum);
    if (!exeN.empty() && exeN == autoN) {
        return 90;
    }
    if (exeN.size() >= 5 && autoN.find(exeN) != std::wstring::npos) {
        return 88;
    }
    if (autoN.size() >= 5 && exeN.find(autoN) != std::wstring::npos) {
        return 84;
    }

    // Subsequence: TOTALCMD64 ⊂ TOTALCOMMANDER64BIT (not short initials).
    std::wstring exeLetters;
    for (wchar_t c : exeN) {
        if (c < L'0' || c > L'9') {
            exeLetters.push_back(c);
        }
    }
    if (exeN.size() >= 5 && IsSubsequence(exeN, autoN)) {
        return 72;
    }
    if (exeLetters.size() >= 5 && IsSubsequence(exeLetters, autoN)) {
        return 70;
    }

    // Initials only when they are a *substantial* prefix of the exe.
    // VSC vs VSCODIUM (3/8) → reject; avoids VS Code button stealing VSCodium.
    // TC64 vs TOTALCMD64 — initials may be TC64B; require prefix of exe length
    // and initials covering ≥ half the exe stem.
    std::wstring initials = InitialsAlnum(automationName);
    if (initials.size() >= 3) {
        if (exeN.rfind(initials, 0) == 0 &&
            initials.size() * 2 >= exeN.size()) {
            return 55;
        }
        // Exe is essentially the initials (rare).
        if (exeN == initials) {
            return 95;
        }
    }

    return 0;
}

bool ExeMatchesAutomationName(const std::wstring& displayName,
                              const std::wstring& automationName) {
    return ScoreExeToAutomationName(displayName, automationName) > 0;
}

// Window title ↔ taskbar button title (both normalized).
int ScoreTitleToAutomationName(const std::wstring& windowTitle,
                               const std::wstring& automationName) {
    if (windowTitle.empty() || automationName.empty()) {
        return 0;
    }
    std::wstring t = AlnumUpper(NormalizeAutomationName(windowTitle));
    std::wstring a = AlnumUpper(NormalizeAutomationName(automationName));
    if (t.empty() || a.empty()) {
        return 0;
    }
    if (t == a) {
        return 98;
    }
    // Button title contained in window title or vice versa (min length 4).
    if (t.size() >= 4 && a.find(t) != std::wstring::npos) {
        return 93;
    }
    if (a.size() >= 4 && t.find(a) != std::wstring::npos) {
        return 91;
    }
    // Significant shared prefix (e.g. WINDHAWK…).
    // Do not use this when both sides have distinct [bracket] paths —
    // "Lister - [c:\tmp\a.txt]" vs "Lister - [c:\tmp\c.txt]" share LISTERCTMP.
    const std::wstring tPath =
        ToUpper(ExtractBracketedPath(NormalizeAutomationName(windowTitle)));
    const std::wstring aPath =
        ToUpper(ExtractBracketedPath(NormalizeAutomationName(automationName)));
    if (!tPath.empty() && !aPath.empty() && tPath != aPath) {
        return 0;
    }
    size_t pref = 0;
    while (pref < t.size() && pref < a.size() && t[pref] == a[pref]) {
        ++pref;
    }
    if (pref >= 6) {
        return 85;
    }
    return 0;
}

// True if this button's automation name is a legitimate label for the rank.
// Class-qualified ranks (TLISTER) must not bind to "Total Commander".
bool AutomationNameFitsRank(const AppFocusInfo& info,
                            const std::wstring& autoName) {
    if (autoName.empty()) {
        return false;
    }
    const std::wstring cls = !info.classUpper.empty()
                                 ? info.classUpper
                                 : ClassFromAppKey(info.key);
    if (!cls.empty() && cls.find(L"LISTER") != std::wstring::npos) {
        return AlnumUpper(NormalizeAutomationName(autoName)).find(L"LISTER") !=
               std::wstring::npos;
    }
    return ExeMatchesAutomationName(info.displayName, autoName);
}

void StoreAutomationNameIfFits(const AppFocusInfo& info,
                               const std::wstring& autoName) {
    if (!AutomationNameFitsRank(info, autoName)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_keyToAutomationName[info.key] = autoName;
}

void StoreAutomationName(const AppFocusInfo& info,
                         const std::wstring& autoName) {
    if (info.key.empty() || autoName.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_keyToAutomationName[info.key] = autoName;
}

// Trusted writes only: path/exe binds, or Active-button associate (Windhawk
// button for VSCodium.exe). Name-equals is then enough, except we refuse
// a non-fitting name when this exe has two icons (TC vs Lister).
bool CacheEntryValid(const AppFocusInfo& info,
                     const std::wstring& cachedAutoName,
                     const std::wstring& buttonAutoName) {
    if (cachedAutoName.empty() || buttonAutoName.empty()) {
        return false;
    }
    const bool nameEquals =
        _wcsicmp(cachedAutoName.c_str(), buttonAutoName.c_str()) == 0 ||
        _wcsicmp(NormalizeAutomationName(cachedAutoName).c_str(),
                 NormalizeAutomationName(buttonAutoName).c_str()) == 0;
    if (!nameEquals) {
        return false;
    }
    if (AutomationNameFitsRank(info, buttonAutoName)) {
        return true;
    }
    const std::wstring path = PathFromAppKey(info.key);
    return !PathHasSplitTaskbarButtons(path);
}

// Score this button against one ranked app. Higher is better; 0 = no match.
int ScoreButtonForRank(FrameworkElement button,
                       const AppFocusInfo& info,
                       bool requireRunning) {
    if (!button) {
        return 0;
    }
    if (requireRunning && !TaskListButton_IsRunning(button)) {
        return 0;
    }

    const ButtonIdentity ident = GetCachedButtonIdentity(button);
    const std::wstring rankPath = PathFromAppKey(info.key);
    const std::wstring rankCls = !info.classUpper.empty()
                                     ? info.classUpper
                                     : ClassFromAppKey(info.key);
    const std::wstring rankAppId = !info.appIdUpper.empty()
                                       ? info.appIdUpper
                                       : AppIdFromAppKey(info.key);

    auto hwndOnButton = [&](HWND h) -> bool {
        if (!h) {
            return false;
        }
        if (ident.sampleHwnd == h) {
            return true;
        }
        for (HWND g : ident.groupHwnds) {
            if (g == h) {
                return true;
            }
        }
        if (ident.sampleHwnd && SamePidAndClass(h, ident.sampleHwnd)) {
            return true;
        }
        return false;
    };

    if (hwndOnButton(info.lastHwnd)) {
        return 1000;
    }

    const std::wstring pathKey =
        !rankPath.empty() ? rankPath : PathFromAppKey(info.key);
    const bool pathOk =
        !ident.pathUpper.empty() &&
        (ident.pathUpper == info.key || ident.pathUpper == pathKey ||
         (!info.displayName.empty() &&
          ToUpper(FileNameFromPath(ident.pathUpper)) ==
              ToUpper(info.displayName)));
    const bool split = PathHasSplitTaskbarButtons(
        !pathKey.empty() ? pathKey : ident.pathUpper);

    // Street join: same image path. Only require window class when this
    // exe has two icons (Lister vs Total Commander).
    if (pathOk) {
        if (!split) {
            return ident.pathUpper == info.key || ident.pathUpper == pathKey
                       ? 1000
                       : 900;
        }
        if (!rankCls.empty() && ident.classUpper == rankCls) {
            return 1000;
        }
        if (!rankCls.empty() && !ident.classUpper.empty() &&
            ident.classUpper != rankCls) {
            // other half of a split pair
        } else if (rankCls.empty() || ident.classUpper.empty()) {
            return 900;
        }
    }

    const bool identityConflict =
        split && !rankCls.empty() && !ident.classUpper.empty() &&
        ident.classUpper != rankCls;

    std::wstring autoName = GetButtonAutomationName(button);
    if (autoName.empty()) {
        return 0;
    }

    // Name fallback when path cache missed: "Lister" button vs TLister focus.
    if (!rankCls.empty() &&
        rankCls.find(L"LISTER") != std::wstring::npos) {
        std::wstring n = AlnumUpper(NormalizeAutomationName(autoName));
        if (n.find(L"LISTER") != std::wstring::npos) {
            return 1000;
        }
    }

    int score = 0;
    if (!identityConflict) {
        score = ScoreExeToAutomationName(info.displayName, autoName);
    }

    // Do not score last window/tab title against taskbar buttons.
    // Terminal tabs (and VSCodium editing a Windhawk mod) rename the HWND
    // to the document — that is preview identity, not which icon to glow.

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_keyToAutomationName.find(info.key);
        if (it != g_keyToAutomationName.end() && !it->second.empty()) {
            if (CacheEntryValid(info, it->second, autoName)) {
                score = (std::max)(score, 96);
            } else if (_wcsicmp(it->second.c_str(), autoName.c_str()) == 0 ||
                       _wcsicmp(NormalizeAutomationName(it->second).c_str(),
                                NormalizeAutomationName(autoName).c_str()) ==
                           0) {
                Wh_Log(L"Dropping bad cache: %s was \"%s\" (exe/class mismatch)",
                       info.displayName.c_str(), it->second.c_str());
                g_keyToAutomationName.erase(it);
            }
        }
    }
    return score;
}

// Best rank for a single button (1-based), or 0. Prefer highest score.
// requireRunning=false: allow match when IsRunning flickers during Alt-Tab.
int FindRankForButton(FrameworkElement button,
                      const std::vector<AppFocusInfo>& ranks,
                      bool requireRunning = true) {
    if (!button || ranks.empty()) {
        return 0;
    }

    int bestRank = 0;
    int bestScore = 0;
    for (size_t i = 0; i < ranks.size(); i++) {
        int s = ScoreButtonForRank(button, ranks[i], requireRunning);
        if (s > bestScore) {
            bestScore = s;
            bestRank = static_cast<int>(i) + 1;
        }
    }
    // Minimum confidence: reject weak initials-only if ever reintroduced.
    if (bestScore < 70) {
        return 0;
    }

    if (bestRank > 0) {
        StoreAutomationNameIfFits(
            ranks[static_cast<size_t>(bestRank - 1)],
            GetButtonAutomationName(button));
    }
    return bestRank;
}

void AssociateActiveButtonWithKey(const std::wstring& key) {
    if (key.empty()) {
        return;
    }

    std::wstring displayName;
    std::wstring windowTitle;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_appFocusMap.find(key);
        if (it != g_appFocusMap.end()) {
            displayName = it->second.displayName;
            windowTitle = it->second.lastWindowTitle;
        }
    }
    if (displayName.empty()) {
        displayName = FileNameFromPath(key);
    }

    std::vector<winrt::weak_ref<FrameworkElement>> buttons;
    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        buttons = g_trackedButtons;
    }

    // Prefer active button; fall back to best-scoring running button for this
    // app (Windhawk / TC sometimes fail IsVisualStateActive timing).
    int bestScore = 0;
    std::wstring bestName;

    for (auto& weak : buttons) {
        FrameworkElement button = nullptr;
        try {
            button = weak.get();
        } catch (...) {
            continue;
        }
        if (!button || !TaskListButton_IsRunning(button)) {
            continue;
        }

        std::wstring autoName = GetButtonAutomationName(button);
        if (autoName.empty()) {
            continue;
        }

        int score = ScoreExeToAutomationName(displayName, autoName);
        // Identity from path cache (Terminal, VSCodium) beats a window
        // title that happens to mention another app ("Discord icon…").
        const ButtonIdentity ident = GetCachedButtonIdentity(button);
        const std::wstring rankPath = PathFromAppKey(key);
        if (!ident.pathUpper.empty() && !rankPath.empty() &&
            ident.pathUpper == rankPath) {
            score = (std::max)(score, 1000);
        } else if (!ident.pathUpper.empty() &&
                   ToUpper(FileNameFromPath(ident.pathUpper)) ==
                       ToUpper(displayName)) {
            score = (std::max)(score, 900);
        }
        const bool active = IsVisualStateActive(button);
        if (active) {
            score += 5;
            // Real Active* only (Inactive* is not Active). Windhawk's button
            // is named "Windhawk" while the process is VSCodium.exe.
            if (score < 70) {
                score = 70;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestName = autoName;
        }

        if (active && score < 70) {
            Wh_Log(L"Skip associate: active \"%s\" weak match for %s (score=%d)",
                   autoName.c_str(), displayName.c_str(), score);
        }
    }

    AppFocusInfo assocInfo;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_appFocusMap.find(key);
        if (it != g_appFocusMap.end()) {
            assocInfo = it->second;
        }
    }
    if (assocInfo.key.empty()) {
        assocInfo.key = key;
        assocInfo.displayName = displayName;
    }

    if (bestScore >= 70 && !bestName.empty()) {
        // Always remember the Active button, even when the label is the host
        // ("Windhawk") and the process is VSCodium.exe. Scoring trusts this
        // cache; other write sites still use StoreAutomationNameIfFits.
        StoreAutomationName(assocInfo, bestName);
        Wh_Log(L"Associated %s -> \"%s\" (score=%d, fits=%d, title=\"%s\")",
               displayName.c_str(), bestName.c_str(), bestScore,
               AutomationNameFitsRank(assocInfo, bestName) ? 1 : 0,
               windowTitle.c_str());
    } else {
        Wh_Log(L"Associate failed for %s (bestScore=%d, title=\"%s\")",
               displayName.c_str(), bestScore, windowTitle.c_str());
    }
}

// ---------------------------------------------------------------------------
// Apply / clear visual highlight on one button (UI thread)
// ---------------------------------------------------------------------------

FrameworkElement GetIconPanel(FrameworkElement button) {
    auto iconPanel = FindChildByName(button, L"IconPanel");
    if (!iconPanel) {
        iconPanel = FindDescendantByName(button, L"IconPanel");
    }
    return iconPanel;
}

void RemoveNamedChild(Controls::Panel panel, PCWSTR name) {
    if (!panel) {
        return;
    }
    if (auto child = FindChildByName(panel, name)) {
        uint32_t idx = 0;
        if (panel.Children().IndexOf(child, idx)) {
            panel.Children().RemoveAt(idx);
        }
    }
}

void SpanHostOverPanel(FrameworkElement host, Controls::Panel panel) {
    if (!host || !panel) {
        return;
    }
    try {
        if (auto grid = panel.try_as<Controls::Grid>()) {
            auto cols = grid.ColumnDefinitions().Size();
            auto rows = grid.RowDefinitions().Size();
            Controls::Grid::SetColumn(host, 0);
            Controls::Grid::SetRow(host, 0);
            if (cols > 0) {
                Controls::Grid::SetColumnSpan(host, cols);
            }
            if (rows > 0) {
                Controls::Grid::SetRowSpan(host, rows);
            }
        }
    } catch (...) {
    }
}

void BringToFront(Controls::Panel panel, UIElement host) {
    if (!panel || !host) {
        return;
    }
    try {
        auto children = panel.Children();
        uint32_t idx = 0;
        if (!children.IndexOf(host, idx)) {
            return;
        }
        const uint32_t last = children.Size() > 0 ? children.Size() - 1 : 0;
        if (idx != last) {
            children.RemoveAt(idx);
            children.Append(host);
        }
    } catch (...) {
    }
}

// Only clear clip on our own host — walking ancestors with Clip(nullptr) can
// leave native BackgroundElement / RunningIndicator stuck after long idle.
void ClearOurHostClip(FrameworkElement host) {
    if (!host) {
        return;
    }
    try {
        host.Clip(nullptr);
    } catch (...) {
    }
}

// One glow layer: stroked rounded rect; optional fill for Full style.
void StyleGlowRectangle(Shapes::Rectangle rect,
                        const winrt::Windows::UI::Color& stroke,
                        const winrt::Windows::UI::Color& fill,
                        double strokeThickness,
                        double corner,
                        double inset,
                        double opacity) {
    rect.Stroke(Media::SolidColorBrush{stroke});
    rect.StrokeThickness(strokeThickness);
    rect.Fill(Media::SolidColorBrush{fill});
    rect.RadiusX(corner);
    rect.RadiusY(corner);
    rect.Opacity(opacity);
    rect.HorizontalAlignment(HorizontalAlignment::Stretch);
    rect.VerticalAlignment(VerticalAlignment::Stretch);
    rect.Margin(Thickness{inset, inset, inset, inset});
    rect.ClearValue(FrameworkElement::WidthProperty());
    rect.ClearValue(FrameworkElement::HeightProperty());
    rect.IsHitTestVisible(false);
    rect.Visibility(Visibility::Visible);
}

FrameworkElement FindRunningIndicator(FrameworkElement iconPanel) {
    if (!iconPanel) {
        return nullptr;
    }
    auto indicator = FindChildByName(iconPanel, L"RunningIndicator");
    if (!indicator) {
        indicator = FindDescendantByName(iconPanel, L"RunningIndicator");
    }
    return indicator;
}

// Undo any local values we may have applied to the native RunningIndicator.
// Prefer only Visibility (current bottomBar); also clear legacy Fill/size from
// older builds so short inactive bars can reappear after ClearValue.
void ClearRunningIndicatorStyle(FrameworkElement iconPanel) {
    auto indicator = FindRunningIndicator(iconPanel);
    if (!indicator) {
        return;
    }
    try {
        if (auto shape = indicator.try_as<Shapes::Shape>()) {
            if (shape.ReadLocalValue(Shapes::Shape::FillProperty()) !=
                DependencyProperty::UnsetValue()) {
                shape.ClearValue(Shapes::Shape::FillProperty());
            }
        }
        if (indicator.ReadLocalValue(FrameworkElement::HeightProperty()) !=
            DependencyProperty::UnsetValue()) {
            indicator.ClearValue(FrameworkElement::HeightProperty());
        }
        if (indicator.ReadLocalValue(FrameworkElement::MinWidthProperty()) !=
            DependencyProperty::UnsetValue()) {
            indicator.ClearValue(FrameworkElement::MinWidthProperty());
        }
        if (indicator.ReadLocalValue(FrameworkElement::WidthProperty()) !=
            DependencyProperty::UnsetValue()) {
            indicator.ClearValue(FrameworkElement::WidthProperty());
        }
        if (indicator.ReadLocalValue(UIElement::OpacityProperty()) !=
            DependencyProperty::UnsetValue()) {
            indicator.ClearValue(UIElement::OpacityProperty());
        }
        if (indicator.ReadLocalValue(UIElement::VisibilityProperty()) !=
            DependencyProperty::UnsetValue()) {
            indicator.ClearValue(UIElement::VisibilityProperty());
        }
    } catch (...) {
    }
}

// Place glow host in the IconPanel child list without thrashing native chrome.
//
// Moving RunningIndicator every paint (mouse-over UpdateVisualStates) causes
// the short/long underline to flicker. Instead, put our host *under* native
// RunningIndicator / MultiWindowElement / ProgressIndicator so they always
// paint on top — only reorder when the host is in the wrong place.
void EnsureGlowHostZOrder(Controls::Panel panel,
                          UIElement host,
                          GlowStyle style) {
    if (!panel || !host) {
        return;
    }
    try {
        auto children = panel.Children();
        uint32_t hostIdx = 0;
        if (!children.IndexOf(host, hostIdx)) {
            return;
        }

        if (style == GlowStyle::Full) {
            // Plate behind icon and indicators.
            if (hostIdx != 0) {
                children.RemoveAt(hostIdx);
                children.InsertAt(0, host);
            }
            return;
        }

        // Frame / left / bottom: above icon, below native chrome that must
        // stay on top — including OverlayIcon (Discord ping badge).
        uint32_t insertBefore = children.Size();
        bool foundNative = false;
        for (PCWSTR name : {L"RunningIndicator", L"MultiWindowElement",
                            L"ProgressIndicator", L"OverlayIcon"}) {
            auto el = FindChildByName(panel.as<FrameworkElement>(), name);
            if (!el) {
                continue;
            }
            uint32_t idx = 0;
            if (!children.IndexOf(el, idx)) {
                continue;
            }
            if (!foundNative || idx < insertBefore) {
                insertBefore = idx;
                foundNative = true;
            }
        }

        // Re-resolve host index after any earlier mutations.
        if (!children.IndexOf(host, hostIdx)) {
            return;
        }

        if (foundNative) {
            if (hostIdx < insertBefore) {
                // Host already under native chrome — leave tree alone (no flicker).
                return;
            }
            // hostIdx >= insertBefore: host is on top of native — move under.
            children.RemoveAt(hostIdx);
            // host was at hostIdx >= insertBefore, so insertBefore is unchanged.
            children.InsertAt(insertBefore, host);
        } else if (hostIdx + 1 != children.Size()) {
            children.RemoveAt(hostIdx);
            children.Append(host);
        }
    } catch (...) {
    }
}

// TaskListLabeledButtonPanel paints later children on top. Native template
// order is BackgroundElement (back) → Icon → OverlayIcon → RunningIndicator
// (front). Inserting/removing our host can leave BackgroundElement in front
// of the running underscore. Combined with Discord RequestingAttention (red
// plate on BackgroundElement) that looks like: missing underscore + red bg.
void RestoreIconPanelNativeZOrder(FrameworkElement iconPanel) {
    if (!iconPanel) {
        return;
    }
    auto panel = iconPanel.try_as<Controls::Panel>();
    if (!panel) {
        return;
    }
    try {
        auto children = panel.Children();
        auto indexOfName = [&](PCWSTR name) -> int {
            auto el = FindChildByName(iconPanel, name);
            if (!el) {
                return -1;
            }
            uint32_t idx = 0;
            if (!children.IndexOf(el, idx)) {
                return -1;
            }
            return static_cast<int>(idx);
        };

        const int bg = indexOfName(kBackgroundElementName);
        const int run = indexOfName(L"RunningIndicator");
        if (bg < 0 || run < 0 || bg < run) {
            return;
        }

        auto bgEl = FindChildByName(iconPanel, kBackgroundElementName);
        if (!bgEl) {
            return;
        }
        uint32_t bgIdx = 0;
        if (!children.IndexOf(bgEl, bgIdx)) {
            return;
        }
        children.RemoveAt(bgIdx);
        children.InsertAt(0, bgEl);
        Wh_Log(L"Restored IconPanel z-order (BackgroundElement was in front of "
               L"RunningIndicator)");
    } catch (...) {
    }
}

bool ButtonHasOurChrome(FrameworkElement button) {
    if (!button) {
        return false;
    }
    auto iconPanel = GetIconPanel(button);
    if (!iconPanel) {
        return FindDescendantByName(button, kGlowElementName) != nullptr ||
               FindDescendantByName(button, kBottomBarMarkerName) != nullptr;
    }
    return FindChildByName(iconPanel, kGlowElementName) != nullptr ||
           FindDescendantByName(iconPanel, kGlowElementName) != nullptr ||
           FindChildByName(iconPanel, kBottomBarMarkerName) != nullptr ||
           FindDescendantByName(iconPanel, kBottomBarMarkerName) != nullptr;
}

void ClearButtonHighlight(FrameworkElement button) {
    if (!button) {
        return;
    }

    auto iconPanelEarly = GetIconPanel(button);

    // Skip no-op clears on every mouse-over (UpdateVisualStates storms).
    // Still heal z-order: after a timed-out glow, our host is gone but
    // BackgroundElement can remain in front of RunningIndicator (Discord
    // ping + decay left a red plate and no underscore).
    if (!ButtonHasOurChrome(button) && !g_pendingOverlaySweep.load()) {
        RestoreIconPanelNativeZOrder(iconPanelEarly);
        return;
    }

    try {
        auto iconPanel = iconPanelEarly ? iconPanelEarly : GetIconPanel(button);
        if (!iconPanel) {
            // Still try to strip our named overlay from the button root.
            if (auto panel = button.try_as<Controls::Panel>()) {
                RemoveNamedChild(panel, kGlowElementName);
                RemoveNamedChild(panel, kBottomBarMarkerName);
            }
            return;
        }

        // Only undo RunningIndicator when we previously applied bottomBar
        // (marker present). Never clear it for leftBar/frame/full — that was
        // wiping the native grey active/running bar.
        const bool hadBottomBar =
            FindChildByName(iconPanel, kBottomBarMarkerName) != nullptr ||
            FindDescendantByName(iconPanel, kBottomBarMarkerName) != nullptr;

        // Do NOT ClearValue BackgroundElement — we no longer style it; clearing
        // local values can leave a stuck pale hover plate after decay.

        if (auto panel = iconPanel.try_as<Controls::Panel>()) {
            // Remove by name even if multiple generations of hosts exist.
            for (int guard = 0; guard < 4; ++guard) {
                if (!FindChildByName(panel, kGlowElementName) &&
                    !FindDescendantByName(iconPanel, kGlowElementName)) {
                    break;
                }
                RemoveNamedChild(panel, kGlowElementName);
                // Descendant host (unexpected nesting): walk children once.
                if (auto orphan =
                        FindDescendantByName(iconPanel, kGlowElementName)) {
                    try {
                        if (auto parent =
                                Media::VisualTreeHelper::GetParent(orphan)
                                    .try_as<Controls::Panel>()) {
                            uint32_t idx = 0;
                            if (parent.Children().IndexOf(orphan, idx)) {
                                parent.Children().RemoveAt(idx);
                            }
                        }
                    } catch (...) {
                    }
                }
            }
            RemoveNamedChild(panel, kBottomBarMarkerName);
        }

        if (hadBottomBar) {
            ClearRunningIndicatorStyle(iconPanel);
        }
        // Do not heal Visibility on every clear — that ClearValue thrashing
        // also flickers the native underline during hover storms.

        if (auto icon = FindChildByName(iconPanel, L"Icon")) {
            auto localTf =
                icon.ReadLocalValue(UIElement::RenderTransformProperty());
            if (localTf != DependencyProperty::UnsetValue()) {
                if (icon.RenderTransform().try_as<Media::ScaleTransform>()) {
                    icon.ClearValue(UIElement::RenderTransformProperty());
                    icon.ClearValue(UIElement::RenderTransformOriginProperty());
                }
            }
        }

        RestoreIconPanelNativeZOrder(iconPanel);
    } catch (...) {
        HRESULT hr = winrt::to_hresult();
        Wh_Log(L"ClearButtonHighlight error %08X", hr);
    }
}

// Ensure glow host grid exists (L0–L2 rectangles). Returns host or nullptr.
Controls::Grid EnsureGlowHost(Controls::Panel panel,
                              FrameworkElement iconPanel) {
    Controls::Grid host = nullptr;
    if (auto existing = FindChildByName(iconPanel, kGlowElementName)) {
        host = existing.try_as<Controls::Grid>();
        if (host && !FindChildByName(host, kGlowLayerNames[0])
                         .try_as<Shapes::Rectangle>()) {
            RemoveNamedChild(panel, kGlowElementName);
            host = nullptr;
        } else if (!host) {
            RemoveNamedChild(panel, kGlowElementName);
        }
    }

    if (!host) {
        PCWSTR xaml =
            LR"(
            <Grid
                xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
                Name="WhRecentFocusGlow"
                IsHitTestVisible="False"
                HorizontalAlignment="Stretch"
                VerticalAlignment="Stretch">
                <Rectangle Name="WhRecentFocusGlowL0"
                           IsHitTestVisible="False"
                           Fill="Transparent"/>
                <Rectangle Name="WhRecentFocusGlowL1"
                           IsHitTestVisible="False"
                           Fill="Transparent"/>
                <Rectangle Name="WhRecentFocusGlowL2"
                           IsHitTestVisible="False"
                           Fill="Transparent"/>
            </Grid>
        )";
        host = Markup::XamlReader::Load(xaml).as<Controls::Grid>();
        panel.Children().Append(host);
    }

    SpanHostOverPanel(host, panel);
    host.Visibility(Visibility::Visible);
    ClearOurHostClip(host);
    return host;
}

void HideAllGlowLayers(Controls::Grid host) {
    if (!host) {
        return;
    }
    for (int i = 0; i < kGlowMaxLayers; ++i) {
        if (auto r =
                FindChildByName(host, kGlowLayerNames[i]).try_as<Shapes::Rectangle>()) {
            r.Visibility(Visibility::Collapsed);
            r.Stroke(nullptr);
            r.Fill(Media::SolidColorBrush{
                winrt::Windows::UI::Color{0, 0, 0, 0}});
        }
    }
}

PCWSTR GlowStyleName(GlowStyle s) {
    switch (s) {
        case GlowStyle::Full:
            return L"full";
        case GlowStyle::LeftBar:
            return L"leftBar";
        case GlowStyle::BottomBar:
            return L"bottomBar";
        case GlowStyle::Frame:
        default:
            return L"frame";
    }
}

PCWSTR PromoteModeName(PromoteMode m) {
    switch (m) {
        case PromoteMode::ImmediateTopN:
            return L"immediateTopN";
        case PromoteMode::AlwaysWait:
            return L"alwaysWait";
        case PromoteMode::ImmediateTracked:
        default:
            return L"immediateTracked";
    }
}

// True if this focus change may skip the app min-focus timer.
// Caller must hold g_stateMutex when checking ranked list (or we take it).
bool ShouldSkipAppMinFocus(const std::wstring& key, bool alreadyTracked) {
    if (g_settings.minFocusSeconds <= 0) {
        return true;
    }
    switch (g_settings.promoteMode) {
        case PromoteMode::AlwaysWait:
            return false;
        case PromoteMode::ImmediateTopN: {
            if (!alreadyTracked) {
                return false;
            }
            std::lock_guard<std::mutex> lock(g_stateMutex);
            for (const auto& r : g_rankedApps) {
                if (r.key == key) {
                    return true;
                }
            }
            return false;
        }
        case PromoteMode::ImmediateTracked:
        default:
            return alreadyTracked;
    }
}

PCWSTR PreviewStyleName(PreviewStyle s) {
    switch (s) {
        case PreviewStyle::TitleBg:
            return L"titleBg";
        case PreviewStyle::Plate:
            return L"plate";
        case PreviewStyle::TitleBar:
            return L"titleBar";
        case PreviewStyle::Ring:
        default:
            return L"ring";
    }
}

void ApplyButtonHighlight(FrameworkElement button, int rankOneBased) {
    if (!button) {
        return;
    }

    if (rankOneBased <= 0 || g_unloading.load() || !g_settings.enabled) {
        ClearButtonHighlight(button);
        return;
    }

    try {
        auto iconPanel = GetIconPanel(button);
        if (!iconPanel) {
            return;
        }

        auto panel = iconPanel.try_as<Controls::Panel>();
        if (!panel) {
            return;
        }

        const int rankIdx = rankOneBased - 1;
        const int intensity = RankIntensity(rankIdx);
        const int sizeBoost = RankSizeBoost(rankIdx);
        const double t = intensity / 100.0;

        winrt::Windows::UI::Color base = ResolveGlowBaseColor();

        auto withAlpha = [](winrt::Windows::UI::Color c,
                            int a) -> winrt::Windows::UI::Color {
            c.A = static_cast<uint8_t>((std::max)(0, (std::min)(255, a)));
            return c;
        };

        const int layers =
            (std::max)(1, (std::min)(kGlowMaxLayers, g_settings.glowLayers));
        const double thickness = static_cast<double>(
            (std::max)(1, (std::min)(16, g_settings.glowThickness)));
        const double roundnessFrac =
            (std::max)(0, (std::min)(50, g_settings.glowRoundness)) / 100.0;
        const double sizeFrac =
            (std::max)(40, (std::min)(100, g_settings.glowSize)) / 100.0;
        const GlowStyle style = g_settings.glowStyle;
        const int fillOpacitySetting =
            (std::max)(0, (std::min)(100, g_settings.glowFillOpacity));

        double panelW = iconPanel.ActualWidth();
        double panelH = iconPanel.ActualHeight();
        if (!(panelW > 1.0)) {
            panelW = 44.0;
        }
        if (!(panelH > 1.0)) {
            panelH = 44.0;
        }

        Controls::Grid host = EnsureGlowHost(panel, iconPanel);
        if (!host) {
            return;
        }

        // Heal native stacking first (Discord overlay / leftover attention
        // plate), then place our host under RunningIndicator + OverlayIcon.
        RestoreIconPanelNativeZOrder(iconPanel);

        // Z-order once when wrong — never yank RunningIndicator every paint
        // (that caused short/long underline flicker on mouse-over).
        EnsureGlowHostZOrder(panel, host, style);

        HideAllGlowLayers(host);

        // Drop bottom-bar marker / indicator overrides when using other styles.
        if (style != GlowStyle::BottomBar) {
            if (FindChildByName(iconPanel, kBottomBarMarkerName)) {
                if (auto p = iconPanel.try_as<Controls::Panel>()) {
                    RemoveNamedChild(p, kBottomBarMarkerName);
                }
                ClearRunningIndicatorStyle(iconPanel);
            }
        }

        if (style == GlowStyle::Frame || style == GlowStyle::Full) {
            const double baseInset =
                (std::min)(panelW, panelH) * (1.0 - sizeFrac) * 0.5;
            const bool isFrame = style == GlowStyle::Frame;

            for (int i = 0; i < kGlowMaxLayers; ++i) {
                auto rect = FindChildByName(host, kGlowLayerNames[i])
                                .try_as<Shapes::Rectangle>();
                if (!rect) {
                    continue;
                }
                if (i >= layers) {
                    continue;
                }

                const double step =
                    (i == 0) ? 0.0 : (3.0 + thickness * 0.55) * i;
                const double inset = baseInset + step;
                const double inner =
                    (std::max)(8.0, (std::min)(panelW, panelH) - 2.0 * inset);
                const double corner = inner * roundnessFrac;
                const double layerT = t * (1.0 - 0.15 * i);
                const double th =
                    (std::max)(1.0, thickness * (1.0 - 0.1 * i));
                const int strokeA =
                    static_cast<int>((100 + 130 * layerT) * (1.0 - 0.12 * i));
                const double opacity = 0.70 + 0.30 * layerT;

                winrt::Windows::UI::Color fill{0, 0, 0, 0};
                if (!isFrame && i == 0) {
                    int fillA = static_cast<int>(fillOpacitySetting * 2.55 *
                                                 (0.45 + 0.55 * layerT));
                    fill = withAlpha(base, fillA);
                }

                StyleGlowRectangle(rect, withAlpha(base, strokeA), fill, th,
                                   corner, inset, opacity);
            }
        } else if (style == GlowStyle::LeftBar) {
            // Vertical pill on the left — similar short-side size to the
            // native bottom running indicator (~2–4px), height follows size%.
            const double barW = thickness + 1.0;  // short dimension
            const double barH = panelH * sizeFrac;
            const double vMargin = (std::max)(0.0, (panelH - barH) * 0.5);
            const double corner = barW * 0.5;  // full pill
            const int fillBase =
                static_cast<int>(fillOpacitySetting * 2.55 * (0.55 + 0.45 * t));
            const int nLeft = (std::max)(1, (std::min)(layers, 2));

            for (int i = 0; i < nLeft; ++i) {
                auto rect = FindChildByName(host, kGlowLayerNames[i])
                                .try_as<Shapes::Rectangle>();
                if (!rect) {
                    continue;
                }
                // Outer soft glow slightly wider / more transparent.
                const double w = barW + i * 2.0;
                const double h = barH + i * 2.0;
                const double left = 1.0 + static_cast<double>(i);
                const double top =
                    (std::max)(0.0, vMargin - static_cast<double>(i));
                const int fillA =
                    i == 0 ? fillBase
                           : static_cast<int>(fillBase * 0.35);
                const double opacity = i == 0 ? (0.85 + 0.15 * t) : 0.45;

                rect.Stroke(nullptr);
                rect.StrokeThickness(0);
                rect.Fill(Media::SolidColorBrush{withAlpha(base, fillA)});
                rect.RadiusX(corner + i);
                rect.RadiusY(corner + i);
                rect.Width(w);
                rect.Height(h);
                rect.HorizontalAlignment(HorizontalAlignment::Left);
                rect.VerticalAlignment(VerticalAlignment::Top);
                rect.Margin(Thickness{left, top, 0, 0});
                rect.Opacity(opacity);
                rect.IsHitTestVisible(false);
                rect.Visibility(Visibility::Visible);
            }
        } else if (style == GlowStyle::BottomBar) {
            // Draw our own bottom pill — do NOT set Fill/Width/Height on the
            // native RunningIndicator (ClearValue left short inactive bars
            // missing until a full explorer restart). Hide native while we
            // paint; Clear restores Visibility via ClearValue.
            auto indicator = FindRunningIndicator(iconPanel);
            if (indicator) {
                try {
                    indicator.Visibility(Visibility::Collapsed);
                } catch (...) {
                }
            } else if (g_settings.glowDebugLog) {
                Wh_Log(L"BottomBar: RunningIndicator not found on \"%s\"",
                       GetButtonAutomationName(button).c_str());
            }

            const int fillA =
                static_cast<int>(fillOpacitySetting * 2.55 * (0.6 + 0.4 * t));
            const double barH =
                (std::max)(2.0, (std::min)(6.0, thickness));
            const double barW =
                panelW * sizeFrac * (0.55 + 0.45 * t);
            const double left = (std::max)(0.0, (panelW - barW) * 0.5);
            const double bottom = 2.0;

            if (auto rect = FindChildByName(host, kGlowLayerNames[0])
                                .try_as<Shapes::Rectangle>()) {
                rect.Stroke(nullptr);
                rect.StrokeThickness(0);
                rect.Fill(Media::SolidColorBrush{withAlpha(base, fillA)});
                rect.RadiusX(barH * 0.5);
                rect.RadiusY(barH * 0.5);
                rect.Width(barW);
                rect.Height(barH);
                rect.HorizontalAlignment(HorizontalAlignment::Left);
                rect.VerticalAlignment(VerticalAlignment::Bottom);
                rect.Margin(Thickness{left, 0, 0, bottom});
                rect.Opacity(0.9 + 0.1 * t);
                rect.IsHitTestVisible(false);
                rect.Visibility(Visibility::Visible);
            }

            // Marker: ClearButtonHighlight restores native indicator.
            if (!FindChildByName(iconPanel, kBottomBarMarkerName)) {
                try {
                    PCWSTR markerXaml =
                        LR"(
                        <Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                                Name="WhRecentFocusBottomBar"
                                Width="0" Height="0" Opacity="0"
                                IsHitTestVisible="False" Visibility="Collapsed"/>
                    )";
                    auto marker = Markup::XamlReader::Load(markerXaml)
                                      .as<FrameworkElement>();
                    panel.Children().Append(marker);
                } catch (...) {
                }
            }
            // Host already ordered under native chrome; native is Collapsed.
        }

        if (auto icon = FindChildByName(iconPanel, L"Icon")) {
            if (sizeBoost > 0) {
                double s = 1.0 + static_cast<double>(sizeBoost) / 100.0;
                Media::ScaleTransform scale;
                scale.ScaleX(s);
                scale.ScaleY(s);
                icon.RenderTransformOrigin(
                    winrt::Windows::Foundation::Point{0.5f, 0.5f});
                icon.RenderTransform(scale);
            } else {
                if (icon.RenderTransform().try_as<Media::ScaleTransform>()) {
                    icon.ClearValue(UIElement::RenderTransformProperty());
                    icon.ClearValue(UIElement::RenderTransformOriginProperty());
                }
            }
        }

        if (g_settings.glowDebugLog) {
            Wh_Log(L"Glow rank %d \"%s\" style=%s th=%.0f round=%d%% size=%d%% "
                   L"layers=%d intensity=%d",
                   rankOneBased, GetButtonAutomationName(button).c_str(),
                   GlowStyleName(style), thickness, g_settings.glowRoundness,
                   g_settings.glowSize, layers, intensity);
        }
    } catch (...) {
        HRESULT hr = winrt::to_hresult();
        Wh_Log(L"ApplyButtonHighlight error %08X", hr);
    }
}

void PruneTrackedButtons_UIThread() {
    std::lock_guard<std::mutex> lock(g_buttonsMutex);
    g_trackedButtons.erase(
        std::remove_if(g_trackedButtons.begin(), g_trackedButtons.end(),
                       [](winrt::weak_ref<FrameworkElement>& weak) {
                           try {
                               return !weak.get();
                           } catch (...) {
                               return true;
                           }
                       }),
        g_trackedButtons.end());
}

void TrackButton_UIThread(FrameworkElement button) {
    if (!button) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        g_dispatcherAnchor = winrt::make_weak(button);

        // Avoid duplicates.
        for (auto& weak : g_trackedButtons) {
            try {
                if (weak.get() == button) {
                    EnsureButtonPathCached(button, /*force=*/false);
                    return;
                }
            } catch (...) {
            }
        }
        g_trackedButtons.push_back(winrt::make_weak(button));
    }
    EnsureButtonPathCached(button, /*force=*/false);
}

// ---------------------------------------------------------------------------
// Button → process path (option C) — adapted from taskbar-volume-control-per-app
// ---------------------------------------------------------------------------

thread_local bool g_captureTaskGroup = false;
thread_local void* g_capturedTaskGroup = nullptr;

WCHAR g_clickSentinel[] = L"wh-rfh-click-sentinel";
void* g_clickSentinel_TaskGroup = nullptr;
void* g_clickSentinel_TaskItem = nullptr;

void* QueryViaVtable(void* object, void* vtable) {
    void* ptr = object;
    while (*(void**)ptr != vtable) {
        ptr = (void**)ptr + 1;
    }
    return ptr;
}

using CWindowTaskItem_GetWindow_t = HWND(WINAPI*)(void* pThis);
CWindowTaskItem_GetWindow_t CWindowTaskItem_GetWindow;

using CImmersiveTaskItem_GetAppWindow_t = HWND(WINAPI*)(void* pThis);
CImmersiveTaskItem_GetAppWindow_t CImmersiveTaskItem_GetAppWindow;

void* CImmersiveTaskItem_vftable = nullptr;
void* CImmersiveTaskItem_vftable_ITaskItem = nullptr;
void* CWindowTaskItem_vftable = nullptr;
void* CWindowTaskItem_vftable_ITaskItem = nullptr;

using TryGetItemFromContainer_TaskListWindowViewModel_t =
    void*(WINAPI*)(void** output, UIElement* container);
TryGetItemFromContainer_TaskListWindowViewModel_t
    TryGetItemFromContainer_TaskListWindowViewModel_Original;

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

using CTaskGroup_GetNumItems_t = int(WINAPI*)(void* pThis);
CTaskGroup_GetNumItems_t CTaskGroup_GetNumItems;

HDPA GetTaskItemsArray(void* taskGroup) {
    if (!CTaskGroup_GetNumItems || !taskGroup) {
        return nullptr;
    }
    static size_t offset = []() -> size_t {
        constexpr int kIntArraySize = 256;
        int arrayOfInts[kIntArraySize];
        int* arrayOfIntPtrs[kIntArraySize];
        for (int i = 0; i < kIntArraySize; i++) {
            arrayOfInts[i] = i;
            arrayOfIntPtrs[i] = &arrayOfInts[i];
        }
        return static_cast<size_t>(CTaskGroup_GetNumItems(arrayOfIntPtrs));
    }();
    return (HDPA)((void**)taskGroup)[offset];
}

using CTaskListWnd_HandleClick_t = HRESULT(WINAPI*)(void* pThis,
                                                    void* taskGroup,
                                                    void* taskItem,
                                                    void** launcherOptions);
CTaskListWnd_HandleClick_t CTaskListWnd_HandleClick_Original;
HRESULT WINAPI CTaskListWnd_HandleClick_Hook(void* pThis,
                                             void* taskGroup,
                                             void* taskItem,
                                             void** launcherOptions) {
    if (launcherOptions && *launcherOptions == &g_clickSentinel) {
        g_clickSentinel_TaskGroup = taskGroup;
        g_clickSentinel_TaskItem = taskItem;
        return S_OK;
    }
    return CTaskListWnd_HandleClick_Original
               ? CTaskListWnd_HandleClick_Original(pThis, taskGroup, taskItem,
                                                   launcherOptions)
               : E_FAIL;
}

using TaskItem_ReportClicked_t = int(WINAPI*)(void* pThis, void* param);
TaskItem_ReportClicked_t TaskItem_ReportClicked_Original;

using TaskGroup_ReportClicked_t = int(WINAPI*)(void* pThis, void* param);
TaskGroup_ReportClicked_t TaskGroup_ReportClicked_Original;

void* GetNativeTaskItemFromWindowsUdkTaskItem(void* windowsUdkTaskItem) {
    if (!TaskItem_ReportClicked_Original || !windowsUdkTaskItem) {
        return nullptr;
    }
    g_clickSentinel_TaskItem = nullptr;
    TaskItem_ReportClicked_Original(windowsUdkTaskItem, &g_clickSentinel);
    return g_clickSentinel_TaskItem;
}

void* GetNativeTaskGroupFromWindowsUdkTaskGroup(void* windowsUdkTaskGroup) {
    if (!TaskGroup_ReportClicked_Original || !windowsUdkTaskGroup) {
        return nullptr;
    }
    g_clickSentinel_TaskGroup = nullptr;
    TaskGroup_ReportClicked_Original(windowsUdkTaskGroup, &g_clickSentinel);
    return g_clickSentinel_TaskGroup;
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
    return g_capturedTaskGroup;
}

HWND GetWindowFromTaskItem(void* taskItem) {
    if (!taskItem) {
        return nullptr;
    }
    if (CImmersiveTaskItem_vftable_ITaskItem &&
        *(void**)taskItem == CImmersiveTaskItem_vftable_ITaskItem &&
        CImmersiveTaskItem_GetAppWindow && CImmersiveTaskItem_vftable) {
        void* immersiveTaskItem =
            QueryViaVtable(taskItem, CImmersiveTaskItem_vftable);
        return CImmersiveTaskItem_GetAppWindow(immersiveTaskItem);
    }
    if (CWindowTaskItem_GetWindow) {
        void* windowTaskItem = taskItem;
        if (CWindowTaskItem_vftable_ITaskItem && CWindowTaskItem_vftable &&
            *(void**)taskItem == CWindowTaskItem_vftable_ITaskItem) {
            windowTaskItem = QueryViaVtable(taskItem, CWindowTaskItem_vftable);
        }
        return CWindowTaskItem_GetWindow(windowTaskItem);
    }
    return nullptr;
}

// Button → PID via taskband (volume-mod approach).
DWORD GetProcessIdFromTaskListButton(UIElement element) {
    try {
        auto windowsUdkTaskItem =
            GetWindowsUdkTaskItemFromTaskListButton(element);
        if (windowsUdkTaskItem) {
            void* nativeTaskItem = GetNativeTaskItemFromWindowsUdkTaskItem(
                windowsUdkTaskItem.get());
            if (nativeTaskItem) {
                HWND hWnd = GetWindowFromTaskItem(nativeTaskItem);
                if (hWnd) {
                    DWORD processId = 0;
                    GetWindowThreadProcessId(hWnd, &processId);
                    return processId;
                }
            }
        }

        void* windowsUdkTaskGroup =
            GetWindowsUdkTaskGroupFromTaskListButton(element);
        if (windowsUdkTaskGroup) {
            void* nativeTaskGroup =
                GetNativeTaskGroupFromWindowsUdkTaskGroup(windowsUdkTaskGroup);
            if (nativeTaskGroup) {
                HDPA taskItemsArray = GetTaskItemsArray(nativeTaskGroup);
                if (taskItemsArray && DPA_GetPtrCount(taskItemsArray) > 0) {
                    void* taskItem = DPA_GetPtr(taskItemsArray, 0);
                    HWND hWnd = GetWindowFromTaskItem(taskItem);
                    if (hWnd) {
                        DWORD processId = 0;
                        GetWindowThreadProcessId(hWnd, &processId);
                        return processId;
                    }
                }
            }
        }
    } catch (...) {
    }
    return 0;
}

// Resolve button → path; force=true on click. Returns path upper or empty.
std::wstring EnsureButtonPathCached(FrameworkElement button, bool force) {
    if (!button || !g_taskbandResolveReady.load()) {
        return {};
    }

    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_buttonPathMutex);
        for (auto& e : g_buttonPathCache) {
            FrameworkElement b = nullptr;
            try {
                b = e.button.get();
            } catch (...) {
                continue;
            }
            if (b != button) {
                continue;
            }
            const ULONGLONG debounceMs = e.pathUpper.empty() ? 250 : 2000;
            if (!force && e.resolveAttempted &&
                (now - e.lastResolveTick) < debounceMs) {
                return e.pathUpper;
            }
            // fall through to re-resolve below using this entry
            break;
        }
    }

    DWORD pid = 0;
    HWND hwnd = nullptr;
    std::vector<HWND> groupHwnds;
    try {
        UIElement el = button.as<UIElement>();
        // Prefer full resolve (also captures HWND via item path).
        auto udkItem = GetWindowsUdkTaskItemFromTaskListButton(el);
        if (udkItem) {
            void* native =
                GetNativeTaskItemFromWindowsUdkTaskItem(udkItem.get());
            hwnd = GetWindowFromTaskItem(native);
            if (hwnd) {
                GetWindowThreadProcessId(hwnd, &pid);
                groupHwnds.push_back(hwnd);
            }
        }
        void* windowsUdkTaskGroup =
            GetWindowsUdkTaskGroupFromTaskListButton(el);
        if (windowsUdkTaskGroup) {
            void* nativeTaskGroup =
                GetNativeTaskGroupFromWindowsUdkTaskGroup(windowsUdkTaskGroup);
            if (nativeTaskGroup) {
                HDPA taskItemsArray = GetTaskItemsArray(nativeTaskGroup);
                if (taskItemsArray) {
                    const int n = DPA_GetPtrCount(taskItemsArray);
                    for (int i = 0; i < n; ++i) {
                        HWND h = GetWindowFromTaskItem(
                            DPA_GetPtr(taskItemsArray, i));
                        if (!h || !IsWindow(h)) {
                            continue;
                        }
                        if (std::find(groupHwnds.begin(), groupHwnds.end(),
                                      h) == groupHwnds.end()) {
                            groupHwnds.push_back(h);
                        }
                        if (!hwnd) {
                            hwnd = h;
                        }
                        if (!pid) {
                            GetWindowThreadProcessId(h, &pid);
                        }
                    }
                }
            }
        }
        if (!pid) {
            pid = GetProcessIdFromTaskListButton(el);
        }
    } catch (...) {
        pid = 0;
    }

    std::wstring pathUpper;
    if (pid) {
        std::wstring path = GetProcessImagePath(pid);
        if (!path.empty()) {
            pathUpper = ToUpper(path);
        }
    }

    std::wstring classUpper =
        hwnd ? ToUpper(GetWindowClassName(hwnd)) : std::wstring{};
    std::wstring appIdUpper =
        hwnd ? ToUpper(GetWindowAppUserModelId(hwnd)) : std::wstring{};
    std::wstring autoIdUpper = GetButtonAutomationAppId(button);
    if (appIdUpper.empty()) {
        appIdUpper = autoIdUpper;
    }

    {
        std::lock_guard<std::mutex> lock(g_buttonPathMutex);
        bool found = false;
        for (auto& e : g_buttonPathCache) {
            FrameworkElement b = nullptr;
            try {
                b = e.button.get();
            } catch (...) {
                continue;
            }
            if (b != button) {
                continue;
            }
            e.pathUpper = pathUpper;
            e.appIdUpper = appIdUpper;
            e.classUpper = classUpper;
            e.autoIdUpper = autoIdUpper;
            e.pid = pid;
            e.sampleHwnd = hwnd;
            e.groupHwnds = groupHwnds;
            e.resolveAttempted = true;
            e.lastResolveTick = now;
            found = true;
            break;
        }
        if (!found) {
            ButtonPathCacheEntry e;
            e.button = winrt::make_weak(button);
            e.pathUpper = pathUpper;
            e.appIdUpper = appIdUpper;
            e.classUpper = classUpper;
            e.autoIdUpper = autoIdUpper;
            e.pid = pid;
            e.sampleHwnd = hwnd;
            e.groupHwnds = std::move(groupHwnds);
            e.resolveAttempted = true;
            e.lastResolveTick = now;
            g_buttonPathCache.push_back(std::move(e));
        }
        // Prune dead weaks occasionally.
        if (g_buttonPathCache.size() > 128) {
            g_buttonPathCache.erase(
                std::remove_if(
                    g_buttonPathCache.begin(), g_buttonPathCache.end(),
                    [](ButtonPathCacheEntry& e) {
                        try {
                            return !e.button.get();
                        } catch (...) {
                            return true;
                        }
                    }),
                g_buttonPathCache.end());
        }
    }

    if (g_settings.glowDebugLog) {
        Wh_Log(L"Button path cache: pid=%u path=%s class=%s appId=%s force=%d "
               L"name=\"%s\"",
               pid, pathUpper.empty() ? L"(none)" : pathUpper.c_str(),
               classUpper.empty() ? L"?" : classUpper.c_str(),
               appIdUpper.empty() ? L"?" : appIdUpper.c_str(), force ? 1 : 0,
               GetButtonAutomationName(button).c_str());
    }
    return pathUpper;
}

// Lookup only (no resolve).
std::wstring GetCachedButtonPath(FrameworkElement button) {
    return GetCachedButtonIdentity(button).pathUpper;
}

ButtonIdentity GetCachedButtonIdentity(FrameworkElement button) {
    ButtonIdentity out;
    if (!button) {
        return out;
    }
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    for (auto& e : g_buttonPathCache) {
        try {
            if (e.button.get() == button) {
                out.pathUpper = e.pathUpper;
                out.appIdUpper = e.appIdUpper;
                out.classUpper = e.classUpper;
                out.autoIdUpper = e.autoIdUpper;
                out.pid = e.pid;
                out.sampleHwnd = e.sampleHwnd;
                out.groupHwnds = e.groupHwnds;
                return out;
            }
        } catch (...) {
        }
    }
    return out;
}

void ApplyAllHighlights_UIThread() {
    PruneTrackedButtons_UIThread();

    std::vector<AppFocusInfo> ranks;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        ranks = g_rankedApps;
    }

    std::vector<winrt::weak_ref<FrameworkElement>> buttons;
    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        buttons = g_trackedButtons;
    }

    if (g_settings.glowDebugLog) {
        for (size_t i = 0; i < ranks.size(); ++i) {
            Wh_Log(L"  rank list[%zu]: %s", i + 1, ranks[i].displayName.c_str());
        }
    }
    Wh_Log(L"ApplyAllHighlights: %zu tracked buttons, %zu ranks, enabled=%d",
           buttons.size(), ranks.size(), g_settings.enabled ? 1 : 0);

    if (!g_settings.enabled || g_unloading.load() || ranks.empty()) {
        for (auto& weak : buttons) {
            FrameworkElement button = nullptr;
            try {
                button = weak.get();
            } catch (...) {
                continue;
            }
            if (!button) {
                continue;
            }
            try {
                auto dispatcher = button.Dispatcher();
                if (dispatcher && !dispatcher.HasThreadAccess()) {
                    continue;
                }
            } catch (...) {
            }
            ClearButtonHighlight(button);
        }
        g_pendingOverlaySweep = false;
        if (ranks.empty()) {
            Wh_Log(L"ApplyAllHighlights: no ranks — cleared overlays on %zu "
                   L"tracked buttons",
                   buttons.size());
        }
        return;
    }

    // Collect live buttons on *this* dispatcher (secondary taskbars may
    // use a different XAML island / CoreDispatcher).
    std::vector<FrameworkElement> live;
    live.reserve(buttons.size());
    for (auto& weak : buttons) {
        FrameworkElement button = nullptr;
        try {
            button = weak.get();
        } catch (...) {
            continue;
        }
        if (!button) {
            continue;
        }
        try {
            auto dispatcher = button.Dispatcher();
            if (dispatcher && !dispatcher.HasThreadAccess()) {
                continue;
            }
        } catch (...) {
        }
        live.push_back(button);
    }

    if (g_settings.glowDebugLog) {
        int dumped = 0;
        for (auto& button : live) {
            Wh_Log(L"  button[%d]: running=%d name=\"%s\"", dumped,
                   TaskListButton_IsRunning(button) ? 1 : 0,
                   GetButtonAutomationName(button).c_str());
            if (++dumped >= 24) {
                break;
            }
        }
    }

    // Prefer process-path cache (option C), then fuzzy name scores.
    // 1:1 assignment, highest score wins.
    struct Cand {
        int score;
        size_t rankIdx;
        size_t buttonIdx;
    };
    std::vector<Cand> cands;
    std::vector<std::wstring> buttonPaths(live.size());
    for (size_t bi = 0; bi < live.size(); ++bi) {
        // Resolve once if missing (cheap if cached).
        buttonPaths[bi] = EnsureButtonPathCached(live[bi], /*force=*/false);
        for (size_t ri = 0; ri < ranks.size(); ++ri) {
            int s = ScoreButtonForRank(live[bi], ranks[ri], true);
            if (s < 70) {
                s = ScoreButtonForRank(live[bi], ranks[ri], false);
            }
            if (s >= 70) {
                cands.push_back({s, ri, bi});
            }
        }
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.score > b.score; });

    std::vector<int> buttonRank(live.size(), 0);  // 1-based rank or 0
    std::vector<bool> rankTaken(ranks.size(), false);
    std::vector<bool> buttonTaken(live.size(), false);

    // Strong matches (path / exact-ish name, score >= 90): the same rank
    // may bind many buttons — primary + secondary taskbar, or Never Combine
    // (one button per window of the same exe). Weak fuzzy stays 1:1 so
    // VS Code cannot steal VSCodium.
    for (const auto& c : cands) {
        if (buttonTaken[c.buttonIdx]) {
            continue;
        }
        if (c.score >= 90) {
            buttonTaken[c.buttonIdx] = true;
            rankTaken[c.rankIdx] = true;
            buttonRank[c.buttonIdx] = static_cast<int>(c.rankIdx) + 1;
        } else if (!rankTaken[c.rankIdx]) {
            rankTaken[c.rankIdx] = true;
            buttonTaken[c.buttonIdx] = true;
            buttonRank[c.buttonIdx] = static_cast<int>(c.rankIdx) + 1;
        } else {
            continue;
        }

        std::wstring autoName = GetButtonAutomationName(live[c.buttonIdx]);
        StoreAutomationNameIfFits(ranks[c.rankIdx], autoName);
        // Successful bind ⇒ this process has a taskbar presence.
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            auto it = g_appFocusMap.find(ranks[c.rankIdx].key);
            if (it != g_appFocusMap.end()) {
                it->second.seenOnTaskbar = true;
            }
        }
        if (g_settings.glowDebugLog) {
            Wh_Log(L"  bind rank %zu %s -> \"%s\" (score=%d path=%s)",
                   c.rankIdx + 1, ranks[c.rankIdx].displayName.c_str(),
                   autoName.c_str(), c.score,
                   buttonPaths[c.buttonIdx].empty()
                       ? L"?"
                       : buttonPaths[c.buttonIdx].c_str());
        }
    }

    for (size_t bi = 0; bi < live.size(); ++bi) {
        if (buttonRank[bi] > 0) {
            ApplyButtonHighlight(live[bi], buttonRank[bi]);
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

// Second pass: unmatched ranks — pick best free button with lower bar
    // (uses window title). Helps Windhawk and odd product names.
    std::vector<std::wstring> demoteKeys;
    for (size_t ri = 0; ri < ranks.size(); ++ri) {
        if (rankTaken[ri]) {
            continue;
        }
        int bestScore = 0;
        size_t bestBi = SIZE_MAX;
        for (size_t bi = 0; bi < live.size(); ++bi) {
            if (buttonTaken[bi]) {
                continue;
            }
            int s = ScoreButtonForRank(live[bi], ranks[ri], false);
            if (s > bestScore) {
                bestScore = s;
                bestBi = bi;
            }
        }
        std::wstring autoName = (bestBi != SIZE_MAX)
                                    ? GetButtonAutomationName(live[bestBi])
                                    : std::wstring{};
        if (bestBi != SIZE_MAX && bestScore >= 70 &&
            AutomationNameFitsRank(ranks[ri], autoName)) {
            rankTaken[ri] = true;
            buttonTaken[bestBi] = true;
            buttonRank[bestBi] = static_cast<int>(ri) + 1;
            StoreAutomationNameIfFits(ranks[ri], autoName);
            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                auto it = g_appFocusMap.find(ranks[ri].key);
                if (it != g_appFocusMap.end()) {
                    it->second.seenOnTaskbar = true;
                }
            }
            Wh_Log(L"  bind rank %zu %s -> \"%s\" (score=%d, fallback)",
                   ri + 1, ranks[ri].displayName.c_str(), autoName.c_str(),
                   bestScore);
            ApplyButtonHighlight(live[bestBi], buttonRank[bestBi]);
        } else {
            Wh_Log(L"  UNMATCHED rank %zu: %s (bestScore=%d title=\"%s\")",
                   ri + 1, ranks[ri].displayName.c_str(), bestScore,
                   ranks[ri].lastWindowTitle.c_str());
            if (g_settings.glowDebugLog && bestBi != SIZE_MAX) {
                Wh_Log(L"    closest button was \"%s\"",
                       GetButtonAutomationName(live[bestBi]).c_str());
            }
            // Tray-only / no button: drop from ranks so it stops occupying a
            // slot (e.g. HA Desktop Widget). Only demote once we already know
            // several real taskbar paths (avoid false demote at explorer start).
            size_t resolvedButtons = 0;
            {
                std::lock_guard<std::mutex> lock(g_buttonPathMutex);
                for (const auto& e : g_buttonPathCache) {
                    if (!e.pathUpper.empty()) {
                        ++resolvedButtons;
                    }
                }
            }
            if (g_settings.requireTaskbarButton && bestScore == 0 &&
                resolvedButtons >= 2) {
                demoteKeys.push_back(ranks[ri].key);
            }
        }
    }
    // Demote after the loop so rank list stays stable while matching.
    if (!demoteKeys.empty()) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (const auto& key : demoteKeys) {
            auto it = g_appFocusMap.find(key);
            if (it != g_appFocusMap.end() && !it->second.seenOnTaskbar) {
                Wh_Log(L"  demoting non-taskbar app from ranks: %s",
                       it->second.displayName.c_str());
                it->second.lastConfirmedFocusTick = 0;
                it->second.seenOnTaskbar = false;
            }
        }
        RecomputeRanksLocked();
    }
}

void ClearAllHighlights_UIThread() {
    PruneTrackedButtons_UIThread();

    std::vector<winrt::weak_ref<FrameworkElement>> buttons;
    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        buttons = g_trackedButtons;
    }

    for (auto& weak : buttons) {
        FrameworkElement button = nullptr;
        try {
            button = weak.get();
        } catch (...) {
            continue;
        }
        if (!button) {
            continue;
        }
        try {
            auto dispatcher = button.Dispatcher();
            if (dispatcher && !dispatcher.HasThreadAccess()) {
                continue;
            }
        } catch (...) {
        }
        ClearButtonHighlight(button);
    }
}

// ---------------------------------------------------------------------------
// Thumbnail preview glow (multi-window flyouts)
// ---------------------------------------------------------------------------

void AddThumbnailTaskItemMapping(
    winrt::Windows::Foundation::IInspectable thumbnail,
    void* taskGroup,
    void* taskItem) {
    if (!thumbnail || !taskItem) {
        return;
    }
    HWND hwnd = GetWindowFromTaskItem(taskItem);
    std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
    std::erase_if(g_thumbnailTaskItemMapping, [&](const ThumbnailTaskItemMapping& item) {
        try {
            auto t = item.thumbnail.get();
            if (!t || t == thumbnail) {
                return true;
            }
        } catch (...) {
            return true;
        }
        return item.taskGroup == taskGroup && item.taskItem == taskItem;
    });
    ThumbnailTaskItemMapping entry;
    entry.thumbnail = winrt::make_weak(thumbnail);
    entry.taskGroup = taskGroup;
    entry.taskItem = taskItem;
    entry.hwnd = hwnd;
    g_thumbnailTaskItemMapping.push_back(std::move(entry));
    // Soft cap
    if (g_thumbnailTaskItemMapping.size() > 128) {
        std::erase_if(g_thumbnailTaskItemMapping, [](const ThumbnailTaskItemMapping& item) {
            try {
                return !item.thumbnail.get();
            } catch (...) {
                return true;
            }
        });
    }
}

HWND HwndFromMappingEntry(const ThumbnailTaskItemMapping& item) {
    if (item.hwnd && IsWindow(item.hwnd)) {
        return item.hwnd;
    }
    if (item.taskItem) {
        HWND h = GetWindowFromTaskItem(item.taskItem);
        if (h && IsWindow(h)) {
            return h;
        }
    }
    return nullptr;
}

// Closed flyouts leave dead weaks; their HWNDs/groups must not be reused to
// resolve a later app's sibling count (TC Lister vs an earlier Chrome hover).
bool ThumbnailMappingLive(const ThumbnailTaskItemMapping& item) {
    try {
        return item.thumbnail.get() != nullptr;
    } catch (...) {
        return false;
    }
}

// True if two WinRT objects are the same COM identity (different projections
// of the same TaskItemThumbnail still match).
bool SameInspectableIdentity(winrt::Windows::Foundation::IInspectable const& a,
                             winrt::Windows::Foundation::IInspectable const& b) {
    if (!a || !b) {
        return false;
    }
    try {
        if (a == b) {
            return true;
        }
        if (winrt::get_abi(a) == winrt::get_abi(b)) {
            return true;
        }
        // Canonical COM identity (QI to IUnknown).
        auto ua = a.as<::IUnknown>();
        auto ub = b.as<::IUnknown>();
        return winrt::get_abi(ua) == winrt::get_abi(ub);
    } catch (...) {
        return false;
    }
}

// Strong resolve: TaskItemThumbnail model (DataContext) → HWND.
// Returns also taskGroup when found (for index-order fill of siblings).
HWND ResolveHwndFromThumbnailModel(
    winrt::Windows::Foundation::IInspectable thumbnail,
    void** outTaskGroup = nullptr) {
    if (outTaskGroup) {
        *outTaskGroup = nullptr;
    }
    if (!thumbnail) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
    for (const auto& item : g_thumbnailTaskItemMapping) {
        try {
            auto t = item.thumbnail.get();
            if (!t) {
                continue;
            }
            if (!SameInspectableIdentity(t, thumbnail)) {
                continue;
            }
            if (outTaskGroup) {
                *outTaskGroup = item.taskGroup;
            }
            return HwndFromMappingEntry(item);
        } catch (...) {
        }
    }
    return nullptr;
}

// HWNDs for one task group in construction order (matches flyout item order
// when TaskItemThumbnail ctors ran in display order).
std::vector<HWND> HwndsForTaskGroupInOrder(void* taskGroup) {
    std::vector<HWND> out;
    if (!taskGroup) {
        return out;
    }
    std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
    for (const auto& item : g_thumbnailTaskItemMapping) {
        if (item.taskGroup != taskGroup) {
            continue;
        }
        HWND h = HwndFromMappingEntry(item);
        if (h) {
            // De-dupe while preserving first-seen order.
            if (std::find(out.begin(), out.end(), h) == out.end()) {
                out.push_back(h);
            }
        }
    }
    return out;
}

// Pick a taskGroup that has exactly `siblingCount` unique HWNDs (current flyout).
void* FindTaskGroupForSiblingCount(size_t siblingCount) {
    if (siblingCount == 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
    // group -> ordered unique hwnds
    struct GroupAcc {
        void* group = nullptr;
        std::vector<HWND> hwnds;
    };
    std::vector<GroupAcc> groups;
    for (const auto& item : g_thumbnailTaskItemMapping) {
        if (!item.taskGroup || !ThumbnailMappingLive(item)) {
            continue;
        }
        HWND h = HwndFromMappingEntry(item);
        if (!h) {
            continue;
        }
        GroupAcc* acc = nullptr;
        for (auto& g : groups) {
            if (g.group == item.taskGroup) {
                acc = &g;
                break;
            }
        }
        if (!acc) {
            groups.push_back({item.taskGroup, {}});
            acc = &groups.back();
        }
        if (std::find(acc->hwnds.begin(), acc->hwnds.end(), h) ==
            acc->hwnds.end()) {
            acc->hwnds.push_back(h);
        }
    }
    // Last exact match wins — maps are append-only, so the newest live
    // flyout is preferred over an older group with the same window count.
    void* exact = nullptr;
    for (const auto& g : groups) {
        if (g.hwnds.size() == siblingCount) {
            exact = g.group;
        }
    }
    if (exact) {
        return exact;
    }
    // Prefer largest group that is at least siblingCount (partial flyout).
    void* best = nullptr;
    size_t bestN = 0;
    for (const auto& g : groups) {
        if (g.hwnds.size() >= siblingCount && g.hwnds.size() > bestN) {
            bestN = g.hwnds.size();
            best = g.group;
        }
    }
    return best;
}

// Last N unique HWNDs from the mapping vector (most recently constructed models
// — typically the open flyout when maps were just created on hover).
std::vector<HWND> LastMappedHwnds(size_t n) {
    std::vector<HWND> out;
    if (n == 0) {
        return out;
    }
    std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
    for (auto it = g_thumbnailTaskItemMapping.rbegin();
         it != g_thumbnailTaskItemMapping.rend() && out.size() < n; ++it) {
        if (!ThumbnailMappingLive(*it)) {
            continue;
        }
        HWND h = HwndFromMappingEntry(*it);
        if (!h) {
            continue;
        }
        if (std::find(out.begin(), out.end(), h) == out.end()) {
            out.push_back(h);
        }
    }
    // We walked newest→oldest; reverse to construction/display order.
    std::reverse(out.begin(), out.end());
    return out;
}

HWND ResolveHwndForThumbnailView(FrameworkElement thumbView,
                                 void** outTaskGroup = nullptr) {
    if (outTaskGroup) {
        *outTaskGroup = nullptr;
    }
    if (!thumbView) {
        return nullptr;
    }
    try {
        // DataContext is often the TaskItemThumbnail model object.
        auto dc = thumbView.DataContext();
        if (dc) {
            if (HWND h = ResolveHwndFromThumbnailModel(dc, outTaskGroup)) {
                return h;
            }
        }
    } catch (...) {
    }
    // No title fallback here — identical titles (Calibre 2× same file) would
    // all bind to the same HWND. Callers do unique assignment separately.
    return nullptr;
}

// Title → HWND only for unique assignment (each HWND at most once).
HWND MatchTitleToUnusedRecent(const std::wstring& autoName,
                              const std::vector<WindowFocusInfo>& recent,
                              const std::unordered_set<HWND>& used) {
    if (autoName.empty()) {
        return nullptr;
    }
    const std::wstring cardPath = ToUpper(ExtractBracketedPath(autoName));
    const std::wstring cardFile =
        cardPath.empty() ? std::wstring{}
                         : ToUpper(FileNameFromPath(cardPath));

    int bestScore = 0;
    HWND bestHwnd = nullptr;
    ULONGLONG bestTick = 0;
    for (const auto& info : recent) {
        if (!info.hwnd || used.count(info.hwnd)) {
            continue;
        }
        int s = ScoreTitleToAutomationName(info.windowTitle, autoName);
        if (IsWindow(info.hwnd)) {
            const std::wstring liveTitle = GetWindowTitle(info.hwnd);
            s = (std::max)(s, ScoreTitleToAutomationName(liveTitle, autoName));
            if (!cardPath.empty()) {
                const std::wstring winPath =
                    ToUpper(ExtractBracketedPath(liveTitle));
                const std::wstring infoPath =
                    ToUpper(ExtractBracketedPath(info.windowTitle));
                if (winPath == cardPath || infoPath == cardPath) {
                    s = (std::max)(s, 100);
                } else if (!cardFile.empty()) {
                    if (ToUpper(FileNameFromPath(winPath)) == cardFile ||
                        ToUpper(FileNameFromPath(infoPath)) == cardFile ||
                        ToUpper(FileNameFromPath(liveTitle)) == cardFile ||
                        ToUpper(FileNameFromPath(info.windowTitle)) ==
                            cardFile) {
                        s = (std::max)(s, 96);
                    }
                }
            }
        }
        if (s > bestScore ||
            (s == bestScore && s >= 70 && info.lastConfirmedTick > bestTick)) {
            bestScore = s;
            bestHwnd = info.hwnd;
            bestTick = info.lastConfirmedTick;
        }
    }
    return bestScore >= 70 ? bestHwnd : nullptr;
}

void CollectThumbnailViewsUnder(FrameworkElement root,
                                std::vector<FrameworkElement>& out) {
    if (!root) {
        return;
    }
    try {
        if (winrt::get_class_name(root) == L"Taskbar.TaskItemThumbnailView") {
            out.push_back(root);
        }
        int n = Media::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < n; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(root, i)
                             .try_as<FrameworkElement>();
            if (child) {
                CollectThumbnailViewsUnder(child, out);
            }
        }
    } catch (...) {
    }
}

FrameworkElement FindAncestorByClassName(FrameworkElement element,
                                         PCWSTR className) {
    FrameworkElement cur = element;
    for (int guard = 0; guard < 32 && cur; ++guard) {
        try {
            if (winrt::get_class_name(cur) == className) {
                return cur;
            }
            cur = Media::VisualTreeHelper::GetParent(cur)
                      .try_as<FrameworkElement>();
        } catch (...) {
            break;
        }
    }
    return nullptr;
}

std::vector<FrameworkElement> CollectSiblingThumbnailViews(
    FrameworkElement thumbView) {
    std::vector<FrameworkElement> out;
    if (!thumbView) {
        return out;
    }

    // Prefer known list hosts from the XAML thumbnail flyout.
    static const PCWSTR kHosts[] = {
        L"Taskbar.TaskItemThumbnailList",
        L"Taskbar.TaskItemThumbnailScrollableList",
        L"Taskbar.FlyoutFrame",
    };
    FrameworkElement host = nullptr;
    for (auto name : kHosts) {
        host = FindAncestorByClassName(thumbView, name);
        if (host) {
            break;
        }
    }
    if (!host) {
        // Walk up a few parents and collect from the highest with 2+ thumbs.
        FrameworkElement cur = thumbView;
        for (int i = 0; i < 8 && cur; ++i) {
            try {
                cur = Media::VisualTreeHelper::GetParent(cur)
                          .try_as<FrameworkElement>();
            } catch (...) {
                cur = nullptr;
            }
            if (cur) {
                host = cur;
            }
        }
    }
    if (host) {
        CollectThumbnailViewsUnder(host, out);
    }
    if (out.empty()) {
        out.push_back(thumbView);
    }
    return out;
}

void TrackThumbView_UIThread(FrameworkElement thumbView) {
    if (!thumbView) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
    for (auto& weak : g_trackedThumbViews) {
        try {
            if (weak.get() == thumbView) {
                return;
            }
        } catch (...) {
        }
    }
    g_trackedThumbViews.push_back(winrt::make_weak(thumbView));
    if (g_trackedThumbViews.size() > 64) {
        g_trackedThumbViews.erase(
            std::remove_if(
                g_trackedThumbViews.begin(), g_trackedThumbViews.end(),
                [](winrt::weak_ref<FrameworkElement>& weak) {
                    try {
                        return !weak.get();
                    } catch (...) {
                        return true;
                    }
                }),
            g_trackedThumbViews.end());
    }
}

void RemoveNamedDescendant(FrameworkElement root, PCWSTR name) {
    if (!root || !name) {
        return;
    }
    for (int guard = 0; guard < 4; ++guard) {
        auto existing = FindDescendantByName(root, name);
        if (!existing) {
            return;
        }
        try {
            if (auto parent = Media::VisualTreeHelper::GetParent(existing)
                                  .try_as<Controls::Panel>()) {
                uint32_t idx = 0;
                if (parent.Children().IndexOf(existing, idx)) {
                    parent.Children().RemoveAt(idx);
                    continue;
                }
            }
        } catch (...) {
        }
        break;
    }
}

// Find a Panel to host overlays (root Grid under TaskItemThumbnailView).
Controls::Panel GetThumbnailHostPanel(FrameworkElement thumbView) {
    if (!thumbView) {
        return nullptr;
    }
    try {
        if (auto p = thumbView.try_as<Controls::Panel>()) {
            return p;
        }
        int n = Media::VisualTreeHelper::GetChildrenCount(thumbView);
        for (int i = 0; i < n; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(thumbView, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }
            if (winrt::get_class_name(child) ==
                L"Windows.UI.Xaml.Controls.Grid") {
                if (auto p = child.try_as<Controls::Panel>()) {
                    return p;
                }
            }
            if (auto p = child.try_as<Controls::Panel>()) {
                return p;
            }
        }
    } catch (...) {
    }
    return nullptr;
}

FrameworkElement FindThumbnailTitleElement(FrameworkElement thumbView) {
    if (!thumbView) {
        return nullptr;
    }
    static const PCWSTR kNames[] = {
        L"DisplayNameTextBlock",
        L"DisplayName",
        L"TitleTextBlock",
        L"Title",
    };
    for (auto name : kNames) {
        if (auto el = FindDescendantByName(thumbView, name)) {
            if (winrt::get_class_name(el) == L"Windows.UI.Xaml.Controls.TextBlock" ||
                el.try_as<Controls::TextBlock>()) {
                return el;
            }
            // Name matched a wrapper — prefer a TextBlock inside.
            if (auto tb = FindDescendantByName(el, L"DisplayNameTextBlock")) {
                return tb;
            }
            return el;
        }
    }
    // First TextBlock in the tree (title is usually the only one besides close).
    try {
        std::function<FrameworkElement(FrameworkElement)> walk;
        walk = [&](FrameworkElement root) -> FrameworkElement {
            if (!root) {
                return nullptr;
            }
            auto cn = winrt::get_class_name(root);
            if (cn == L"Windows.UI.Xaml.Controls.TextBlock") {
                // Skip close-button glyphs (often single-char / Segoe icons).
                try {
                    if (auto tb = root.try_as<Controls::TextBlock>()) {
                        auto text = tb.Text();
                        if (text.size() >= 2) {
                            return root;
                        }
                    }
                } catch (...) {
                    return root;
                }
            }
            int n = Media::VisualTreeHelper::GetChildrenCount(root);
            for (int i = 0; i < n; ++i) {
                auto child = Media::VisualTreeHelper::GetChild(root, i)
                                 .try_as<FrameworkElement>();
                if (auto found = walk(child)) {
                    return found;
                }
            }
            return nullptr;
        };
        return walk(thumbView);
    } catch (...) {
    }
    return nullptr;
}

std::wstring GetThumbnailMatchTitle(FrameworkElement thumbView) {
    std::wstring autoName;
    try {
        autoName =
            Automation::AutomationProperties::GetName(thumbView).c_str();
    } catch (...) {
    }
    autoName = NormalizeAutomationName(std::move(autoName));

    std::wstring text;
    try {
        if (auto titleEl = FindThumbnailTitleElement(thumbView)) {
            if (auto tb = titleEl.try_as<Controls::TextBlock>()) {
                text = tb.Text().c_str();
            } else {
                text = titleEl.Name().c_str();
            }
        }
    } catch (...) {
    }
    text = NormalizeAutomationName(std::move(text));

    // Prefer the more specific string (file name vs generic "Lister").
    if (text.size() > autoName.size()) {
        return text;
    }
    if (autoName.size() > text.size()) {
        return autoName;
    }
    return !text.empty() ? text : autoName;
}

// Snap-group card in the same flyout as the individual windows
// ("Group | Lister - [file] and 1 other window"). Must not be treated as a
// window thumbnail — it steals group-order HWND 0 and the recent glow.
bool IsSnapGroupThumbnailView(FrameworkElement view) {
    if (!view) {
        return false;
    }
    auto looksLikeGroup = [](const std::wstring& s) -> bool {
        if (s.empty()) {
            return false;
        }
        if (s.size() >= 5 && _wcsnicmp(s.c_str(), L"Group", 5) == 0 &&
            (s.size() == 5 || s[5] == L' ' || s[5] == L'|' || s[5] == L'-')) {
            return true;
        }
        if (s.find(L" other window") != std::wstring::npos) {
            return true;
        }
        return false;
    };

    try {
        std::wstring name =
            Automation::AutomationProperties::GetName(view).c_str();
        if (looksLikeGroup(name) ||
            looksLikeGroup(NormalizeAutomationName(name))) {
            return true;
        }
    } catch (...) {
    }

    try {
        if (auto title = FindThumbnailTitleElement(view)) {
            if (auto tb = title.try_as<Controls::TextBlock>()) {
                std::wstring text = tb.Text().c_str();
                if (looksLikeGroup(text)) {
                    return true;
                }
            }
        }
    } catch (...) {
    }

    if (auto repeater = FindDescendantByName(view, L"IconsRepeater")) {
        try {
            if (Media::VisualTreeHelper::GetChildrenCount(repeater) >= 2) {
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

struct EnumSameClassCtx {
    DWORD pid = 0;
    std::wstring classUpper;
    std::vector<HWND>* out = nullptr;
};

BOOL CALLBACK EnumSameClassWndProc(HWND hWnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumSameClassCtx*>(lParam);
    if (!ctx || !ctx->out) {
        return FALSE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid != ctx->pid || !IsWindowVisible(hWnd)) {
        return TRUE;
    }
    if (GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD) {
        return TRUE;
    }
    if (ToUpper(GetWindowClassName(hWnd)) != ctx->classUpper) {
        return TRUE;
    }
    if (std::find(ctx->out->begin(), ctx->out->end(), hWnd) ==
        ctx->out->end()) {
        ctx->out->push_back(hWnd);
    }
    return TRUE;
}

std::vector<HWND> ExpandSameClassWindows(const std::vector<HWND>& seeds) {
    std::vector<HWND> out;
    for (HWND seed : seeds) {
        if (!seed || !IsWindow(seed)) {
            continue;
        }
        if (std::find(out.begin(), out.end(), seed) == out.end()) {
            out.push_back(seed);
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(seed, &pid);
        std::wstring cls = ToUpper(GetWindowClassName(seed));
        if (!pid || cls.empty()) {
            continue;
        }
        EnumSameClassCtx ctx{pid, std::move(cls), &out};
        EnumWindows(EnumSameClassWndProc, reinterpret_cast<LPARAM>(&ctx));
    }
    return out;
}

FrameworkElement FindThumbnailBackgroundBorder(FrameworkElement thumbView) {
    if (!thumbView) {
        return nullptr;
    }
    if (auto b = FindDescendantByName(thumbView, L"BackgroundBorder")) {
        return b;
    }
    // First Border under the root grid (styler targets this).
    try {
        auto panel = GetThumbnailHostPanel(thumbView);
        if (!panel) {
            return nullptr;
        }
        auto fe = panel.as<FrameworkElement>();
        int n = Media::VisualTreeHelper::GetChildrenCount(fe);
        for (int i = 0; i < n; ++i) {
            auto child = Media::VisualTreeHelper::GetChild(fe, i)
                             .try_as<FrameworkElement>();
            if (!child) {
                continue;
            }
            auto cn = winrt::get_class_name(child);
            if (cn == L"Windows.UI.Xaml.Controls.Border") {
                return child;
            }
        }
    } catch (...) {
    }
    return nullptr;
}

void ClearThumbnailNativeStyles(FrameworkElement thumbView) {
    if (!thumbView) {
        return;
    }
    const bool hadMarker =
        FindDescendantByName(thumbView, kThumbNativeStyleMarker) != nullptr;
    if (!hadMarker) {
        return;
    }
    try {
        // Only restore BackgroundBorder when we previously tinted it (Plate).
        if (auto border = FindThumbnailBackgroundBorder(thumbView)) {
            if (auto b = border.try_as<Controls::Border>()) {
                if (b.ReadLocalValue(Controls::Border::BackgroundProperty()) !=
                    DependencyProperty::UnsetValue()) {
                    b.ClearValue(Controls::Border::BackgroundProperty());
                }
            }
        }
    } catch (...) {
    }
    RemoveNamedDescendant(thumbView, kThumbNativeStyleMarker);
}

void ClearThumbnailHighlight(FrameworkElement thumbView) {
    if (!thumbView) {
        return;
    }
    try {
        RemoveNamedDescendant(thumbView, kThumbGlowElementName);
        RemoveNamedDescendant(thumbView, kThumbTitleBgName);
        RemoveNamedDescendant(thumbView, kThumbTitleBarName);
        ClearThumbnailNativeStyles(thumbView);
        if (auto panel = thumbView.try_as<Controls::Panel>()) {
            RemoveNamedChild(panel, kThumbGlowElementName);
            RemoveNamedChild(panel, kThumbTitleBgName);
            RemoveNamedChild(panel, kThumbTitleBarName);
            RemoveNamedChild(panel, kThumbNativeStyleMarker);
        }
        auto hostPanel = GetThumbnailHostPanel(thumbView);
        if (hostPanel) {
            RemoveNamedChild(hostPanel, kThumbGlowElementName);
            RemoveNamedChild(hostPanel, kThumbTitleBgName);
            RemoveNamedChild(hostPanel, kThumbTitleBarName);
            RemoveNamedChild(hostPanel, kThumbNativeStyleMarker);
        }
    } catch (...) {
    }
}

void BringElementToFront(Controls::Panel panel, UIElement el) {
    if (!panel || !el) {
        return;
    }
    try {
        auto children = panel.Children();
        uint32_t idx = 0;
        if (children.IndexOf(el, idx) && idx + 1 != children.Size()) {
            children.RemoveAt(idx);
            children.Append(el);
        }
    } catch (...) {
    }
}

// Overlay host that must NOT affect parent layout.
//
// Thumbnail cards often use a Grid with rows (title | image). A normal child
// lands in (0,0) — the title row — and expands that row (bar between icon and
// text + card grows sideways). Same fix as icon glow: span every row/column
// and Stretch to the arranged card size. Children are positioned with
// RenderTransform so their layout slot stays tiny (Width×Height of the bar
// only, transform ignored by measure).
Controls::Grid EnsureThumbOverlayHost(Controls::Panel panel) {
    FrameworkElement hostEl =
        FindChildByName(panel.as<FrameworkElement>(), kThumbGlowElementName);
    Controls::Grid host = hostEl ? hostEl.try_as<Controls::Grid>() : nullptr;
    if (!host) {
        PCWSTR xaml = LR"(
            <Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                  Name="WhRecentFocusThumbGlow"
                  IsHitTestVisible="False"
                  HorizontalAlignment="Stretch"
                  VerticalAlignment="Stretch">
                <Rectangle Name="WhRecentFocusThumbGlowL0"
                           IsHitTestVisible="False" Visibility="Collapsed"/>
                <Rectangle Name="WhRecentFocusThumbGlowL1"
                           IsHitTestVisible="False" Visibility="Collapsed"/>
                <Border Name="WhRecentFocusThumbTitleBg"
                        IsHitTestVisible="False" Visibility="Collapsed"/>
                <Rectangle Name="WhRecentFocusThumbTitleBar"
                           IsHitTestVisible="False" Visibility="Collapsed"/>
            </Grid>
        )";
        host = Markup::XamlReader::Load(xaml).as<Controls::Grid>();
        panel.Children().Append(host);
    }

    // Critical: cover title+image rows, not just row 0 (title).
    SpanHostOverPanel(host, panel);

    host.ClearValue(FrameworkElement::WidthProperty());
    host.ClearValue(FrameworkElement::HeightProperty());
    host.ClearValue(FrameworkElement::MaxWidthProperty());
    host.ClearValue(FrameworkElement::MaxHeightProperty());
    host.MinWidth(0);
    host.MinHeight(0);
    host.Margin(Thickness{0, 0, 0, 0});
    host.HorizontalAlignment(HorizontalAlignment::Stretch);
    host.VerticalAlignment(VerticalAlignment::Stretch);
    host.IsHitTestVisible(false);
    ClearOurHostClip(host);
    BringElementToFront(panel, host);
    return host;
}

// Layout slot is width×height at (0,0); visual position is (x,y) via transform
// so measure does not include the offset (avoids expanding the title row).
void PlaceOverlayChild(FrameworkElement el,
                       double x,
                       double y,
                       double width,
                       double height) {
    if (!el) {
        return;
    }
    try {
        el.HorizontalAlignment(HorizontalAlignment::Left);
        el.VerticalAlignment(VerticalAlignment::Top);
        el.Margin(Thickness{0, 0, 0, 0});
        if (width > 0) {
            el.Width(width);
        } else {
            el.ClearValue(FrameworkElement::WidthProperty());
        }
        if (height > 0) {
            el.Height(height);
        } else {
            el.ClearValue(FrameworkElement::HeightProperty());
        }
        Media::TranslateTransform tf;
        tf.X(x);
        tf.Y(y);
        el.RenderTransform(tf);
        el.Visibility(Visibility::Visible);
    } catch (...) {
    }
}

void HideThumbOverlayChildren(Controls::Grid host) {
    if (!host) {
        return;
    }
    for (auto name : {kThumbGlowLayerNames[0], kThumbGlowLayerNames[1],
                      kThumbTitleBgName, kThumbTitleBarName}) {
        if (auto el = FindChildByName(host, name)) {
            try {
                el.Visibility(Visibility::Collapsed);
                el.ClearValue(UIElement::RenderTransformProperty());
                if (auto r = el.try_as<Shapes::Rectangle>()) {
                    r.Stroke(nullptr);
                    r.Fill(Media::SolidColorBrush{
                        winrt::Windows::UI::Color{0, 0, 0, 0}});
                }
                if (auto b = el.try_as<Controls::Border>()) {
                    b.Background(nullptr);
                }
                el.ClearValue(FrameworkElement::WidthProperty());
                el.ClearValue(FrameworkElement::HeightProperty());
                el.ClearValue(FrameworkElement::MarginProperty());
            } catch (...) {
            }
        }
    }
}

// Title bottom Y relative to `relativeTo` (prefer overlay host).
// Returns false if transform looks unusable (layout not ready / wrong ancestor).
bool GetTitleBottomRelative(FrameworkElement title,
                            FrameworkElement relativeTo,
                            double cardH,
                            double& outTop,
                            double& outBottom) {
    outTop = outBottom = 0;
    if (!title || !relativeTo) {
        return false;
    }
    try {
        try {
            title.UpdateLayout();
        } catch (...) {
        }
        double th = title.ActualHeight();
        if (!(th > 1.0)) {
            th = title.DesiredSize().Height;
        }
        if (!(th > 1.0)) {
            return false;  // not laid out yet — caller should defer
        }
        auto xform = title.TransformToVisual(relativeTo);
        auto topLeft = xform.TransformPoint(
            winrt::Windows::Foundation::Point{0.f, 0.f});
        auto bottomLeft = xform.TransformPoint(
            winrt::Windows::Foundation::Point{0.f, static_cast<float>(th)});
        // Reject nonsense (wrong ancestor / mid-layout).
        if (topLeft.Y < -5.0f || topLeft.Y > cardH * 0.55) {
            return false;
        }
        if (bottomLeft.Y < topLeft.Y + 4.0f || bottomLeft.Y > cardH * 0.60) {
            return false;
        }
        outTop = static_cast<double>(topLeft.Y);
        outBottom = static_cast<double>(bottomLeft.Y);
        return true;
    } catch (...) {
        return false;
    }
}

// Deferred re-layout for titleBar/titleBg after the flyout finishes measuring.
// At most one nested pass (avoids infinite Low-priority loops when title never
// reports a size).
std::atomic<bool> g_thumbRelayoutPending{false};
std::atomic<int> g_thumbRelayoutDepth{0};

void ScheduleThumbnailRelayout(FrameworkElement thumbView) {
    if (!thumbView || g_thumbRelayoutDepth.load() > 0) {
        return;  // already inside the deferred pass
    }
    if (g_thumbRelayoutPending.exchange(true)) {
        return;
    }
    try {
        auto dispatcher = thumbView.Dispatcher();
        if (!dispatcher) {
            g_thumbRelayoutPending = false;
            return;
        }
        winrt::weak_ref<FrameworkElement> weak =
            winrt::make_weak(thumbView);
        // Low priority so it runs after the current layout pass.
        dispatcher.RunAsync(
            winrt::Windows::UI::Core::CoreDispatcherPriority::Low,
            [weak]() {
                g_thumbRelayoutPending = false;
                try {
                    FrameworkElement el = weak.get();
                    if (!el || g_unloading.load()) {
                        return;
                    }
                    g_thumbRelayoutDepth = 1;
                    RefreshThumbnailFlyout_UIThread(el);
                    g_thumbRelayoutDepth = 0;
                } catch (...) {
                    g_thumbRelayoutDepth = 0;
                }
            });
    } catch (...) {
        g_thumbRelayoutPending = false;
    }
}

void ApplyThumbnailHighlight(FrameworkElement thumbView) {
    if (!thumbView || g_unloading.load() || !g_settings.enabled ||
        !g_settings.previewHighlightEnabled) {
        ClearThumbnailHighlight(thumbView);
        return;
    }

    try {
        // Start clean so style switches don't leave mixed chrome.
        ClearThumbnailHighlight(thumbView);

        auto panel = GetThumbnailHostPanel(thumbView);
        if (!panel) {
            return;
        }
        auto panelFe = panel.as<FrameworkElement>();

        const int intensity = RankIntensity(0);
        const double t = intensity / 100.0;
        winrt::Windows::UI::Color base = ResolveGlowBaseColor();
        auto withAlpha = [](winrt::Windows::UI::Color c, int a) {
            c.A = static_cast<uint8_t>((std::max)(0, (std::min)(255, a)));
            return c;
        };

        const double thickness = static_cast<double>(
            (std::max)(2, (std::min)(8, g_settings.glowThickness)));
        const double roundnessFrac =
            (std::max)(0, (std::min)(50, g_settings.glowRoundness)) / 100.0;
        // Preview plate / titleBg use dedicated opacity (not taskbar fill).
        const int fillOpacitySetting =
            (std::max)(0, (std::min)(100, g_settings.previewFillOpacity));
        const PreviewStyle style = g_settings.previewStyle;

        // Measure card BEFORE injecting any overlay (critical for layout).
        double cardW = 0, cardH = 0;
        try {
            cardW = panelFe.ActualWidth();
            cardH = panelFe.ActualHeight();
            if (!(cardW > 1.0)) {
                cardW = thumbView.ActualWidth();
            }
            if (!(cardH > 1.0)) {
                cardH = thumbView.ActualHeight();
            }
        } catch (...) {
        }
        const bool cardSizeFallback = !(cardW > 1.0) || !(cardH > 1.0);
        if (!(cardW > 1.0)) {
            cardW = 180.0;
        }
        if (!(cardH > 1.0)) {
            cardH = 120.0;
        }
        // 180×120 means layout not ready — bar placement will be wrong until
        // the deferred remeasure runs.
        if (cardSizeFallback) {
            ScheduleThumbnailRelayout(thumbView);
        }

        auto title = FindThumbnailTitleElement(thumbView);

        // Prefer BackgroundBorder size — more stable mid-layout.
        if (auto bb = FindThumbnailBackgroundBorder(thumbView)) {
            try {
                double bw = bb.ActualWidth();
                double bh = bb.ActualHeight();
                if (bw > 1.0) {
                    cardW = bw;
                }
                if (bh > 1.0) {
                    cardH = bh;
                }
            } catch (...) {
            }
        }

        // Plate prefers native BackgroundBorder (no layout child).
        if (style == PreviewStyle::Plate) {
            bool usedNative = false;
            if (auto borderEl = FindThumbnailBackgroundBorder(thumbView)) {
                try {
                    if (auto border = borderEl.try_as<Controls::Border>()) {
                        const int fillA = static_cast<int>(
                            fillOpacitySetting * 2.55 * (0.45 + 0.55 * t));
                        border.Background(
                            Media::SolidColorBrush{withAlpha(base, fillA)});
                        usedNative = true;
                    }
                } catch (...) {
                }
            }
            if (usedNative) {
                PCWSTR markerXaml = LR"(
                    <Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                            Name="WhRecentFocusThumbNative"
                            Width="0" Height="0" Opacity="0"
                            IsHitTestVisible="False" Visibility="Collapsed"/>
                )";
                try {
                    auto marker =
                        Markup::XamlReader::Load(markerXaml).as<UIElement>();
                    panel.Children().Append(marker);
                } catch (...) {
                }
            } else {
                Controls::Grid host = EnsureThumbOverlayHost(panel);
                HideThumbOverlayChildren(host);
                if (auto plate = FindChildByName(host, kThumbGlowLayerNames[0])
                                     .try_as<Shapes::Rectangle>()) {
                    const int fillA = static_cast<int>(
                        fillOpacitySetting * 2.55 * (0.45 + 0.55 * t));
                    plate.Fill(Media::SolidColorBrush{withAlpha(base, fillA)});
                    plate.Stroke(nullptr);
                    plate.StrokeThickness(0);
                    plate.RadiusX(cardW * roundnessFrac * 0.12);
                    plate.RadiusY(cardH * roundnessFrac * 0.12);
                    plate.Opacity(0.85 + 0.15 * t);
                    PlaceOverlayChild(plate, 0, 0, cardW, cardH);
                }
            }

            if (g_settings.glowDebugLog) {
                Wh_Log(L"Preview glow style=%s on \"%s\"",
                       PreviewStyleName(style),
                       Automation::AutomationProperties::GetName(thumbView)
                           .c_str());
            }
            return;
        }

        Controls::Grid host = EnsureThumbOverlayHost(panel);
        HideThumbOverlayChildren(host);

        if (style == PreviewStyle::Ring) {
            const double inset = 3.0;
            const double corner =
                (std::max)(12.0, (std::min)(cardW, cardH) - 2.0 * inset) *
                roundnessFrac;

            for (int i = 0; i < 2; ++i) {
                auto rect = FindChildByName(host, kThumbGlowLayerNames[i])
                                .try_as<Shapes::Rectangle>();
                if (!rect) {
                    continue;
                }
                const double step = i == 0 ? 0.0 : 3.0;
                const double layerInset = inset + step;
                const int strokeA =
                    static_cast<int>((140 + 100 * t) * (1.0 - 0.2 * i));
                const double opacity = 0.75 + 0.25 * t;
                const double th =
                    (std::max)(1.5, thickness * (1.0 - 0.15 * i));
                const double size = (std::max)(
                    8.0, (std::min)(cardW, cardH) - 2.0 * layerInset);
                rect.Stroke(Media::SolidColorBrush{withAlpha(base, strokeA)});
                rect.StrokeThickness(th);
                rect.Fill(Media::SolidColorBrush{
                    winrt::Windows::UI::Color{0, 0, 0, 0}});
                rect.RadiusX(corner + step);
                rect.RadiusY(corner + step);
                rect.Opacity(opacity);
                PlaceOverlayChild(rect, layerInset, layerInset, size, size);
            }
        } else if (style == PreviewStyle::TitleBg) {
            double top = 4.0;
            double stripH = 28.0;
            double titleTop = 0, titleBottom = 0;
            bool laidOut = GetTitleBottomRelative(title, host, cardH, titleTop,
                                                  titleBottom);
            if (laidOut) {
                top = (std::max)(2.0, titleTop - 3.0);
                stripH = (std::max)(20.0, titleBottom - top + 4.0);
            } else {
                // Layout not ready — default strip + one deferred remeasure.
                ScheduleThumbnailRelayout(thumbView);
            }
            const double hPad = 8.0;
            const double stripW = (std::max)(24.0, cardW - 2.0 * hPad);

            auto chip =
                FindChildByName(host, kThumbTitleBgName).try_as<Controls::Border>();
            if (chip) {
                // Soft wash above title glyphs — map previewFillOpacity (0–100)
                // into a readable alpha band (~12–90) so low settings stay soft.
                const int chipA = static_cast<int>((std::max)(
                    12, (std::min)(90, static_cast<int>(
                                           12 + fillOpacitySetting * 0.78 *
                                                   (0.55 + 0.45 * t)))));
                chip.Background(Media::SolidColorBrush{withAlpha(base, chipA)});
                chip.CornerRadius(CornerRadius{stripH * 0.35});
                chip.Opacity(1.0);
                PlaceOverlayChild(chip, hPad, top, stripW, stripH);
            }
        } else {  // TitleBar — underline just under the title glyphs
            // Prefer transform relative to host (same coordinate space as
            // PlaceOverlayChild). Small gap under baseline — enough to avoid
            // strikethrough, not so much that the bar hugs the thumbnail image.
            constexpr double kGapBelowTitle = 2.0;
            double top = 30.0;  // fallback under a ~28px header
            double titleTop = 0, titleBottom = 0;
            bool laidOut = GetTitleBottomRelative(title, host, cardH, titleTop,
                                                  titleBottom);
            if (laidOut) {
                top = titleBottom + kGapBelowTitle;
            } else {
                ScheduleThumbnailRelayout(thumbView);
            }
            top = (std::max)(18.0, (std::min)(top, cardH * 0.38));

            const double barH =
                (std::max)(2.0, (std::min)(5.0, thickness + 0.5));
            const double hPad = 10.0;
            const double barW = (std::max)(24.0, cardW - 2.0 * hPad);

            auto bar = FindChildByName(host, kThumbTitleBarName)
                           .try_as<Shapes::Rectangle>();
            if (bar) {
                const int fillA = static_cast<int>(180 + 75 * t);
                bar.Fill(Media::SolidColorBrush{withAlpha(base, fillA)});
                bar.Stroke(nullptr);
                bar.StrokeThickness(0);
                bar.RadiusX(barH * 0.5);
                bar.RadiusY(barH * 0.5);
                bar.Opacity(0.92 + 0.08 * t);
                PlaceOverlayChild(bar, hPad, top, barW, barH);
            }
        }

        if (g_settings.glowDebugLog) {
            Wh_Log(L"Preview glow style=%s card=%.0fx%.0f on \"%s\"",
                   PreviewStyleName(style), cardW, cardH,
                   Automation::AutomationProperties::GetName(thumbView)
                       .c_str());
        }
    } catch (...) {
        HRESULT hr = winrt::to_hresult();
        Wh_Log(L"ApplyThumbnailHighlight error %08X", hr);
    }
}

void RefreshThumbnailFlyout_UIThread(FrameworkElement anyThumb) {
    if (!anyThumb) {
        return;
    }
    TrackThumbView_UIThread(anyThumb);

    auto allViews = CollectSiblingThumbnailViews(anyThumb);
    if (!g_settings.enabled || !g_settings.previewHighlightEnabled ||
        g_unloading.load()) {
        for (auto& s : allViews) {
            ClearThumbnailHighlight(s);
        }
        return;
    }

    // Snap-group cards sit in the same ItemsRepeater as the windows
    // (UWPSpy: PositionInSet 1/4 = "Group | Lister - [file] and 1 other
    // window"). Never glow those; they are not a window HWND.
    std::vector<FrameworkElement> siblings;
    siblings.reserve(allViews.size());
    for (auto& v : allViews) {
        if (IsSnapGroupThumbnailView(v)) {
            ClearThumbnailHighlight(v);
        } else {
            siblings.push_back(v);
        }
    }

    // Product rule: only multi-window flyouts (group card does not count).
    if (siblings.size() <= 1) {
        for (auto& s : siblings) {
            ClearThumbnailHighlight(s);
        }
        return;
    }

    enum class ResolveHow : int { None = 0, TaskItem, GroupOrder, Title };
    struct Scored {
        FrameworkElement view{nullptr};
        HWND hwnd = nullptr;
        ULONGLONG tick = 0;
        ResolveHow how = ResolveHow::None;
    };
    std::vector<Scored> scored(siblings.size());
    std::unordered_set<HWND> usedHwnds;
    void* sharedGroup = nullptr;

    auto recent = CopyRecentWindowsForPreview();

    auto tickFor = [](HWND hwnd) -> ULONGLONG {
        ULONGLONG tick = 0;
        if (!hwnd) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(g_stateMutex);
        IsWindowRecentForPreviewLocked(hwnd, &tick);
        return tick;
    };
    auto seqFor = [](HWND hwnd) -> ULONGLONG {
        if (!hwnd) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto it = g_windowFocusMap.find(hwnd);
        if (it == g_windowFocusMap.end()) {
            return 0;
        }
        return it->second.confirmSeq;
    };

    // Pass 1: strong identity (TaskItemThumbnail model → HWND). Never use bare
    // title here — two Calibre windows with the same file name would both bind
    // to the same HWND.
    for (size_t i = 0; i < siblings.size(); ++i) {
        TrackThumbView_UIThread(siblings[i]);
        scored[i].view = siblings[i];
        void* group = nullptr;
        HWND hwnd = ResolveHwndForThumbnailView(siblings[i], &group);
        if (group && !sharedGroup) {
            sharedGroup = group;
        }
        if (hwnd && IsWindow(hwnd) && !usedHwnds.count(hwnd)) {
            scored[i].hwnd = hwnd;
            scored[i].tick = tickFor(hwnd);
            scored[i].how = ResolveHow::TaskItem;
            usedHwnds.insert(hwnd);
        }
    }

    // Pass 1b: fill from TaskItem maps by group / construction order.
    // DataContext often does NOT match our ctor IInspectable (logs showed
    // "2 taskitem maps" but how=title/none) — so discover the group even
    // when pass 1 resolved nothing.
    if (usedHwnds.size() < siblings.size()) {
        void* group = sharedGroup;
        if (!group) {
            std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
            for (const auto& item : g_thumbnailTaskItemMapping) {
                HWND h = HwndFromMappingEntry(item);
                if (h && usedHwnds.count(h) && item.taskGroup) {
                    group = item.taskGroup;
                    break;
                }
            }
        }
        if (!group) {
            group = FindTaskGroupForSiblingCount(siblings.size());
        }

        std::vector<HWND> groupHwnds = HwndsForTaskGroupInOrder(group);
        // Fallback: last N mapped HWNDs (just-created flyout models).
        if (groupHwnds.size() != siblings.size()) {
            auto last = LastMappedHwnds(siblings.size());
            if (last.size() == siblings.size()) {
                groupHwnds = std::move(last);
            }
        }
        // Do NOT EnumWindows-expand here and then assign by index.
        // Z-order (focused window first) is not left-to-right flyout order —
        // that pinned the recent Lister HWND onto card 0 every time.

        if (groupHwnds.size() == siblings.size()) {
            // Prefer map order for ALL siblings when counts match — more
            // reliable than leaving a half-filled title match in place.
            // Only overwrite title-resolved slots when they have no taskitem id.
            bool anyTaskItem = false;
            for (const auto& s : scored) {
                if (s.how == ResolveHow::TaskItem) {
                    anyTaskItem = true;
                    break;
                }
            }
            if (!anyTaskItem) {
                usedHwnds.clear();
                for (size_t i = 0; i < siblings.size(); ++i) {
                    HWND h = groupHwnds[i];
                    scored[i].hwnd = h;
                    scored[i].tick = tickFor(h);
                    scored[i].how = ResolveHow::GroupOrder;
                    if (h) {
                        usedHwnds.insert(h);
                    }
                }
            } else {
                for (size_t i = 0; i < siblings.size(); ++i) {
                    if (scored[i].hwnd) {
                        continue;
                    }
                    HWND h = groupHwnds[i];
                    if (!h || usedHwnds.count(h)) {
                        continue;
                    }
                    scored[i].hwnd = h;
                    scored[i].tick = tickFor(h);
                    scored[i].how = ResolveHow::GroupOrder;
                    usedHwnds.insert(h);
                }
            }
        } else if (!groupHwnds.empty()) {
            size_t gi = 0;
            for (size_t i = 0; i < siblings.size() && gi < groupHwnds.size();
                 ++i) {
                if (scored[i].hwnd) {
                    continue;
                }
                while (gi < groupHwnds.size() &&
                       usedHwnds.count(groupHwnds[gi])) {
                    ++gi;
                }
                if (gi >= groupHwnds.size()) {
                    break;
                }
                HWND h = groupHwnds[gi++];
                scored[i].hwnd = h;
                scored[i].tick = tickFor(h);
                scored[i].how = ResolveHow::GroupOrder;
                usedHwnds.insert(h);
            }
        }
    }

    // Title pool: recency map plus live same-class windows (TLister a/b/c).
    // Enumerated HWNDs are candidates only — never assigned by index.
    std::vector<HWND> titleSeeds;
    for (const auto& r : recent) {
        if (r.hwnd) {
            titleSeeds.push_back(r.hwnd);
        }
    }
    for (const auto& s : scored) {
        if (s.hwnd) {
            titleSeeds.push_back(s.hwnd);
        }
    }
    std::vector<WindowFocusInfo> titlePool = recent;
    {
        std::unordered_set<HWND> have;
        for (const auto& r : titlePool) {
            if (r.hwnd) {
                have.insert(r.hwnd);
            }
        }
        for (HWND h : ExpandSameClassWindows(titleSeeds)) {
            if (!h || have.count(h)) {
                continue;
            }
            WindowFocusInfo extra;
            extra.hwnd = h;
            extra.windowTitle = GetWindowTitle(h);
            extra.lastConfirmedTick = tickFor(h);
            titlePool.push_back(std::move(extra));
            have.insert(h);
        }
    }

    auto titleKeyOf = [](FrameworkElement view) -> std::wstring {
        std::wstring t = GetThumbnailMatchTitle(view);
        std::wstring path = ToUpper(ExtractBracketedPath(t));
        if (!path.empty()) {
            return path;
        }
        return AlnumUpper(t);
    };

    bool titlesDistinct = siblings.size() >= 2;
    {
        std::unordered_set<std::wstring> seen;
        for (auto& view : siblings) {
            std::wstring key = titleKeyOf(view);
            if (key.empty() || !seen.insert(key).second) {
                titlesDistinct = false;
                break;
            }
        }
    }

    // Unique titles (Lister - [c:\tmp\a.txt] vs b.txt vs c.txt) beat
    // group-order. Index order from maps/EnumWindows is not flyout order.
    if (titlesDistinct) {
        for (auto& s : scored) {
            if (s.how == ResolveHow::GroupOrder) {
                if (s.hwnd) {
                    usedHwnds.erase(s.hwnd);
                }
                s.hwnd = nullptr;
                s.tick = 0;
                s.how = ResolveHow::None;
            }
        }
    }

    // Pass 2: title fallback with unique HWND assignment only.
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (scored[i].hwnd) {
            continue;
        }
        std::wstring autoName = GetThumbnailMatchTitle(siblings[i]);
        HWND h = MatchTitleToUnusedRecent(autoName, titlePool, usedHwnds);
        if (h) {
            scored[i].hwnd = h;
            scored[i].tick = tickFor(h);
            if (scored[i].tick == 0) {
                // Live window not yet in recency map — still use its title
                // tick from the pool entry if we recorded one.
                for (const auto& info : titlePool) {
                    if (info.hwnd == h && info.lastConfirmedTick > 0) {
                        scored[i].tick = info.lastConfirmedTick;
                        break;
                    }
                }
            }
            scored[i].how = ResolveHow::Title;
            usedHwnds.insert(h);
        }
    }

    // Pass 3: ITaskItem HWND and EVENT_SYSTEM_FOREGROUND HWND can differ
    // (owned Lister windows, tab proxies). Copy recency from a same-PID
    // recent window when the card's HWND itself has tick 0.
    for (size_t i = 0; i < scored.size(); ++i) {
        if (!scored[i].hwnd || scored[i].tick > 0) {
            continue;
        }
        DWORD cardPid = 0;
        GetWindowThreadProcessId(scored[i].hwnd, &cardPid);
        if (!cardPid) {
            continue;
        }

        std::wstring autoName = GetThumbnailMatchTitle(scored[i].view);

        int bestScore = 0;
        ULONGLONG bestTick = 0;
        for (const auto& info : recent) {
            if (!info.hwnd) {
                continue;
            }
            DWORD rpid = 0;
            GetWindowThreadProcessId(info.hwnd, &rpid);
            if (rpid != cardPid) {
                continue;
            }
            int s = 0;
            if (!autoName.empty()) {
                s = ScoreTitleToAutomationName(info.windowTitle, autoName);
                if (s < 70) {
                    s = (std::max)(
                        s, ScoreTitleToAutomationName(GetWindowTitle(info.hwnd),
                                                      autoName));
                }
            }
            if (s > bestScore) {
                bestScore = s;
                bestTick = info.lastConfirmedTick;
            }
        }
        // Require a unique title/path (96+). Score 85 prefix would copy the
        // latest Lister tick onto a.txt AND c.txt, then card 0 always wins.
        if (bestScore >= 96) {
            scored[i].tick = bestTick;
        }
    }

    // Glow the sibling whose HWND is the most recently focused (max tick > 0).
    // On a tick tie (GetTickCount64 granularity, or two confirms in one ms)
    // prefer the actual foreground window, not sibling[0].
    HWND foreground = GetForegroundWindow();
    size_t bestIdx = SIZE_MAX;
    ULONGLONG bestTick = 0;
    ULONGLONG bestSeq = 0;
    for (size_t i = 0; i < scored.size(); ++i) {
        if (!scored[i].hwnd || scored[i].tick == 0) {
            continue;
        }
        const ULONGLONG seq = seqFor(scored[i].hwnd);
        const bool betterTick = scored[i].tick > bestTick;
        const bool betterSeq =
            scored[i].tick == bestTick && seq > bestSeq;
        const bool tieFg = scored[i].tick == bestTick && seq == bestSeq &&
                           scored[i].hwnd == foreground &&
                           (bestIdx == SIZE_MAX ||
                            scored[bestIdx].hwnd != foreground);
        if (betterTick || betterSeq || tieFg) {
            bestTick = scored[i].tick;
            bestSeq = seq;
            bestIdx = i;
        }
    }

    if (g_settings.glowDebugLog) {
        size_t mapCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
            mapCount = g_thumbnailTaskItemMapping.size();
        }
        Wh_Log(L"Preview resolve: %zu siblings, %zu taskitem maps, "
               L"bestIdx=%zu bestTick=%llu",
               siblings.size(), mapCount, bestIdx,
               static_cast<unsigned long long>(bestTick));
        for (size_t i = 0; i < scored.size(); ++i) {
            PCWSTR how = L"none";
            switch (scored[i].how) {
                case ResolveHow::TaskItem:
                    how = L"taskitem";
                    break;
                case ResolveHow::GroupOrder:
                    how = L"group-order";
                    break;
                case ResolveHow::Title:
                    how = L"title";
                    break;
                default:
                    break;
            }
            std::wstring name;
            try {
                name = Automation::AutomationProperties::GetName(scored[i].view)
                           .c_str();
            } catch (...) {
            }
            Wh_Log(L"  sibling[%zu]: hwnd=%p tick=%llu how=%s%s name=\"%s\"",
                   i, scored[i].hwnd,
                   static_cast<unsigned long long>(scored[i].tick), how,
                   (i == bestIdx) ? L" [GLOW]" : L"", name.c_str());
        }
    }

    for (size_t i = 0; i < scored.size(); ++i) {
        if (i == bestIdx) {
            ApplyThumbnailHighlight(scored[i].view);
        } else {
            ClearThumbnailHighlight(scored[i].view);
        }
    }
}

void ClearAllThumbnailHighlights_UIThread() {
    std::vector<winrt::weak_ref<FrameworkElement>> thumbs;
    {
        std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
        thumbs = g_trackedThumbViews;
    }
    for (auto& weak : thumbs) {
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
            if (dispatcher && !dispatcher.HasThreadAccess()) {
                continue;
            }
        } catch (...) {
        }
        ClearThumbnailHighlight(el);
    }
}

void RequestApplyPreviewVisuals() {
    if (g_unloading.load()) {
        return;
    }
    // Prefer dispatching via existing XAML anchor.
    if (!RunOnUiThread([]() {
            std::vector<winrt::weak_ref<FrameworkElement>> thumbs;
            {
                std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
                thumbs = g_trackedThumbViews;
            }
            // Refresh unique flyouts by touching each live view (idempotent).
            for (auto& weak : thumbs) {
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
                    if (dispatcher && !dispatcher.HasThreadAccess()) {
                        continue;
                    }
                } catch (...) {
                }
                RefreshThumbnailFlyout_UIThread(el);
            }
        })) {
        if (g_hookThreadHwnd) {
            PostMessage(g_hookThreadHwnd, WM_APP_REQUEST_PREVIEW_APPLY, 0, 0);
        }
    }
}

// Expected at startup before any TaskListButton is seen — log once, not per call.
std::atomic<bool> g_loggedNoDispatcherAnchor{false};

bool RunOnUiThread(const winrt::Windows::UI::Core::DispatchedHandler& handler) {
    std::vector<winrt::Windows::UI::Core::CoreDispatcher> dispatchers;
    auto addDispatcher = [&](FrameworkElement el) {
        if (!el) {
            return;
        }
        try {
            auto dispatcher = el.Dispatcher();
            if (!dispatcher) {
                return;
            }
            for (const auto& existing : dispatchers) {
                if (existing == dispatcher) {
                    return;
                }
            }
            dispatchers.push_back(dispatcher);
        } catch (...) {
        }
    };

    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        try {
            addDispatcher(g_dispatcherAnchor.get());
        } catch (...) {
        }
        for (auto& weak : g_trackedButtons) {
            try {
                addDispatcher(weak.get());
            } catch (...) {
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
        for (auto& weak : g_trackedThumbViews) {
            try {
                addDispatcher(weak.get());
            } catch (...) {
            }
        }
    }

    if (dispatchers.empty()) {
        // App + preview apply both hit this before the first button hook.
        if (!g_loggedNoDispatcherAnchor.exchange(true)) {
            Wh_Log(L"RunOnUiThread: no dispatcher anchor yet (no buttons seen) "
                   L"— will apply on first TaskListButton");
        }
        return false;
    }

    bool any = false;
    for (auto& dispatcher : dispatchers) {
        try {
            if (dispatcher.HasThreadAccess()) {
                handler();
                any = true;
                continue;
            }
            if (dispatcher.TryRunAsync(
                    winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                    handler)) {
                any = true;
            }
        } catch (...) {
            HRESULT hr = winrt::to_hresult();
            Wh_Log(L"RunOnUiThread error %08X", hr);
        }
    }
    return any;
}

void RequestApplyVisuals() {
    // Prefer UI-thread apply; also keep logging from caller context.
    if (!RunOnUiThread([]() { ApplyAllHighlights_UIThread(); })) {
        // Buttons not tracked yet — will apply on next UpdateVisualStates.
        // Avoid rank dump spam at startup (preview + app both request apply).
        if (g_settings.glowDebugLog) {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            Wh_Log(L"Ranks ready (%zu) — waiting for TaskListButton hooks",
                   g_rankedApps.size());
            for (size_t i = 0; i < g_rankedApps.size(); i++) {
                Wh_Log(L"  Rank %zu: %s", i + 1,
                       g_rankedApps[i].displayName.c_str());
            }
        }
    }
}

void ApplyVisualHighlights() {
    // Legacy entry used by timers / focus path.
    RequestApplyVisuals();
}

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

// Apply current rank styling to one button. Prefer stable matching so Alt-Tab
// visual-state flicker does not clear glows (IsRunning can briefly be false).
void RefreshButtonHighlight(FrameworkElement button) {
    if (!button || g_unloading.load()) {
        return;
    }

    // After decay/sleep: strip any leftover WhRecentFocusGlow even if this
    // button is no longer in our weak-ref list of "previously ranked" icons.
    if (g_pendingOverlaySweep.load()) {
        ClearButtonHighlight(button);
    }

    std::vector<AppFocusInfo> ranks;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        ranks = g_rankedApps;
    }

    if (!g_settings.enabled || ranks.empty()) {
        ClearButtonHighlight(button);
        return;
    }

    // Only learn rank-1 cache when the active button actually belongs to it.
    if (TaskListButton_IsRunning(button) && IsVisualStateActive(button)) {
        StoreAutomationNameIfFits(ranks[0], GetButtonAutomationName(button));
    }

    int rank = FindRankForButton(button, ranks, /*requireRunning=*/true);
    if (rank <= 0) {
        // Keep highlight during transient IsRunning glitches.
        rank = FindRankForButton(button, ranks, /*requireRunning=*/false);
    }

    if (rank > 0) {
        ApplyButtonHighlight(button, rank);
    } else {
        ClearButtonHighlight(button);
    }
}

// When any button updates, repaint *all* ranked buttons — inactive ones may
// not get another UpdateVisualStates after Windows cleared sibling visuals.
// Per UI thread so primary and secondary taskbars don't throttle each other.
thread_local ULONGLONG g_lastFullRefreshTick = 0;

void ScheduleRefreshAllHighlights(FrameworkElement dispatcherAnchor) {
    if (!dispatcherAnchor) {
        return;
    }
    try {
        winrt::weak_ref<FrameworkElement> weak =
            winrt::make_weak(dispatcherAnchor);
        dispatcherAnchor.Dispatcher().TryRunAsync(
            winrt::Windows::UI::Core::CoreDispatcherPriority::Low,
            [weak]() {
                try {
                    const ULONGLONG now = GetTickCount64();
                    // Throttle: at most ~20 Hz full refresh *on this dispatcher*.
                    if (now - g_lastFullRefreshTick < 50 &&
                        g_lastFullRefreshTick != 0) {
                        return;
                    }
                    g_lastFullRefreshTick = now;
                    if (!weak.get()) {
                        return;
                    }
                    ApplyAllHighlights_UIThread();
                } catch (...) {
                }
            });
    } catch (...) {
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
    RefreshButtonHighlight(button);
    ScheduleRefreshAllHighlights(button);
}

// XAML thumbnail view template — apply preview glow when flyout builds items.
using TaskItemThumbnailView_OnApplyTemplate_t = void(WINAPI*)(void* pThis);
TaskItemThumbnailView_OnApplyTemplate_t
    TaskItemThumbnailView_OnApplyTemplate_Original;
void WINAPI TaskItemThumbnailView_OnApplyTemplate_Hook(void* pThis) {
    if (TaskItemThumbnailView_OnApplyTemplate_Original) {
        TaskItemThumbnailView_OnApplyTemplate_Original(pThis);
    }
    if (g_unloading.load() || !g_settings.enabled ||
        !g_settings.previewHighlightEnabled) {
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
            RefreshThumbnailFlyout_UIThread(element);
        }
    } catch (...) {
        HRESULT hr = winrt::to_hresult();
        Wh_Log(L"TaskItemThumbnailView_OnApplyTemplate error %08X", hr);
    }
}

// Option C: re-resolve button → path on click.
using TaskListButton_OnPointerPressed_t = int(WINAPI*)(void* pThis, void* pArgs);
TaskListButton_OnPointerPressed_t TaskListButton_OnPointerPressed_Original;
int WINAPI TaskListButton_OnPointerPressed_Hook(void* pThis, void* pArgs) {
    int ret = TaskListButton_OnPointerPressed_Original
                  ? TaskListButton_OnPointerPressed_Original(pThis, pArgs)
                  : 0;
    if (g_unloading.load() || !g_taskbandResolveReady.load()) {
        return ret;
    }
    try {
        UIElement element = nullptr;
        ((IUnknown*)pThis)
            ->QueryInterface(winrt::guid_of<UIElement>(),
                             winrt::put_abi(element));
        if (!element) {
            return ret;
        }
        if (winrt::get_class_name(element) != L"Taskbar.TaskListButton") {
            return ret;
        }
        auto button = element.try_as<FrameworkElement>();
        if (button) {
            EnsureButtonPathCached(button, /*force=*/true);
        }
    } catch (...) {
    }
    return ret;
}

bool HookTaskbarViewDllSymbols(HMODULE module) {
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
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskListButton,struct winrt::Windows::UI::Xaml::Controls::IControlOverrides>::OnPointerPressed(void *))"},
            &TaskListButton_OnPointerPressed_Original,
            TaskListButton_OnPointerPressed_Hook,
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
    };

    if (!HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks))) {
        Wh_Log(L"HookSymbols failed for Taskbar.View.dll");
        return false;
    }

    if (TaskItemThumbnailView_OnApplyTemplate_Original) {
        Wh_Log(L"Hooked Taskbar.View.dll symbols (identity + paint + thumbnails)");
    } else {
        Wh_Log(L"Hooked Taskbar.View.dll symbols (identity + paint; "
               L"thumbnail OnApplyTemplate unavailable)");
    }
    return true;
}

// Capture TaskItemThumbnail model → ITaskItem for HWND resolve (optional).
using TaskItemThumbnail_TaskItemThumbnail_t = void*(WINAPI*)(void* param1,
                                                             void* param2,
                                                             void* taskGroup,
                                                             void* taskItem,
                                                             void* taskListUi,
                                                             void* param6,
                                                             void* param7,
                                                             bool param8);
TaskItemThumbnail_TaskItemThumbnail_t
    TaskItemThumbnail_TaskItemThumbnail_Original;
void* WINAPI TaskItemThumbnail_TaskItemThumbnail_Hook(void* param1,
                                                      void* param2,
                                                      void* taskGroup,
                                                      void* taskItem,
                                                      void* taskListUi,
                                                      void* param6,
                                                      void* param7,
                                                      bool param8) {
    void* result = TaskItemThumbnail_TaskItemThumbnail_Original(
        param1, param2, taskGroup, taskItem, taskListUi, param6, param7,
        param8);
    if (result) {
        try {
            winrt::Windows::Foundation::IInspectable obj = nullptr;
            ((IUnknown*)result + 2)
                ->QueryInterface(
                    winrt::guid_of<winrt::Windows::Foundation::IInspectable>(),
                    winrt::put_abi(obj));
            AddThumbnailTaskItemMapping(obj, taskGroup, taskItem);
        } catch (...) {
        }
    }
    return result;
}

using TaskItemThumbnail_TaskItemThumbnail_2_t =
    void*(WINAPI*)(void* param1,
                   void* param2,
                   void* taskGroup,
                   void* taskItem,
                   void* taskListUi,
                   void* param6,
                   bool param7);
TaskItemThumbnail_TaskItemThumbnail_2_t
    TaskItemThumbnail_TaskItemThumbnail_2_Original;
void* WINAPI TaskItemThumbnail_TaskItemThumbnail_2_Hook(void* param1,
                                                        void* param2,
                                                        void* taskGroup,
                                                        void* taskItem,
                                                        void* taskListUi,
                                                        void* param6,
                                                        bool param7) {
    void* result = TaskItemThumbnail_TaskItemThumbnail_2_Original(
        param1, param2, taskGroup, taskItem, taskListUi, param6, param7);
    if (result) {
        try {
            winrt::Windows::Foundation::IInspectable obj = nullptr;
            ((IUnknown*)result + 2)
                ->QueryInterface(
                    winrt::guid_of<winrt::Windows::Foundation::IInspectable>(),
                    winrt::put_abi(obj));
            AddThumbnailTaskItemMapping(obj, taskGroup, taskItem);
        } catch (...) {
        }
    }
    return result;
}

bool HookTaskbarDllSymbols() {
    HMODULE module =
        LoadLibraryEx(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        Wh_Log(L"Could not load taskbar.dll — path cache unavailable");
        return false;
    }

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {
            {LR"(public: virtual int __cdecl CTaskGroup::GetNumItems(void))"},
            &CTaskGroup_GetNumItems,
        },
        {
            {LR"(public: virtual struct HWND__ * __cdecl CWindowTaskItem::GetWindow(void))"},
            &CWindowTaskItem_GetWindow,
        },
        {
            {LR"(public: virtual struct HWND__ * __cdecl CImmersiveTaskItem::GetAppWindow(void))"},
            &CImmersiveTaskItem_GetAppWindow,
        },
        {
            {LR"(const CImmersiveTaskItem::`vftable')"},
            &CImmersiveTaskItem_vftable,
        },
        {
            {LR"(const CImmersiveTaskItem::`vftable'{for `ITaskItem'})"},
            &CImmersiveTaskItem_vftable_ITaskItem,
        },
        {
            {LR"(const CWindowTaskItem::`vftable')"},
            &CWindowTaskItem_vftable,
            nullptr,
            true,
        },
        {
            {LR"(const CWindowTaskItem::`vftable'{for `ITaskItem'}")},
            &CWindowTaskItem_vftable_ITaskItem,
            nullptr,
            true,
        },
        {
            {LR"(public: virtual long __cdecl CTaskListWnd::HandleClick(struct ITaskGroup *,struct ITaskItem *,struct winrt::Windows::System::LauncherOptions const &))"},
            &CTaskListWnd_HandleClick_Original,
            CTaskListWnd_HandleClick_Hook,
        },
        {
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::WindowsUdk::UI::Shell::implementation::TaskItem,struct winrt::WindowsUdk::UI::Shell::ITaskItem>::ReportClicked(void *))"},
            &TaskItem_ReportClicked_Original,
        },
        {
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::WindowsUdk::UI::Shell::implementation::TaskGroup,struct winrt::WindowsUdk::UI::Shell::ITaskGroup>::ReportClicked(void *))"},
            &TaskGroup_ReportClicked_Original,
        },
        {
            // Optional: older XAML thumbnail model ctor.
            {LR"(public: __cdecl winrt::WindowsUdk::UI::Shell::implementation::TaskItemThumbnail::TaskItemThumbnail(struct winrt::WindowsUdk::UI::Shell::TaskItem const &,struct ITaskGroup *,struct ITaskItem *,struct ITaskListUI *,struct IWICImagingFactory *,struct ITaskListAcc *,bool))"},
            &TaskItemThumbnail_TaskItemThumbnail_Original,
            TaskItemThumbnail_TaskItemThumbnail_Hook,
            true,
        },
        {
            // Optional: newer ctor (e.g. 10.0.26100.8328+).
            {LR"(public: __cdecl winrt::WindowsUdk::UI::Shell::implementation::TaskItemThumbnail::TaskItemThumbnail(struct winrt::WindowsUdk::UI::Shell::TaskItem const &,struct ITaskGroup *,struct ITaskItem *,struct ITaskListUI *,struct IWICImagingFactory *,bool))"},
            &TaskItemThumbnail_TaskItemThumbnail_2_Original,
            TaskItemThumbnail_TaskItemThumbnail_2_Hook,
            true,
        },
    };

    if (!HookSymbols(module, taskbarDllHooks, ARRAYSIZE(taskbarDllHooks))) {
        Wh_Log(L"HookSymbols failed for taskbar.dll");
        return false;
    }

    g_taskbarDllHooked = true;
    g_taskbandResolveReady = true;
    g_previewHooksReady =
        TaskItemThumbnail_TaskItemThumbnail_Original != nullptr ||
        TaskItemThumbnail_TaskItemThumbnail_2_Original != nullptr;
    if (g_previewHooksReady) {
        Wh_Log(L"Hooked taskbar.dll identity + thumbnail model symbols");
    } else {
        Wh_Log(L"Hooked taskbar.dll identity symbols (preview HWND mapping "
               L"unavailable — title fallback only)");
    }
    return true;
}

HMODULE GetTaskbarViewModuleHandle() {
    HMODULE module = GetModuleHandle(L"Taskbar.View.dll");
    if (!module) {
        module = GetModuleHandle(L"ExplorerExtensions.dll");
    }
    return module;
}

void HandleLoadedModuleIfTaskbarView(HMODULE module, LPCWSTR lpLibFileName) {
    if (!g_taskbarViewDllLoaded && GetTaskbarViewModuleHandle() == module &&
        !g_taskbarViewDllLoaded.exchange(true)) {
        Wh_Log(L"Loaded %s", lpLibFileName);
        if (HookTaskbarViewDllSymbols(module)) {
            Wh_ApplyHookOperations();
        }
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

// ---------------------------------------------------------------------------
// Focus promotion (min focus time)
// ---------------------------------------------------------------------------

void CancelMinFocusTimer() {
    if (g_hookThreadHwnd) {
        KillTimer(g_hookThreadHwnd, kMinFocusTimerId);
    }
}

void CancelPreviewMinFocusTimer() {
    if (g_hookThreadHwnd) {
        KillTimer(g_hookThreadHwnd, kPreviewMinFocusTimerId);
    }
}

void OnPreviewMinFocusTimerElapsed() {
    if (!g_settings.enabled || !g_settings.previewHighlightEnabled ||
        g_unloading.load()) {
        return;
    }

    PendingFocus pending;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_pendingFocus.valid) {
            return;
        }
        pending = g_pendingFocus;
    }

    HWND foreground = GetForegroundWindow();
    HWND confirmHwnd = pending.hwnd;
    std::wstring title = pending.windowTitle;

    if (!foreground || foreground != pending.hwnd) {
        DWORD fgPid = 0;
        if (foreground) {
            GetWindowThreadProcessId(foreground, &fgPid);
        }
        // Same process: accept new top-level window of the app.
        if (foreground && fgPid == pending.processId) {
            confirmHwnd = foreground;
            std::wstring t = GetWindowTitle(foreground);
            if (!t.empty()) {
                title = std::move(t);
            }
        } else if (!foreground || fgPid != pending.processId) {
            // Focus left the app before preview confirm — drop only preview;
            // app timer may still be pending separately.
            return;
        }
    }

    if (!confirmHwnd || !IsWindow(confirmHwnd)) {
        return;
    }

    const ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        WindowFocusInfo& info = g_windowFocusMap[confirmHwnd];
        info.hwnd = confirmHwnd;
        info.processKey = PathFromAppKey(pending.key);
        if (info.processKey.empty()) {
            info.processKey = ToUpper(GetProcessImagePath(pending.processId));
        }
        if (!title.empty()) {
            info.windowTitle = title;
        }
        info.lastConfirmedTick = now;
        info.confirmSeq = g_windowConfirmSeq.fetch_add(1) + 1;
        PruneWindowFocusMapLocked();
        Wh_Log(L"Preview focus confirmed: hwnd=%p %s title=\"%s\" (map=%zu)",
               confirmHwnd, pending.displayName.c_str(), title.c_str(),
               g_windowFocusMap.size());
    }

    RequestApplyPreviewVisuals();
}

void OnMinFocusTimerElapsed() {
    PendingFocus pending;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_pendingFocus.valid) {
            return;
        }
        pending = g_pendingFocus;
    }

    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground != pending.hwnd) {
        DWORD fgPid = 0;
        if (foreground) {
            GetWindowThreadProcessId(foreground, &fgPid);
        }
        if (!foreground || fgPid != pending.processId) {
            Wh_Log(L"Min-focus timer: focus left %s before confirmation",
                   pending.displayName.c_str());
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (g_pendingFocus.hwnd == pending.hwnd) {
                g_pendingFocus = {};
            }
            return;
        }
        pending.hwnd = foreground;
        // Refresh title if focus moved to another window of same PID.
        std::wstring t = GetWindowTitle(foreground);
        if (!t.empty()) {
            pending.windowTitle = std::move(t);
        }
    }

    const ULONGLONG now = GetTickCount64();
    bool alsoConfirmPreviewWindow = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        AppFocusInfo& info = g_appFocusMap[pending.key];
        info.key = pending.key;
        info.displayName = pending.displayName;
        if (!pending.windowTitle.empty()) {
            info.lastWindowTitle = pending.windowTitle;
        }
        info.lastHwnd = pending.hwnd;
        info.classUpper = ToUpper(GetWindowClassName(pending.hwnd));
        info.appIdUpper = ToUpper(GetWindowAppUserModelId(pending.hwnd));
        info.lastConfirmedFocusTick = now;

        // If preview min-focus is not longer than app min-focus, promote the
        // window here too (covers minFocus=0 / already-tracked immediate path
        // without waiting for a separate preview timer). When preview min is
        // longer, leave pending so the preview timer can still fire.
        alsoConfirmPreviewWindow =
            g_settings.previewHighlightEnabled &&
            g_settings.previewMinFocusSeconds <=
                (std::max)(0, g_settings.minFocusSeconds);

        if (alsoConfirmPreviewWindow && pending.hwnd &&
            IsWindow(pending.hwnd)) {
            WindowFocusInfo& winfo = g_windowFocusMap[pending.hwnd];
            winfo.hwnd = pending.hwnd;
            winfo.processKey = PathFromAppKey(pending.key);
            if (winfo.processKey.empty()) {
                winfo.processKey =
                    ToUpper(GetProcessImagePath(pending.processId));
            }
            if (!pending.windowTitle.empty()) {
                winfo.windowTitle = pending.windowTitle;
            }
            winfo.lastConfirmedTick = now;
            winfo.confirmSeq = g_windowConfirmSeq.fetch_add(1) + 1;
        }

        // Keep pending alive while a longer preview timer may still need it.
        const bool previewTimerMayRemain =
            g_settings.previewHighlightEnabled &&
            g_settings.previewMinFocusSeconds >
                (std::max)(0, g_settings.minFocusSeconds);
        if (!previewTimerMayRemain &&
            (g_pendingFocus.hwnd == pending.hwnd ||
             g_pendingFocus.processId == pending.processId)) {
            g_pendingFocus = {};
        }

        RecomputeRanksLocked();
        Wh_Log(L"Confirmed focus: %s key=%s (map size=%zu, ranks=%zu, title=\"%s\")",
               pending.displayName.c_str(), pending.key.c_str(),
               g_appFocusMap.size(), g_rankedApps.size(),
               pending.windowTitle.c_str());
    }

    if (alsoConfirmPreviewWindow) {
        RequestApplyPreviewVisuals();
    }

    // Learn button mapping on UI thread, then apply. Drop tray-only apps that
    // never show a TaskListButton (HA widget, etc.).
    RunOnUiThread([key = pending.key, displayName = pending.displayName]() {
        AssociateActiveButtonWithKey(key);

        // Refresh path cache for currently tracked buttons.
        std::vector<winrt::weak_ref<FrameworkElement>> buttons;
        {
            std::lock_guard<std::mutex> lock(g_buttonsMutex);
            buttons = g_trackedButtons;
        }
        for (auto& weak : buttons) {
            FrameworkElement b = nullptr;
            try {
                b = weak.get();
            } catch (...) {
                continue;
            }
            if (b) {
                EnsureButtonPathCached(b, /*force=*/false);
            }
        }

        bool hasNameCache = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            hasNameCache = g_keyToAutomationName.contains(key);
        }
        bool exeNameOnAButton = false;
        for (auto& weak : buttons) {
            FrameworkElement b = nullptr;
            try {
                b = weak.get();
            } catch (...) {
                continue;
            }
            if (!b) {
                continue;
            }
            if (ScoreExeToAutomationName(displayName,
                                         GetButtonAutomationName(b)) >= 70) {
                exeNameOnAButton = true;
                break;
            }
        }
        const bool appears = PathAppearsOnTaskbar(key, displayName) ||
                             hasNameCache || exeNameOnAButton;

        size_t resolvedButtons = 0;
        {
            std::lock_guard<std::mutex> lock(g_buttonPathMutex);
            for (const auto& e : g_buttonPathCache) {
                if (!e.pathUpper.empty()) {
                    ++resolvedButtons;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            auto it = g_appFocusMap.find(key);
            if (it != g_appFocusMap.end()) {
                if (appears) {
                    it->second.seenOnTaskbar = true;
                } else if (g_settings.requireTaskbarButton &&
                           resolvedButtons >= 2) {
                    Wh_Log(L"Ignoring non-taskbar app: %s (title=\"%s\")",
                           displayName.c_str(),
                           it->second.lastWindowTitle.c_str());
                    it->second.lastConfirmedFocusTick = 0;
                    it->second.seenOnTaskbar = false;
                } else {
                    // Path cache not ready — keep the rank, name-match later.
                    it->second.seenOnTaskbar = true;
                }
            }
            RecomputeRanksLocked();
        }

        ApplyAllHighlights_UIThread();
    });
}

void SchedulePreviewConfirm(bool windowAlreadyTracked) {
    if (!g_settings.previewHighlightEnabled || !g_settings.enabled) {
        return;
    }
    CancelPreviewMinFocusTimer();
    const int previewMin = (std::max)(0, g_settings.previewMinFocusSeconds);
    if (previewMin <= 0 || windowAlreadyTracked) {
        OnPreviewMinFocusTimerElapsed();
        return;
    }
    if (g_hookThreadHwnd) {
        SetTimer(g_hookThreadHwnd, kPreviewMinFocusTimerId,
                 static_cast<UINT>(previewMin) * 1000U, nullptr);
    }
}

void HandleForegroundChanged(HWND hWnd) {
    if (g_unloading.load() || !g_settings.enabled) {
        return;
    }

    hWnd = NormalizeFocusHwnd(hWnd);

    std::wstring key;
    std::wstring displayName;
    std::wstring windowTitle;
    DWORD processId = 0;
    if (!ResolveAppIdentity(hWnd, key, displayName, processId, &windowTitle)) {
        CancelMinFocusTimer();
        CancelPreviewMinFocusTimer();
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_pendingFocus = {};
        // Still repaint ranks — taskbar active states changed.
        if (!g_rankedApps.empty()) {
            RequestApplyVisuals();
        }
        return;
    }

    const ULONGLONG now = GetTickCount64();
    const int minSeconds = (std::max)(0, g_settings.minFocusSeconds);

    bool alreadyTracked = false;
    bool windowAlreadyTracked = false;
    bool ranksNonEmpty = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        ranksNonEmpty = !g_rankedApps.empty();
        auto it = g_appFocusMap.find(key);
        alreadyTracked =
            it != g_appFocusMap.end() && it->second.lastConfirmedFocusTick > 0;
        // Keep title fresh for matching even before min-focus confirms.
        if (it != g_appFocusMap.end() && !windowTitle.empty()) {
            it->second.lastWindowTitle = windowTitle;
        }
        auto wit = g_windowFocusMap.find(hWnd);
        windowAlreadyTracked =
            wit != g_windowFocusMap.end() && wit->second.lastConfirmedTick > 0;
        if (wit != g_windowFocusMap.end() && !windowTitle.empty()) {
            wit->second.windowTitle = windowTitle;
        }
    }

    // Always repaint existing ranks on any focus change (Alt-Tab must not
    // leave other ranked icons unstyled until the min-focus timer fires).
    if (ranksNonEmpty || alreadyTracked) {
        RequestApplyVisuals();
    }

    bool sameAppPending = false;
    bool hwndChanged = false;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        if (g_pendingFocus.valid && g_pendingFocus.key == key) {
            hwndChanged = g_pendingFocus.hwnd != hWnd;
            g_pendingFocus.hwnd = hWnd;
            g_pendingFocus.processId = processId;
            if (!windowTitle.empty()) {
                g_pendingFocus.windowTitle = windowTitle;
            }
            sameAppPending = true;
        } else {
            g_pendingFocus.hwnd = hWnd;
            g_pendingFocus.processId = processId;
            g_pendingFocus.key = key;
            g_pendingFocus.displayName = displayName;
            g_pendingFocus.windowTitle = windowTitle;
            g_pendingFocus.focusStartTick = now;
            g_pendingFocus.valid = true;
        }
    }

    if (sameAppPending) {
        // Same app: keep app min-focus timer; re-schedule preview if HWND moved
        // between instances (multi-window VS Code / Terminal).
        if (hwndChanged || !windowAlreadyTracked) {
            SchedulePreviewConfirm(windowAlreadyTracked);
        }
        return;
    }

    CancelMinFocusTimer();
    CancelPreviewMinFocusTimer();

    const bool skipMinFocus = ShouldSkipAppMinFocus(key, alreadyTracked);

    Wh_Log(L"Focus candidate: %s key=%s (minFocus=%ds, tracked=%d, skipMin=%d, "
           L"promote=%s, previewTracked=%d)",
           displayName.c_str(), key.c_str(), minSeconds,
           alreadyTracked ? 1 : 0, skipMinFocus ? 1 : 0,
           PromoteModeName(g_settings.promoteMode),
           windowAlreadyTracked ? 1 : 0);

    SchedulePreviewConfirm(windowAlreadyTracked);

    if (skipMinFocus) {
        // minFocus=0, or promoteMode allows instant re-focus (tracked map
        // and/or currently highlighted top-N).
        OnMinFocusTimerElapsed();
        return;
    }

    if (g_hookThreadHwnd) {
        SetTimer(g_hookThreadHwnd, kMinFocusTimerId,
                 static_cast<UINT>(minSeconds) * 1000U, nullptr);
    }
}

void OnDecayTimer() {
    size_t before = 0;
    size_t after = 0;
    size_t windowsBefore = 0;
    size_t windowsAfter = 0;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        before = g_rankedApps.size();
        RecomputeRanksLocked();
        after = g_rankedApps.size();
        windowsBefore = g_windowFocusMap.size();
        PruneWindowFocusMapLocked();
        windowsAfter = g_windowFocusMap.size();
    }
    if (after != before || after == 0) {
        Wh_Log(L"Decay recompute: ranks %zu -> %zu", before, after);
        // Ensure overlays are stripped even if some weak refs are stale after
        // sleep — every live button will clear on next UpdateVisualStates too.
        g_pendingOverlaySweep = true;
        RequestApplyVisuals();
    }
    if (windowsAfter != windowsBefore) {
        Wh_Log(L"Preview decay: windows %zu -> %zu", windowsBefore,
               windowsAfter);
        RequestApplyPreviewVisuals();
    }
}

// ---------------------------------------------------------------------------
// WinEvent hook thread
// ---------------------------------------------------------------------------

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
    if (event != EVENT_SYSTEM_FOREGROUND) {
        return;
    }
    if (idObject != OBJID_WINDOW || !hWnd) {
        return;
    }

    if (g_hookThreadHwnd) {
        PostMessage(g_hookThreadHwnd, WM_APP_FOREGROUND_CHANGED,
                    reinterpret_cast<WPARAM>(hWnd), 0);
    }
}

LRESULT CALLBACK HookThreadWndProc(HWND hWnd,
                                   UINT msg,
                                   WPARAM wParam,
                                   LPARAM lParam) {
    switch (msg) {
        case WM_APP_FOREGROUND_CHANGED:
            HandleForegroundChanged(reinterpret_cast<HWND>(wParam));
            return 0;
        case WM_APP_REQUEST_APPLY:
            RequestApplyVisuals();
            return 0;
        case WM_APP_REQUEST_PREVIEW_APPLY:
            RequestApplyPreviewVisuals();
            return 0;
        case WM_TIMER:
            if (wParam == kMinFocusTimerId) {
                KillTimer(hWnd, kMinFocusTimerId);
                OnMinFocusTimerElapsed();
            } else if (wParam == kPreviewMinFocusTimerId) {
                KillTimer(hWnd, kPreviewMinFocusTimerId);
                OnPreviewMinFocusTimerElapsed();
            } else if (wParam == kDecayTimerId) {
                OnDecayTimer();
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hWnd, kMinFocusTimerId);
            KillTimer(hWnd, kPreviewMinFocusTimerId);
            KillTimer(hWnd, kDecayTimerId);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

DWORD WINAPI WinEventHookThread(LPVOID /*param*/) {
    WNDCLASSEXW wc{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = HookThreadWndProc,
        .hInstance = GetModuleHandle(nullptr),
        .lpszClassName = L"Windhawk_TaskbarRecentFocusHighlight_MsgWnd",
    };
    RegisterClassExW(&wc);

    g_hookThreadHwnd =
        CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                        nullptr, wc.hInstance, nullptr);
    if (!g_hookThreadHwnd) {
        Wh_Log(L"Failed to create message window: %u", GetLastError());
        return 1;
    }

    HWINEVENTHOOK hook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!hook) {
        Wh_Log(L"SetWinEventHook failed: %u", GetLastError());
    } else {
        Wh_Log(L"EVENT_SYSTEM_FOREGROUND hook installed");
    }

    SetTimer(g_hookThreadHwnd, kDecayTimerId, kDecayCheckIntervalMs, nullptr);

    if (HWND fg = GetForegroundWindow()) {
        PostMessage(g_hookThreadHwnd, WM_APP_FOREGROUND_CHANGED,
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

    if (g_hookThreadHwnd) {
        DestroyWindow(g_hookThreadHwnd);
        g_hookThreadHwnd = nullptr;
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    Wh_Log(L"WinEvent hook thread exiting");
    return 0;
}

void StartWinEventHookThread() {
    std::lock_guard<std::mutex> lock(g_winEventHookThreadMutex);
    if (g_winEventHookThread) {
        return;
    }
    HANDLE hThread =
        CreateThread(nullptr, 0, WinEventHookThread, nullptr, 0, nullptr);
    if (hThread) {
        g_winEventHookThread = hThread;
        Wh_Log(L"WinEvent hook thread started");
    } else {
        Wh_Log(L"CreateThread failed: %u", GetLastError());
    }
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

    DWORD threadId = GetThreadId(hThread);
    PostThreadMessage(threadId, WM_APP, 0, 0);
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    Wh_Log(L"WinEvent hook thread stopped");
}

// ---------------------------------------------------------------------------
// Settings load
// ---------------------------------------------------------------------------

void LoadSettings() {
    g_settings.enabled = Wh_GetIntSetting(L"enabled") != 0;
    g_settings.highlightCount = Wh_GetIntSetting(L"highlightCount");
    if (g_settings.highlightCount < 0) {
        g_settings.highlightCount = 0;
    }
    if (g_settings.highlightCount > 16) {
        g_settings.highlightCount = 16;
    }

    g_settings.minFocusSeconds = Wh_GetIntSetting(L"minFocusSeconds");
    if (g_settings.minFocusSeconds < 0) {
        g_settings.minFocusSeconds = 0;
    }

    PCWSTR promoteMode = Wh_GetStringSetting(L"promoteMode");
    g_settings.promoteMode = PromoteMode::ImmediateTracked;
    if (promoteMode) {
        if (wcscmp(promoteMode, L"immediateTopN") == 0) {
            g_settings.promoteMode = PromoteMode::ImmediateTopN;
        } else if (wcscmp(promoteMode, L"alwaysWait") == 0) {
            g_settings.promoteMode = PromoteMode::AlwaysWait;
        } else if (wcscmp(promoteMode, L"immediateTracked") == 0) {
            g_settings.promoteMode = PromoteMode::ImmediateTracked;
        }
    }
    Wh_FreeStringSetting(promoteMode);

    PCWSTR glowColor = Wh_GetStringSetting(L"glowColor");
    g_settings.glowColor = GlowColorMode::Accent;
    if (wcscmp(glowColor, L"green") == 0) {
        g_settings.glowColor = GlowColorMode::Green;
    } else if (wcscmp(glowColor, L"blue") == 0) {
        g_settings.glowColor = GlowColorMode::Blue;
    } else if (wcscmp(glowColor, L"orange") == 0) {
        g_settings.glowColor = GlowColorMode::Orange;
    } else if (wcscmp(glowColor, L"white") == 0) {
        g_settings.glowColor = GlowColorMode::White;
    } else if (wcscmp(glowColor, L"custom") == 0) {
        g_settings.glowColor = GlowColorMode::Custom;
    }
    Wh_FreeStringSetting(glowColor);

    PCWSTR customColor = Wh_GetStringSetting(L"customGlowColor");
    g_settings.customGlowColor = customColor ? customColor : L"#00C853";
    Wh_FreeStringSetting(customColor);

    g_settings.glowIntensity[0] = Wh_GetIntSetting(L"glowIntensityRank1");
    g_settings.glowIntensity[1] = Wh_GetIntSetting(L"glowIntensityRank2");
    g_settings.glowIntensity[2] = Wh_GetIntSetting(L"glowIntensityRank3");
    for (int& v : g_settings.glowIntensity) {
        if (v < 0) {
            v = 0;
        }
        if (v > 100) {
            v = 100;
        }
    }

    g_settings.sizeBoostPercent[0] = Wh_GetIntSetting(L"sizeBoostRank1");
    g_settings.sizeBoostPercent[1] = Wh_GetIntSetting(L"sizeBoostRank2");
    g_settings.sizeBoostPercent[2] = Wh_GetIntSetting(L"sizeBoostRank3");
    for (int& v : g_settings.sizeBoostPercent) {
        if (v < 0) {
            v = 0;
        }
        if (v > 50) {
            v = 50;
        }
    }

    PCWSTR glowStyle = Wh_GetStringSetting(L"glowStyle");
    g_settings.glowStyle = GlowStyle::LeftBar;
    if (glowStyle) {
        if (wcscmp(glowStyle, L"full") == 0) {
            g_settings.glowStyle = GlowStyle::Full;
        } else if (wcscmp(glowStyle, L"frame") == 0) {
            g_settings.glowStyle = GlowStyle::Frame;
        } else if (wcscmp(glowStyle, L"leftBar") == 0) {
            g_settings.glowStyle = GlowStyle::LeftBar;
        } else if (wcscmp(glowStyle, L"bottomBar") == 0) {
            g_settings.glowStyle = GlowStyle::BottomBar;
        }
    }
    Wh_FreeStringSetting(glowStyle);

    g_settings.glowThickness = Wh_GetIntSetting(L"glowThickness");
    if (g_settings.glowThickness < 1) {
        g_settings.glowThickness = 1;
    }
    if (g_settings.glowThickness > 16) {
        g_settings.glowThickness = 16;
    }

    g_settings.glowRoundness = Wh_GetIntSetting(L"glowRoundness");
    if (g_settings.glowRoundness < 0) {
        g_settings.glowRoundness = 0;
    }
    if (g_settings.glowRoundness > 50) {
        g_settings.glowRoundness = 50;
    }

    g_settings.glowSize = Wh_GetIntSetting(L"glowSize");
    if (g_settings.glowSize < 40) {
        g_settings.glowSize = 40;
    }
    if (g_settings.glowSize > 100) {
        g_settings.glowSize = 100;
    }

    g_settings.glowLayers = Wh_GetIntSetting(L"glowLayers");
    if (g_settings.glowLayers < 1) {
        g_settings.glowLayers = 1;
    }
    if (g_settings.glowLayers > 3) {
        g_settings.glowLayers = 3;
    }

    g_settings.glowFillOpacity = Wh_GetIntSetting(L"glowFillOpacity");
    if (g_settings.glowFillOpacity < 0) {
        g_settings.glowFillOpacity = 0;
    }
    if (g_settings.glowFillOpacity > 100) {
        g_settings.glowFillOpacity = 100;
    }

    g_settings.previewFillOpacity = Wh_GetIntSetting(L"previewFillOpacity");
    if (g_settings.previewFillOpacity < 0) {
        g_settings.previewFillOpacity = 0;
    }
    if (g_settings.previewFillOpacity > 100) {
        g_settings.previewFillOpacity = 100;
    }

    g_settings.glowDebugLog = Wh_GetIntSetting(L"glowDebugLog") != 0;

    g_settings.decayMinutes = Wh_GetIntSetting(L"decayMinutes");
    if (g_settings.decayMinutes < 0) {
        g_settings.decayMinutes = 0;
    }

    g_settings.requireTaskbarButton =
        Wh_GetIntSetting(L"requireTaskbarButton") != 0;

    g_settings.previewHighlightEnabled =
        Wh_GetIntSetting(L"previewHighlightEnabled") != 0;
    g_settings.previewMinFocusSeconds =
        Wh_GetIntSetting(L"previewMinFocusSeconds");
    if (g_settings.previewMinFocusSeconds < 0) {
        g_settings.previewMinFocusSeconds = 0;
    }
    g_settings.previewDecayMinutes = Wh_GetIntSetting(L"previewDecayMinutes");
    if (g_settings.previewDecayMinutes < 0) {
        g_settings.previewDecayMinutes = 0;
    }

    PCWSTR previewStyle = Wh_GetStringSetting(L"previewStyle");
    g_settings.previewStyle = PreviewStyle::TitleBar;
    if (previewStyle) {
        if (wcscmp(previewStyle, L"ring") == 0) {
            g_settings.previewStyle = PreviewStyle::Ring;
        } else if (wcscmp(previewStyle, L"titleBg") == 0) {
            g_settings.previewStyle = PreviewStyle::TitleBg;
        } else if (wcscmp(previewStyle, L"plate") == 0) {
            g_settings.previewStyle = PreviewStyle::Plate;
        } else if (wcscmp(previewStyle, L"titleBar") == 0) {
            g_settings.previewStyle = PreviewStyle::TitleBar;
        }
    }
    Wh_FreeStringSetting(previewStyle);

    g_settings.excludedPrograms.clear();
    for (int i = 0;; i++) {
        PCWSTR program = Wh_GetStringSetting(L"excludedPrograms[%d]", i);
        bool hasProgram = program && *program;
        if (hasProgram) {
            g_settings.excludedPrograms.insert(ToUpper(program));
        }
        Wh_FreeStringSetting(program);
        if (!hasProgram) {
            break;
        }
    }

    Wh_Log(L"Settings: enabled=%d style=%s th=%d round=%d%% size=%d%% "
           L"layers=%d fillOp=%d previewFillOp=%d debug=%d decay=%dmin "
           L"minFocus=%ds promote=%s preview=%d previewStyle=%s "
           L"previewMin=%ds previewDecay=%dmin",
           g_settings.enabled ? 1 : 0, GlowStyleName(g_settings.glowStyle),
           g_settings.glowThickness, g_settings.glowRoundness,
           g_settings.glowSize, g_settings.glowLayers,
           g_settings.glowFillOpacity, g_settings.previewFillOpacity,
           g_settings.glowDebugLog ? 1 : 0, g_settings.decayMinutes,
           g_settings.minFocusSeconds, PromoteModeName(g_settings.promoteMode),
           g_settings.previewHighlightEnabled ? 1 : 0,
           PreviewStyleName(g_settings.previewStyle),
           g_settings.previewMinFocusSeconds, g_settings.previewDecayMinutes);
}

// ---------------------------------------------------------------------------
// Windhawk entry points
// ---------------------------------------------------------------------------

BOOL Wh_ModInit() {
    Wh_Log(L"> Taskbar Recent Focus Highlight init v0.8.13");

    g_unloading = false;
    LoadSettings();

    // Identity resolve (taskband) — optional; fuzzy names remain as fallback.
    if (!HookTaskbarDllSymbols()) {
        Wh_Log(L"Warning: taskbar.dll identity hooks failed — fuzzy match only");
    }

    if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
        g_taskbarViewDllLoaded = true;
        if (!HookTaskbarViewDllSymbols(taskbarViewModule)) {
            Wh_Log(L"Warning: Taskbar.View hooks failed — visuals unavailable");
        }
    } else {
        Wh_Log(L"Taskbar view module not loaded yet");
    }

    HMODULE kernelBaseModule = GetModuleHandle(L"kernelbase.dll");
    auto pKernelBaseLoadLibraryExW = (decltype(&LoadLibraryExW))GetProcAddress(
        kernelBaseModule, "LoadLibraryExW");
    WindhawkUtils::SetFunctionHook(pKernelBaseLoadLibraryExW,
                                   LoadLibraryExW_Hook,
                                   &LoadLibraryExW_Original);

    StartWinEventHookThread();
    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");

    if (!g_taskbarViewDllLoaded) {
        if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
            if (!g_taskbarViewDllLoaded.exchange(true)) {
                Wh_Log(L"Got Taskbar.View.dll");
                if (HookTaskbarViewDllSymbols(taskbarViewModule)) {
                    Wh_ApplyHookOperations();
                }
            }
        }
    }

    if (g_hookThreadHwnd) {
        if (HWND fg = GetForegroundWindow()) {
            PostMessage(g_hookThreadHwnd, WM_APP_FOREGROUND_CHANGED,
                        reinterpret_cast<WPARAM>(fg), 0);
        }
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");
    g_unloading = true;
    g_taskbandResolveReady = false;
    g_previewHooksReady = false;

    // Clear visuals before tearing down hooks/thread.
    RunOnUiThread([]() {
        ClearAllHighlights_UIThread();
        ClearAllThumbnailHighlights_UIThread();
    });
    // Give the UI thread a brief moment to run the clear.
    Sleep(50);

    StopWinEventHookThread();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_appFocusMap.clear();
        g_rankedApps.clear();
        g_pendingFocus = {};
        g_keyToAutomationName.clear();
        g_windowFocusMap.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        g_trackedButtons.clear();
        g_dispatcherAnchor = {};
    }
    {
        std::lock_guard<std::mutex> lock(g_buttonPathMutex);
        g_buttonPathCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_thumbnailMapMutex);
        g_thumbnailTaskItemMapping.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
        g_trackedThumbViews.clear();
    }
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    Wh_Log(L">");
    if (bReload) {
        *bReload = FALSE;
    }

    LoadSettings();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        for (auto it = g_appFocusMap.begin(); it != g_appFocusMap.end();) {
            std::wstring displayUpper = ToUpper(it->second.displayName);
            if (IsExcludedKey(it->first, displayUpper)) {
                g_keyToAutomationName.erase(it->first);
                it = g_appFocusMap.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_windowFocusMap.begin();
             it != g_windowFocusMap.end();) {
            std::wstring fileUpper =
                ToUpper(FileNameFromPath(it->second.processKey));
            if (IsExcludedKey(it->second.processKey, fileUpper)) {
                it = g_windowFocusMap.erase(it);
            } else {
                ++it;
            }
        }
        PruneWindowFocusMapLocked();
        RecomputeRanksLocked();
    }

    RequestApplyVisuals();
    RequestApplyPreviewVisuals();
    return TRUE;
}
