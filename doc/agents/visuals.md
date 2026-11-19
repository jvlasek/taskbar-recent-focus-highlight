# Icon visuals: glow host, z-order, contours, bars

Read before changing how a ranked taskbar icon is painted or cleared. Preview
card painting is in `previews.md`; which button gets which rank is in
`identity.md`.

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Icon chrome | Own `WhRecentFocusGlow` only | Never style `BackgroundElement` |
| Icon default | **Side bar** (`leftBar`) | Left on bottom/top taskbar; under icon on left/right — stays off the native running pill |
| Frame Z-order | Host before the first badge/progress/native thin-pill anchor, otherwise at end | Preserve native order; frame coverage needs visual validation where native anchors precede the glyph |
| Full Z-order | Host before earliest glyph/badge/progress/thin-pill anchor | Fill behind glyph and native indicators, without moving native children |
| UVS vs rebind | Cached paint: **-1** unknown, **0** unranked, **>0** paint if `(rank, generation, accent, edge, panel size)` changed. | `{rank, gen, accent}` alone left the bar on the old side after a taskbar-edge / icon-size relayout. |
| Native z-order | Move only WhRecentFocusGlow when its style position changes; remove when unranked | Every insertion, including creation at end, uses InsertAt. Append omits deferred-index notification. Never rewrite native order or ZIndex |
| Multi-monitor | Same cache on every tracked button | Secondary if UVS fires |
| Virtual desktops | Nested recency maps; no taskband reordering hooks | Explorer already filters `IsRunning` |
| Decay clear | Recompute + `g_pendingOverlaySweep` | No orphan plates |
| Never | `ClearValue` BackgroundElement; clip null ancestors | Pale hover leftovers |
| Preview layout | Span rows + Margin (not RenderTransform) | Title-row expansion; transform clipped the right cap |
| Preview titleBar | ~2px under baseline | Not hugging image; not strikethrough |
| Preview titleBg ("Title") | Tint-opacity ceiling × linear rank intensity; plate over the title row 4px in from the card, card corner radius, gap below the title = gap above (0.12.1) | Readable; 100 vs 5 must differ. The old 8px strip with 4px corners looked off next to a whole plate |
| Preview titleOutline ("Title with accent") | titleBg geometry + accent border (stroke alpha × rank, fill half the tint) | Next to a whole plate it reads as a smaller tag |
| Preview plate | Own `Border` `WhRecentFocusThumbCard` right above `BackgroundBorder`, geometry bound to it; fill = `previewFillOpacity` × rank (see `previews.md`) | Survives the card's hover state; the native brush is not touched |
| Preview ranks | Per-flyout top N, `previewIntensity[3]` | Same ladder idea as icons |
| RunningIndicator | Never set Fill/Width/Height or native ZIndex | Frame/side hosts go below a thin pill; a full-cell Styler plate is not an above-host anchor |
| Bar geometry | Size vs glow **host** (padded inner box), `Center` alignment | IconPanel is 48×32 on a left taskbar but the host is 40×28 (padding 4,2). Length is `size%` of that cell, **same for every rank** (rank is opacity). Icon-width underlines on a left taskbar are too short to scan. |
| RunningIndicator on style switch | Never ClearValue Visibility/Width/Height, never GoToState | VSM stores InactiveRunningIndicator `Visible` as a local value. ClearValue → template Collapsed. GoToState of the *current* state is a no-op, so the short unfocused pill stays gone. |
| Bar auto-rotate | `leftBar` = side (perpendicular) | Removed `bottomBar` falls back to Side. Detect: `VerticalOrientation` / panel 48×32 (wider than tall ⇒ **vertical** bar) first. Do not treat leftover RunningIndicator `VA=Bottom` as a bottom taskbar. |
| OverlayIcon | Native ordering belongs to Windows; badge is an above-host anchor for frame/side styles | No native badge repair. Cached paints recompute host position so newly realized anchors are considered |
| Size boost | Removed in 0.11.38 (it was the one write to a native element, the icon `ScaleTransform`) | Other mods (taskbar-dock-animation) scale the same `Icon` |
| Rank intensity | Element Opacity (bars/frames) or brush alpha (native plate / titleBg) **once** | Do not also multiply fill/stroke alpha by `t`. 100/80/60 must read as 100/80/60, not ~100/64/36. |
| Hit testing | `IsHitTestVisible=False` | Clicks pass through |
| Coexistence | Own names; own elements only (no native brush writes since 0.11.16); side bar above Styler hover plate | Taskbar Styler (themes restyle `RunningIndicator` into a full-cell acrylic on PointerOver) |
| Pinned-only | No highlight | Product rule |

**Not done yet:** Composition `DropShadow` / true GPU glow. Large spread may clip.

## Icon host lifecycle (since 0.9.45)

Create only for ranked buttons with explicit `InsertAt(Size(), host)`. Position
only the host with RemoveAt/InsertAt when its desired slot changes; no native
ZIndex writes, native moves, order snapshots or badge-healing callbacks.
Full: before earliest glyph/badge/progress/thin-pill anchor.
Frame/side: before the earliest badge/progress/multiwindow/thin-pill anchor;
side additionally stays below Icon/DefaultIcon. A full-cell Styler indicator
is not treated as a thin-pill anchor. Native custom ZIndex values remain in
force; arbitrary themes can override the intended collection-based layering.

Clear restores owned icon scaling and removes the host, including on unload.
Creation/reposition must never use Append, even as an end-insertion shortcut.
The current host position planner prioritizes foreground anchors without
rewriting native order; frame appearance and Styler themes need live checks.

## Native icon contours, Frame / Full (since 0.9.45)

Frame/Full use one owned Border (`WhRecentFocusContour`) inside the glow host.
Read BackgroundElement bounds and CornerRadius (or Rectangle radii), or a
full-cell Styler RunningIndicator plate. Transform into native panel coordinates;
reject rotated, skewed and non-uniform transforms. Inset at least 1 DIP and
preserve each native corner radius, clamping to fit. Unknown backgrounds use content bounds
with 4-DIP corners. Center our Border relative to the panel content center
(accounting for Grid padding/border), using balanced margins. Never measure
against the newly created host: its first layout has not happened yet.
Edge was removed in .45: native running/progress indicators already occupy
that space. Legacy bottomBar settings fall back to Side. Full fill has a 60% ceiling
before rank opacity; Layers apply only to Side; Roundness remains for previews.

Experimental (0.12.1, branch `exp/title-outline`), for a side-by-side test:
- `sideBar`: Side bar without the halo (layer 1 = 2px larger rectangle at
  0.35 × fill alpha × 0.45 opacity; XAML cannot blur it, so magnified it is a
  hard-edged rim).
- `plate`: same contour Border, **flush** (inset 0, Size ignored), full Fill
  opacity, no stroke. Full sat at 92 % with 60 % fill — almost exactly where
  Windows draws its hover/active and attention plates, and paler than both.
- `plateAccent`: flush, half fill, accent border (`icons.glowAccentThickness`,
  default 2, alpha 255). An outline is the one thing no Windows button state
  draws (attention = red plate + red pill), so it stays recognisable.
- Both plates sit below the glyph (z-order as Full). Open: a ranked app that
  also flashes — our plate is drawn over Windows' attention tint.

Geometry must be finite, positive and bounded before XAML assignment or integer
conversion. No division by native width/height. Failed native reads fall back
or skip. Frame/Full bypass the old paint-cache early return so native
geometry is reread on paints; Side keeps its existing cache. Check all styles
live, including square/circular themes and active/inactive pills. Automated
math/placement tests are `python tests/run-icon-order-tests.py`.

## Rules when changing this

1. Own-named elements only (`WhRecentFocusGlow`, `WhRecentFocusContour`).
   Never style, `ClearValue`, reorder or re-ZIndex a native child, and never
   touch a button that was never painted. No exceptions since 0.11.38
   (size boost removed).
2. `InsertAt` for every insertion, also at the end; never `Append`.
3. Rank intensity is applied once (element `Opacity`, or brush alpha), never
   multiplied into both.
4. Geometry must be finite, positive and bounded before it is assigned to a
   XAML property or converted to an integer.
5. `UpdateVisualStates` must stay cheap: no edge detection there (the edge is
   cached on the layout watch and refreshed from `SizeChanged`), no clear just
   because `g_pendingOverlaySweep` is set, no-op when rank 0 and no chrome.
6. Inspect real trees with [UWPSpy](https://ramensoftware.com/uwpspy);
   element names vary by build and by Taskbar Styler theme.
