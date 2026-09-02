# Agent / contributor guide

Developer context for `taskbar-recent-focus-highlight.wh.cpp` (v0.9.x). Read this
before changing focus tracking, button matching, thumbnail previews, or visuals.

## What this project is

A **Windhawk mod** injected into `explorer.exe` that:

1. Watches which app/window the user is actually using (window focus).
2. Maintains **app-level** recency ranking (top N processes) → taskbar icon glow.
3. Maintains **window-level** recency (`HWND`) → per-flyout thumbnail ranks.
4. Paints own-named XAML overlays (never permanent restyles that hover states wipe).

Reference mods live in `example/` (icon size, thumbnail size, volume-per-app,
taskbar styler). Patterns (Taskbar.View hooks, `FindChildByName`, settings YAML,
LoadLibrary hooks, TaskItemThumbnail ctor maps) come from those files and from
[ramensoftware/windhawk-mods](https://github.com/ramensoftware/windhawk-mods).

---

## Why “app matching” exists (important)

Focus tracking and taskbar UI live in **two different worlds**. The mod must
bridge them.

### World A — focus / process identity

`EVENT_SYSTEM_FOREGROUND` gives an `HWND`. From that we resolve:

| Field | Example | Role |
|--------|---------|------|
| Process image path | `C:\…\WindowsTerminal.exe` | Stable **key** for Win32 ranking |
| AppUserModelID | `windows.immersivecontrolpanel_…` | **Key** for AFH/WWAHost (`APPID:…`) |
| File name / window title | `WindowsTerminal.exe` / `Settings` | Logs, exclude list, fuzzy match |
| PID | `12345` | Min-focus “still same app?” (Win32) |
| HWND + PID | window handle | Preview recency map (PID rejects recycle) |

The app recency map is keyed by **uppercase full path**, except UWP windows
hosted by `ApplicationFrameHost.exe` / `WWAHost.exe`, which use `APPID:` +
AppUserModelID. Win32 keeps path-only keys (Windhawk’s editor is still
VSCodium.exe even when the window AppId is RAMENSOFTWARE.WINDHAWK).

### World B — taskbar XAML buttons

Highlights are applied to `Taskbar.TaskListButton` elements inside
`Taskbar.View.dll`. A button is **not** an HWND and does not expose a simple
“process path” property.

What we *can* observe:

- `AutomationProperties.Name` — human label, e.g. `"Windows Terminal"`
- Visual states (`ActiveNormal`, …) — which button is currently active
- `get_IsRunning` — running vs pinned-only
- Child elements: `IconPanel`, `Icon`, `BackgroundElement`, …
- Via taskband hooks: button → `ITaskItem` / group → `HWND` / PID → path

### App matching (in practice)

```
ranked apps (path / exe)
        │
        ▼
   path cache (option C) + name scores  ──►  TaskListButton
        │
        ▼
   ApplyButtonHighlight(rank)  or  ClearButtonHighlight()
```

Order of preference:

1. **Process path cache (option C)** — resolve button → HWND/PID → image path;
   score 1000 exact / 900 same file name (900 is **1:1**, not a replica).
2. **AppUserModelID** — only when the rank key is `APPID:…` (UWP host). Button
   AutomationId / window AUMID, score 1000. Mismatch → score 0 (no fuzzy).
   Do not use PID+class: AFH is shared.
3. **Cached automation name** — after learn `path → "Windows Terminal"` (1:1).
4. **Fuzzy / title scores** — alphanumeric compare, greedy 1:1 assignment.
5. **Active-button association** — on confirm, store active running button name
   (APPID keys only associate buttons with the same AUMID).

Only **score 1000** may bind the same rank to many buttons (secondary taskbar /
Never Combine). Name-cache 96 used to copy Settings onto Windows Security.

Pinned-only icons: no highlight (`IsRunning == false`). Virtual-desktop lists
are separate: a pinned icon that is not running on this desktop must not glow.

### Why not only match on “active button”?

Only the **currently focused** app is Active. Ranks 2 and 3 are recent but
**not** active, so they need identity matching.

### Known gaps (app matching)

| Gap | Impact | Possible fix |
|-----|--------|----------------|
| Fuzzy false positive/negative | Wrong or missing glow | Prefer path cache / AppId |
| Combined icons | One button per app group | Correct for combined mode |
| Localization of automation names | Fuzzy may fail | Path cache + active association |

---

## Window-level recency + thumbnail matching

App ranks answer “which **app**?”. Thumbnail ranks answer “which **windows**
in this flyout?”.

| Layer | Key | Timers |
|-------|-----|--------|
| App ranks | Path (UPPER) or `APPID:…` per desktop GUID | `minFocusSeconds`, `decayMinutes` |
| Preview glow | `HWND` + PID per desktop GUID | `previewMinFocusSeconds`, `previewDecayMinutes` |

### Product rules

- Each multi-window flyout has its **own** recency ladder (top N among
  siblings), not a global window rank and not “most recent only”.
- **Skip** flyouts with ≤1 thumbnail.
- Preview timers are **independent** of app timers.
- Do **not** require the app to be in icon top-N.
- Preview ranks reuse the same intensity idea as icons (`previewIntensity[3]`;
  ranks 4+ reuse rank 3). Count is `previewHighlightCount` (0–16).

### Matching a `TaskItemThumbnailView` to an HWND

```
TaskItemThumbnail ctor (optional) → map { model, taskGroup, taskItem, hwnd }
TaskItemThumbnailView::OnApplyTemplate → collect siblings → assign HWNDs → paint
```

Resolve order in `RefreshThumbnailFlyout_UIThread`:

1. **TaskItem** — `DataContext` ↔ ctor map (COM identity compare). Often fails
   in practice (projection mismatch) even when maps exist. Rejected when the
   HWND title uniquely matches a *different* card (same-process ebooks).
2. **Group-order** — live mappings only (current flyout ctor order, not the
   previous hover). Find `taskGroup` with N unique HWNDs matching sibling
   count, or last N mapped HWNDs. Siblings sorted by `PositionInSet`.
   Dropped whenever card titles are distinct — index ≠ visual after a click.
3. **Title unique** — prefer `DisplayNameTextBlock` when those texts differ
   across siblings (ebook book names). Automation Name is often the shared
   `"App - 2 running windows"`. Each HWND used once. **Ambiguous** when two
   windows share the same title. Bracketed `[EPUB]` / `[PDF]` is a format
   tag, **not** a file path — only `[c:\…\file]` or `[name.ext]` is an
   identity key (Calibre used to bind every book to the first HWND).

Then sort siblings with a recency tick (tick, confirmSeq, foreground) and
paint the top `previewHighlightCount` at `previewIntensity` ranks.

Hooks for thumbnails are **optional**. Missing symbols: app ranks still work;
preview may fall back to title-only (weak for identical titles).

### Preview visuals (must not break layout)

Thumbnail cards use a Grid with **rows** (title | image). A normal child in
cell (0,0) expands the title row (bar between icon and text + card grows).

Rules:

1. Overlay host **spans all rows/columns** (`SpanHostOverPanel`) + Stretch.
2. Children positioned with **explicit size + `RenderTransform`** so measure
   does not include visual offset.
3. Own names: `WhRecentFocusThumbGlow`, `WhRecentFocusThumbTitleBar`,
   `WhRecentFocusThumbTitleBg`, marker `WhRecentFocusThumbNative` for plate.

| `previewStyle` | Implementation |
|----------------|----------------|
| `titleBar` | Thin rect just under title baseline (~2px gap) |
| `titleBg` | Soft wash; alpha = tint-opacity × rank intensity (linear) |
| `plate` | Tint `BackgroundBorder`; marker Tag holds the previous Brush (Taskbar Styler / template) and is restored on clear |
| `plateTitle` | Rank 1 = plate; ranks 2+ = titleBg |
| `ring` | Hollow frame via transform (placeholder) |

Clear always removes named overlays. Plate restores the saved `BackgroundBorder`
brush (or `ClearValue` if there was no local value) so Taskbar Styler tints
survive. If the marker cannot be created, fall back to our overlay plate.

---

## Architecture overview

```
┌─────────────────────────────────────────────────────────────┐
│ Focus thread (message-only HWND + GetMessage loop)          │
│  • SetWinEventHook FOREGROUND + DESKTOPSWITCH OUTOFCONTEXT  │
│  • App min-focus timer + preview min-focus timer + decay    │
│  • g_desktopMaps[desktopGuid] app + window recency          │
│  • RequestApplyVisuals / RequestApplyPreviewVisuals         │
└────────────────────────────┬────────────────────────────────┘
                             │
          ┌──────────────────┴──────────────────┐
          ▼                                     ▼
┌─────────────────────────┐       ┌─────────────────────────────┐
│ Explorer UI — icons     │       │ Explorer UI — thumbnails    │
│ TaskListButton hooks    │       │ TaskItemThumbnailView       │
│ path cache + glow host  │       │ ctor maps + overlay host    │
└─────────────────────────┘       └─────────────────────────────┘
```

### Why a dedicated focus thread?

`WINEVENT_OUTOFCONTEXT` needs a message pump for timers + `PostMessage`. Keeps
focus work off the critical taskbar paint path except when marshaling apply.

### Why marshal to the UI thread?

XAML must run on the tree’s dispatcher. `RunOnUiThread` uses a weak button
anchor. Log “no dispatcher anchor” **once** until the first button is seen.
`RequestApplyPreviewVisuals` must **not** `PostMessage` to the focus thread
on failure (that used to self-post `WM_APP_REQUEST_PREVIEW_APPLY` forever and
starve `WM_TIMER`). Same as `RequestApplyVisuals`: wait for the first button
or thumbnail `OnApplyTemplate`.

### Why hook `UpdateVisualStates`?

- Re-apply after Windows resets visuals (paint the **cached** rank only)
- Register buttons (`g_trackedButtons`)
- Same pattern as other taskbar mods
- Full identity rebind is debounced (300ms + trailing timer), not every hover

`LoadLibraryExW` covers late `Taskbar.View.dll` load.

---

## Recency engine decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| App rank key | Full image path (UPPER); `APPID:…` for ApplicationFrameHost / WWAHost | Distinct same-named exes; UWP apps share a host exe |
| Recency scope | Per virtual desktop GUID | Workspaces don’t share top-N |
| Current desktop | Registry `CurrentVirtualDesktop`, VDM fallback | Public APIs only |
| Window key | `HWND` + PID (per desktop map) | Multi-instance previews; PID rejects a recycled handle |
| Display name | Win32: file name; UWP host: window title (else AUMID stem) | Logs / exclude UX |
| App min focus | Default 8s | Alt+Tab noise |
| Promote mode | immediateTracked / immediateTopN / alwaysWait | When re-focus skips app min-focus |
| Preview min focus | Default 1s | Snappier window mark |
| Same-PID during app timer | Still same candidate (Win32). `APPID:` also needs same AUMID | New window of app; AFH is shared |
| App decay | Default 30 min | List stays “recent” |
| Preview decay | Default 15 min | Separate |
| Exclude list | Path / file / AppId, case-insensitive | Standard Windhawk UX |
| Shell hosts ignored | explorer, SearchHost, StartMenu, ShellHost, TextInputHost | Don’t rank the shell. AFH/WWAHost **are** ranked via `APPID:` |
| Highlight count | 0–16 (UI suggests 1–6) | Settings-capped |
| Preview highlight count | 0–16 (UI suggests 1–6) | Per-flyout cap |
| Tray-only | `requireTaskbarButton` default on | No TaskListButton ⇒ not ranked |

Promotion (apps):

1. Foreground → `g_pendingFocus` (tagged with current desktop GUID)
2. After `minFocusSeconds`, or immediately per `promoteMode` → that desktop’s app map tick
3. Sort → top `highlightCount` → that desktop’s `rankedApps`
4. UI apply uses **current** desktop’s ranks; `IsRunning` (plus 400ms grace)
5. Desktop switch → `EVENT_SYSTEM_DESKTOPSWITCH` / registry GUID change →
   load that desktop’s ranks and sweep overlays

Shared confirm helpers (do not fork another copy):

- `StillPendingForeground` — same PID still focused (new top-level HWND OK);
  `APPID:` keys also require the same AppUserModelID (AFH is shared)
- `StampWindowRecencyLocked` — preview HWND tick + confirmSeq + PID + prune
- `DecayMsFromMinutes` / `IsTickDecayed` / `RemainingDeadlineMs` — app map, window map, min-focus timers
- `SettingsSnap` / `PublishSettings` — immutable settings; never mutate in place

`KillTimer` does not flush a `WM_TIMER` already queued. App and preview
min-focus handlers (`FromTimer`) re-check the *current* candidate’s start tick
(`focusStartTick` / `previewStartTick`) and re-arm for the remainder instead
of confirming a newer pending focus early. `Immediate` (min=0 / promoteMode /
already-tracked window) skips that wait.

Alt-Tab UI, taskbar, desktop, and IME (`IsTransientForeground`) are **not**
a leave: do not clear `g_pendingFocus` or cancel min-focus timers. The landed
app often does not get a second `EVENT_SYSTEM_FOREGROUND`. Same-app
foreground events call `EnsurePendingAppTimer` so a stale `WM_TIMER` that
`KillTimer`’d the live one-shot cannot leave a candidate with no clock.

Promotion (windows): `previewMinFocusSeconds` → `StampWindowRecencyLocked` on
that desktop’s window map. On flyout open, siblings are sorted by **this
desktop’s** map (tick, then confirmSeq) and the top `previewHighlightCount`
get ranks 1…N. HWND resolve is TaskItem → group-order → unique title.

---

## Visual decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Icon chrome | Own `WhRecentFocusGlow` only | Never style `BackgroundElement` |
| Icon default | **Side bar** (`leftBar`) | Left on bottom/top taskbar; under icon on left/right — stays off the native running pill |
| Frame Z-order | Overlay last (above icon) | Stroke not covered |
| Full Z-order | Overlay first (behind icon) | Plate under glyph |
| Button identity | Option C path cache + fuzzy | Volume-per-app stack |
| Path cache | First UVS + force on press; 2s debounce | Not every paint |
| UVS vs rebind | UVS re-paints cached rank; full identity rebind on recency / 300ms debounce | Hover `UpdateVisualStates` must not O(buttons×ranks) every mouse-over. Siblings Windows resets without another UVS wait for the debounce. |
| Rank match | Only score 1000 (path / HWND / AUMID) is a replica. 96 name-cache and 900 filename are 1:1. | Settings must not copy onto Windows Security. Secondary taskbar still multi-binds exact path/AUMID. |
| Tray-only | `requireTaskbarButton` | Widgets / tray popups |
| Multi-monitor | Same cache on every tracked button | Secondary if UVS fires |
| Virtual desktops | Nested recency maps; no taskband reordering hooks | Explorer already filters `IsRunning` |
| Decay clear | Recompute + `g_pendingOverlaySweep` | No orphan plates |
| Never | `ClearValue` BackgroundElement; clip null ancestors | Pale hover leftovers |
| Preview layout | Span rows + RenderTransform | Title-row expansion bug |
| Preview titleBar | ~2px under baseline | Not hugging image; not strikethrough |
| Preview titleBg | Tint-opacity ceiling × linear rank intensity | Readable; 100 vs 5 must differ |
| Preview plate | BackgroundBorder tint via `previewFillOpacity` × rank; previous Brush stashed on marker Tag | Strong signal; Styler survives clear |
| Preview ranks | Per-flyout top N, `previewIntensity[3]` | Same ladder idea as icons |
| RunningIndicator | Never set Fill/Width/Height; never reorder every paint | Edge bar draws own pill. Glow host sits *under* a native thin pill, *above* a Taskbar Styler hover plate (RunningIndicator restyled to fill the icon cell — otherwise PointerOver acrylic covers the side bar). On taskbar-edge relayout restore z-order so the native pill is not left behind BackgroundElement. |
| Bar geometry | Size vs glow **host** (padded inner box), `Center` alignment | IconPanel is 48×32 on a left taskbar but the host is 40×28 (padding 4,2). Length is `size%` of that cell, **same for every rank** (rank is opacity). Icon-width underlines on a left taskbar are too short to scan. |
| RunningIndicator on style switch | Cover Edge bar via z-order only. Never ClearValue Visibility/Width/Height, never GoToState | VSM stores InactiveRunningIndicator `Visible` as a local value. ClearValue → template Collapsed. GoToState of the *current* state is a no-op, so the short unfocused pill stays gone. |
| Bar auto-rotate | `leftBar` = side (perpendicular); `bottomBar` = edge (screen edge) | Settings keys stay `leftBar`/`bottomBar`. Detect: `VerticalOrientation` / panel 48×32 (wider than tall ⇒ **vertical** bar) first. Do not treat leftover RunningIndicator `VA=Bottom` as a bottom taskbar. |
| OverlayIcon | Keep after Icon / DefaultIcon | Discord/Thunderbird/WhatsApp badge; our host insert can leave it behind the glyph |
| Size boost | Icon `ScaleTransform` only | No layout width change |
| Hit testing | `IsHitTestVisible=False` | Clicks pass through |
| Coexistence | Own names; plate save/restore `BackgroundBorder`; side bar above Styler hover plate | Taskbar Styler (themes restyle `RunningIndicator` into a full-cell acrylic on PointerOver) |
| Pinned-only | No highlight | Product rule |

**Not done yet:** Composition `DropShadow` / true GPU glow. Large spread may clip.

---

## Threading & state map

| Object | Guard | Thread |
|--------|--------|--------|
| `g_desktopMaps`, `g_currentDesktopId`, `g_pendingFocus`, `g_keyToAutomationName` | `g_stateMutex` | Focus write; UI read under lock |
| `g_vdm` | `g_vdmMutex` | Lazy public `IVirtualDesktopManager` |
| `g_trackedButtons`, `g_dispatcherAnchor` | `g_buttonsMutex` | UI primarily |
| `g_buttonPathCache` (includes `lastPaintRank`) | `g_buttonPathMutex` | UI / resolve / paint-only UVS |
| `g_thumbnailTaskItemMapping` | `g_thumbnailMapMutex` | Taskband / UI |
| `g_trackedThumbViews` | `g_thumbViewsMutex` | UI |
| `g_layoutWatches` | `g_layoutWatchMutex` | UI (`IconPanel` SizeChanged). Revoke only on the panel’s dispatcher. |
| `g_settingsPtr` (`SettingsSnap`) | `g_settingsMutex` + `shared_ptr<const Settings>` | LoadSettings publishes a whole object; any thread copies the pointer under the mutex. Never take `g_settingsMutex` then `g_stateMutex`. |
| Click sentinels (`g_clickSentinel_Task*`) | `thread_local` + save/restore | Nested / cross-thread ReportClicked |
| `g_unloading` | atomic | Any |
| Focus thread shutdown | `WM_APP_SHUTDOWN` to the message HWND + `PostThreadMessage`; wait 8s and log if still running | Uninit |
| UI uninit | `RunOnEachUiDispatcherAndWait` (High cleanup + Low drain) | Revoke `SizeChanged` per dispatcher; do not `Sleep` and hope |
| Native probing | `QueryViaVtable` max 32 slots; task-items array offset under 64; fail closed | Explorer-safe |

Do **not** hold `g_stateMutex` or `g_layoutWatchMutex` across XAML or `Dispatcher` calls.
`SettingsSnap()` takes only `g_settingsMutex` and is safe under `g_stateMutex`
(lock order: state → settings). Do not publish settings while holding `g_stateMutex`.

---

## Settings ↔ code

YAML keys map to `LoadSettings()`, which fills a local `Settings` then
`PublishSettings()` (mutex + `shared_ptr<const Settings>`). Readers use
`SettingsSnap()`.
Windhawk rejects unknown metadata such as `$group` (schema: no additional
properties). UI grouping is done with **list order** and `$name` prefixes:
`[General]`, `[Icons]`, `[Previews]`, `[Advanced]`. **Keys are stable** for
saved configs.

| Group | Setting key | Field |
|-------|-------------|--------|
| General | `enabled` | `Settings::enabled` |
| General | `highlightCount` | `Settings::highlightCount` |
| General | `minFocusSeconds` | `Settings::minFocusSeconds` |
| General | `promoteMode` | `ImmediateTracked` / `ImmediateTopN` / `AlwaysWait` |
| General | `decayMinutes` | app decay |
| General | `requireTaskbarButton` | tray-only filter |
| General | `excludedPrograms[i]` | uppercase set |
| Taskbar icons | `glowStyle` | `LeftBar` (side bar) / `Frame` / `Full` / `BottomBar` (edge bar) |
| Taskbar icons | `glowColor` / `customGlowColor` | color mode + hex |
| Taskbar icons | `glowIntensityRank1..3` | `glowIntensity[3]` |
| Taskbar icons | `glowThickness` / `glowRoundness` / `glowSize` / `glowLayers` | metrics |
| Taskbar icons | `glowFillOpacity` | Full / side / edge icon bar strength |
| Taskbar icons | `sizeBoostRank1..3` | `sizeBoostPercent[3]` |
| Thumbnail previews | `previewHighlightEnabled` | preview master (also needs `enabled`) |
| Thumbnail previews | `previewHighlightCount` | per-flyout top N |
| Thumbnail previews | `previewStyle` | `titleBar` / `titleBg` / `plate` / `plateTitle` / `ring` |
| Thumbnail previews | `previewIntensityRank1..3` | `previewIntensity[3]` |
| Thumbnail previews | `previewFillOpacity` | plate + titleBg wash (not title bar line) |
| Thumbnail previews | `previewMinFocusSeconds` | window confirm |
| Thumbnail previews | `previewDecayMinutes` | window decay |
| Advanced | `glowDebugLog` | verbose bind + preview resolve logs |

Ranks beyond 3 reuse rank-3 intensity/size (icons and previews separately).
Preview reuses icon color and shared thickness/roundness; plate/titleBg use
`previewFillOpacity` scaled by that window’s preview intensity (no size boost
on thumbnails).

---

## File layout

```
whawk-lru/
  README.md                              # product + high-level architecture
  AGENTS.md                              # this file
  taskbar-recent-focus-highlight.wh.cpp  # single translation unit for Windhawk
  example/                               # reference mods (read-only)
```

Keep helpers in the one `.wh.cpp` unless the mod is split for non-Windhawk builds.

---

## When changing code

1. **Matching bugs (wrong icon):** prefer path cache / AppId over fuzzier names.
2. **Matching bugs (wrong preview):** prefer TaskItem HWND maps / group-order;
   never assign the same HWND to two siblings; don’t rely on title for twins.
3. **Visual bugs:** [UWPSpy](https://ramensoftware.com/uwpspy); names vary by build.
4. **Layout bugs on thumbnails:** never add sized children only to grid row 0;
   use span + transform.
5. **Crashes:** try/catch around XAML; don’t block focus hooks; clear on uninit.
   Bound `QueryViaVtable` / task-item array offsets; fail closed on miss.
   Uninit must wait for each dispatcher (High cleanup + Low drain) and the
   focus thread; never `Sleep` and hope. Revoke `SizeChanged` only on that
   panel’s dispatcher.
6. **WinRT collections:** include `winrt/Windows.Foundation.Collections.h`.
7. **Hooks:** `WindhawkUtils::SetFunctionHook` / `SYMBOL_HOOK` with **optional**
   for thumbnail symbols so older builds still load.
8. **Test:** app min-focus 0–1s; preview 0–1s (explorer start with preview
   min 0 must not spin the focus thread / starve timers); rapid Alt-Tab
   between two unranked apps then rest on one — that app must still confirm
   after min-focus (switcher/tray must not drop the candidate); three windows of one app (ranks
   1>2>3 in that flyout only); two same-title windows; debug log
   `Preview resolve:` + `sibling[` + `rank=` + `how=taskitem|group-order|title`;
   disable clears all chrome;
   two virtual desktops: glow on D1 must not remain on pinned-not-running
   icons on D2; D1 ranks return after switching back. Move the taskbar to
   left/right/top: side bar must not cover the native running pill; unranked
   icons keep their dots after relayout; edge bar follows the screen edge
   and is centered on the icon. Hover-storm: ranked glows stay without
   `ApplyAllHighlights` log spam (debug off). Total Commander + Lister:
   focusing Lister must glow Lister, not Commander (preview flyout ranks
   Lister windows independently). Two copies of the same exe in different
   folders must not share one icon rank. Disable/unload while a flyout is
   open and while hovering: chrome gone, no explorer crash, Styler plate
   tints return. UWP: Calculator vs Settings (ApplicationFrameHost) must
   get separate icon ranks; Windows Security must **not** copy Settings.
   Taskbar Styler: hover a ranked icon — side bar stays; vanilla native pill
   still shows. Two `python.exe` folders stay distinct.

### Useful log substrings

| Substring | Meaning |
|-----------|---------|
| `Focus candidate:` / `Confirmed focus:` | App recency |
| `Current virtual desktop:` / `Virtual desktop switch:` | Per-desktop recency |
| `Preview focus confirmed:` | Window recency |
| `Preview click confirmed:` | Thumbnail / grouped-icon click → window recency |
| `HWND recycled` | Preview map dropped a reused handle (PID mismatch) |
| `Preview resolve:` / `sibling[` | Per-card HWND + `how=taskitem\|group-order\|title` |
| `ApplyAllHighlights` | Full identity rebind (debug log only) |
| `Hooked Taskbar.View.dll` | View symbols |
| `thumbnail OnApplyTemplate unavailable` | Optional miss |
| `no dispatcher anchor` | Before first button (logged once) |
| `ERROR: UI dispatcher cleanup` / `SizeChanged watches not revoked` / `focus thread still running` | Unload handshake failed — explorer may crash |
| `Button path cache:` | Option C resolve |
| `IconPanel relayout:` / `Taskbar edge` | Button size / screen-edge change (heal running dots) |

---

## Future work (ordered suggestions)

Done in 0.9.x and not listed: UWP `APPID:` keys, preview plate brush restore,
Styler hover-plate z-order, settings snapshots, timer deadlines, transient
foreground, bounded vtable probe, deterministic unload.

1. Composition shadow / true GPU outer glow if XAML halo stays clipped
   (optional polish; current bar/frame/plate is the product).
2. Stronger DataContext ↔ TaskItemThumbnail identity (less group-order).
3. Classic / non-XAML thumbnail path if still needed on some builds.
4. Multi-monitor secondary taskbars if weak refs only cover primary.
5. Per-desktop prune of deleted virtual desktop GUIDs beyond decay.
6. Per-monitor taskbar edge if a secondary bar can sit on a different side.
7. Test seam + `make release` concat (pure ranking/identity table tests).
