# Agent / contributor guide

Developer context for `taskbar-recent-focus-highlight.wh.cpp` (v0.7.x). Read this
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
| Process image path | `C:\…\WindowsTerminal.exe` | Stable **key** for app ranking |
| File name | `WindowsTerminal.exe` | Logs, exclude list, fuzzy match |
| PID | `12345` | Min-focus “still same app?” checks |
| HWND | window handle | Preview recency map key |

The app recency map is keyed by **uppercase full path**. That is deliberate: two
different `foo.exe` binaries in different folders stay distinct.

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
   score 1000 exact / 900 same file name. Same stack as volume-per-app.
2. **Cached automation name** — after learn `path → "Windows Terminal"`.
3. **Fuzzy / title scores** — alphanumeric compare, greedy 1:1 assignment.
4. **Active-button association** — on confirm, store active running button name.

Pinned-only icons: no highlight (`IsRunning == false`).

### Why not only match on “active button”?

Only the **currently focused** app is Active. Ranks 2 and 3 are recent but
**not** active, so they need identity matching.

### Known gaps (app matching)

| Gap | Impact | Possible fix |
|-----|--------|----------------|
| UWP / `ApplicationFrameHost.exe` | Skipped today | AppUserModelID / package family |
| Fuzzy false positive/negative | Wrong or missing glow | Prefer path cache / AppId |
| Combined icons | One button per app group | Correct for combined mode |
| Localization of automation names | Fuzzy may fail | Path cache + active association |

---

## Window-level recency + thumbnail matching

App ranks answer “which **app**?”. Thumbnail ranks answer “which **windows**
in this flyout?”.

| Layer | Key | Timers |
|-------|-----|--------|
| App ranks | Process path (UPPER) | `minFocusSeconds`, `decayMinutes` |
| Preview glow | `HWND` in `g_windowFocusMap` | `previewMinFocusSeconds`, `previewDecayMinutes` |

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
| `plate` | Prefer tint `BackgroundBorder`; marker-cleared on clear |
| `plateTitle` | Rank 1 = plate; ranks 2+ = titleBg |
| `ring` | Hollow frame via transform (placeholder) |

Clear always removes named overlays; plate clears local Background when marker present.

---

## Architecture overview

```
┌─────────────────────────────────────────────────────────────┐
│ Focus thread (message-only HWND + GetMessage loop)          │
│  • SetWinEventHook(EVENT_SYSTEM_FOREGROUND) OUTOFCONTEXT    │
│  • App min-focus timer + preview min-focus timer + decay    │
│  • g_appFocusMap / g_rankedApps / g_windowFocusMap          │
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

### Why hook `UpdateVisualStates`?

- Re-apply after Windows resets visuals
- Register buttons (`g_trackedButtons`)
- Same pattern as other taskbar mods

`LoadLibraryExW` covers late `Taskbar.View.dll` load.

---

## Recency engine decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| App rank key | Full image path (UPPER) | Distinct same-named exes |
| Window key | `HWND` | Multi-instance previews |
| Display name | File name only | Logs / exclude UX |
| App min focus | Default 8s | Alt+Tab noise |
| Promote mode | immediateTracked / immediateTopN / alwaysWait | When re-focus skips app min-focus |
| Preview min focus | Default 1s | Snappier window mark |
| Same-PID during app timer | Still same candidate | New top-level window of app |
| App decay | Default 30 min | List stays “recent” |
| Preview decay | Default 15 min | Separate |
| Exclude list | Path / file / AppId, case-insensitive | Standard Windhawk UX |
| Shell hosts ignored | explorer, AFH, SearchHost, … | Don’t rank the shell |
| Highlight count | 0–16 (UI suggests 1–6) | Settings-capped |
| Preview highlight count | 0–16 (UI suggests 1–6) | Per-flyout cap |
| Tray-only | `requireTaskbarButton` default on | No TaskListButton ⇒ not ranked |

Promotion (apps):

1. Foreground → `g_pendingFocus`
2. After `minFocusSeconds`, or immediately per `promoteMode` → app map tick
3. Sort → top `highlightCount` → `g_rankedApps`
4. UI apply

Promotion (windows): parallel with `previewMinFocusSeconds` → `g_windowFocusMap`.
On flyout open, siblings are sorted by that map (tick, then confirmSeq) and
the top `previewHighlightCount` get ranks 1…N.

---

## Visual decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Icon chrome | Own `WhRecentFocusGlow` only | Never style `BackgroundElement` |
| Icon default | **Left bar** | Preferred product default |
| Frame Z-order | Overlay last (above icon) | Stroke not covered |
| Full Z-order | Overlay first (behind icon) | Plate under glyph |
| Button identity | Option C path cache + fuzzy | Volume-per-app stack |
| Path cache | First UVS + force on press; 2s debounce | Not every paint |
| Rank match | Path 1000 / file 900 / fuzzy | 1:1 greedy |
| Tray-only | `requireTaskbarButton` | Widgets / tray popups |
| Multi-monitor | Same cache on every tracked button | Secondary if UVS fires |
| Decay clear | Recompute + `g_pendingOverlaySweep` | No orphan plates |
| Never | `ClearValue` BackgroundElement; clip null ancestors | Pale hover leftovers |
| Preview layout | Span rows + RenderTransform | Title-row expansion bug |
| Preview titleBar | ~2px under baseline | Not hugging image; not strikethrough |
| Preview titleBg | Tint-opacity ceiling × linear rank intensity | Readable; 100 vs 5 must differ |
| Preview plate | BackgroundBorder tint via `previewFillOpacity` × rank | Strong signal |
| Preview ranks | Per-flyout top N, `previewIntensity[3]` | Same ladder idea as icons |
| RunningIndicator | Never set Fill/Width/Height; never reorder every paint | BottomBar draws own pill; glow host sits *under* native chrome |
| OverlayIcon | Keep after Icon / DefaultIcon | Discord/Thunderbird/WhatsApp badge; our host insert can leave it behind the glyph |
| Size boost | Icon `ScaleTransform` only | No layout width change |
| Hit testing | `IsHitTestVisible=False` | Clicks pass through |
| Coexistence | Own names; clear on unload | Taskbar Styler friendlier |
| Pinned-only | No highlight | Product rule |

**Not done yet:** Composition `DropShadow` / true GPU glow. Large spread may clip.

---

## Threading & state map

| Object | Guard | Thread |
|--------|--------|--------|
| `g_appFocusMap`, `g_rankedApps`, `g_pendingFocus`, `g_keyToAutomationName`, `g_windowFocusMap` | `g_stateMutex` | Focus write; UI read under lock |
| `g_trackedButtons`, `g_dispatcherAnchor` | `g_buttonsMutex` | UI primarily |
| `g_buttonPathCache` | `g_buttonPathMutex` | UI / resolve |
| `g_thumbnailTaskItemMapping` | `g_thumbnailMapMutex` | Taskband / UI |
| `g_trackedThumbViews` | `g_thumbViewsMutex` | UI |
| `g_settings` | init / settings-changed | Read-mostly |
| `g_unloading` | atomic | Any |

Do **not** hold `g_stateMutex` across XAML or `Dispatcher` calls.

---

## Settings ↔ code

YAML keys map to `LoadSettings()`. Windhawk rejects unknown metadata such as
`$group` (schema: no additional properties). UI grouping is done with **list
order** and `$name` prefixes: `[General]`, `[Icons]`, `[Previews]`,
`[Advanced]`. **Keys are stable** for saved configs.

| Group | Setting key | Field |
|-------|-------------|--------|
| General | `enabled` | `g_settings.enabled` |
| General | `highlightCount` | `g_settings.highlightCount` |
| General | `minFocusSeconds` | `g_settings.minFocusSeconds` |
| General | `promoteMode` | `ImmediateTracked` / `ImmediateTopN` / `AlwaysWait` |
| General | `decayMinutes` | app decay |
| General | `requireTaskbarButton` | tray-only filter |
| General | `excludedPrograms[i]` | uppercase set |
| Taskbar icons | `glowStyle` | `LeftBar` / `Frame` / `Full` / `BottomBar` |
| Taskbar icons | `glowColor` / `customGlowColor` | color mode + hex |
| Taskbar icons | `glowIntensityRank1..3` | `glowIntensity[3]` |
| Taskbar icons | `glowThickness` / `glowRoundness` / `glowSize` / `glowLayers` | metrics |
| Taskbar icons | `glowFillOpacity` | Full / left / bottom icon bar strength |
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
6. **WinRT collections:** include `winrt/Windows.Foundation.Collections.h`.
7. **Hooks:** `WindhawkUtils::SetFunctionHook` / `SYMBOL_HOOK` with **optional**
   for thumbnail symbols so older builds still load.
8. **Test:** app min-focus 0–1s; preview 0–1s; three windows of one app (ranks
   1>2>3 in that flyout only); two same-title windows; debug log
   `Preview resolve:` + `sibling[` + `rank=`; disable clears all chrome.

### Useful log substrings

| Substring | Meaning |
|-----------|---------|
| `Focus candidate:` / `Confirmed focus:` | App recency |
| `Preview focus confirmed:` | Window recency |
| `Preview click confirmed:` | Thumbnail / grouped-icon click → window recency |
| `Preview resolve:` / `sibling[` | Per-card HWND + `how=taskitem\|group-order\|title` |
| `ApplyAllHighlights` | Icon apply / empty ranks |
| `Hooked Taskbar.View.dll` | View symbols |
| `thumbnail OnApplyTemplate unavailable` | Optional miss |
| `no dispatcher anchor` | Before first button (logged once) |
| `Button path cache:` | Option C resolve |

---

## Future work (ordered suggestions)

1. Composition shadow / true GPU outer glow if XAML halo stays clipped.
2. Reliable UWP identity (AppUserModelID / package).
3. Stronger DataContext ↔ TaskItemThumbnail identity (less group-order reliance).
4. Classic / non-XAML thumbnail path if still needed on some builds.
5. Multi-monitor secondary taskbars if weak refs only cover primary.
