// ---------------------------------------------------------------------------
// Card overlay (Plate / Outline preview styles, 0.11.16)
// ---------------------------------------------------------------------------
// Our own Border, inserted right above the card's BackgroundBorder and bound
// to its geometry (size, margin, alignment, rounded corners), so it follows
// Taskbar Styler themes without measuring. Same construction as
// taskbar-active-window-highlight (EphremTil17, GPL-3.0). Before 0.11.16 the
// plate recoloured BackgroundBorder itself, and the card's hover state
// replaced that brush (rank-1 plate vanished on hover). The overlay sits
// above BackgroundBorder and below the title/thumbnail, so hover changes the
// background underneath and still shows through a partial fill.

constexpr wchar_t kThumbCardOverlayName[] = L"WhRecentFocusThumbCard";

void BindCardOverlay(FrameworkElement const& target,
                     DependencyObject const& source,
                     DependencyProperty const& property,
                     PCWSTR path) {
    Data::Binding binding;
    binding.Source(source);
    binding.Path(PropertyPath(path));
    binding.Mode(Data::BindingMode::OneWay);
    target.SetBinding(property, binding);
}

Controls::Border FindCardOverlay(FrameworkElement thumbView) {
    if (auto el = FindDescendantByName(thumbView, kThumbCardOverlayName)) {
        return el.try_as<Controls::Border>();
    }
    return nullptr;
}

void HideCardOverlay(FrameworkElement thumbView) {
    try {
        if (auto overlay = FindCardOverlay(thumbView)) {
            overlay.Visibility(Visibility::Collapsed);
        }
    } catch (...) {
    }
}

// Returns false when the card has no BackgroundBorder in a panel (unknown
// template); the caller then uses the old overlay-host plate.
bool PaintCardOverlay(FrameworkElement thumbView,
                      winrt::Windows::UI::Color fill,
                      winrt::Windows::UI::Color stroke,
                      double strokeThickness) {
    auto background = FindThumbnailBackgroundBorder(thumbView);
    if (!background) {
        return false;
    }
    auto panel =
        Media::VisualTreeHelper::GetParent(background).try_as<Controls::Panel>();
    if (!panel) {
        return false;
    }
    Controls::Border overlay = FindCardOverlay(thumbView);
    if (!overlay) {
        overlay = Controls::Border();
        overlay.Name(kThumbCardOverlayName);
        overlay.IsHitTestVisible(false);
        Controls::Grid::SetRow(overlay, Controls::Grid::GetRow(background));
        Controls::Grid::SetColumn(overlay, Controls::Grid::GetColumn(background));
        Controls::Grid::SetRowSpan(overlay, Controls::Grid::GetRowSpan(background));
        Controls::Grid::SetColumnSpan(overlay,
                                      Controls::Grid::GetColumnSpan(background));
        BindCardOverlay(overlay, background, FrameworkElement::WidthProperty(),
                        L"Width");
        BindCardOverlay(overlay, background, FrameworkElement::HeightProperty(),
                        L"Height");
        BindCardOverlay(overlay, background, FrameworkElement::MarginProperty(),
                        L"Margin");
        BindCardOverlay(overlay, background,
                        FrameworkElement::HorizontalAlignmentProperty(),
                        L"HorizontalAlignment");
        BindCardOverlay(overlay, background,
                        FrameworkElement::VerticalAlignmentProperty(),
                        L"VerticalAlignment");
        if (background.try_as<Controls::Border>()) {
            BindCardOverlay(overlay, background,
                            Controls::Border::CornerRadiusProperty(),
                            L"CornerRadius");
        }
        auto children = panel.Children();
        uint32_t index = 0;
        const bool found = children.IndexOf(background, index);
        // Explicit index: directly above the background, below the content.
        children.InsertAt(found ? index + 1 : 0, overlay);
    }
    overlay.Background(Media::SolidColorBrush{fill});
    if (strokeThickness > 0.0) {
        overlay.BorderBrush(Media::SolidColorBrush{stroke});
        overlay.BorderThickness(Thickness{strokeThickness, strokeThickness,
                                          strokeThickness, strokeThickness});
    } else {
        overlay.BorderBrush(nullptr);
        overlay.BorderThickness(Thickness{0, 0, 0, 0});
    }
    overlay.Visibility(Visibility::Visible);
    return true;
}

void ClearThumbnailHighlight(FrameworkElement thumbView) {
    if (!thumbView) {
        return;
    }
    try {
        if (!FindDescendantByName(thumbView, kThumbGlowElementName) &&
            !FindDescendantByName(thumbView, kThumbCardOverlayName)) {
            return;
        }
        RemoveNamedDescendant(thumbView, kThumbCardOverlayName);
        RemoveNamedDescendant(thumbView, kThumbGlowElementName);
        RemoveNamedDescendant(thumbView, kThumbTitleBgName);
        RemoveNamedDescendant(thumbView, kThumbTitleBarName);
    } catch (...) {
    }
}

// Preview panels use explicit insertion too: keep deferred-child index
// notifications balanced with removal, even when inserting at the end.
// Put el directly above the card's BackgroundBorder when that border is a
// child of this panel: above the card background, below the title text and
// thumbnail. Returns false if there is no such border here.
bool PlaceAboveCardBackground(Controls::Panel panel, UIElement el) {
    if (!panel || !el) {
        return false;
    }
    try {
        auto children = panel.Children();
        int bg = -1;
        for (uint32_t i = 0; i < children.Size(); ++i) {
            auto fe = children.GetAt(i).try_as<FrameworkElement>();
            if (fe && fe.Name() == L"BackgroundBorder") {
                bg = static_cast<int>(i);
                break;
            }
        }
        if (bg < 0) {
            return false;
        }
        uint32_t idx = 0;
        if (!children.IndexOf(el, idx)) {
            return false;
        }
        if (static_cast<int>(idx) == bg + 1) {
            return true;
        }
        children.RemoveAt(idx);
        if (static_cast<int>(idx) < bg) {
            --bg;
        }
        children.InsertAt(static_cast<uint32_t>(bg + 1), el);
        return true;
    } catch (...) {
        return false;
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
            children.InsertAt(children.Size(), el);
        }
    } catch (...) {
    }
}

// Overlay host that must NOT affect parent layout.
//
// Thumbnail cards often use a Grid with rows (title | image). A normal child
// lands in (0,0) — the title row — and expands that row (bar between icon and
// text + card grows sideways). Same fix as icon glow: span every row/column
// and Stretch to the arranged card size. Children use Margin (not
// RenderTransform — that clips the right end-cap).
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
        auto children = panel.Children();
        children.InsertAt(children.Size(), host);
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
    // Under the card content, right above its background (0.11.17): a title
    // wash on top of the title made the text look dim at higher tint
    // opacity. Front only if this template has no BackgroundBorder here.
    if (!PlaceAboveCardBackground(panel, host)) {
        BringElementToFront(panel, host);
    }
    return host;
}

// Position with Margin, not RenderTransform: the layout clip is the
// un-transformed slot, so a translate of `pad` clipped the right end-cap.
// The far margins take back the offset and the size, so the desired size is
// 0 and an overlay can never make the card larger than Windows laid it out
// (as the icon plates, ZeroDesiredMargin). Placement unchanged, and no
// layout clip: the slot minus these margins only grows.
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
        el.Margin(Thickness{x, y, width > 0 ? -(x + width) : 0,
                            height > 0 ? -(y + height) : 0});
        el.ClearValue(UIElement::RenderTransformProperty());
        if (width > 0) {
            el.Width(width);
            el.MaxWidth(width);
        } else {
            el.ClearValue(FrameworkElement::WidthProperty());
            el.ClearValue(FrameworkElement::MaxWidthProperty());
        }
        if (height > 0) {
            el.Height(height);
            el.MaxHeight(height);
        } else {
            el.ClearValue(FrameworkElement::HeightProperty());
            el.ClearValue(FrameworkElement::MaxHeightProperty());
        }
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
                    // Title outline: else a switch to titleBg keeps it.
                    b.BorderBrush(nullptr);
                    b.BorderThickness(Thickness{0, 0, 0, 0});
                }
                el.ClearValue(FrameworkElement::WidthProperty());
                el.ClearValue(FrameworkElement::HeightProperty());
                el.ClearValue(FrameworkElement::MaxWidthProperty());
                el.ClearValue(FrameworkElement::MaxHeightProperty());
                el.ClearValue(FrameworkElement::MarginProperty());
            } catch (...) {
            }
        }
    }
}

// Keep the overlay host; only restore plate + hide children (style switch /
// rank change). Full ClearThumbnailHighlight tears down the host and forces
// XamlReader::Load on the next paint.
void ResetThumbnailOverlayForRepaint(FrameworkElement thumbView) {
    HideCardOverlay(thumbView);
    try {
        auto panel = GetThumbnailHostPanel(thumbView);
        if (!panel) {
            return;
        }
        auto hostEl =
            FindChildByName(panel.as<FrameworkElement>(), kThumbGlowElementName);
        if (auto host = hostEl.try_as<Controls::Grid>()) {
            HideThumbOverlayChildren(host);
        }
    } catch (...) {
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

// One Low-priority flyout refresh per UI thread. Title remeasure, hover-switch
// (TargetItemKey), and OnApplyTemplate all coalesce here. The callback picks a
// card that still has an ItemsRepeater ancestor — do not reuse the weak_ref
// captured at schedule time (often the oldest tracked card, already recycled).
// At most one follow-up when the first pass still sees unmeasured cards.
thread_local bool g_previewFlyoutRefreshPending = false;
thread_local bool g_previewFlyoutRefreshNeeded = false;
thread_local int g_thumbRelayoutDepth = 0;
thread_local int g_previewFlyoutRefreshFollowUps = 0;

FrameworkElement PickLiveFlyoutThumbOnThisDispatcher() {
    FrameworkElement withRepeater = nullptr;
    FrameworkElement any = nullptr;
    std::vector<winrt::weak_ref<FrameworkElement>> thumbs;
    {
        std::lock_guard<std::mutex> lock(g_thumbViewsMutex);
        thumbs = g_trackedThumbViews;
    }
    ForEachLiveElementOnThisDispatcher(thumbs, [&](FrameworkElement el) {
        if (!any) {
            any = el;
        }
        if (!withRepeater &&
            FindAncestorByClassName(
                el, L"Microsoft.UI.Xaml.Controls.ItemsRepeater")) {
            withRepeater = el;
        }
    });
    return withRepeater ? withRepeater : any;
}

void SchedulePreviewFlyoutRefresh(FrameworkElement dispatcherAnchor) {
    if (!dispatcherAnchor || g_unloading.load()) {
        return;
    }
    if (g_thumbRelayoutDepth > 0) {
        g_previewFlyoutRefreshNeeded = true;
        return;
    }
    if (g_previewFlyoutRefreshPending) {
        return;
    }
    g_previewFlyoutRefreshPending = true;
    try {
        auto dispatcher = dispatcherAnchor.Dispatcher();
        if (!dispatcher) {
            g_previewFlyoutRefreshPending = false;
            return;
        }
        winrt::weak_ref<FrameworkElement> weak =
            winrt::make_weak(dispatcherAnchor);
        if (!DispatcherTryRun(
                dispatcher,
                winrt::Windows::UI::Core::CoreDispatcherPriority::Low,
                [weak]() {
                g_previewFlyoutRefreshPending = false;
                try {
                    if (g_unloading.load()) {
                        return;
                    }
                    FrameworkElement el = nullptr;
                    try {
                        el = weak.get();
                    } catch (...) {
                    }
                    if (el && !FindAncestorByClassName(
                                  el,
                                  L"Microsoft.UI.Xaml.Controls.ItemsRepeater")) {
                        el = nullptr;
                    }
                    if (!el) {
                        el = PickLiveFlyoutThumbOnThisDispatcher();
                    }
                    if (!el) {
                        return;
                    }
                    g_thumbRelayoutDepth = 1;
                    g_previewFlyoutRefreshNeeded = false;
                    RefreshThumbnailFlyout_UIThread(el);
                    g_thumbRelayoutDepth = 0;
                    const bool again = g_previewFlyoutRefreshNeeded &&
                                       g_previewFlyoutRefreshFollowUps < 1;
                    g_previewFlyoutRefreshNeeded = false;
                    if (again) {
                        ++g_previewFlyoutRefreshFollowUps;
                        SchedulePreviewFlyoutRefresh(el);
                    } else {
                        g_previewFlyoutRefreshFollowUps = 0;
                    }
                } catch (...) {
                    g_thumbRelayoutDepth = 0;
                    g_previewFlyoutRefreshFollowUps = 0;
                }
            })) {
            g_previewFlyoutRefreshPending = false;
        }
    } catch (...) {
        g_previewFlyoutRefreshPending = false;
    }
}

