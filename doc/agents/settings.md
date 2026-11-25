# Settings: keys, code mapping, compatibility between releases

Read before adding, renaming, removing or re-meaning a setting.

YAML keys map to `LoadSettings()`, which fills a local `Settings` then
`PublishSettings()` (mutex + `shared_ptr<const Settings>`). Readers use
`SettingsSnap()`.
Nested YAML groups (compact-start-menu / accent-color-sync shape) render as
real groups in the Windhawk UI. `$group` is still rejected by the schema.
Only the exclude list (for icons and previews) is top-level since 0.12.2;
icon/preview keys are dotted.

| Group | Setting key | Field |
|-------|-------------|--------|
| Taskbar icons | `icons.highlightCount` (top-level `highlightCount` until 0.12.1) | `Settings::highlightCount` |
| Taskbar icons | `icons.minFocusSeconds` (top-level `minFocusSeconds` until 0.12.1) | `Settings::minFocusSeconds` |
| Taskbar icons | `icons.promoteMode` (top-level `promoteMode` until 0.12.1) | `ImmediateTracked` / `ImmediateTopN` / `AlwaysWait` |
| Taskbar icons | `icons.decayMinutes` (top-level `decayMinutes` until 0.12.1) | app decay |
| General | `excludedPrograms[i]` | uppercase set |
| Taskbar icons | `icons.glowColor` / `icons.customGlowColor` | UI "Highlight color" / "Custom highlight color" since 0.12.2 (ids kept) |
| Taskbar icons | `icons.glowStyle` | `leftBar` (Side bar) / `full` (Plate) / `frame` (Plate with accent) — UI names and look changed in 0.12.1, ids kept; legacy `bottomBar` falls back to Side |
| Taskbar icons | `icons.glowAccentThickness` | Plate with accent border, 1–8 px (0.12.1) |
| Taskbar icons | `icons.glowColor` / `icons.customGlowColor` | color mode + hex |
| Taskbar icons | `icons.glowIntensityRank1..3` | `glowIntensity[3]` |
| Taskbar icons | `icons.glowThickness` / `icons.glowSize` | side bar thickness / length (UI renamed in 0.12.1; until then also Frame stroke / plate inset) |
| Taskbar icons | `icons.glowFillOpacity` | Side bar / Plate strength; half for Plate with accent |
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

Current transitions (both 0.11 -> 0.12, to be removed in 1.0):
- `previews.style` -> `previews.topStyle` + `previews.restStyle`
  (`ApplyLegacyPreviewStyle`; mod storage key `legacyPreviewStyleState`: 0
  unchecked, 1 legacy in use, 2 done).
- 0.12.2: `highlightCount`, `minFocusSeconds`, `promoteMode`, `decayMinutes`
  -> `icons.*` (`ApplyLegacyAppSettings`; `legacyAppSettingsState`, same
  values). Gated on a stored `previews.style` (exists only on a 0.11
  install) and the new keys unset or at their defaults.
  `tests/run-settings-transition-tests.py`.

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
