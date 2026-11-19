#include <windhawk_utils.h>

#include <cmath>
#include <initguid.h>
#include <inspectable.h>
#include <propkey.h>
#include <objbase.h>
#include <shellapi.h>
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
#include <winrt/Windows.UI.Xaml.Data.h>
// WinUI 2 ItemsRepeater (thumbnail flyout). Same as taskbar-thumbnail-reorder.
#define WH_WINRT_WINUI2
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/base.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace winrt::Windows::UI::Xaml;

// Public IVirtualDesktopManager (Win10+). Defined here so we do not depend on
// a particular SDK header / uuid.lib export of the CLSID.
#ifndef __IVirtualDesktopManager_INTERFACE_DEFINED__
#define __IVirtualDesktopManager_INTERFACE_DEFINED__
MIDL_INTERFACE("a5cd92ff-29be-454c-8d04-d82879fb3f1b")
IVirtualDesktopManager : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsWindowOnCurrentVirtualDesktop(
        HWND topLevelWindow,
        BOOL* onCurrentDesktop) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWindowDesktopId(HWND topLevelWindow,
                                                         GUID* desktopId) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveWindowToDesktop(HWND topLevelWindow,
                                                          REFGUID desktopId) = 0;
};
#endif

// AA509086-5CA9-4C25-8F95-589D3C07B48A
static const CLSID kClsidVirtualDesktopManager = {
    0xaa509086,
    0x5ca9,
    0x4c25,
    {0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a}};

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

// 0.12.1: Frame / Full became Plate with accent / Plate (flush with the
// native plate); the setting ids `frame` / `full` are kept.
enum class GlowStyle {
    PlateAccent,  // id `frame`: native plate, half fill, accent border
    Plate,        // id `full`: native plate, full Fill opacity
    LeftBar,      // side bar: left on horizontal taskbar, bottom on vertical
};

// Physical screen edge the taskbar is on (Win11 24H2/25H2 can use all four).
enum class TaskbarEdge {
    Bottom,
    Left,
    Top,
    Right,
};

// Where a bar highlight is painted on the icon button.
enum class BarSide {
    Left,
    Top,
    Right,
    Bottom,
};

// When re-focus may skip the app min-focus timer (see HandleForegroundChanged).
enum class PromoteMode {
    ImmediateTracked,  // any app still in this desktop's recency map
    ImmediateTopN,     // only if currently highlighted on this desktop
    AlwaysWait,        // always use minFocusSeconds
};

// Thumbnail flyout highlight (independent of icon GlowStyle).
enum class PreviewStyle {
    TitleBg,    // tinted plate over the title row
    Plate,      // tint whole preview card (hover-like plate)
    TitleBar,   // thin bar under the title, above the thumbnail image
    Outline,    // plate plus an accent border (card overlay)
    TitleOutline,  // title plate plus an accent border (0.12.1)
    None,       // not marked (ranks 2+ only)
};

// Setting ids <-> enum values. The ids double as log names.
template <typename E>
struct EnumName {
    PCWSTR name;
    E value;
};
constexpr EnumName<GlowColorMode> kGlowColorNames[] = {
    {L"accent", GlowColorMode::Accent}, {L"green", GlowColorMode::Green},
    {L"blue", GlowColorMode::Blue},     {L"orange", GlowColorMode::Orange},
    {L"white", GlowColorMode::White},   {L"custom", GlowColorMode::Custom},
};
constexpr EnumName<GlowStyle> kGlowStyleNames[] = {
    {L"leftBar", GlowStyle::LeftBar},
    {L"frame", GlowStyle::PlateAccent},
    {L"full", GlowStyle::Plate},
};
constexpr EnumName<PromoteMode> kPromoteModeNames[] = {
    {L"immediateTracked", PromoteMode::ImmediateTracked},
    {L"immediateTopN", PromoteMode::ImmediateTopN},
    {L"alwaysWait", PromoteMode::AlwaysWait},
};
constexpr EnumName<PreviewStyle> kPreviewStyleNames[] = {
    {L"plate", PreviewStyle::Plate},       {L"outline", PreviewStyle::Outline},
    {L"titleBg", PreviewStyle::TitleBg},   {L"titleBar", PreviewStyle::TitleBar},
    {L"titleOutline", PreviewStyle::TitleOutline},
    {L"none", PreviewStyle::None},
};
constexpr EnumName<TaskbarEdge> kTaskbarEdgeNames[] = {
    {L"bottom", TaskbarEdge::Bottom}, {L"left", TaskbarEdge::Left},
    {L"top", TaskbarEdge::Top},       {L"right", TaskbarEdge::Right},
};
constexpr EnumName<BarSide> kBarSideNames[] = {
    {L"left", BarSide::Left}, {L"top", BarSide::Top},
    {L"right", BarSide::Right}, {L"bottom", BarSide::Bottom},
};

// Name of a value; the table's first entry for an unknown one.
template <typename E, size_t N>
PCWSTR EnumToName(const EnumName<E> (&table)[N], E value) {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return table[0].name;
}

// Value for a name; false (out unchanged) for an unknown one.
template <typename E, size_t N>
bool NameToEnum(const EnumName<E> (&table)[N], PCWSTR name, E& out) {
    for (const auto& entry : table) {
        if (name && wcscmp(entry.name, name) == 0) {
            out = entry.value;
            return true;
        }
    }
    return false;
}

struct Settings {
    int highlightCount = 3;
    int minFocusSeconds = 8;
    PromoteMode promoteMode = PromoteMode::ImmediateTracked;
    GlowColorMode glowColor = GlowColorMode::Accent;
    std::wstring customGlowColor = L"#00C853";
    uint32_t customGlowPacked = 0xFF00C853;  // parsed once in LoadSettings
    int glowIntensity[3] = {100, 70, 45};
    GlowStyle glowStyle = GlowStyle::LeftBar;
    int glowThickness = 3;      // px, side bar
    int glowSize = 92;          // % side bar length (clamped to fit)
    int glowFillOpacity = 40;   // % side bar / plate (half for PlateAccent)
    int glowAccentThickness = 2;  // px, PlateAccent border
    int previewFillOpacity = 40;  // % for thumbnail plate / titleBg only
    int decayMinutes = 30;
    bool previewHighlightEnabled = true;
    int previewHighlightCount = 3;
    bool previewSingleWindow = false;  // also rank single-window flyouts
    int previewIntensity[3] = {100, 70, 45};
    int previewMinFocusSeconds = 1;
    int previewDecayMinutes = 15;
    // Rank 1 and ranks 2+ are styled separately (0.11.16; replaced the
    // fixed "hybrid" option: rank 1 plate, ranks 2+ title tint = defaults).
    PreviewStyle previewTopStyle = PreviewStyle::Plate;
    PreviewStyle previewRestStyle = PreviewStyle::TitleBg;
    int previewOutlineThickness = 2;
    std::unordered_set<std::wstring> excludedPrograms;
    // Bumped in PublishSettings so UVS can skip a no-op repaint.
    uint32_t generation = 0;
};

// Published as a whole. LoadSettings builds a local Settings, then stores it
// here so the focus thread and UI dispatchers never iterate a set mid-rehash.
// Windhawk's libc++ has no std::atomic<shared_ptr> specialization (T must be
// trivially copyable), so the pointer is swapped under a mutex.
std::mutex g_settingsMutex;
std::shared_ptr<const Settings> g_settingsPtr =
    std::make_shared<const Settings>();

std::shared_ptr<const Settings> SettingsSnap() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settingsPtr;
}

void PublishSettings(Settings s) {
    static std::atomic<uint32_t> seq{0};
    s.generation = seq.fetch_add(1, std::memory_order_relaxed) + 1;
    auto next = std::make_shared<const Settings>(std::move(s));
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settingsPtr = std::move(next);
}

// Accent is not inside Settings: ColorValuesChanged must not republish a
// stale snapshot over a concurrent LoadSettings. Packed ARGB.
constexpr uint32_t kDefaultAccentPacked = 0xFF0078D7u;  // #0078D7
std::atomic<uint32_t> g_cachedAccent{kDefaultAccentPacked};

uint32_t PackColor(winrt::Windows::UI::Color c) {
    return (static_cast<uint32_t>(c.A) << 24) |
           (static_cast<uint32_t>(c.R) << 16) |
           (static_cast<uint32_t>(c.G) << 8) | static_cast<uint32_t>(c.B);
}

winrt::Windows::UI::Color UnpackColor(uint32_t v) {
    return {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16),
            static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
}

[[clang::no_destroy]] std::optional<
    winrt::Windows::UI::ViewManagement::UISettings>
    g_uiSettings;
winrt::event_token g_colorValuesChangedToken{};

// WM_TIMER may be a leftover from a previous candidate (KillTimer does not
// flush a message already queued). Immediate confirm skips the deadline.
enum class MinFocusConfirmMode {
    FromTimer,
    Immediate,
};

