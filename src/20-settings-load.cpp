// ---------------------------------------------------------------------------
// Settings load
// ---------------------------------------------------------------------------

// Settings transition 0.11 -> 0.12 (remove a release or two after 0.12.0).
// 0.11.1 had one preview style for all ranks, `previews.style`
// (plateTitle = default hybrid, plate, titleBg, titleBar). 0.11.16 split it
// into `previews.topStyle` + `previews.restStyle`. Windhawk keeps the old
// stored value and has no migration mechanism (m417z, Discord 2026-11-16), so
// a user who had picked a non-default style would silently get the new
// defaults. When both new keys are unset or still at their defaults, use the
// old choice for both ranks until the user first saves settings: from then on
// the settings page is what they see. A mod cannot write its own settings, so
// the state lives in mod storage: 0 = not checked yet, 1 = legacy style in
// use, 2 = done. Older dev-only values (e.g. "ring") and the old default map
// to nothing.
constexpr wchar_t kLegacyPreviewStyleState[] = L"legacyPreviewStyleState";

bool LegacyPreviewStyle(PreviewStyle& out) {
    auto legacy = WindhawkUtils::StringSetting::make(L"previews.style");
    const PCWSTR v = legacy.get();
    if (wcscmp(v, L"plate") == 0) {
        out = PreviewStyle::Plate;
    } else if (wcscmp(v, L"titleBg") == 0) {
        out = PreviewStyle::TitleBg;
    } else if (wcscmp(v, L"titleBar") == 0) {
        out = PreviewStyle::TitleBar;
    } else {
        return false;
    }
    return true;
}

void ApplyLegacyPreviewStyle(Settings& s, bool userSavedSettings) {
    int state = Wh_GetIntValue(kLegacyPreviewStyleState, 0);
    if (state == 2) {
        return;
    }
    if (userSavedSettings) {
        if (state == 1) {
            Wh_Log(L"Settings: legacy previews.style no longer applied "
                   L"(settings saved)");
        }
        Wh_SetIntValue(kLegacyPreviewStyleState, 2);
        return;
    }
    PreviewStyle legacy = PreviewStyle::Plate;
    const bool haveLegacy = LegacyPreviewStyle(legacy);
    if (state == 0) {
        auto top = WindhawkUtils::StringSetting::make(L"previews.topStyle");
        auto rest = WindhawkUtils::StringSetting::make(L"previews.restStyle");
        const bool newAtDefaults =
            (!*top.get() || wcscmp(top.get(), L"plate") == 0) &&
            (!*rest.get() || wcscmp(rest.get(), L"titleBg") == 0);
        state = (haveLegacy && newAtDefaults) ? 1 : 2;
        Wh_SetIntValue(kLegacyPreviewStyleState, state);
    }
    if (state == 1 && haveLegacy) {
        s.previewTopStyle = legacy;
        s.previewRestStyle = legacy;
        Wh_Log(L"Settings: using legacy previews.style=%s for all ranks until "
               L"settings are saved",
               PreviewStyleName(legacy));
    }
}

void LoadSettings(bool userSavedSettings) {
    Settings s;

    s.highlightCount = Wh_GetIntSetting(L"highlightCount");
    if (s.highlightCount < 0) {
        s.highlightCount = 0;
    }
    if (s.highlightCount > 16) {
        s.highlightCount = 16;
    }

    s.minFocusSeconds = Wh_GetIntSetting(L"minFocusSeconds");
    if (s.minFocusSeconds < 0) {
        s.minFocusSeconds = 0;
    }

    auto promoteMode = WindhawkUtils::StringSetting::make(L"promoteMode");
    s.promoteMode = PromoteMode::ImmediateTracked;
    if (wcscmp(promoteMode.get(), L"immediateTopN") == 0) {
        s.promoteMode = PromoteMode::ImmediateTopN;
    } else if (wcscmp(promoteMode.get(), L"alwaysWait") == 0) {
        s.promoteMode = PromoteMode::AlwaysWait;
    } else if (wcscmp(promoteMode.get(), L"immediateTracked") == 0) {
        s.promoteMode = PromoteMode::ImmediateTracked;
    }

    auto glowColor = WindhawkUtils::StringSetting::make(L"icons.glowColor");
    s.glowColor = GlowColorMode::Accent;
    if (wcscmp(glowColor.get(), L"green") == 0) {
        s.glowColor = GlowColorMode::Green;
    } else if (wcscmp(glowColor.get(), L"blue") == 0) {
        s.glowColor = GlowColorMode::Blue;
    } else if (wcscmp(glowColor.get(), L"orange") == 0) {
        s.glowColor = GlowColorMode::Orange;
    } else if (wcscmp(glowColor.get(), L"white") == 0) {
        s.glowColor = GlowColorMode::White;
    } else if (wcscmp(glowColor.get(), L"custom") == 0) {
        s.glowColor = GlowColorMode::Custom;
    }

    auto customColor =
        WindhawkUtils::StringSetting::make(L"icons.customGlowColor");
    s.customGlowColor =
        *customColor.get() ? customColor.get() : L"#00C853";
    {
        winrt::Windows::UI::Color parsed{255, 0, 200, 83};
        if (!ParseHexColor(s.customGlowColor, parsed)) {
            parsed = {255, 0, 200, 83};
        }
        s.customGlowPacked = PackColor(parsed);
    }

    s.glowIntensity[0] = Wh_GetIntSetting(L"icons.glowIntensityRank1");
    s.glowIntensity[1] = Wh_GetIntSetting(L"icons.glowIntensityRank2");
    s.glowIntensity[2] = Wh_GetIntSetting(L"icons.glowIntensityRank3");
    for (int& v : s.glowIntensity) {
        if (v < 0) {
            v = 0;
        }
        if (v > 100) {
            v = 100;
        }
    }


    auto glowStyle = WindhawkUtils::StringSetting::make(L"icons.glowStyle");
    // Removed bottomBar and unknown saved values fall back to Side.
    s.glowStyle = GlowStyle::LeftBar;
    if (wcscmp(glowStyle.get(), L"full") == 0) {
        s.glowStyle = GlowStyle::Full;
    } else if (wcscmp(glowStyle.get(), L"frame") == 0) {
        s.glowStyle = GlowStyle::Frame;
    }

    s.glowThickness = Wh_GetIntSetting(L"icons.glowThickness");
    if (s.glowThickness < 1) {
        s.glowThickness = 1;
    }
    if (s.glowThickness > 16) {
        s.glowThickness = 16;
    }


    s.glowSize = Wh_GetIntSetting(L"icons.glowSize");
    if (s.glowSize < 40) {
        s.glowSize = 40;
    }
    if (s.glowSize > 100) {
        s.glowSize = 100;
    }


    s.glowFillOpacity = Wh_GetIntSetting(L"icons.glowFillOpacity");
    if (s.glowFillOpacity < 0) {
        s.glowFillOpacity = 0;
    }
    if (s.glowFillOpacity > 100) {
        s.glowFillOpacity = 100;
    }

    s.previewFillOpacity = Wh_GetIntSetting(L"previews.fillOpacity");
    if (s.previewFillOpacity < 0) {
        s.previewFillOpacity = 0;
    }
    if (s.previewFillOpacity > 100) {
        s.previewFillOpacity = 100;
    }

    s.decayMinutes = Wh_GetIntSetting(L"decayMinutes");
    if (s.decayMinutes < 0) {
        s.decayMinutes = 0;
    }


    s.previewHighlightEnabled =
        Wh_GetIntSetting(L"previews.highlightEnabled") != 0;

    s.previewHighlightCount = Wh_GetIntSetting(L"previews.highlightCount");
    if (s.previewHighlightCount < 0) {
        s.previewHighlightCount = 0;
    }
    if (s.previewHighlightCount > 16) {
        s.previewHighlightCount = 16;
    }

    s.previewSingleWindow = Wh_GetIntSetting(L"previews.singleWindow") != 0;

    s.previewIntensity[0] = Wh_GetIntSetting(L"previews.intensityRank1");
    s.previewIntensity[1] = Wh_GetIntSetting(L"previews.intensityRank2");
    s.previewIntensity[2] = Wh_GetIntSetting(L"previews.intensityRank3");
    for (int& v : s.previewIntensity) {
        if (v < 0) {
            v = 0;
        }
        if (v > 100) {
            v = 100;
        }
    }

    s.previewMinFocusSeconds = Wh_GetIntSetting(L"previews.minFocusSeconds");
    if (s.previewMinFocusSeconds < 0) {
        s.previewMinFocusSeconds = 0;
    }
    s.previewDecayMinutes = Wh_GetIntSetting(L"previews.decayMinutes");
    if (s.previewDecayMinutes < 0) {
        s.previewDecayMinutes = 0;
    }

    auto parsePreviewStyle = [](PCWSTR name, PreviewStyle fallback,
                                bool allowNone) {
        auto value = WindhawkUtils::StringSetting::make(name);
        const PCWSTR v = value.get();
        if (wcscmp(v, L"plate") == 0) return PreviewStyle::Plate;
        if (wcscmp(v, L"outline") == 0) return PreviewStyle::Outline;
        if (wcscmp(v, L"titleBg") == 0) return PreviewStyle::TitleBg;
        if (wcscmp(v, L"titleBar") == 0) return PreviewStyle::TitleBar;
        if (allowNone && wcscmp(v, L"none") == 0) return PreviewStyle::None;
        return fallback;
    };
    s.previewTopStyle =
        parsePreviewStyle(L"previews.topStyle", PreviewStyle::Plate, false);
    s.previewRestStyle =
        parsePreviewStyle(L"previews.restStyle", PreviewStyle::TitleBg, true);
    ApplyLegacyPreviewStyle(s, userSavedSettings);
    s.previewOutlineThickness = (std::max)(
        1, (std::min)(8, Wh_GetIntSetting(L"previews.outlineThickness")));

    s.excludedPrograms.clear();
    for (int i = 0;; i++) {
        auto program =
            WindhawkUtils::StringSetting::make(L"excludedPrograms[%d]", i);
        if (!*program.get()) {
            break;
        }
        s.excludedPrograms.insert(ToUpper(program.get()));
    }

    Wh_Log(L"Settings: style=%s th=%d size=%d%% "
           L"fillOp=%d previewFillOp=%d decay=%dmin "
           L"minFocus=%ds promote=%s preview=%d previewCount=%d single=%d "
           L"previewI=%d/%d/%d previewStyle=%s previewMin=%ds previewDecay=%dmin",
           GlowStyleName(s.glowStyle), s.glowThickness,
           s.glowSize, s.glowFillOpacity,
           s.previewFillOpacity, s.decayMinutes,
           s.minFocusSeconds, PromoteModeName(s.promoteMode),
           s.previewHighlightEnabled ? 1 : 0, s.previewHighlightCount,
           s.previewSingleWindow ? 1 : 0,
           s.previewIntensity[0], s.previewIntensity[1], s.previewIntensity[2],
           PreviewStyleName(s.previewTopStyle), s.previewMinFocusSeconds,
           s.previewDecayMinutes);

    PublishSettings(std::move(s));
    PostToHookThread(WM_APP_REFRESH_ACCENT);
}

