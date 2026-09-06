# Claude submission review (round 3) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5546535603

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.2 (post-round-2: no icon fuzzy, path resolve once, unload order, `no_destroy` dispatchers).

---

## Draft PR comment

Thanks for the third pass. Round 2 dropped the icon name-heuristic tier, stopped re-resolving paths on a timer, made `RegisterClassExW` failure fatal, and put `g_uiDispatchers` behind `no_destroy`. This round is about not doing work (or rewriting native trees) on icons we never highlight, plus one real unload hole. Responses below.

**1. `UpdateVisualStates` work on every button — agreed, will fix.**

Confirmed: every UVS still does `TrackButton_UIThread` (linear `weak.get()` scan) → `EnsureButtonPathCached` (another scan) → `EnsureIconPanelLayoutWatch` (tree walk + third scan) → `RefreshButtonHighlight` → `GetCachedPaintRank` (fourth scan) → `ApplyButtonHighlight` / `ClearButtonHighlight` (more `FindChildByName` / `FindDescendantByName`), plus `ScheduleRefreshAllHighlights` allocating a `TryRunAsync` that immediately debounce-returns. `ApplyButtonHighlight` then re-detects the taskbar edge (`SHAppBarMessage` on the inconclusive path) and rebuilds brushes even when rank/style/size did not change.

We still need the **map** (tracked buttons + path cache) so a newly ranked app can bind. We do **not** need paint, native z-order, edge detection, or layout watches on icons we have never highlighted.

Will:

- Key the button / path / layout-watch caches by `IUnknown` identity (`SameInspectableIdentity` already exists) so UVS is hash lookups, not four COM-resolving linear scans.
- Early-out paint: bump a settings generation in `PublishSettings`; skip `ApplyButtonHighlight` when `(rank, generation)` are unchanged and our glow host is still present. Rank 0 with no chrome is a no-op (no `ClearButtonHighlight`).
- Use `IconPanelLayoutWatch::lastEdge` / `haveEdge` (they are written and never read today). Detect edge on first watch + `SizeChanged`, not per paint. No `SHAppBarMessage` on the UVS path.
- Keep tracking + one-shot path cache so the identity map stays complete.

**2. Native `IconPanel` reorder on buttons we never touched — agreed, will stop.**

The no-chrome early-out in `ClearButtonHighlight` still calls `RestoreIconPanelNativeZOrder` (moves `BackgroundElement` to index 0, raises `OverlayIcon` above `Icon`). That heal was for a real leftover of *our* host: after decay removed the overlay, `BackgroundElement` could sit in front of `RunningIndicator` (Discord ping + a timed-out glow left a plate and no underscore). Running it on every unranked UVS was leftover over-application of that debug/heal path, and it is not undone on unload.

Will only restore native z-order on buttons where we actually inserted or removed `WhRecentFocusGlow`. Untouched icons stay in whatever order Windows / Taskbar Styler / badge mods left them.

**3. Layout watch on a dispatcher we cannot drain — agreed, will fix.**

`RememberUiDispatcher` can return without recording (null/throwing `Dispatcher()`, or `g_uiDispatchers` already reset), and `EnsureIconPanelLayoutWatch` still registers `SizeChanged`. Uninit only revokes via `g_uiDispatchers`. That is a real post-`FreeLibrary` crash on the next panel resize.

Will make `RememberUiDispatcher` return whether it recorded a dispatcher, and skip the `SizeChanged` registration if it did not.

### Optional

Will take all of these:

- **Dead code:** `RankIntensity` / `PreviewRankIntensity` / `RankSizeBoost`; `kBottomBarMarkerName` and the “older builds” strip (this mod has not shipped); `g_dispatcherAnchor`; unused `lastEdge`/`haveEdge` once item 1 reads them or they go; `g_previewHooksReady` as a global (local at the hook site is enough).
- **Settings groups.** The `[General]` / `[Icons]` prefixes were a workaround: `$group` is rejected by the settings schema, so we flattened the list. Nested YAML (as in `compact-start-menu` / `accent-color-sync`) is the supported form — we had not tried that shape. Will switch; children become `icons.glowStyle`, `previews.previewStyle`, etc. Fine to change keys before catalog (no shipped users).
- **Debug log.** Will drop `glowDebugLog`. Windhawk already has None / Mod logs / Detailed debug on the Advanced tab; `Wh_Log` is a cheap `if (g_logsOn)` when logging is off. Verbose bind / preview-resolve lines become plain `Wh_Log`.
- **`[General] Enabled`.** Early-testing leftover. Users enable/disable the mod in Windhawk. Will remove the setting and the ~15 `enabled` checks.
- **`WindhawkUtils::StringSetting`** for the six string settings + the exclude-list loop.
- **`seqFor` inside `std::sort`.** Will copy `confirmSeq` onto `Scored` in the same pass as `tick`, then sort on plain fields.
- **Unused deps.** Will drop `-loleaut32` / `-lpropsys` / `#include <winrt/Windows.UI.Xaml.Input.h>` if the compile still links (`PropVariantClear` is ole32; `PKEY_AppUserModel_ID` is local via `initguid.h` + `propkey.h`).
- **Leaks.** Close `g_hookThreadReadyEvent` if `CreateThread` fails (`StopWinEventHookThread` currently returns before the close). `LoadLibraryEx(taskbar.dll)` stays as last resort after `GetModuleHandle`; we will not `FreeLibrary` it (Explorer still needs the module).
- **Comments.** The second “Pass 3” in `RefreshThumbnailFlyout_UIThread` is a same-PID recency copy, not a third HWND resolve. Will relabel.

### Functionality notes

Will address:

- **Re-hovering a different icon.** `HoverFlyoutModel::TargetItemKey` is already hooked only to capture the thumbnails collection. Will post a preview refresh from there so a recycled `ItemsRepeater` does not keep the previous app’s bars.
- **Repeater index drift.** `CollectRepeaterThumbnailViews` skips unrealized / non-thumbnail elements, then `HwndFromThumbnailsGetAt(ri)` uses the compacted vector index. Will store the source index next to the element.
- **Accent colour.** `cachedAccent` is filled once in `LoadSettings` (Windhawk thread; `UISettings` can throw there and we silently keep `#0078D7`). Will re-query on `WM_DWMCOLORIZATIONCOLORCHANGED` / `WM_SETTINGCHANGE` on the hook-thread window, and query the accent on that STA rather than in `LoadSettings`.

Leaving for later (not this pass):

- **Preview unique-title / English `NormalizeAutomationName`.** Icon bind is already path / AUMID / HWND only. Unique-title remains a last-resort for preview cards when `GetAt` misses, and identical titles stay unmatched. Prefer a missed highlight over a wrong one on the icon side; on the preview side we still want a highlight when the card title uniquely identifies the window. A later pass can log when that fallback fires vs `how=repeater|taskitem`, so we can measure how often the straight path fails before adding or dropping more heuristics. A README note on the English suffix strip is enough for now.
- Further cutting remaining preview heuristics purely for line count.

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| 1 | Valid | Identity-keyed maps; paint skip on `(rank, settingsGen)` + host present; rank 0 + no chrome = no Clear; cache edge on SizeChanged; keep track + path cache |
| 2 | Valid; not *only* debug leftover | Heal was Discord/decay after *our* host; UVS-on-unranked is over-application. Scope `RestoreIconPanelNativeZOrder` to insert/remove of `WhRecentFocusGlow` |
| 3 | Valid | `RememberUiDispatcher` → bool; no SizeChanged unless recorded |
| Optional dead code | Valid | Delete listed leftovers |
| Optional nested groups | Valid; we tried `$group` not nested YAML | Switch to compact-start-menu shape; dotted keys; bump settings in LoadSettings |
| Optional glowDebugLog | Drop | `Wh_Log` + Advanced tab. There is **no** mod API that distinguishes “Mod logs” vs “Detailed debug” — the latter is engine internals. Verbose lines will show whenever Mod logs is on, which is the intended Windhawk model |
| Optional enabled | Drop | Early testing artefact |
| Optional StringSetting | Take | RAII |
| Optional sort mutex | Take | confirmSeq on Scored |
| Optional unused deps | Check at compile | Drop if still links |
| Optional leaks | Event: yes; taskbar.dll: leave loaded | Close ready event on CreateThread fail |
| Optional Pass 3 comment | Take | Relabel |
| Non-crit re-hover | Take | TargetItemKey → RequestApplyPreviewVisuals |
| Non-crit index drift | Take | Store repeater source index |
| Non-crit accent | Take | Hook-thread DWM/SETTINGCHANGE; do not QuerySystemAccentColor on Windhawk thread only |
| Non-crit English title | Skip generalize / skip drop fallback | README note; later logfile for fallback rate |
| Non-crit 7.5k lines | Skip this pass | Icon fuzzy already gone |

### Item 2 extra context

```
// Skip no-op clears on every mouse-over (UpdateVisualStates storms).
// Still heal z-order: after a timed-out glow, our host is gone but
// BackgroundElement can remain in front of RunningIndicator (Discord
// ping + decay left a red plate and no underscore).
if (!ButtonHasOurChrome(button) && !g_pendingOverlaySweep.load()) {
    RestoreIconPanelNativeZOrder(iconPanelEarly);
    return;
}
```

That comment is the whole story: the heal is real for buttons we *had* painted; the bug is applying it to every button UVS never painted. After the fix, decay/clear of our host still restores z-order (inside the has-chrome / just-removed-host branch). Unranked buttons are not “healed.”

### Nested settings shape (for the implementation pass)

```
- icons:
    - glowStyle: leftBar
      $name: Highlight style
  $name: Taskbar icons
```

Read as `Wh_GetStringSetting(L"icons.glowStyle")`. Same for `previews.*`. Top-level general keys (`highlightCount`, `minFocusSeconds`, …) can stay ungrouped or sit under `general:`.

### Debug logging (for the implementation pass)

Do **not** invent a second log API. Drop the setting. Keep `Wh_Log` on Confirmed focus / Preview resolve / errors always (cheap when off). The ~12 `if (settings->glowDebugLog)` sites become `Wh_Log`. Users who want bind spam turn on **Mod logs** in Advanced.

The later “brief logfile when unique-title fires” is a **separate** experiment, not this PR pass.
