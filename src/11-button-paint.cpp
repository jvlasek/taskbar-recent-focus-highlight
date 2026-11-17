void ApplyButtonHighlight(FrameworkElement button, int rankOneBased) {
    if (!button) {
        return;
    }

    auto settings = SettingsSnap();
    if (rankOneBased <= 0 || g_unloading.load()) {
        ClearButtonHighlight(button);
        return;
    }

    try {
        auto iconPanel = GetIconPanel(button);
        if (!iconPanel) {
            return;
        }
        EnsureIconPanelLayoutWatch(button);

        auto panel = iconPanel.try_as<Controls::Panel>();
        if (!panel) {
            return;
        }

        double panelW = iconPanel.ActualWidth();
        double panelH = iconPanel.ActualHeight();
        if (!SafeGlowMetric(panelW) || !(panelW > 1.0)) {
            panelW = 44.0;
        }
        if (!SafeGlowMetric(panelH) || !(panelH > 1.0)) {
            panelH = 44.0;
        }
        const TaskbarEdge edge = CachedTaskbarEdge(iconPanel);
        auto existingHost = FindChildByName(iconPanel, kGlowElementName);
        double keyW = panelW;
        double keyH = panelH;
        GlowContentBoxSize(existingHost, iconPanel, panelW, panelH, keyW, keyH);
        if (!SafeGlowBounds(0, 0, keyW, keyH)) { keyW = panelW; keyH = panelH; }
        const int boxWi = static_cast<int>(keyW + 0.5);
        const int boxHi = static_cast<int>(keyH + 0.5);
        {
            auto painted = GetCachedPaintState(button);
            if (settings->glowStyle == GlowStyle::LeftBar &&
                painted.rank == rankOneBased &&
                painted.settingsGen == settings->generation &&
                painted.accent ==
                    g_cachedAccent.load(std::memory_order_relaxed) &&
                painted.edge == edge && painted.boxW == boxWi &&
                painted.boxH == boxHi && ButtonHasOurChrome(button)) {
                EnsureGlowHostZOrder(panel, existingHost, settings->glowStyle);
                return;
            }
        }

        const int rankIdx = rankOneBased - 1;
        const int intensity = RankIntensity(settings->glowIntensity, rankIdx);
        const double t = intensity / 100.0;

        winrt::Windows::UI::Color base = ResolveGlowBaseColor(*settings);

        auto withAlpha = [](winrt::Windows::UI::Color c,
                            int a) -> winrt::Windows::UI::Color {
            c.A = static_cast<uint8_t>((std::max)(0, (std::min)(255, a)));
            return c;
        };

        const int layers = kGlowMaxLayers;  // main bar + soft outer glow
        const double thickness = static_cast<double>(
            (std::max)(1, (std::min)(16, settings->glowThickness)));
        const double sizeFrac =
            (std::max)(40, (std::min)(100, settings->glowSize)) / 100.0;
        const GlowStyle style = settings->glowStyle;
        const int fillOpacitySetting =
            (std::max)(0, (std::min)(100, settings->glowFillOpacity));

        Controls::Grid host = EnsureGlowHost(panel, iconPanel);
        if (!host) {
            return;
        }

        double boxW = panelW;
        double boxH = panelH;
        GlowContentBoxSize(host, iconPanel, panelW, panelH, boxW, boxH);

        const BarSide barSide = SideBarForTaskbarEdge(edge);

        // Position only our host; native children keep their relative order.
        EnsureGlowHostZOrder(panel, host, style);

        HideAllGlowLayers(host);

        if (t <= 0.0) {
            SetCachedPaintState(button, rankOneBased, settings->generation,
                                edge, boxWi, boxHi);
            return;
        }

        if (style == GlowStyle::Frame || style == GlowStyle::Full) {
            PaintNativeContour(host, iconPanel, style, base, thickness, sizeFrac,
                               t, fillOpacitySetting);
        } else if (style == GlowStyle::LeftBar) {
            // Side bar: left of the icon on a bottom/top taskbar, under the
            // icon on a left/right taskbar — never the native-pill edge.
            const double barT = thickness + 1.0;
            const double barLen =
                BarLengthForSide(boxW, boxH, barSide, sizeFrac);
            const int fillBase =
                static_cast<int>(fillOpacitySetting * 2.55 + 0.5);

            for (int i = 0; i < layers; ++i) {
                auto rect = FindChildByName(host, kGlowLayerNames[i])
                                .try_as<Shapes::Rectangle>();
                if (!rect) {
                    continue;
                }
                const int fillA =
                    i == 0 ? fillBase : static_cast<int>(fillBase * 0.35);
                const double opacity = i == 0 ? t : (0.45 * t);
                StyleGlowBarOnSide(rect, withAlpha(base, fillA), barT, barLen,
                                   barSide, i, opacity);
            }
        }

        Wh_Log(L"Glow rank %d \"%s\" style=%s edge=%s bar=%s box=%.0fx%.0f "
               L"th=%.0f size=%d%% intensity=%d",
               rankOneBased, GetButtonAutomationName(button).c_str(),
               GlowStyleName(style), TaskbarEdgeName(edge),
               BarSideName(barSide), boxW, boxH, thickness,
               settings->glowSize, intensity);
        SetCachedPaintState(button, rankOneBased, settings->generation, edge,
                            boxWi, boxHi);
    } catch (...) {
        HRESULT hr = winrt::to_hresult();
        Wh_Log(L"ApplyButtonHighlight error %08X", hr);
    }
}

void PruneTrackedButtons_UIThread() {
    std::lock_guard<std::mutex> lock(g_buttonsMutex);
    for (auto it = g_trackedButtons.begin(); it != g_trackedButtons.end();) {
        try {
            if (!it->second.get()) {
                it = g_trackedButtons.erase(it);
            } else {
                ++it;
            }
        } catch (...) {
            it = g_trackedButtons.erase(it);
        }
    }
}

// A destroyed TaskListButton often never reports IsRunning=false.
// AppAppearsOnTaskbar runs off this thread and must not weak.get().
void PruneDeadButtonPathCache_UIThread() {
    std::lock_guard<std::mutex> lock(g_buttonPathMutex);
    for (auto it = g_buttonPathCache.begin(); it != g_buttonPathCache.end();) {
        try {
            if (!it->second.button.get()) {
                it = g_buttonPathCache.erase(it);
            } else {
                ++it;
            }
        } catch (...) {
            it = g_buttonPathCache.erase(it);
        }
    }
}

void TrackButton_UIThread(FrameworkElement button) {
    if (!button) {
        return;
    }

    // Hover-hot (every UpdateVisualStates): a thread has one dispatcher, and
    // a known button keeps its weak ref unless Explorer reused the address.
    static thread_local bool dispatcherRemembered = false;
    if (!dispatcherRemembered) {
        dispatcherRemembered = RememberUiDispatcher(button);
    }
    if (void* id = InspectableIdentity(button)) {
        std::lock_guard<std::mutex> lock(g_buttonsMutex);
        auto it = g_trackedButtons.find(id);
        if (it == g_trackedButtons.end()) {
            g_trackedButtons.emplace(id, winrt::make_weak(button));
        } else if (!WeakIsSameElement(it->second, button)) {
            it->second = winrt::make_weak(button);
        }
    }
}

