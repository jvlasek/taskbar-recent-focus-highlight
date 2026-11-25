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
    // Old ids: plateTitle (= the new defaults), plate, titleBg, titleBar.
    auto legacy = WindhawkUtils::StringSetting::make(L"previews.style");
    PreviewStyle v = PreviewStyle::None;
    if (!NameToEnum(kPreviewStyleNames, legacy.get(), v) ||
        v == PreviewStyle::Outline || v == PreviewStyle::TitleOutline ||
        v == PreviewStyle::None) {
        return false;
    }
    out = v;
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

int ClampedIntSetting(PCWSTR name, int lo, int hi) {
    return (std::min)(hi, (std::max)(lo, Wh_GetIntSetting(L"%s", name)));
}

// Unknown or removed ids (e.g. the old bottomBar style) keep the fallback.
template <typename E, size_t N>
E EnumSetting(PCWSTR name, const EnumName<E> (&table)[N], E fallback) {
    auto value = WindhawkUtils::StringSetting::make(L"%s", name);
    NameToEnum(table, value.get(), fallback);
    return fallback;
}

// Settings transition 0.11 -> 0.12, part 2 (0.12.2; remove with the
// previews.style one, planned for 1.0). The app-ranking settings moved from
// the top level into the Taskbar icons group, so their ids changed:
// highlightCount, minFocusSeconds, promoteMode, decayMinutes ->
// icons.<same>. The settings page shows the new keys at their defaults, so
// the old values are used until the user first saves settings, like the
// preview style above. Only a 0.11 install has old values stored:
// previews.style existed only there and Windhawk stores every key of a mod's
// settings together, so its presence gates the transition (without stored
// settings the old values were the defaults anyway). Mod storage
// "legacyAppSettingsState": 0 = not checked yet, 1 = old values in use,
// 2 = done.
constexpr wchar_t kLegacyAppSettingsState[] = L"legacyAppSettingsState";

void ApplyLegacyAppSettings(Settings& s, bool userSavedSettings) {
    int state = Wh_GetIntValue(kLegacyAppSettingsState, 0);
    if (state == 2) {
        return;
    }
    if (userSavedSettings) {
        if (state == 1) {
            Wh_Log(L"Settings: 0.11 app settings no longer applied (settings "
                   L"saved)");
        }
        Wh_SetIntValue(kLegacyAppSettingsState, 2);
        return;
    }
    if (state == 0) {
        auto oldStyle = WindhawkUtils::StringSetting::make(L"previews.style");
        auto newPromote =
            WindhawkUtils::StringSetting::make(L"icons.promoteMode");
        // Unset reads as 0 / empty; otherwise the defaults of 0.12.2.
        const int count = Wh_GetIntSetting(L"icons.highlightCount");
        const int minFocus = Wh_GetIntSetting(L"icons.minFocusSeconds");
        const int decay = Wh_GetIntSetting(L"icons.decayMinutes");
        const bool newAtDefaults =
            (count == 0 || count == 3) && (minFocus == 0 || minFocus == 8) &&
            (decay == 0 || decay == 30) &&
            (!*newPromote.get() ||
             wcscmp(newPromote.get(), L"immediateTracked") == 0);
        state = (*oldStyle.get() && newAtDefaults) ? 1 : 2;
        Wh_SetIntValue(kLegacyAppSettingsState, state);
    }
    if (state == 1) {
        constexpr int kNoMax = 1 << 30;
        s.highlightCount = ClampedIntSetting(L"highlightCount", 0, 16);
        s.minFocusSeconds = ClampedIntSetting(L"minFocusSeconds", 0, kNoMax);
        s.promoteMode = EnumSetting(L"promoteMode", kPromoteModeNames,
                                    PromoteMode::ImmediateTracked);
        s.decayMinutes = ClampedIntSetting(L"decayMinutes", 0, kNoMax);
        Wh_Log(L"Settings: using the 0.11 app settings (count=%d minFocus=%ds "
               L"promote=%s decay=%dmin) until settings are saved",
               s.highlightCount, s.minFocusSeconds,
               PromoteModeName(s.promoteMode), s.decayMinutes);
    }
}

void LoadSettings(bool userSavedSettings) {
    Settings s;
    constexpr int kNoMax = 1 << 30;

    s.highlightCount = ClampedIntSetting(L"icons.highlightCount", 0, 16);
    s.minFocusSeconds = ClampedIntSetting(L"icons.minFocusSeconds", 0, kNoMax);
    s.promoteMode = EnumSetting(L"icons.promoteMode", kPromoteModeNames,
                                PromoteMode::ImmediateTracked);
    s.decayMinutes = ClampedIntSetting(L"icons.decayMinutes", 0, kNoMax);
    ApplyLegacyAppSettings(s, userSavedSettings);

    s.glowStyle =
        EnumSetting(L"icons.glowStyle", kGlowStyleNames, GlowStyle::LeftBar);
    s.glowColor =
        EnumSetting(L"icons.glowColor", kGlowColorNames, GlowColorMode::Accent);
    auto customColor =
        WindhawkUtils::StringSetting::make(L"icons.customGlowColor");
    s.customGlowColor = *customColor.get() ? customColor.get() : L"#00C853";
    {
        winrt::Windows::UI::Color parsed{255, 0, 200, 83};
        if (!ParseHexColor(s.customGlowColor, parsed)) {
            parsed = {255, 0, 200, 83};
        }
        s.customGlowPacked = PackColor(parsed);
    }
    s.glowIntensity[0] = ClampedIntSetting(L"icons.glowIntensityRank1", 0, 100);
    s.glowIntensity[1] = ClampedIntSetting(L"icons.glowIntensityRank2", 0, 100);
    s.glowIntensity[2] = ClampedIntSetting(L"icons.glowIntensityRank3", 0, 100);
    s.glowThickness = ClampedIntSetting(L"icons.glowThickness", 1, 16);
    s.glowSize = ClampedIntSetting(L"icons.glowSize", 40, 100);
    s.glowFillOpacity = ClampedIntSetting(L"icons.glowFillOpacity", 0, 100);
    s.glowAccentThickness =
        ClampedIntSetting(L"icons.glowAccentThickness", 1, 8);

    s.previewHighlightEnabled =
        Wh_GetIntSetting(L"previews.highlightEnabled") != 0;
    s.previewHighlightCount = ClampedIntSetting(L"previews.highlightCount", 0, 16);
    s.previewSingleWindow = Wh_GetIntSetting(L"previews.singleWindow") != 0;
    s.previewIntensity[0] = ClampedIntSetting(L"previews.intensityRank1", 0, 100);
    s.previewIntensity[1] = ClampedIntSetting(L"previews.intensityRank2", 0, 100);
    s.previewIntensity[2] = ClampedIntSetting(L"previews.intensityRank3", 0, 100);
    s.previewFillOpacity = ClampedIntSetting(L"previews.fillOpacity", 0, 100);
    s.previewMinFocusSeconds =
        ClampedIntSetting(L"previews.minFocusSeconds", 0, kNoMax);
    s.previewDecayMinutes = ClampedIntSetting(L"previews.decayMinutes", 0, kNoMax);
    s.previewTopStyle =
        EnumSetting(L"previews.topStyle", kPreviewStyleNames, PreviewStyle::Plate);
    if (s.previewTopStyle == PreviewStyle::None) {  // not offered for rank 1
        s.previewTopStyle = PreviewStyle::Plate;
    }
    s.previewRestStyle = EnumSetting(L"previews.restStyle", kPreviewStyleNames,
                                     PreviewStyle::TitleBg);
    ApplyLegacyPreviewStyle(s, userSavedSettings);
    s.previewOutlineThickness =
        ClampedIntSetting(L"previews.outlineThickness", 1, 8);

    for (int i = 0;; i++) {
        auto program =
            WindhawkUtils::StringSetting::make(L"excludedPrograms[%d]", i);
        if (!*program.get()) {
            break;
        }
        s.excludedPrograms.insert(ToUpper(program.get()));
    }

    Wh_Log(L"Settings: count=%d style=%s color=%s th=%d size=%d%% fillOp=%d "
           L"previewFillOp=%d decay=%dmin minFocus=%ds promote=%s preview=%d "
           L"previewCount=%d single=%d previewI=%d/%d/%d previewStyle=%s/%s "
           L"previewMin=%ds previewDecay=%dmin excluded=%zu",
           s.highlightCount, GlowStyleName(s.glowStyle),
           EnumToName(kGlowColorNames, s.glowColor),
           s.glowThickness, s.glowSize, s.glowFillOpacity, s.previewFillOpacity,
           s.decayMinutes, s.minFocusSeconds, PromoteModeName(s.promoteMode),
           s.previewHighlightEnabled ? 1 : 0, s.previewHighlightCount,
           s.previewSingleWindow ? 1 : 0, s.previewIntensity[0],
           s.previewIntensity[1], s.previewIntensity[2],
           PreviewStyleName(s.previewTopStyle),
           PreviewStyleName(s.previewRestStyle), s.previewMinFocusSeconds,
           s.previewDecayMinutes, s.excludedPrograms.size());

    PublishSettings(std::move(s));
    PostToHookThread(WM_APP_REFRESH_ACCENT);
}

