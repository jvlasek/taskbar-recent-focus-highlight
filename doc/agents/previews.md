# Thumbnail previews: window recency, card matching, card visuals

Read before changing the flyout hooks, the card -> window resolve, preview
ranking or preview painting.

App ranks answer “which **app**?”. Thumbnail ranks answer “which **windows**
in this flyout?”.

| Layer | Key | Timers |
|-------|-----|--------|
| App ranks | `APPID:<Explorer AppId>` per desktop GUID (see `identity.md`) | `minFocusSeconds`, `decayMinutes` |
| Preview glow | `HWND` + PID per desktop GUID | `previewMinFocusSeconds`, `previewDecayMinutes` |

## Product rules

- Each multi-window flyout has its **own** recency ladder (top N among
  siblings), not a global window rank and not “most recent only”.
- **Skip** flyouts with ≤1 thumbnail, unless `previews.singleWindow` is on
  (0.11.23; 0.11.27: ranked like any flyout, so rank 1 while recent — for one
  window that means "used within the preview decay", by design, user 2026-11-16).
- Preview timers are **independent** of app timers.
- Do **not** require the app to be in icon top-N.
- Preview ranks reuse the same intensity idea as icons (`previewIntensity[3]`;
  ranks 4+ reuse rank 3). Count is `previewHighlightCount` (0–16).

## Matching a `TaskItemThumbnailView` to an HWND

```
TaskItemThumbnailView::OnApplyTemplate / put_ThumbnailSource / ElementClearing
  → collect siblings → each card's view model → HWND → rank → paint
```

Since 0.11.19 there is one resolver in `RefreshThumbnailFlyout_UIThread`:
card → `ThumbnailSource` → `TaskItemThumbnailViewModel` → `TaskItemThumbnail`
→ `ParentItem.WindowId` (`ResolveHwndFromCardViewModel`). Explorer's own
binding, per card; cannot shift when cards are removed or recycled. UWP gives
the `ApplicationFrameWindow`, same as focus. Optional symbols; log
`Preview resolve: view model on/unavailable`. A card without one stays
unmarked; no unique-title fallback (stash: `stash/preview-unique-title.cpp`).
Snap-group cards: `IconsRepeater` with 2+ icons **and** no view-model HWND.

Repaint triggers: `put_ThumbnailSource` (card rebound) schedules a coalesced
refresh; card removal (window closed from the flyout) fires neither, so the
flyout `ItemsRepeater.ElementClearing` is subscribed once per repeater (UI
thread) and revoked per dispatcher in `Wh_ModUninit`
(`RevokeRepeaterWatchesOnThisDispatcher`); `HoverFlyoutModel::TargetItemKey`
repaints on retarget and clears tracked cards only when the key differs from
the previous one on that thread (0.11.39; the same key repeats while one
flyout stays open).

Removed in 0.11.19 (history): the `TaskItemThumbnail` constructor hooks and
their DataContext ↔ model map (never matched on 26100-era builds — the card
DataContext is null), and the repeater-index + captured `TaskGroup::Thumbnails`
`GetAt` pass (shifted after a card was closed: wrong card painted until the
flyout reopened, 2026-11-15).

Do **not** assign HWNDs by group construction order or `EnumWindows`.
`AutomationProperties.PositionInSet` is not refreshed on thumbnail reorder;
repeater index is the visual order. Snap-group cards: `IconsRepeater` with
2+ children **and** no window HWND — never glow those. A window card that
gained a second icon still resolves an HWND and is not skipped.

Then sort siblings with a recency tick (tick, confirmSeq, foreground) and
paint the top `previewHighlightCount` at `previewIntensity` ranks.

`OnApplyTemplate`, hover-switch (`TargetItemKey`), and title remeasure all
coalesce onto one Low-priority flyout refresh. The callback prefers the card
that scheduled the pass if it is still live under an `ItemsRepeater`, else a
live in-repeater tracked view. Since 0.11.20 a thumbnail click has no
special path: it is a focus change like any other and waits the preview
minimum (the `HandleClick` hook, `WM_APP_PREVIEW_CLICK` /
`ConfirmPreviewFocusNow` and the vtable-probed native HWND getters were
removed together with every taskbar.dll hook).

Hooks for thumbnails are **optional**. Missing symbols: app ranks still work;
preview cards without a TaskItem/repeater HWND stay unmarked.

## Preview visuals (must not break layout)

Thumbnail cards use a Grid with **rows** (title | image). A normal child in
cell (0,0) expands the title row (bar between icon and text + card grows).

Rules:

1. Overlay host **spans all rows/columns** (`SpanHostOverPanel`) + Stretch.
2. Children positioned with **explicit size + Margin** (not `RenderTransform`:
   the layout clip is the un-transformed slot and squares the right end-cap).
3. Own names: `WhRecentFocusThumbGlow`, `WhRecentFocusThumbTitleBar`,
   `WhRecentFocusThumbTitleBg`, marker `WhRecentFocusThumbNative` for plate.

| `previewStyle` | Implementation |
|----------------|----------------|
| `titleBar` (UI "Bar under title") | Thin rect just under title baseline (~2px gap); 8px side inset |
| `titleBg` (UI "Title") | 0.12.1: Border `WhRecentFocusThumbTitleBg` 4px in from the card's `BackgroundBorder` (left/top/right, via `TransformToVisual(host)`), with its CornerRadius (radius − 4, concentric, looked too sharp); bottom = title bottom + the gap above the title (max 4px + border). Before 0.12.1: 8px in from the panel, 4px corners (`CornerRadius{4,4,4,4}` — `{4}` is TopLeft only), ~6px top vs ~1px bottom gap, thin tint left of the icon; that strip is still the fallback before layout / without a BackgroundBorder |
| `titleOutline` (UI "Title with accent") | 0.12.1: same geometry + accent `BorderBrush` (alpha 255 × rank) and `previews.outlineThickness`; fill = half of Tint opacity like `outline`. In the fallback strip the border grows the strip (drawn inside) and keeps its bottom edge under the 6px top clamp. `HideThumbOverlayChildren` clears BorderBrush/BorderThickness, else a switch to `titleBg` keeps the border. No new XAML. Tried and dropped on branch `exp/title-outline`: the 8px strip with a border (a focused text box on a single-window flyout) and a flush 0px variant (too big next to a whole plate) |
| `plate` (UI "Whole plate") | 0.11.16+: own `Border` `WhRecentFocusThumbCard` inserted right after `BackgroundBorder` in its panel; Width/Height/Margin/alignment/CornerRadius bound to it; fill only. Survives the card's hover state (which rewrites `BackgroundBorder.Background`). Fallback without a BackgroundBorder: overlay-host rectangle. Native-brush restore code removed in 0.11.38 |
| `outline` (UI "Whole plate with accent") | Same overlay with accent `BorderBrush` and `previews.outlineThickness`; fill = half of Tint opacity (0.11.38; was `previews.outlineFillOpacity`, default 20) — a full tint hid the border |
| overlay host z-order | 0.11.17+: directly above `BackgroundBorder` (below title/thumbnail), so the title wash no longer dims the text; front only if no BackgroundBorder child |
| `none` | Ranks 2+ only: not marked |

Styles are chosen per rank: `previews.topStyle` (rank 1, default `plate`) and
`previews.restStyle` (ranks 2+, default `titleBg`). The UI names changed in
0.12.1; the ids did not, so no transition. The 0.11.1 `previews.style` is
still honoured by `ApplyLegacyPreviewStyle` (see `settings.md`): `plate`,
`titleBg`, `titleBar` apply to all ranks; `plateTitle` ("hybrid") equals the
defaults; `outline` / `titleOutline` / `none` never existed there and are
ignored.

Clear (unload / unranked) removes the named overlays. A ranked repaint keeps
`WhRecentFocusThumbGlow` and only hides its children and the card overlay, so
the markup is not parsed again on every paint. The native-brush restore path
(`ClearThumbnailNativeStyles`, marker `WhRecentFocusThumbNative`) only undoes
paint from before 0.11.16; nothing creates that marker any more.

## Rules when changing this

1. Wrong card marked: the only resolver is the card's own view model
   (`ResolveHwndFromCardViewModel`). Do not add index-, order-, title- or
   `EnumWindows`-based fallbacks; a card without a view-model window stays
   unmarked. Never give one HWND to two cards.
2. A preview "already tracked" check is `IsWindowRecentForPreviewLocked`
   (live HWND, same PID, not decayed), not "has any tick".
3. Layout: never add a sized child only to grid row 0. Span the overlay host
   over all rows and columns and position children with explicit size and
   Margin, not `RenderTransform`.
4. Insert into panels with `InsertAt(index, element)`, also at the end; never
   `Append` (it skips XAML's deferred-element index notification, which
   `RemoveAt` sends).
5. No `UpdateLayout` from `OnApplyTemplate` (layout cycle). If a card is not
   measured yet, paint with the fallback and schedule the coalesced Low pass
   (`SchedulePreviewFlyoutRefresh`); at most one follow-up.
6. All thumbnail symbols are optional hooks. Missing ones must leave app
   ranks working.
7. Event handlers registered on XAML objects (`ElementClearing`) are revoked
   on their own dispatcher during unload; see `threading-unload.md`.
