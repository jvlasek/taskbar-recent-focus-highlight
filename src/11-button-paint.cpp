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
                EnsureGlowHostZOrder(panel, existingHost);
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
        EnsureGlowHostZOrder(panel, host);

        HideAllGlowLayers(host);

        if (t <= 0.0) {
            SetCachedPaintState(button, rankOneBased, settings->generation,
                                edge, boxWi, boxHi);
            return;
        }

        if (style == GlowStyle::Plate || style == GlowStyle::PlateAccent) {
            PaintNativeContour(host, iconPanel, style, base,
                               settings->glowAccentThickness, t,
                               fillOpacitySetting);
        } else {
            // Side bar: left of the icon on a bottom/top taskbar, under the
            // icon on a left/right taskbar — never the native-pill edge.
            const double barT = thickness + 1.0;
            const double barLen =
                BarLengthForSide(boxW, boxH, barSide, sizeFrac);
            const int fillA =
                static_cast<int>(fillOpacitySetting * 2.55 + 0.5);
            if (auto rect = FindChildByName(host, kGlowBarName)
                                .try_as<Shapes::Rectangle>()) {
                StyleGlowBarOnSide(rect, withAlpha(base, fillA), barT, barLen,
                                   barSide, t);
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
// Weak refs are resolved outside g_buttonStateMutex: resolving touches every
// cached XAML object, and after an idle period (pages trimmed) that once held
// the lock for 14 ms while the worker may need it (perf run 2026-11-17).
void PruneDeadButtonStates_UIThread() {
    std::vector<std::pair<void*, winrt::weak_ref<FrameworkElement>>> entries;
    {
        std::lock_guard<std::mutex> lock(g_buttonStateMutex);
        entries.reserve(g_buttonStates.size());
        for (const auto& [id, e] : g_buttonStates) {
            entries.emplace_back(id, e.button);
        }
    }
    std::vector<void*> dead;
    for (const auto& [id, weak] : entries) {
        bool alive = false;
        try {
            alive = static_cast<bool>(weak.get());
        } catch (...) {
        }
        if (!alive) {
            dead.push_back(id);
        }
    }
    if (dead.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_buttonStateMutex);
    for (void* id : dead) {
        auto it = g_buttonStates.find(id);
        if (it == g_buttonStates.end()) {
            continue;
        }
        bool alive = false;
        try {
            alive = static_cast<bool>(it->second.button.get());
        } catch (...) {
        }
        if (!alive) {  // re-check: the slot may have been reused meanwhile
            g_buttonStates.erase(it);
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

