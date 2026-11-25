void ApplyThumbnailHighlight(FrameworkElement thumbView, int rankOneBased) {
    auto settings = SettingsSnap();
    if (!thumbView || rankOneBased <= 0 || g_unloading.load() ||
        !settings->previewHighlightEnabled) {
        ClearThumbnailHighlight(thumbView);
        return;
    }

    try {
        // Start clean so style switches don't leave mixed chrome. Keep the
        // overlay host — re-parsing the markup on every ranked paint is wasted.
        ResetThumbnailOverlayForRepaint(thumbView);

        auto panel = GetThumbnailHostPanel(thumbView);
        if (!panel) {
            return;
        }
        auto panelFe = panel.as<FrameworkElement>();

        const int rankIdx = rankOneBased - 1;
        const int intensity = RankIntensity(settings->previewIntensity, rankIdx);
        const double t = intensity / 100.0;
        winrt::Windows::UI::Color base = ResolveGlowBaseColor(*settings);
        auto withAlpha = [](winrt::Windows::UI::Color c, int a) {
            c.A = static_cast<uint8_t>((std::max)(0, (std::min)(255, a)));
            return c;
        };

        const double thickness = static_cast<double>(
            (std::max)(2, (std::min)(8, settings->glowThickness)));
        // Fallback plate (card without BackgroundBorder) corner roundness:
        // the former icons.glowRoundness default (setting removed in 0.11.38).
        constexpr double roundnessFrac = 0.28;
        // Preview plate / titleBg use dedicated opacity (not taskbar fill).
        const int fillOpacitySetting =
            (std::max)(0, (std::min)(100, settings->previewFillOpacity));
        const PreviewStyle style = rankOneBased <= 1
                                       ? settings->previewTopStyle
                                       : settings->previewRestStyle;
        if (t <= 0.0 || style == PreviewStyle::None) {
            ClearThumbnailHighlight(thumbView);
            return;
        }
        PreviewStyle paintStyle = style;

        // Tint base. An accent style halves it so its same-colour border
        // still reads (0.11.38). One tint level per flyout (0.12.1): with an
        // accent style at rank 1, ranks 2+ in a non-accent style are halved
        // too. Before, a rank-2 Title plate (Tint 40, intensity 85: 34 %)
        // was tinted more than the rank-1 Whole plate with accent (20 %),
        // against the ladder; the accent styles themselves are unchanged.
        auto isAccent = [](PreviewStyle s) {
            return s == PreviewStyle::Outline ||
                   s == PreviewStyle::TitleOutline;
        };
        const bool halfTint =
            isAccent(style) ||
            (rankOneBased > 1 && isAccent(settings->previewTopStyle));
        const int tintSetting =
            halfTint ? fillOpacitySetting / 2 : fillOpacitySetting;

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
            SchedulePreviewFlyoutRefresh(thumbView);
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

        // Plate / Outline: our own Border above BackgroundBorder (see
        // PaintCardOverlay). Zero tint and no outline leaves the card alone.
        if (paintStyle == PreviewStyle::Plate ||
            paintStyle == PreviewStyle::Outline) {
            const bool outline = paintStyle == PreviewStyle::Outline;
            const int fillA =
                static_cast<int>(tintSetting * 2.55 * t + 0.5);
            const int strokeA = static_cast<int>(255 * t + 0.5);
            // painted: card overlay drawn, or nothing to draw (zero tint).
            bool painted = !outline && tintSetting <= 0;
            if (!painted) {
                try {
                    painted = PaintCardOverlay(
                        thumbView, withAlpha(base, fillA),
                        withAlpha(base, strokeA),
                        outline ? settings->previewOutlineThickness : 0.0);
                } catch (...) {
                    painted = false;
                }
            }
            if (!painted) {
                Controls::Grid host = EnsureThumbOverlayHost(panel);
                HideThumbOverlayChildren(host);
                if (auto plate = FindChildByName(host, kThumbGlowLayerNames[0])
                                     .try_as<Shapes::Rectangle>()) {
                    const int plateA =
                        static_cast<int>(tintSetting * 2.55 + 0.5);
                    plate.Fill(Media::SolidColorBrush{withAlpha(base, plateA)});
                    plate.Stroke(nullptr);
                    plate.StrokeThickness(0);
                    plate.RadiusX(cardW * roundnessFrac * 0.12);
                    plate.RadiusY(cardH * roundnessFrac * 0.12);
                    plate.Opacity(t);
                    PlaceOverlayChild(plate, 0, 0, cardW, cardH);
                }
            }

            Wh_Log(L"Preview glow rank %d style=%s paint=%s intensity=%d "
                   L"on \"%s\"",
                   rankOneBased, PreviewStyleName(style),
                   PreviewStyleName(paintStyle), intensity,
                   Automation::AutomationProperties::GetName(thumbView)
                       .c_str());
            return;
        }

        Controls::Grid host = EnsureThumbOverlayHost(panel);
        HideThumbOverlayChildren(host);

        if (paintStyle == PreviewStyle::TitleBg ||
            paintStyle == PreviewStyle::TitleOutline) {
            // Title / title with accent (0.12.1): a plate over the title row,
            // 4px in from the card's BackgroundBorder (left, top, right) with
            // the card's corner radius, ending under the title with the gap
            // above the title mirrored below it. Before 0.12.1 the title tint
            // sat 8px in from the panel with 4px corners and uneven gaps
            // (thin tint left of the icon). The accent variant adds an
            // accent border and halves the tint, as Outline does for the card.
            const bool outline = paintStyle == PreviewStyle::TitleOutline;
            const double stroke =
                outline ? settings->previewOutlineThickness : 0.0;
            constexpr double kEdgeInset = 4.0;
            double top = 4.0;
            double stripH = 28.0;
            double titleTop = 0, titleBottom = 0;
            bool laidOut = GetTitleBottomRelative(title, host, cardH, titleTop,
                                                  titleBottom);
            if (laidOut) {
                // Fallback strip (no BackgroundBorder). An outline is drawn
                // inside: grow by its width so the gap around the title stays.
                top = (std::max)(2.0, titleTop - 3.0 - stroke);
                stripH = (std::max)(20.0, titleBottom - top + 4.0 + stroke);
            } else {
                // Layout not ready — default strip + one deferred remeasure.
                SchedulePreviewFlyoutRefresh(thumbView);
            }
            auto chip =
                FindChildByName(host, kThumbTitleBgName).try_as<Controls::Border>();
            if (chip) {
                const double pad = 8.0;
                double x = pad;
                double w = (std::max)(24.0, cardW - 2.0 * pad);
                // Brace-init {4} only fills TopLeft (C++ struct). All four.
                CornerRadius radius{4, 4, 4, 4};
                bool fromCard = false;
                if (laidOut) {
                    try {
                        if (auto bb = FindThumbnailBackgroundBorder(thumbView)) {
                            const double bw = bb.ActualWidth();
                            auto origin =
                                bb.TransformToVisual(host).TransformPoint(
                                    winrt::Windows::Foundation::Point{0.f,
                                                                      0.f});
                            const double edgeTop = origin.Y + kEdgeInset;
                            // Gap below the title = gap above it, at most
                            // 4px + border.
                            const double gap = (std::min)(
                                titleTop - edgeTop, 8.0 - kEdgeInset + stroke);
                            if (bw > 2.0 * kEdgeInset + 24.0 && gap >= 0.0) {
                                x = origin.X + kEdgeInset;
                                w = bw - 2.0 * kEdgeInset;
                                top = edgeTop;
                                stripH = (std::max)(20.0,
                                                    titleBottom + gap - top);
                                // The card's own radius: radius - inset
                                // (concentric) looked too sharp.
                                if (auto b = bb.try_as<Controls::Border>()) {
                                    radius = b.CornerRadius();
                                }
                                fromCard = true;
                            }
                        }
                    } catch (...) {
                    }
                }
                if (!fromCard) {
                    const double bottom = top + stripH;
                    top = (std::max)(6.0, top);
                    if (outline) {
                        // Keep the bottom edge where it was, or the outline
                        // crowds the thumbnail when the top is pushed down.
                        stripH = (std::max)(20.0, bottom - top);
                    }
                }
                const int chipA =
                    static_cast<int>(tintSetting * 2.55 * t + 0.5);
                chip.Background(Media::SolidColorBrush{withAlpha(base, chipA)});
                if (outline) {
                    const int strokeA = static_cast<int>(255 * t + 0.5);
                    chip.BorderBrush(
                        Media::SolidColorBrush{withAlpha(base, strokeA)});
                    chip.BorderThickness(
                        Thickness{stroke, stroke, stroke, stroke});
                }
                chip.CornerRadius(radius);
                chip.Opacity(1.0);
                PlaceOverlayChild(chip, x, top, w, stripH);
            }
        } else {  // TitleBar — underline just under the title glyphs
            // Small gap under baseline — enough to avoid strikethrough, not
            // so much that the bar hugs the thumbnail image.
            constexpr double kGapBelowTitle = 2.0;
            double top = 30.0;  // fallback under a ~28px header
            double titleTop = 0, titleBottom = 0;
            bool laidOut = GetTitleBottomRelative(title, host, cardH, titleTop,
                                                  titleBottom);
            if (laidOut) {
                top = titleBottom + kGapBelowTitle;
            } else {
                SchedulePreviewFlyoutRefresh(thumbView);
            }
            top = (std::max)(18.0, (std::min)(top, cardH * 0.38));

            const double barH =
                (std::max)(2.0, (std::min)(5.0, thickness + 0.5));

            auto bar = FindChildByName(host, kThumbTitleBarName)
                           .try_as<Shapes::Rectangle>();
            if (bar) {
                // Wider alpha range than the old rank-1-only bar so flyout
                // ranks read as a ladder; t=1 stays fully opaque accent.
                // Intensity is Opacity only (do not also scale brush alpha).
                const double pad = 8.0;
                bar.Fill(Media::SolidColorBrush{withAlpha(base, 255)});
                bar.Stroke(nullptr);
                bar.StrokeThickness(0);
                bar.RadiusX(barH * 0.5);
                bar.RadiusY(barH * 0.5);
                bar.Opacity(t);
                PlaceOverlayChild(bar, pad, top,
                                  (std::max)(24.0, cardW - 2.0 * pad), barH);
            }
        }

        Wh_Log(L"Preview glow rank %d style=%s paint=%s intensity=%d "
               L"card=%.0fx%.0f on \"%s\"",
               rankOneBased, PreviewStyleName(style),
               PreviewStyleName(paintStyle), intensity, cardW, cardH,
               Automation::AutomationProperties::GetName(thumbView)
                   .c_str());
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

    auto repeaterViews = CollectRepeaterThumbnailViews(anyThumb);
    const bool usedRepeater = !repeaterViews.empty();
    auto allViews =
        usedRepeater ? std::move(repeaterViews)
                     : CollectSiblingThumbnailViews(anyThumb);
    if (!SettingsSnap()->previewHighlightEnabled || g_unloading.load()) {
        for (auto& s : allViews) {
            ClearThumbnailHighlight(s);
        }
        return;
    }

    // Snap-group cards sit in the same ItemsRepeater as the windows.
    // Never glow those; they are not a window HWND. Compute once — the
    // lookup walks IconsRepeater and resolves the card's view model.
    std::vector<char> isSnap(allViews.size(), 0);
    for (size_t i = 0; i < allViews.size(); ++i) {
        if (IsSnapGroupThumbnailView(allViews[i])) {
            isSnap[i] = 1;
        }
    }
    std::vector<FrameworkElement> siblings;
    siblings.reserve(allViews.size());
    for (size_t i = 0; i < allViews.size(); ++i) {
        if (isSnap[i]) {
            ClearThumbnailHighlight(allViews[i]);
        } else {
            siblings.push_back(allViews[i]);
        }
    }

    // Each flyout ranks its own cards (below). A single-window flyout is
    // ranked the same way only with previews.singleWindow: its one card is
    // rank 1 while its window is a confirmed recent window (preview min-focus
    // and decay), like the latest window of a multi-window flyout.
    if (siblings.empty() ||
        (siblings.size() == 1 && !SettingsSnap()->previewSingleWindow)) {
        for (auto& s : siblings) {
            ClearThumbnailHighlight(s);
        }
        return;
    }

    // HWND per card (this flyout only — not a global window ladder): the
    // card's own view model, ParentItem.WindowId (Explorer's binding). Per
    // card; cannot shift when cards are removed or recycled. A card without
    // one stays unmarked. (Before 0.11.19 also a DataContext/ctor-map pass and
    // a repeater-index + captured Thumbnails pass; the first never matched
    // on 26100-era builds, the second shifted after a card was closed.)
    struct Scored {
        FrameworkElement view{nullptr};
        HWND hwnd = nullptr;
        ULONGLONG tick = 0;
        ULONGLONG confirmSeq = 0;
        int rank = 0;  // 1-based flyout rank; 0 = not highlighted
        CardResolveFail fail = CardResolveFail::None;
        UINT64 rawWindowId = 0;
    };
    std::vector<Scored> scored(siblings.size());

    // Card view model -> ParentItem.WindowId (no lock held).
    for (size_t i = 0; i < siblings.size(); ++i) {
        TrackThumbView_UIThread(siblings[i]);
        scored[i].view = siblings[i];
        scored[i].hwnd = ResolveHwndFromCardViewModel(
            siblings[i], &scored[i].fail, &scored[i].rawWindowId);
    }

    // Recency for every card in one hold.
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        auto& desk = CurrentDeskLocked();
        for (auto& sc : scored) {
            if (!sc.hwnd) {
                continue;
            }
            IsWindowRecentForPreviewLocked(desk, sc.hwnd, &sc.tick);
            auto it = desk.windowFocusMap.find(sc.hwnd);
            if (it != desk.windowFocusMap.end()) {
                sc.confirmSeq = it->second.confirmSeq;
            }
        }
    }

    // Rank this flyout only (not a global window ladder). Sort siblings that
    // have a recency tick; ties: confirmSeq, then the live foreground HWND
    // (normalized: an owned dialog stands for its owner's card).
    HWND foreground = NormalizeFocusHwnd(GetForegroundWindow());
    std::vector<size_t> order;
    order.reserve(scored.size());
    for (size_t i = 0; i < scored.size(); ++i) {
        if (scored[i].hwnd && scored[i].tick > 0) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (scored[a].tick != scored[b].tick) {
            return scored[a].tick > scored[b].tick;
        }
        if (scored[a].confirmSeq != scored[b].confirmSeq) {
            return scored[a].confirmSeq > scored[b].confirmSeq;
        }
        const bool aFg = scored[a].hwnd == foreground;
        const bool bFg = scored[b].hwnd == foreground;
        if (aFg != bFg) {
            return aFg;
        }
        return a < b;
    });

    const int limit = (std::max)(0, SettingsSnap()->previewHighlightCount);
    for (size_t r = 0; r < order.size() && static_cast<int>(r) < limit; ++r) {
        scored[order[r]].rank = static_cast<int>(r) + 1;
    }

    {
        const size_t painted =
            (std::min)(order.size(), static_cast<size_t>(limit));
        // Siblings are listed only when the set, a rank or a resolve failure
        // differs from this UI thread's last resolve: reopening the same
        // flyout repeated about 2,100 sibling lines in a day (capture
        // manual-20261120-154409-086197).
        std::wstring sig = std::to_wstring(limit);
        for (const auto& s : scored) {
            sig += L"|" + std::to_wstring(reinterpret_cast<uintptr_t>(s.hwnd)) +
                   L"," + std::to_wstring(s.rank) + L"," +
                   std::to_wstring(static_cast<int>(s.fail));
        }
        thread_local std::wstring t_lastResolveLog;
        const bool changed = sig != t_lastResolveLog;
        if (changed) {
            t_lastResolveLog = sig;
        }
        Wh_Log(L"Preview resolve: %zu siblings, highlightCount=%d "
               L"candidates=%zu painted=%zu%s",
               siblings.size(), limit, order.size(), painted,
               changed ? L"" : L" (unchanged)");
        for (size_t i = 0; changed && i < scored.size(); ++i) {
            std::wstring name;
            try {
                name = Automation::AutomationProperties::GetName(scored[i].view)
                           .c_str();
            } catch (...) {
            }
            Wh_Log(L"  sibling[%zu]: hwnd=%p tick=%llu rank=%d%s name=\"%s\"",
                   i, scored[i].hwnd,
                   static_cast<unsigned long long>(scored[i].tick),
                   scored[i].rank,
                   scored[i].rank > 0 ? L" [GLOW]" : L"", name.c_str());
            if (scored[i].fail != CardResolveFail::None) {
                HWND raw = reinterpret_cast<HWND>(scored[i].rawWindowId);
                wchar_t cls[96] = L"";
                DWORD pid = 0;
                if (raw && IsWindow(raw)) {
                    GetClassNameW(raw, cls, ARRAYSIZE(cls));
                    GetWindowThreadProcessId(raw, &pid);
                }
                Wh_Log(L"    unresolved: %s windowId=0x%llx class=%s pid=%u",
                       CardResolveFailName(scored[i].fail),
                       static_cast<unsigned long long>(scored[i].rawWindowId),
                       cls, pid);
            }
        }
    }

    for (size_t i = 0; i < scored.size(); ++i) {
        if (scored[i].rank > 0) {
            ApplyThumbnailHighlight(scored[i].view, scored[i].rank);
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
    ForEachLiveElementOnThisDispatcher(
        thumbs, [](FrameworkElement el) { ClearThumbnailHighlight(el); });
}

void RequestApplyPreviewVisuals() {
    if (g_unloading.load()) {
        return;
    }
    // Same as RequestApplyVisuals: if no dispatcher yet, wait for the first
    // TaskListButton / thumbnail OnApplyTemplate. Do not PostMessage to this
    // focus thread — the WM_APP_REQUEST_PREVIEW_APPLY handler used to call
    // this again, which spun at 100% CPU and starved WM_TIMER.
    RunOnUiThread([]() {
        FrameworkElement el = PickLiveFlyoutThumbOnThisDispatcher();
        if (el) {
            SchedulePreviewFlyoutRefresh(el);
        }
    });
}

// Expected at startup before any TaskListButton is seen — log once, not per call.
std::atomic<bool> g_loggedNoDispatcherAnchor{false};

