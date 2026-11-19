# Settings: keys, code mapping, compatibility between releases

Read before adding, renaming, removing or re-meaning a setting.

YAML keys map to `LoadSettings()`, which fills a local `Settings` then
`PublishSettings()` (mutex + `shared_ptr<const Settings>`). Readers use
`SettingsSnap()`.
Nested YAML groups (compact-start-menu / accent-color-sync shape) render as
real groups in the Windhawk UI. `$group` is still rejected by the schema.
General keys stay top-level; icon/preview keys are dotted.

| Group | Setting key | Field |
|-------|-------------|--------|
| General | `highlightCount` | `Settings::highlightCount` |
| General | `minFocusSeconds` | `Settings::minFocusSeconds` |
| General | `promoteMode` | `ImmediateTracked` / `ImmediateTopN` / `AlwaysWait` |
| General | `decayMinutes` | app decay |
| General | `excludedPrograms[i]` | uppercase set |
| Taskbar icons | `icons.glowStyle` | `LeftBar` (side bar) / `Frame` / `Full`; legacy `bottomBar` falls back to Side |
| Taskbar icons | `icons.glowColor` / `icons.customGlowColor` | color mode + hex |
| Taskbar icons | `icons.glowIntensityRank1..3` | `glowIntensity[3]` |
| Taskbar icons | `icons.glowThickness` / `icons.glowSize` | metrics |
| Taskbar icons | `icons.glowFillOpacity` | Full / side fill strength (Edge removed) |
| Thumbnail previews | `previews.highlightEnabled` | preview master |
| Thumbnail previews | `previews.highlightCount` | per-flyout top N |
| Thumbnail previews | `previews.singleWindow` | single-window flyouts ranked too (rank 1 while recent; default off) |
| Thumbnail previews | `previews.topStyle` / `previews.restStyle` | `plate` / `outline` / `titleBg` / `titleOutline` (0.12.1) / `titleBar` (+ `none` for ranks 2+); UI names since 0.12.1: Whole plate / Whole plate with accent / Title / Title with accent / Bar under title; legacy `previews.style` mapped by `ApplyLegacyPreviewStyle` |
| Thumbnail previews | `previews.outlineThickness` (UI "Accent thickness" since 0.12.1) | `outline` + `titleOutline` border (fill = half of `previews.fillOpacity`) |
| Thumbnail previews | `previews.intensityRank1..3` | `previewIntensity[3]` |
| Thumbnail previews | `previews.fillOpacity` | plate + titleBg wash (not title bar line) |
| Thumbnail previews | `previews.minFocusSeconds` | window confirm |
| Thumbnail previews | `previews.decayMinutes` | window decay |

## Settings compatibility between releases (rule)

Once a version is in windhawk-mods, users have its settings stored. Windhawk
keeps stored values by key and has no migration mechanism (m417z, Discord
2026-11-16: keep an id where it still makes sense, otherwise changelog/README;
no universal rule). So when a release renames, splits, merges or changes the
meaning of a stored setting:

- Prefer keeping the old key id for the closest new option.
- Otherwise read the old key and map it while the new keys are unset or still
  at their defaults; stop applying it once the user saves settings (track that
  in mod storage, `Wh_GetIntValue` / `Wh_SetIntValue`). A mod cannot write its
  own settings.
- Map only values that a released version could have stored; dev-only values
  fall back to the defaults.
- Mark the code with the release transition (e.g. "0.11 -> 0.12") and a
  removal note (a release or two later), and mention the change in the
  changelog / PR description.
- Purely additive keys need nothing if their default keeps the old behavior.

Current transition: 0.11 -> 0.12, `previews.style` -> `previews.topStyle` +
`previews.restStyle` (`ApplyLegacyPreviewStyle`; mod storage key
`legacyPreviewStyleState`: 0 unchecked, 1 legacy in use, 2 done).

There is no in-mod `enabled` or `glowDebugLog` setting. Disable the mod in
Windhawk to turn highlighting off. Verbose bind / preview-resolve lines use
`Wh_Log` (Windhawk Advanced tab: None / Mod logs / Detailed debug).

Ranks beyond 3 fade from rank 3 (`RankIntensity`: x0.75 per rank, floor
min(rank 3, 10)), icons and previews separately. Preview reuses icon color;
plate/titleBg use `previewFillOpacity` scaled by that window’s preview
intensity.

## Notes

- `requireTaskbarButton` was removed in 0.11.33 (a window without a taskbar
  button is never ranked). A stored value is ignored; no transition.
- Removed in 0.11.38 (Fable review), no transition: `icons.glowRoundness`
  (fixed 28 %), `icons.glowLayers` (fixed 2), `icons.sizeBoostRank1..3` (icon
  scaling dropped), `previews.outlineFillOpacity` (half of Tint opacity;
  the old default 20 = 40 / 2). Stored values are ignored.
- The settings block and the README inside the `.wh.cpp` are the catalog
  page: self-contained, screenshots on `raw.githubusercontent.com`, no links
  to files that are not in windhawk-mods. YAML defaults are real values: a
  user's `0` is never "unset".
