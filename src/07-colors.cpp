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

winrt::Windows::UI::Color QuerySystemAccentColor() {
    try {
        // Hook thread only (as Subscribe/Unsubscribe): reuse its UISettings.
        winrt::Windows::UI::ViewManagement::UISettings uiSettings =
            g_uiSettings ? *g_uiSettings
                         : winrt::Windows::UI::ViewManagement::UISettings{};
        auto c = uiSettings.GetColorValue(
            winrt::Windows::UI::ViewManagement::UIColorType::Accent);
        c.A = 255;
        return c;
    } catch (...) {
        return UnpackColor(kDefaultAccentPacked);
    }
}

void RefreshCachedAccent() {
    winrt::Windows::UI::Color accent = QuerySystemAccentColor();
    const uint32_t packed = PackColor(accent);
    const uint32_t prev =
        g_cachedAccent.exchange(packed, std::memory_order_relaxed);
    if (prev != packed) {
        Wh_Log(L"System accent updated: #%02X%02X%02X", accent.R, accent.G,
               accent.B);
    }
}

void SubscribeAccentChanges() {
    if (g_uiSettings) {
        return;
    }
    try {
        g_uiSettings.emplace();
        g_colorValuesChangedToken = g_uiSettings->ColorValuesChanged(
            [](auto&&, auto&&) { PostToHookThread(WM_APP_REFRESH_ACCENT); });
    } catch (...) {
        g_uiSettings.reset();
        g_colorValuesChangedToken = {};
        Wh_Log(L"UISettings ColorValuesChanged subscribe failed");
    }
}

void UnsubscribeAccentChanges() {
    try {
        if (g_uiSettings) {
            g_uiSettings->ColorValuesChanged(g_colorValuesChangedToken);
        }
    } catch (...) {
    }
    g_colorValuesChangedToken = {};
    g_uiSettings.reset();
}

winrt::Windows::UI::Color ResolveGlowBaseColor(const Settings& settings) {
    winrt::Windows::UI::Color c{255, 0, 200, 83};  // default green-ish

    switch (settings.glowColor) {
        case GlowColorMode::Accent:
            c = UnpackColor(g_cachedAccent.load(std::memory_order_relaxed));
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
            c = UnpackColor(settings.customGlowPacked);
            break;
    }
    return c;
}

struct ButtonIdentity {
    std::wstring appIdUpper;  // Explorer's AppId
    bool uncombined = false;  // own-window button (Never Combine)
    HWND sampleHwnd = nullptr;
    DWORD samplePid = 0;
    std::vector<CapturedWindow> groupWindows;
};
ButtonIdentity GetCachedButtonIdentity(FrameworkElement button);
struct PaintCacheState {
    int rank = -1;
    uint32_t settingsGen = 0;
    uint32_t accent = 0;
    TaskbarEdge edge = TaskbarEdge::Bottom;
    int boxW = 0;
    int boxH = 0;
    uint64_t plateGeometry = 0;  // ReadPlateShapes signature (plates only)
};
PaintCacheState GetCachedPaintState(FrameworkElement button);
void SetCachedPaintState(FrameworkElement button,
                         int rank,
                         uint32_t gen,
                         TaskbarEdge edge = TaskbarEdge::Bottom,
                         int boxW = 0,
                         int boxH = 0,
                         uint64_t plateGeometry = 0);
bool RunOnUiThread(const winrt::Windows::UI::Core::DispatchedHandler& handler);
void ApplyAllHighlights_UIThread();
void RefreshThumbnailFlyout_UIThread(FrameworkElement anyThumb);

