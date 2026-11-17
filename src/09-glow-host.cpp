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

// Native pill is a 2–4px strip. Taskbar Styler themes restyle RunningIndicator
// into a full-cell acrylic hover plate that would cover a side bar sitting
// under it (UWPSpy: 39×38 acrylic on PointerOver vs #00FFFFFF at rest).
bool RunningIndicatorLooksLikeHoverPlate(FrameworkElement ri,
                                         FrameworkElement iconPanel) {
    if (!ri || !iconPanel) {
        return false;
    }
    try {
        double rw = ri.ActualWidth();
        double rh = ri.ActualHeight();
        double pw = iconPanel.ActualWidth();
        double ph = iconPanel.ActualHeight();
        if (!(rw > 1.0) || !(rh > 1.0) || !(pw > 1.0) || !(ph > 1.0)) {
            return false;
        }
        return rw > pw * 0.45 && rh > ph * 0.45;
    } catch (...) {
        return false;
    }
}

// Position in the native-only child sequence. Never reorder native children.
struct GlowOrderChild {
    bool aboveHost;
};
uint32_t GlowHostInsertionIndex(const std::vector<GlowOrderChild>& children) {
    for (uint32_t i = 0; i < children.size(); ++i) {
        if (children[i].aboveHost) {
            return i;
        }
    }
    return static_cast<uint32_t>(children.size());
}

// Preserve the established style layering without changing native properties.
// Explicit InsertAt is required even at the end: Append omits XAML deferred
// index notifications while RemoveAt sends them.
void EnsureGlowHostZOrder(Controls::Panel panel,
                          UIElement host,
                          GlowStyle style) {
    if (!panel || !host)
        return;
    try {
        auto children = panel.Children();
        uint32_t current;
        if (!children.IndexOf(host, current))
            return;
        std::vector<GlowOrderChild> native;
        auto panelFe = panel.as<FrameworkElement>();
        for (auto child : children) {
            if (child == host)
                continue;
            auto fe = child.try_as<FrameworkElement>();
            auto name = fe ? fe.Name() : winrt::hstring{};
            bool running = name == L"RunningIndicator";
            bool above = name == L"OverlayIcon" ||
                         name == L"MultiWindowElement" ||
                         name == L"ProgressIndicator";
            if ((style == GlowStyle::LeftBar || style == GlowStyle::Full) &&
                (name == L"Icon" || name == L"DefaultIcon"))
                above = true;
            if (running && !RunningIndicatorLooksLikeHoverPlate(fe, panelFe))
                above = true;
            native.push_back({above});
        }
        uint32_t target = GlowHostInsertionIndex(native);
        if (target == current)
            return;
        children.RemoveAt(current);
        children.InsertAt(target, host);
    } catch (...) {
        Wh_Log(L"Glow host placement failed: %08X", winrt::to_hresult());
    }
}

bool ButtonHasOurChrome(FrameworkElement button) {
    if (!button) {
        return false;
    }
    auto iconPanel = GetIconPanel(button);
    if (!iconPanel) {
        return FindChildByName(button, kGlowElementName) != nullptr;
    }
    return FindChildByName(iconPanel, kGlowElementName) != nullptr;
}

void ClearButtonHighlight(FrameworkElement button) {
    if (!button) {
        return;
    }

    auto iconPanelEarly = GetIconPanel(button);

    // Skip no-op clears on every mouse-over, including overlay sweep.
    // Sweep only removes leftover WhRecentFocusGlow; do not reorder native
    // children on buttons we never painted.
    if (!ButtonHasOurChrome(button)) {
        SetCachedPaintState(button, 0, SettingsSnap()->generation);
        return;
    }

    try {
        auto iconPanel = iconPanelEarly ? iconPanelEarly : GetIconPanel(button);
        if (!iconPanel) {
            // Still try to strip our named overlay from the button root.
            if (auto panel = button.try_as<Controls::Panel>()) {
                RemoveNamedChild(panel, kGlowElementName);
            }
            SetCachedPaintState(button, 0, SettingsSnap()->generation);
            return;
        }

        // Do NOT ClearValue BackgroundElement — we no longer style it; clearing
        // local values can leave a stuck pale hover plate after decay.

        // EnsureGlowHost adds exactly one host, as a direct child.
        if (auto panel = iconPanel.try_as<Controls::Panel>()) {
            RemoveNamedChild(panel, kGlowElementName);
        }

        SetCachedPaintState(button, 0, SettingsSnap()->generation);
    } catch (...) {
        HRESULT hr = winrt::to_hresult();
        Wh_Log(L"ClearButtonHighlight error %08X", hr);
    }
}

// Ensure glow host grid exists (L0–L1 rectangles). Returns host or nullptr.
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
            </Grid>
        )";
        host = Markup::XamlReader::Load(xaml).as<Controls::Grid>();
        // InsertAt(end), unlike Append, notifies XAML's deferred-element
        // index bookkeeping. Pair this with removal when unhighlighted.
        auto children = panel.Children();
        children.InsertAt(children.Size(), host);
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
    if (auto contour = FindChildByName(host, L"WhRecentFocusContour")) {
        contour.Visibility(Visibility::Collapsed);
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

// Reject malformed geometry before arithmetic or assigning XAML properties.
// Coordinates are DIPs relative to the native panel. Never divide by native dimensions.
bool SafeGlowMetric(double value) {
    return std::isfinite(value) && std::abs(value) <= 16384.0;
}
bool SafeGlowBounds(double x, double y, double w, double h) {
    return SafeGlowMetric(x) && SafeGlowMetric(y) && SafeGlowMetric(w) &&
           SafeGlowMetric(h) && w > 0.5 && h > 0.5;
}
double InsetGlowRadius(double radius, double inset, double w, double h) {
    if (!SafeGlowMetric(radius) || radius < 0 || !SafeGlowMetric(inset) ||
        !SafeGlowBounds(0, 0, w, h))
        return 0;
    return (std::max)(0.0, (std::min)(radius - inset, (std::min)(w, h) * 0.5));
}
struct NativeGlowShape {
    double x = 0, y = 0, w = 0, h = 0;
    CornerRadius corners{0, 0, 0, 0};
};
bool ReadNativeGlowShape(FrameworkElement element,
                         FrameworkElement reference,
                         NativeGlowShape& shape) {
    try {
        if (!element || element.Visibility() != Visibility::Visible)
            return false;
        double w = element.ActualWidth(), h = element.ActualHeight();
        if (!SafeGlowBounds(0, 0, w, h))
            return false;
        CornerRadius corners{0, 0, 0, 0};
        if (auto border = element.try_as<Controls::Border>()) {
            corners = border.CornerRadius();
        } else if (auto rect = element.try_as<Shapes::Rectangle>()) {
            double radius = (std::min)(rect.RadiusX(), rect.RadiusY());
            corners = CornerRadius{radius, radius, radius, radius};
        } else {
            return false;  // Unknown shape: use fallback, not a guessed native
                           // contour.
        }
        auto transform = element.TransformToVisual(reference);
        auto origin = transform.TransformPoint({0, 0});
        auto unitX = transform.TransformPoint({1, 0});
        auto unitY = transform.TransformPoint({0, 1});
        double sx = unitX.X - origin.X, sy = unitY.Y - origin.Y;
        // Border radii cannot reproduce rotations, skew or non-uniform scaling.
        if (!SafeGlowMetric(unitX.Y) || !SafeGlowMetric(unitY.X) ||
            !SafeGlowMetric(sx) || !SafeGlowMetric(sy) || sx <= 0 || sy <= 0 ||
            std::abs(unitX.Y - origin.Y) > 0.01 ||
            std::abs(unitY.X - origin.X) > 0.01 || std::abs(sx - sy) > 0.01)
            return false;
        if (!SafeGlowBounds(origin.X, origin.Y, w * sx, h * sy))
            return false;
        shape = {
            origin.X, origin.Y, w * sx, h * sy,
            CornerRadius{corners.TopLeft * sx, corners.TopRight * sx,
                         corners.BottomRight * sx, corners.BottomLeft * sx}};
        return true;
    } catch (...) {
        return false;  // Detached or changing native visual; use the safe
                       // fallback.
    }
}
void PaintNativeContour(Controls::Grid host,
                        FrameworkElement iconPanel,
                        GlowStyle style,
                        winrt::Windows::UI::Color color,
                        double thickness,
                        double sizeFrac,
                        double opacity,
                        int fillOpacity) {
    // The host may not have been arranged yet. Use the native panel's content
    // center, not TransformToVisual(host), so first paint and later paints
    // agree.
    double panelW = iconPanel.ActualWidth(), panelH = iconPanel.ActualHeight();
    if (!SafeGlowBounds(0, 0, panelW, panelH))
        return;
    Thickness padding{0, 0, 0, 0};
    if (auto grid = iconPanel.try_as<Controls::Grid>()) {
        auto p = grid.Padding();
        auto b = grid.BorderThickness();
        padding = {p.Left + b.Left, p.Top + b.Top, p.Right + b.Right,
                   p.Bottom + b.Bottom};
    }
    if (!SafeGlowMetric(padding.Left) || !SafeGlowMetric(padding.Top) ||
        !SafeGlowMetric(padding.Right) || !SafeGlowMetric(padding.Bottom))
        return;
    double contentW = panelW - padding.Left - padding.Right;
    double contentH = panelH - padding.Top - padding.Bottom;
    if (!SafeGlowBounds(0, 0, contentW, contentH))
        return;
    double centerX = padding.Left + contentW * 0.5;
    double centerY = padding.Top + contentH * 0.5;
    NativeGlowShape shape;
    auto indicator = FindRunningIndicator(iconPanel);
    bool plate =
        indicator && RunningIndicatorLooksLikeHoverPlate(indicator, iconPanel);
    bool found = false;
    if (plate)
        found = ReadNativeGlowShape(indicator, iconPanel, shape);
    if (!found)
        found = ReadNativeGlowShape(
            FindChildByName(iconPanel, L"BackgroundElement"), iconPanel, shape);
    if (!found) {
        shape.x = padding.Left;
        shape.y = padding.Top;
        shape.w = contentW;
        shape.h = contentH;
        if (!SafeGlowBounds(0, 0, shape.w, shape.h))
            return;
        shape.corners = CornerRadius{4, 4, 4, 4};
    }
    double inset =
        (std::max)(1.0, (std::min)(shape.w, shape.h) * (1.0 - sizeFrac) * 0.5);
    double w = shape.w - 2 * inset, h = shape.h - 2 * inset;
    if (!SafeGlowBounds(shape.x + inset, shape.y + inset, w, h))
        return;
    Controls::Border border = FindChildByName(host, L"WhRecentFocusContour")
                                  .try_as<Controls::Border>();
    if (!border) {
        border = Controls::Border();
        border.Name(L"WhRecentFocusContour");
        border.IsHitTestVisible(false);
        auto children = host.Children();
        children.InsertAt(children.Size(), border);
    }
    double dx = shape.x + shape.w * 0.5 - centerX;
    double dy = shape.y + shape.h * 0.5 - centerY;
    if (!SafeGlowMetric(dx) || !SafeGlowMetric(dy))
        return;
    border.HorizontalAlignment(HorizontalAlignment::Center);
    border.VerticalAlignment(VerticalAlignment::Center);
    border.Width(w);
    border.Height(h);
    border.Margin({dx, dy, -dx, -dy});
    border.CornerRadius({InsetGlowRadius(shape.corners.TopLeft, 0, w, h),
                         InsetGlowRadius(shape.corners.TopRight, 0, w, h),
                         InsetGlowRadius(shape.corners.BottomRight, 0, w, h),
                         InsetGlowRadius(shape.corners.BottomLeft, 0, w, h)});
    bool frame = style == GlowStyle::Frame;
    double stroke = frame ? (std::min)(thickness, (std::min)(w, h) * 0.5) : 0;
    border.BorderThickness({stroke, stroke, stroke, stroke});
    auto strokeColor = color;
    strokeColor.A = 230;
    border.BorderBrush(Media::SolidColorBrush{strokeColor});
    color.A = frame ? 0
                    : static_cast<uint8_t>(
                          (std::clamp)(fillOpacity, 0, 100) * 2.55 * 0.6 + 0.5);
    border.Background(Media::SolidColorBrush{color});
    border.Opacity((std::clamp)(opacity, 0.0, 1.0));
    border.Visibility(Visibility::Visible);
}

