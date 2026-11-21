PCWSTR GlowStyleName(GlowStyle s) {
    return EnumToName(kGlowStyleNames, s);
}

PCWSTR PromoteModeName(PromoteMode m) {
    return EnumToName(kPromoteModeNames, m);
}

// True if this focus change may skip the app min-focus timer.
// Takes g_stateMutex itself; do not call with it held.
bool ShouldSkipAppMinFocus(const std::wstring& key, bool alreadyTracked) {
    auto settings = SettingsSnap();
    if (settings->minFocusSeconds <= 0) {
        return true;
    }
    switch (settings->promoteMode) {
        case PromoteMode::AlwaysWait:
            return false;
        case PromoteMode::ImmediateTopN: {
            if (!alreadyTracked) {
                return false;
            }
            std::lock_guard<std::mutex> lock(g_stateMutex);
            for (const auto& r : CurrentDeskLocked().rankedApps) {
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
    return EnumToName(kPreviewStyleNames, s);
}

PCWSTR TaskbarEdgeName(TaskbarEdge e) {
    return EnumToName(kTaskbarEdgeNames, e);
}

PCWSTR BarSideName(BarSide s) {
    return EnumToName(kBarSideNames, s);
}

bool IsInVisualState(FrameworkElement root, PCWSTR stateName) {
    if (!root || !stateName) {
        return false;
    }
    try {
        auto groups = VisualStateManager::GetVisualStateGroups(root);
        for (auto group : groups) {
            auto current = group.CurrentState();
            if (current && current.Name() == stateName) {
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

TaskbarEdge TaskbarEdgeFromAppBar() {
    APPBARDATA abd{};
    abd.cbSize = sizeof(APPBARDATA);
    if (!SHAppBarMessage(ABM_GETTASKBARPOS, &abd)) {
        return TaskbarEdge::Bottom;
    }
    switch (abd.uEdge) {
        case ABE_LEFT:
            return TaskbarEdge::Left;
        case ABE_TOP:
            return TaskbarEdge::Top;
        case ABE_RIGHT:
            return TaskbarEdge::Right;
        default:
            return TaskbarEdge::Bottom;
    }
}

// Prefer OrientationStates. A leftover RunningIndicator VA=Bottom from the
// previous edge must not win: we used to treat a left taskbar as Bottom and
// paint full-cell underlines of rank-dependent length.
// Do not use "wider than tall ⇒ horizontal": a left-edge button is 48×32.
TaskbarEdge DetectTaskbarEdge(FrameworkElement iconPanel) {
    bool verticalState = IsInVisualState(iconPanel, L"VerticalOrientation");
    bool horizontalState = IsInVisualState(iconPanel, L"HorizontalOrientation");
    try {
        auto parent = Media::VisualTreeHelper::GetParent(iconPanel)
                          .try_as<FrameworkElement>();
        if (parent) {
            verticalState =
                verticalState ||
                IsInVisualState(parent, L"VerticalOrientation");
            horizontalState =
                horizontalState ||
                IsInVisualState(parent, L"HorizontalOrientation");
        }
    } catch (...) {
    }

    HorizontalAlignment ha = HorizontalAlignment::Stretch;
    VerticalAlignment va = VerticalAlignment::Stretch;
    double riW = 0;
    double riH = 0;
    if (auto ri = FindRunningIndicator(iconPanel)) {
        try {
            ha = ri.HorizontalAlignment();
            va = ri.VerticalAlignment();
            riW = ri.ActualWidth();
            riH = ri.ActualHeight();
        } catch (...) {
        }
    }

    double pw = 0;
    double ph = 0;
    try {
        pw = iconPanel.ActualWidth();
        ph = iconPanel.ActualHeight();
    } catch (...) {
    }
    // Vertical bar: each button is short along the bar (≈32) and as wide as
    // the bar (≈48). Horizontal bar: roughly square / taller (≈44×48).
    const bool panelLooksVertical = pw > ph + 4.0;
    const bool panelLooksHorizontal = ph > pw + 4.0;
    const bool tallPill = riH > riW + 0.5 && riW > 0.5;

    auto pickVertical = [&]() -> TaskbarEdge {
        if (ha == HorizontalAlignment::Right) {
            return TaskbarEdge::Right;
        }
        if (ha == HorizontalAlignment::Left) {
            return TaskbarEdge::Left;
        }
        return TaskbarEdgeFromAppBar() == TaskbarEdge::Right
                   ? TaskbarEdge::Right
                   : TaskbarEdge::Left;
    };
    auto pickHorizontal = [&]() -> TaskbarEdge {
        if (va == VerticalAlignment::Top) {
            return TaskbarEdge::Top;
        }
        if (va == VerticalAlignment::Bottom) {
            return TaskbarEdge::Bottom;
        }
        return TaskbarEdgeFromAppBar() == TaskbarEdge::Top ? TaskbarEdge::Top
                                                           : TaskbarEdge::Bottom;
    };

    if (verticalState) {
        return pickVertical();
    }
    if (horizontalState) {
        return pickHorizontal();
    }
    if (panelLooksVertical || tallPill) {
        return pickVertical();
    }
    if (panelLooksHorizontal) {
        return pickHorizontal();
    }
    if (ha == HorizontalAlignment::Right) {
        return TaskbarEdge::Right;
    }
    if (ha == HorizontalAlignment::Left) {
        return TaskbarEdge::Left;
    }
    if (va == VerticalAlignment::Top) {
        return TaskbarEdge::Top;
    }
    if (va == VerticalAlignment::Bottom) {
        return TaskbarEdge::Bottom;
    }
    return TaskbarEdgeFromAppBar();
}

BarSide SideBarForTaskbarEdge(TaskbarEdge edge) {
    // Side bar: perpendicular to the taskbar so it does not cover the native
    // running pill (left on bottom/top, under the icon on left/right).
    switch (edge) {
        case TaskbarEdge::Left:
        case TaskbarEdge::Right:
            return BarSide::Bottom;
        case TaskbarEdge::Top:
        case TaskbarEdge::Bottom:
        default:
            return BarSide::Left;
    }
}

void GlowContentBoxSize(FrameworkElement host,
                        FrameworkElement iconPanel,
                        double panelW,
                        double panelH,
                        double& boxW,
                        double& boxH) {
    boxW = 0;
    boxH = 0;
    if (host) {
        try {
            boxW = host.ActualWidth();
            boxH = host.ActualHeight();
        } catch (...) {
        }
    }
    if (!(boxW > 1.0) || !(boxH > 1.0)) {
        if (auto bg = FindChildByName(iconPanel, kBackgroundElementName)) {
            try {
                boxW = bg.ActualWidth();
                boxH = bg.ActualHeight();
            } catch (...) {
            }
        }
    }
    if (!(boxW > 1.0)) {
        boxW = panelW;
    }
    if (!(boxH > 1.0)) {
        boxH = panelH;
    }
}

// Length follows the glow host (padded cell), same for every rank — rank is
// opacity. Icon-sized underlines on a left taskbar were too short to scan
// (Windows uses a very small glyph there). Size % still scales the bar.
double BarLengthForSide(double boxW,
                        double boxH,
                        BarSide side,
                        double sizeFrac) {
    const bool horizontalBar =
        side == BarSide::Top || side == BarSide::Bottom;
    const double cellAlong = horizontalBar ? boxW : boxH;
    double barLen = cellAlong * sizeFrac;
    const double maxLen = (std::max)(4.0, cellAlong - 2.0);
    if (barLen > maxLen) {
        barLen = maxLen;
    }
    if (barLen < 4.0) {
        barLen = 4.0;
    }
    return barLen;
}

void StyleGlowBarOnSide(Shapes::Rectangle rect,
                        const winrt::Windows::UI::Color& fill,
                        double thickness,
                        double length,
                        BarSide side,
                        double opacity) {
    const double t = thickness;
    const double len = length;
    const double edgePad =
        (side == BarSide::Bottom || side == BarSide::Top) ? 2.0 : 1.0;

    rect.Stroke(nullptr);
    rect.StrokeThickness(0);
    rect.Fill(Media::SolidColorBrush{fill});
    rect.RadiusX(t * 0.5);
    rect.RadiusY(t * 0.5);
    rect.Opacity(opacity);
    rect.IsHitTestVisible(false);
    rect.Visibility(Visibility::Visible);

    // Zero desired size, as the plates (ZeroDesiredMargin): the far margin
    // takes back the bar's thickness and its pad, the centred axis half the
    // length on each side. The placement is the same as before.
    const double back = -(t + edgePad);
    switch (side) {
        case BarSide::Left:
            rect.Width(t);
            rect.Height(len);
            rect.HorizontalAlignment(HorizontalAlignment::Left);
            rect.VerticalAlignment(VerticalAlignment::Center);
            rect.Margin(Thickness{edgePad, -len * 0.5, back, -len * 0.5});
            break;
        case BarSide::Right:
            rect.Width(t);
            rect.Height(len);
            rect.HorizontalAlignment(HorizontalAlignment::Right);
            rect.VerticalAlignment(VerticalAlignment::Center);
            rect.Margin(Thickness{back, -len * 0.5, edgePad, -len * 0.5});
            break;
        case BarSide::Top:
            rect.Width(len);
            rect.Height(t);
            rect.HorizontalAlignment(HorizontalAlignment::Center);
            rect.VerticalAlignment(VerticalAlignment::Top);
            rect.Margin(Thickness{-len * 0.5, edgePad, -len * 0.5, back});
            break;
        case BarSide::Bottom:
        default:
            rect.Width(len);
            rect.Height(t);
            rect.HorizontalAlignment(HorizontalAlignment::Center);
            rect.VerticalAlignment(VerticalAlignment::Bottom);
            rect.Margin(Thickness{-len * 0.5, back, -len * 0.5, edgePad});
            break;
    }
}

FrameworkElement TaskListButtonFromDescendant(FrameworkElement start) {
    FrameworkElement cur = start;
    for (int i = 0; i < 12 && cur; ++i) {
        try {
            if (cur.Name() == L"TaskListButton") {
                return cur;
            }
            cur = Media::VisualTreeHelper::GetParent(cur)
                      .try_as<FrameworkElement>();
        } catch (...) {
            return nullptr;
        }
    }
    return nullptr;
}

// Relayout: restore the host placement relative to the native pill / Styler plate.
// Never mutate native collection order or geometry.
void RepositionGlowAfterRelayout(FrameworkElement iconPanel) {
    if (!iconPanel) {
        return;
    }

    auto panel = iconPanel.try_as<Controls::Panel>();
    if (!panel) {
        return;
    }
    try {
        if (auto host = FindChildByName(iconPanel, kGlowElementName)) {
            EnsureGlowHostZOrder(panel, host);
        }
    } catch (...) {
    }
}

// Must run on the panel's dispatcher. Pulls only watches for this thread so
// a primary-taskbar cleanup cannot revoke a secondary bar's tokens.
void RevokeIconPanelLayoutWatchesOnThisDispatcher() {
    // Dispatcher() is XAML. Do not call it while holding the watch mutex.
    std::unordered_map<void*, IconPanelLayoutWatch> taken;
    {
        std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
        taken.swap(g_layoutWatches);
    }
    std::unordered_map<void*, IconPanelLayoutWatch> keep;
    std::vector<IconPanelLayoutWatch> mine;
    for (auto& [id, watch] : taken) {
        bool onThisThread = false;
        try {
            auto panel = watch.panel.get();
            if (!panel) {
                onThisThread = true;
            } else {
                auto dispatcher = panel.Dispatcher();
                onThisThread = dispatcher && dispatcher.HasThreadAccess();
            }
        } catch (...) {
            onThisThread = true;
        }
        if (onThisThread) {
            mine.push_back(std::move(watch));
        } else {
            keep.emplace(id, std::move(watch));
        }
    }
    if (!keep.empty()) {
        std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
        for (auto& [id, watch] : keep) {
            auto panelRef = watch.panel;
            auto token = watch.sizeChanged;
            if (!g_layoutWatches.emplace(id, std::move(watch)).second) {
                try {
                    if (auto panel = panelRef.get()) {
                        if (token) {
                            panel.SizeChanged(token);
                        }
                    }
                } catch (...) {
                    Wh_Log(L"ERROR: SizeChanged revoke failed");
                }
            }
        }
    }
    for (auto& w : mine) {
        try {
            if (auto panel = w.panel.get()) {
                if (w.sizeChanged) {
                    panel.SizeChanged(w.sizeChanged);
                    w.sizeChanged = {};
                }
            }
        } catch (...) {
            Wh_Log(L"ERROR: SizeChanged revoke failed");
        }
    }
}

TaskbarEdge CachedTaskbarEdge(FrameworkElement iconPanel) {
    void* id = InspectableIdentity(iconPanel);
    {
        std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
        auto it = id ? g_layoutWatches.find(id) : g_layoutWatches.end();
        if (it != g_layoutWatches.end()) {
            if (!WeakIsSameElement(it->second.panel, iconPanel)) {
                g_layoutWatches.erase(it);
            } else if (it->second.haveEdge) {
                return it->second.lastEdge;
            }
        }
    }
    const TaskbarEdge edge = DetectTaskbarEdge(iconPanel);
    if (id) {
        std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
        auto it = g_layoutWatches.find(id);
        if (it != g_layoutWatches.end() &&
            WeakIsSameElement(it->second.panel, iconPanel)) {
            it->second.lastEdge = edge;
            it->second.haveEdge = true;
        }
    }
    return edge;
}

// Stores the edge for this panel's watch. The edge belongs to the taskbar,
// not the button, so a full pass detects it once and passes it in.
void RefreshCachedTaskbarEdge(FrameworkElement iconPanel,
                              std::optional<TaskbarEdge>& passEdge) {
    if (!iconPanel) {
        return;
    }
    if (!passEdge) {
        passEdge = DetectTaskbarEdge(iconPanel);
    }
    const TaskbarEdge edge = *passEdge;
    void* id = InspectableIdentity(iconPanel);
    if (!id) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
    auto it = g_layoutWatches.find(id);
    if (it != g_layoutWatches.end() &&
        WeakIsSameElement(it->second.panel, iconPanel)) {
        it->second.lastEdge = edge;
        it->second.haveEdge = true;
    }
}

void EnsureIconPanelLayoutWatch(FrameworkElement button) {
    if (!button || g_unloading.load()) {
        return;
    }
    auto iconPanel = GetIconPanel(button);
    if (!iconPanel) {
        return;
    }
    if (!RememberUiDispatcher(iconPanel)) {
        return;
    }

    void* id = InspectableIdentity(iconPanel);
    if (!id) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
        auto it = g_layoutWatches.find(id);
        if (it != g_layoutWatches.end()) {
            if (WeakIsSameElement(it->second.panel, iconPanel)) {
                return;
            }
            g_layoutWatches.erase(it);
        }
    }

    const TaskbarEdge edge = DetectTaskbarEdge(iconPanel);

    IconPanelLayoutWatch watch;
    watch.panel = winrt::make_weak(iconPanel);
    watch.lastEdge = edge;
    watch.haveEdge = true;
    winrt::weak_ref<FrameworkElement> weakPanel = watch.panel;
    try {
        watch.sizeChanged = iconPanel.SizeChanged(
            [weakPanel](winrt::Windows::Foundation::IInspectable const&,
                        SizeChangedEventArgs const& args) {
                if (g_unloading.load()) {
                    return;
                }
                if (g_iconPanelRelayoutDepth > 0) {
                    return;
                }
                FrameworkElement panel = nullptr;
                try {
                    panel = weakPanel.get();
                } catch (...) {
                    return;
                }
                if (!panel) {
                    return;
                }
                const double nw = args.NewSize().Width;
                const double nh = args.NewSize().Height;
                const double ow = args.PreviousSize().Width;
                const double oh = args.PreviousSize().Height;
                if (!(nw > 1.0 && nh > 1.0)) {
                    return;
                }
                const bool relayout =
                    ow > 1.0 && oh > 1.0 &&
                    ((nw > ow + 1.5 || ow > nw + 1.5) ||
                     (nh > oh + 1.5 || oh > nh + 1.5));
                const bool firstMeasure = !(ow > 1.0 && oh > 1.0);
                if (!relayout && !firstMeasure) {
                    return;
                }
                if (relayout) {
                    Wh_Log(L"IconPanel relayout: %.0fx%.0f -> %.0fx%.0f", ow,
                           oh, nw, nh);
                }
                const TaskbarEdge edgeNow = DetectTaskbarEdge(panel);
                if (void* pid = InspectableIdentity(panel)) {
                    std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
                    auto it = g_layoutWatches.find(pid);
                    if (it != g_layoutWatches.end()) {
                        it->second.lastEdge = edgeNow;
                        it->second.haveEdge = true;
                    }
                }
                ++g_iconPanelRelayoutDepth;
                try {
                    RepositionGlowAfterRelayout(panel);
                    if (auto btn = TaskListButtonFromDescendant(panel)) {
                        RefreshButtonHighlight(btn);
                        ScheduleRefreshAllHighlights(btn);
                    }
                } catch (...) {
                }
                --g_iconPanelRelayoutDepth;
            });
    } catch (...) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_layoutWatchMutex);
    for (auto it = g_layoutWatches.begin(); it != g_layoutWatches.end();) {
        try {
            if (!it->second.panel.get()) {
                it = g_layoutWatches.erase(it);
                continue;
            }
        } catch (...) {
            it = g_layoutWatches.erase(it);
            continue;
        }
        ++it;
    }
    g_layoutWatches[id] = std::move(watch);
}

