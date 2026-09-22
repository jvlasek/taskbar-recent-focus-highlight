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
| File name / window title | `WindowsTerminal.exe` / `Settings` | Logs, exclude list |
| PID | `12345` | Min-focus “still same app?” (Win32) |
| HWND + PID | window handle | Preview recency map (PID rejects recycle). App ranks store `lastHwnd` **and** `lastPid` for the same reason. |

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
ranked apps (path / APPID)
        │
        ▼
   path cache (option C): HWND / AUMID / full path
        │
        ▼
   ApplyButtonHighlight(rank)  or  ClearButtonHighlight()
```

Order of preference (icons — **no name fuzzy**):

1. **HWND** on the button’s task item / group — score 1000.
2. **AppUserModelID** — only when the rank key is `APPID:…` (UWP host). Button
   AutomationId / window AUMID, score 1000. Mismatch → 0. Do not use PID+class:
   AFH is shared.
3. **Process path cache** — button → HWND/PID → image path; score 1000 exact.
   Same path but **different window class** → 0 (two icons from one process).
   No filename-only score. An empty resolve is **not** permanent. A pinned
   AutomationId is **not** a finished Win32 resolve (rank key is the image
   path). Re-resolve on each not-running → running episode
   (`resolvedWhileRunning` and the empty-resolve cap clear when a button
   that was not running is observed running again). Otherwise
   `lastResolveTick` throttles retries.
   A successful path is not forever: if the button is running and the cached
   HWND is dead (or `GetProcessImagePath` of that HWND no longer equals
   `pathUpper`), re-resolve. Explorer reuses a `TaskListButton` when the exe
   is deleted/renamed and another copy with the same name launches. Do not
   treat a missing file on disk as stale (deleted-but-still-running).

Only **score 1000** may bind the same rank to many buttons (secondary taskbar /
Never Combine). If the taskband resolve is missing, **do not glow** — a wrong
icon is worse than none.

Pinned-only icons: no highlight (`IsRunning == false`). Virtual-desktop lists
are separate: a pinned icon that is not running on this desktop must not glow.

### Why not only match on “active button”?

Only the **currently focused** app is Active. Ranks 2 and 3 are recent but
**not** active, so they need identity matching (path / AUMID / HWND).

### Known gaps (app matching)

| Gap | Impact | Possible fix |
|-----|--------|----------------|
| Combined icons | One button per app group | Correct for combined mode |
| Taskband hooks missing | No icon glow | Fail closed (no name guess) |

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

1. **TaskItem** — `DataContext` ↔ ctor map (COM identity, per card). Exact;
   cannot bind another flyout’s HWNDs.
2. **Repeater index** — `ItemsRepeater.TryGetElement(i)` + `Thumbnails.GetAt(i)`
   + ctor map, **holes only**. `g_TaskGroup_Thumbnails` is a global captured
   on `TargetItemKey`. **Clear it on the way in** to that hook so it is
   non-null only for the current target. Skip GetAt if sizes disagree, or if a
   DataContext HWND on the same card disagrees with GetAt. Snap-group extra
   in the repeater: compact to window ordinal. Compute `IsSnapGroupThumbnailView`
   once per flyout (it walks the tree + ctor map).
3. **No unique-title.** Holes stay unmarked. Stash:
   `stash/preview-unique-title.cpp`.

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
live in-repeater tracked view. Thumbnail / grouped-icon click posts
`WM_APP_PREVIEW_CLICK` to the focus thread; `ConfirmPreviewFocusNow` does not
run inside `HandleClick`. Ctor-map `taskItem` is compared, never dereferenced
after the HWND is captured.

Hooks for thumbnails are **optional**. Missing symbols: app ranks still work;
preview cards without a TaskItem/repeater HWND stay unmarked.

### Preview visuals (must not break layout)

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
| `titleBar` | Thin rect just under title baseline (~2px gap); 8px side inset |
| `titleBg` | Soft wash; 4px corners (`CornerRadius{4,4,4,4}` — `{4}` is TopLeft only), 8px inset |
| `plate` | Tint `BackgroundBorder`. The marker remembers the displaced brush and the brush this mod installed. Restore only while the border still has our brush; a later owner is saved on the next takeover. Zero fill does not replace the native background. Unset and explicit null stay distinct |
| `plateTitle` | Rank 1 = plate; ranks 2+ = titleBg |

Clear (unload / unranked) removes named overlays. A ranked repaint keeps
`WhRecentFocusThumbGlow` and only hides children + restores the plate brush.
Plate restores the saved `BackgroundBorder` only while that border still has
the brush this mod installed, so a newer Styler tint survives. Unset local
value uses `ClearValue`; an explicit null is written back as null. Zero fill
opacity does not install a transparent brush. If the marker cannot be
created, fall back to our overlay plate.

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
| App last HWND | `lastHwnd` + `lastPid` | Same recycle guard as preview; HWND-only would bind the rank to whichever button now owns the handle |
| Display name | Win32: file name; UWP host: window title (else AUMID stem) | Logs / exclude UX |
| App min focus | Default 8s | Alt+Tab noise |
| Promote mode | immediateTracked / immediateTopN / alwaysWait | When re-focus skips app min-focus |
| Preview min focus | Default 1s; independent of app min-focus | Snappier window mark. App confirm does not stamp the current HWND. |
| Same-PID during app timer | Still same candidate (Win32). `APPID:` also needs same AUMID | New window of app; AFH is shared |
| App decay | Default 30 min | List stays “recent” |
| Preview decay | Default 15 min | Separate |
| Exclude list | Path / file / AppId, case-insensitive | Standard Windhawk UX |
| Shell hosts ignored | SearchHost, StartMenu, ShellHost, TextInputHost; explorer **except** `CabinetWClass` / `ExploreWClass` | Don’t rank the shell. Folder windows are a real app (same process as the taskbar). AFH/WWAHost **are** ranked via `APPID:` |
| Highlight count | 0–16 (UI suggests 1–6) | Settings-capped |
| Preview highlight count | 0–16 (UI suggests 1–6) | Per-flyout cap |
| Tray-only | `requireTaskbarButton` default on | No TaskListButton ⇒ not ranked. Unmatched + exact path/AUMID gone from the cache, or last observed not-running after grace, ⇒ demote even if `seenOnTaskbar` (filename is not “still on the taskbar”). |

Promotion (apps):

1. Foreground → `g_pendingFocus` (tagged with current desktop GUID)
2. After `minFocusSeconds`, or immediately per `promoteMode` → that desktop’s app map tick
3. Sort → top `highlightCount` → that desktop’s `rankedApps`
4. UI apply snapshots `IsRunning` into `observedRunning`, then recomputes
   eligibility (even if the rank list was empty). Grace is only for a
   just-observed not-running flicker, not a 400 ms heartbeat.
5. Desktop switch → `EVENT_SYSTEM_DESKTOPSWITCH` / registry GUID change →
   clear observed running, load that desktop’s history, UI snapshot + recompute,
   sweep overlays

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
already-tracked window / preview click) skips that wait. A positive minimum
whose deadline has already elapsed resumes through `FromTimer`, so Alt-Tab
grace still applies. Settings changes cancel timers only for a newly excluded
candidate; an allowed pending episode is re-armed with `EnsurePendingAppTimer`
/ `EnsurePendingPreviewTimer`. The 30 s decay timer is **not**
started with the focus thread; first confirmed app or window recency arms it,
and `OnDecayTimer` stops it when every desktop map is empty and there is no
pending focus.

Alt-Tab UI, taskbar, desktop, and IME (`IsTransientForeground`) are **not**
a leave: do not clear `g_pendingFocus` or cancel min-focus timers. The landed
app often does not get a second `EVENT_SYSTEM_FOREGROUND`. After the original
deadline, re-arm at 200 ms only for `TransientGraceMs` (same as min-focus,
at least 2 s), then drop the candidate — File Explorer used to poll forever
because it is this `explorer.exe`. Folder windows (`CabinetWClass`) are a
real leave and can be ranked. Same-app foreground events call
`EnsurePendingAppTimer` so a stale `WM_TIMER` that `KillTimer`’d the live
one-shot cannot leave a candidate with no clock. Transient and “ranks already
exist” repaints use `RequestApplyVisualsDebounced` (300 ms, skip `SetTimer`
if already armed) so Alt+Tab is one full bind, not two. Confirm, decay, and
desktop switch stay immediate.

Promotion (windows): `previewMinFocusSeconds` → `StampWindowRecencyLocked` on
that desktop’s window map. On flyout open, siblings are sorted by **this
desktop’s** map (tick, then confirmSeq) and the top `previewHighlightCount`
get ranks 1…N. HWND resolve is TaskItem → repeater GetAt (no unique-title).
(same process / AUMID; skipped with no exact sibling).

---

## Visual decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Icon chrome | Own `WhRecentFocusGlow` only | Never style `BackgroundElement` |
| Icon default | **Side bar** (`leftBar`) | Left on bottom/top taskbar; under icon on left/right — stays off the native running pill |
| Frame Z-order | Overlay last (above icon) | Stroke not covered |
| Full Z-order | Overlay first (behind icon) | Plate under glyph |
| Button identity | Option C path cache only (HWND / AUMID / path). No automation-name fuzzy. | Wrong glow is worse than none. Catalog review. |
| Path cache | Full bind / press only. Keyed by `IUnknown*` **and** the live weak_ref. AutomationId on a pinned button is not a finished Win32 identity. Each not-running → running episode clears `resolvedWhileRunning` and the empty-resolve cap, then resolves again; `kUnresolvedRetryMs` throttles other retries. Running + dead sample HWND, PID mismatch, or live image-path mismatch also re-resolves. Empty re-resolve does not wipe a known path. `observedRunning` is the last UI `IsRunning` snapshot (sticky until the UI sees false). | UVS must not `ReportClicked`. Explorer reuses the same `TaskListButton` on launch **and** when the exe is replaced by another folder’s copy. A capped empty resolve must not stick across close and relaunch. |
| UVS vs rebind | Cached paint: **-1** unknown, **0** unranked, **>0** paint if `(rank, generation, accent, edge, panel size)` changed. | `{rank, gen, accent}` alone left the bar on the old side after a taskbar-edge / icon-size relayout. |
| Native z-order | `RestoreIconPanelNativeZOrder` only after we insert or remove `WhRecentFocusGlow`. Snapshot child names **before** the first move; restore that list, not an assumed stock order. | Healing unranked buttons fights Taskbar Styler. Stock restore rewrote Styler themes on disable. |
| Rank match | Exact path / HWND / AUMID only. Cached group windows and the sample HWND store the PID captured with the handle; `IdentityMatchesRank` accepts that HWND only while `HwndMatchesStoredPid` still holds. `PathAppearsOnTaskbar` is exact path / AUMID **and** a button last observed running (`observedRunning`, or 400 ms grace after a not-running snapshot). Pinned-only / closed must not occupy a top-N slot. A path-cache row whose button is already dead is erased on the UI thread (`PruneDeadButtonPathCache_UIThread`, from `CollectLiveButtons`) before eligibility; this function does not `weak.get()`. | Two folders of `python.exe` stay distinct; closing a pinned app (or moving the exe on update) frees the slot. Idle apps must not drop because no hover refreshed a tick. A destroyed button must not keep the slot. A recycled group HWND must not light the old button. |
| Tray-only | `requireTaskbarButton` | Widgets / tray popups |
| Multi-monitor | Same cache on every tracked button | Secondary if UVS fires |
| Virtual desktops | Nested recency maps; no taskband reordering hooks | Explorer already filters `IsRunning` |
| Decay clear | Recompute + `g_pendingOverlaySweep` | No orphan plates |
| Never | `ClearValue` BackgroundElement; clip null ancestors | Pale hover leftovers |
| Preview layout | Span rows + Margin (not RenderTransform) | Title-row expansion; transform clipped the right cap |
| Preview titleBar | ~2px under baseline | Not hugging image; not strikethrough |
| Preview titleBg | Tint-opacity ceiling × linear rank intensity; 4px rounded rect, 6px inset both sides | Readable; 100 vs 5 must differ |
| Preview plate | BackgroundBorder tint via `previewFillOpacity` × rank. Marker stores the displaced brush and our installed brush. Restore only while the border’s current brush is still ours; the next takeover saves whoever owns it now. Zero fill does not replace the background | Strong signal; a Styler change during the flyout survives repaint and clear |
| Preview ranks | Per-flyout top N, `previewIntensity[3]` | Same ladder idea as icons |
| RunningIndicator | Never set Fill/Width/Height; never reorder every paint | Edge bar draws own pill. Glow host sits *under* a native thin pill, *above* a Taskbar Styler hover plate (RunningIndicator restyled to fill the icon cell — otherwise PointerOver acrylic covers the side bar). On taskbar-edge relayout restore z-order so the native pill is not left behind BackgroundElement. |
| Bar geometry | Size vs glow **host** (padded inner box), `Center` alignment | IconPanel is 48×32 on a left taskbar but the host is 40×28 (padding 4,2). Length is `size%` of that cell, **same for every rank** (rank is opacity). Icon-width underlines on a left taskbar are too short to scan. |
| RunningIndicator on style switch | Cover Edge bar via z-order only. Never ClearValue Visibility/Width/Height, never GoToState | VSM stores InactiveRunningIndicator `Visible` as a local value. ClearValue → template Collapsed. GoToState of the *current* state is a no-op, so the short unfocused pill stays gone. |
| Bar auto-rotate | `leftBar` = side (perpendicular); `bottomBar` = edge (screen edge) | Settings keys stay `leftBar`/`bottomBar`. Detect: `VerticalOrientation` / panel 48×32 (wider than tall ⇒ **vertical** bar) first. Do not treat leftover RunningIndicator `VA=Bottom` as a bottom taskbar. |
| OverlayIcon | Keep after Icon / DefaultIcon | Discord/Thunderbird/WhatsApp badge; our host insert can leave it behind the glyph |
| Size boost | Icon `ScaleTransform` only; remember our instance and clear only that object. Previous local transform lives on the glow host Tag (not a cache `weak_ref`) and is replaced on every new takeover. Zero intensity/boost still calls `ClearIconScaleIfOurs` (do not skip on the empty-visual return). | Other mods (taskbar-dock-animation) scale the same `Icon` |
| Rank intensity | Element Opacity (bars/frames) or brush alpha (native plate / titleBg) **once** | Do not also multiply fill/stroke alpha by `t`. 100/80/60 must read as 100/80/60, not ~100/64/36. |
| Hit testing | `IsHitTestVisible=False` | Clicks pass through |
| Coexistence | Own names; plate save/restore `BackgroundBorder`; side bar above Styler hover plate | Taskbar Styler (themes restyle `RunningIndicator` into a full-cell acrylic on PointerOver) |
| Pinned-only | No highlight | Product rule |

**Not done yet:** Composition `DropShadow` / true GPU glow. Large spread may clip.

---

## Threading & state map

| Object | Guard | Thread |
|--------|--------|--------|
| `g_desktopMaps`, `g_currentDesktopId`, `g_pendingFocus` | `g_stateMutex` | Focus write; UI read under lock |
| `g_vdm` | `g_vdmMutex` | Created/used/released **only on the focus thread**. UI reads cached `g_currentDesktopId`. |
| `g_trackedButtons` | `g_buttonsMutex` | UI. `unordered_map<IUnknown*, weak_ref>`. Do not `weak.get()` off the UI thread. |
| `g_uiDispatchers` | `g_dispatchersMutex` | `[[clang::no_destroy]] optional<vector<CoreDispatcher>>`. Capture on UI thread. `reset()` in Uninit. Never `weak.get()` XAML off-thread. |
| `g_hookThreadHwnd` | `std::atomic<HWND>` | Focus thread writes; others `PostMessage` / `HookThreadWindow()`. `SetTimer` only on the owner thread. Ready event before `Start` returns. Decay timer armed on first confirm, stopped when maps are empty. |
| `g_buttonPathCache` (includes `lastPaintRank`, `lastPaintSettingsGen`, `lastPaintAccent`, `ourIconScale`, `observedRunning`) | `g_buttonPathMutex` | UI. `unordered_map<IUnknown*, entry>`. Resolve on full bind / press; UVS paints cached rank. Drop entry if weak_ref is not this button. `observedRunning` is written on the UI thread; focus-thread `PathAppearsOnTaskbar` reads it. |
| `g_thumbnailTaskItemMapping` | `g_thumbnailMapMutex` | Taskband / UI |
| `g_trackedThumbViews` | `g_thumbViewsMutex` | UI |
| `g_layoutWatches` | `g_layoutWatchMutex` | UI. `unordered_map` keyed by panel identity. Install only when we paint. Revoke only on that panel’s dispatcher. Never register `SizeChanged` unless `RememberUiDispatcher` recorded the thread. |
| `g_settingsPtr` (`SettingsSnap`) | `g_settingsMutex` + `shared_ptr<const Settings>` | LoadSettings publishes a whole object (`generation` bumped). Never take `g_settingsMutex` then `g_stateMutex`. |
| `g_cachedAccent` | `std::atomic<uint32_t>` packed ARGB | Written on the focus-thread STA (`ColorValuesChanged` posted there). Not inside `Settings`. |
| `g_uiSettings` | `[[clang::no_destroy]] optional` | Subscribe on the focus thread; revoke token before destroying the message window. |
| Click sentinels (`g_clickSentinel_Task*`) | `thread_local` + save/restore | Nested / cross-thread ReportClicked |
| `g_unloading` | atomic | Any |
| Focus thread shutdown | `g_unloading` then **stop worker first** (ready event + `WM_APP_SHUTDOWN` + `PostThreadMessage`); wait **INFINITE**; then UI drain | Worker must not `TryRunAsync` after the drain sentinel. Do not time out — leftover `SizeChanged` crashes Explorer |
| UI uninit | `RunOnEachUiDispatcherAndWait` (High cleanup + Low drain, **INFINITE**) **after** the worker has joined | Revoke `SizeChanged` per dispatcher |
| Native probing | `QueryViaVtable` max 32 slots; task-items array offset under 64; fail closed | Private taskbar ABI. A miss fails closed. Not a proof the probe is safe on every Windows build |

Do **not** hold `g_stateMutex` or `g_layoutWatchMutex` across XAML, COM
(`SHGetPropertyStoreForWindow`, `GetProcessImagePath`), or `Dispatcher` calls.
`SettingsSnap()` takes only `g_settingsMutex` and is safe under `g_stateMutex`
(lock order: state → settings). Do not publish settings while holding `g_stateMutex`.

---

## Settings ↔ code

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
| General | `requireTaskbarButton` | tray-only filter |
| General | `excludedPrograms[i]` | uppercase set |
| Taskbar icons | `icons.glowStyle` | `LeftBar` (side bar) / `Frame` / `Full` / `BottomBar` (edge bar) |
| Taskbar icons | `icons.glowColor` / `icons.customGlowColor` | color mode + hex |
| Taskbar icons | `icons.glowIntensityRank1..3` | `glowIntensity[3]` |
| Taskbar icons | `icons.glowThickness` / `icons.glowRoundness` / `icons.glowSize` / `icons.glowLayers` | metrics |
| Taskbar icons | `icons.glowFillOpacity` | Full / side / edge icon bar strength |
| Taskbar icons | `icons.sizeBoostRank1..3` | `sizeBoostPercent[3]` |
| Thumbnail previews | `previews.highlightEnabled` | preview master |
| Thumbnail previews | `previews.highlightCount` | per-flyout top N |
| Thumbnail previews | `previews.style` | `titleBar` / `titleBg` / `plate` / `plateTitle` (saved `ring` loads as hybrid) |
| Thumbnail previews | `previews.intensityRank1..3` | `previewIntensity[3]` |
| Thumbnail previews | `previews.fillOpacity` | plate + titleBg wash (not title bar line) |
| Thumbnail previews | `previews.minFocusSeconds` | window confirm |
| Thumbnail previews | `previews.decayMinutes` | window decay |

There is no in-mod `enabled` or `glowDebugLog` setting. Disable the mod in
Windhawk to turn highlighting off. Verbose bind / preview-resolve lines use
`Wh_Log` (Windhawk Advanced tab: None / Mod logs / Detailed debug).

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

1. **Matching bugs (wrong icon):** path / AUMID / HWND only. Do **not** add
   automation-name fuzzy, initials, or per-app special cases (`LISTER`, etc.).
   Missing identity → no glow. `PathAppearsOnTaskbar` must not match by
   filename (deleted `C:\A\foo.exe` must not count as still on the taskbar
   because `C:\B\foo.exe` is).
2. **Matching bugs (wrong preview):** TaskItem/DataContext first, then
   repeater GetAt for holes only. Never assign the same HWND to two siblings;
   don’t EnumWindows or assign by construction order. No unique-title. Clear
   `g_TaskGroup_Thumbnails` on `TargetItemKey` entry. A preview skip uses
   `IsWindowRecentForPreviewLocked` (live HWND, PID, and decay).
3. **Visual bugs:** [UWPSpy](https://ramensoftware.com/uwpspy); names vary by build.
4. **Layout bugs on thumbnails:** never add sized children only to grid row 0;
   span the overlay host; position children with Margin (not RenderTransform).
5. **Crashes / unload:** try/catch around XAML; don’t block focus hooks.
   Bound `QueryViaVtable` / task-item array offsets; fail closed on miss.
   `Wh_ModUninit`: set `g_unloading`, **stop the focus thread first**, then
   drain each dispatcher (High cleanup + Low drain, INFINITE). The Low
   callback must not release the waiter; `IAsyncOperation.Completed` does.
   Keep the operation until after that wait. Never time out
   a drain — leftover `SizeChanged` lambdas crash Explorer. Ready event before
   `StartWinEventHookThread` returns so shutdown `PostMessage` cannot miss the
   queue. Register the message-window class with the **mod** module handle
   (`UNCHANGED_REFCOUNT`); `ERROR_CLASS_ALREADY_EXISTS` is a **hard fail**
   (stale `WndProc`). `g_uiDispatchers` is `no_destroy` + `reset()` on Uninit
   — do not put strong XAML/`CoreDispatcher` in a type with a CRT destructor.
   Revoke `SizeChanged` only on that panel’s dispatcher. Do not register
   `SizeChanged` unless `RememberUiDispatcher` recorded that thread. Do not
   reorder native `IconPanel` children on buttons we never painted.
6. **Do not** `SetTimer` on the hook HWND from the UI thread (window must be
   owned by the caller). `PostMessage` and arm the timer in `WndProc`.
   `ConfirmPreviewFocusNow` is posted (`WM_APP_PREVIEW_CLICK` + HWND/PID), not
   called from `HandleClick`.
7. **Do not** call `SHGetPropertyStoreForWindow` / `GetProcessImagePath` /
   `GetWindowClassName` while holding `g_stateMutex` or `g_buttonPathMutex`.
8. **Do not** `CoCreate`/`Release` `IVirtualDesktopManager` off the focus
   thread. UI reads `g_currentDesktopId`. Registry first; VDM only if that
   fails.
9. **Do not** resolve button → path from `UpdateVisualStates`. Full bind
   (`ApplyAllHighlights`) and `OnPointerPressed` `force` only — the sentinel
   `ReportClicked` must not run on hover. On identity-map lookup, drop the
   entry unless the weak_ref is this live element (heap addresses recycle).
   Do **not** cache an empty resolve forever — retry until path or AUMID is
   set. A pinned AutomationId is not a finished Win32 resolve: re-resolve
   once when `IsRunning` becomes true. A known path is not forever either:
   re-resolve when the button is running and the sample HWND is dead or the
   live image path diverges. Do not wipe a known path on an empty retry, and
   do not treat `GetFileAttributes` missing as stale. UVS must not paint a
   cached rank when the sample HWND is gone (clear chrome, rank **-1**,
   schedule the full bind). Do **not** write paint rank 0 when
   identity is still unknown (leave **-1** and schedule the full bind). Do
   not `DetectTaskbarEdge` on every UVS — cache the edge on the layout watch
   and refresh from `SizeChanged`. Rank 0 with no chrome is a no-op; overlay
   sweep must not reorder native `IconPanel` children. UVS must not
   `ClearButtonHighlight` just because `g_pendingOverlaySweep` is set
   (flicker on icons that keep their rank).
10. **WinRT collections:** include `winrt/Windows.Foundation.Collections.h`.
11. **Hooks:** `WindhawkUtils::SetFunctionHook` / `SYMBOL_HOOK` with **optional**
   for thumbnail symbols so older builds still load.
12. **Test:** app min-focus 0–1s; preview 0–1s (explorer start with preview
   min 0 must not spin the focus thread / starve timers); rapid Alt-Tab
   between two unranked apps then rest on one — that app must still confirm
   after min-focus (switcher/tray must not drop the candidate); three windows of one app (ranks
   1>2>3 in that flyout only); two same-title windows; debug log
   `Preview resolve:` + `sibling[` + `rank=` + `how=repeater|taskitem`;
   disable/unload clears all chrome; hover two multi-window apps in sequence
   (recycled flyout cards must re-rank; one Low flyout pass, not per card);
   a flyout that contains a snap-group card plus windows must not shift HWND
   binds; Chrome “GitHub” must not inherit an Edge “GitHub - Profile” rank;
   clicking a thumbnail must rank that window without stalling the
   click; accent change without a settings
   reload must update the glow colour;
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
   still shows. Two `python.exe` folders stay distinct. Delete/rename a
   ranked exe and launch the same file name from another folder — that icon
   must **not** inherit the old rank (no immediate glow; new path confirms
   after min-focus). If the old path was never ranked, the new copy still
   must not stay `UNMATCHED` against a stale cache. Launch a **pinned**
   app (click once, do not click again after it is running) — that icon must
   glow after min-focus. A new taskbar button that appears while ranks
   already exist must pick up a glow without waiting for the next Alt+Tab.
   File Explorer folder windows should join the recency list; focusing the
   desktop / taskbar / Alt-Tab must not, and must not confirm as Explorer.
   Click File Explorer (or sit on the desktop) during another app’s min-focus
   — that candidate must not poll at 200 ms forever. Keep two ranked apps
   open without hovering through a 30 s decay tick — glows stay. Change an
   unrelated setting (color / intensity) while they are ranked — glows stay
   (and 100/80/60 intensity is linear, not squared). Change a setting during
   pending min-focus without leaving the app — it still confirms, including
   after the deadline while Alt-Tab still holds foreground. Exclude a
   Win32 window AUMID while pending and after confirm — icon and preview
   recency both drop. Close a pinned ranked
   app: its slot frees for a still-running lower rank, including when the
   visible count stays at highlightCount. A full bind after an unpinned
   button disappears without a final not-running observation must drop that
   app from the top N, including when that button queued the bind and was
   destroyed before it ran. A recycled group HWND must not light the old
   icon. A pinned button that exhausted empty resolves must resolve again
   on the next launch. A Styler brush applied while a preview plate is
   showing must still be there after repaint and clear. Re-focus A after B
   (default promote): A returns to rank 1. A new Lister window needs its own
   preview min-focus; the app timer must not stamp it early. Hybrid rank 2 / title-background wash is a shallow
   rounded rect covering the icon + title (not a fat pill that starts
   mid-icon on the left and squares off on the right).

### Useful log substrings

| Substring | Meaning |
|-----------|---------|
| `Focus candidate:` / `Confirmed focus:` | App recency |
| `Current virtual desktop:` / `Virtual desktop switch:` | Per-desktop recency |
| `Preview focus confirmed:` | Window recency |
| `Preview click confirmed:` | Thumbnail / grouped-icon click → window recency |
| `HWND recycled` | Preview map dropped a reused handle (PID mismatch) |
| `Preview resolve:` / `sibling[` | Per-card HWND + `how=repeater\|taskitem` |
| `gave up waiting through transient` | Min-focus grace expired; candidate dropped |
| `snap-group extra in repeater` / `size mismatch` | Pass 1 compacted or skipped because Thumbnails ≠ repeater |
| `Decay timer armed` / `Decay timer stopped` | 30 s decay tick started or idled |
| `ApplyAllHighlights` | Full identity rebind (`Wh_Log`; visible when Windhawk **Mod logs** is on) |
| `Hooked Taskbar.View.dll` | View symbols |
| `thumbnail OnApplyTemplate unavailable` | Optional miss |
| `no dispatcher anchor` | Before first button (logged once) |
| `ERROR: UI dispatcher cleanup` / `SizeChanged watches not revoked` / `focus thread wait failed` | Unload handshake failed — explorer may crash |
| `Button path cache:` | Option C resolve |
| `stale identity` | Cached HWND dead or live image path ≠ cached path; re-resolving |
| `IconPanel relayout:` / `Taskbar edge` | Button size / screen-edge change (heal running dots) |

---

## Future work (ordered suggestions)

## What catalog `/ai-review` looks for

PR #5331 is two-stage: Claude (`/ai-review`, you run it) then a human
(`/ready-for-reviewer`). The AI re-reads the **whole** `.wh.cpp` every round.
Required items must be fixed or declined in the PR comment with a reason.
Optionals/functionality notes are “your call” — it will still re-mention
size, leftover heuristics, and English strings until you explicitly park them.

It is not reviewing visual taste (4px vs 8px wash). It is reviewing whether
Explorer can survive Disable, whether the taskbar UI thread stays cheap, and
whether the mod is a catalog citizen (copy existing mods, fail closed, small
surface). A “fix” that adds a new poll, global, or name-match becomes the
next round’s required finding.

### Invariants (re-checked every round)

1. **Unload is total.** When `Wh_ModUninit` returns, no thread, `WndProc`,
   `SizeChanged`, or `TryRunAsync` lambda may still live in the image.
   Unbounded waits. **Stop the worker first**, then drain UI. Ready event
   before `Start` returns. Mod `hInstance` for the message class;
   `ERROR_CLASS_ALREADY_EXISTS` is fatal. No strong XAML in CRT-destroyed
   globals. `TryRunAsync` “queued?” is `GetResults()` / `Status()`, not
   `static_cast<bool>(op)` (that hang uninit). Cite: `taskbar-clock-customization`
   join, `taskbar-vertical` TryRunAsync, wiki “Global objects and process
   shutdown”. Identity/View symbol hook failure at init is `return FALSE`
   except Taskbar.View not loaded yet (LoadLibrary retry).
2. **Own your threads and apartments.** `SetTimer` only on the HWND’s owner.
   COM (`IVirtualDesktopManager`, `SHGetPropertyStoreForWindow`) on one STA,
   never under a mutex the UI thread takes to paint. No
   `weak_ref<FrameworkElement>::get()` off the UI thread. Capture agile
   `CoreDispatcher` on the UI thread. Primary and secondary taskbars share
   one Explorer UI thread; `HasThreadAccess` after `weak.get()` is a skip,
   not a second-thread partition.
3. **Do not tax the shell UI thread.** `UpdateVisualStates` is hover-hot.
   No `ReportClicked`, `EnumWindows`, property-store, `UISettings`, or
   registry on that path. Coalesce flyout work to one Low pass. Cache
   accent/desktop id. Debounce with a deadline (skip re-arm), not a reset
   that can postpone forever. Cite: `taskbar-volume-control-per-app` only
   probes identity from real input.
4. **Do not guess identity from UI strings.** Automation names are
   localized. No English `" running"` / `" pinned"` as logic, no `LISTER`
   special cases, no filename-900, no fuzzy initials. HWND / AUMID / path
   only. Preview unique-title is gone (`stash/preview-unique-title.cpp`).
   Wrong glow is worse than none.
   Cite: `taskbar-thumbnail-reorder` repeater `GetAt` + ctor map.
5. **Do not fight native template / other mods.** Own-named overlays.
   Don’t `ClearValue` native fills/visibility. Don’t reorder `IconPanel`
   children on buttons you never painted; restore from a snapshot, not a
   stock order. Don’t wipe another mod’s `ScaleTransform`. Plate tints
   save/restore the previous brush.
6. **Catalog packaging.** The YAML README is the store page (screenshots on
   `raw.githubusercontent.com`, no tester checklist, no “see the repo
   README”). YAML defaults are real — a user `0` is not “unset”. No in-mod
   enable/debug toggle (`Wh_Log` + Windhawk Advanced). Nested setting
   groups. Keep `-loleaut32`. Thumbnail hooks `optional` so older builds
   still load.
7. **Surface area.** Line count is a finding. Prefer delete or copy a
   cited catalog pattern over another helper/fallback. Don’t add a probe
   to close a bug (that probe becomes the next required item).

### How to spend fewer rounds

- Close every **required** item in the same reply: patch or a short “won’t
  — because …”. Silence is treated as unaddressed.
- While closing a finding, do not introduce a new global, timer, or
  string-match. The next `/ai-review` will treat that as new work.
- Copy the mod they cited (clock uninit, volume-per-app click probe,
  thumbnail-reorder GetAt, audio-scroll `RegisterClass`, accent-color-sync
  YAML) instead of inventing a variant.
- Fail closed (no glow) rather than a new heuristic.
- Unique-title is dropped (fail closed on preview holes).
- Optionals: take the cheap ones or say skip. Size complaints: don’t add
  a helper for a one-token bug (`CornerRadius{4,4,4,4}` not `{4}`).

Incident-level rows below are the concrete hits from those invariants.
Do not regress them; do not grow the table instead of following the
buckets.

## Catalog review lessons (do not regress)

These were flagged on ramensoftware/windhawk-mods PR #5331. The pattern is:
Explorer-injected mods must be unload-safe, keep COM/XAML on the right thread,
and must not guess identity from localized UI strings.

| Lesson | Why it bit us | Rule |
|--------|----------------|------|
| Uninit **worker first**, then UI drain | Drain then stop: decay/`RequestApplyVisuals` `TryRunAsync` after the Low sentinel → `FreeLibrary` then crash | `g_unloading`; stop focus thread; then `RunOnEachUiDispatcherAndWait` |
| Unbounded drain, not a timeout | Timed-out drain leaves `SizeChanged` in the image | INFINITE wait for dispatcher join **and** focus thread |
| Ready event before `Start` returns | `PostThreadMessage` fails until the worker has a queue → INFINITE hang on Disable | Manual-reset event after `CreateWindowExW` (and on every early-fail path) |
| Mod `hInstance` for the message class | `GetModuleHandle(nullptr)` is explorer.exe; leaked class + dangling `WndProc` | `GetModuleHandleExW(FROM_ADDRESS \| UNCHANGED_REFCOUNT)` |
| `ERROR_CLASS_ALREADY_EXISTS` is fatal | Reusing a previous instance’s class jumps into unmapped memory | Fail the thread; do not `CreateWindow` on a stale class |
| No strong XAML in CRT-destroyed globals | Process teardown runs `~vector<CoreDispatcher>` after XAML threads are dead | `[[clang::no_destroy]] optional<…>` + `reset()` in Uninit |
| No `weak_ref<FrameworkElement>::get()` off UI | Last-ref `~FrameworkElement` on the worker/uninit thread | Store agile `CoreDispatcher` captured on the UI thread |
| No `SetTimer` cross-thread | HWND belongs to the focus thread; debounce never armed | `PostMessage` → `SetTimer` in `WndProc` |
| No COM under `g_stateMutex` | UI takes that mutex to paint; hung `SHGetPropertyStoreForWindow` freezes the taskbar | Resolve class/AUMID/path **then** lock |
| VDM on the focus thread only | STA `CoCreate` on one thread, use/release on another | Registry first; VDM fallback on the worker; UI reads cached GUID |
| Accent / desktop id not per-paint | `UISettings` + registry on every UVS; broadcast never reaches `HWND_MESSAGE` | Cache accent in `std::atomic<uint32_t>` outside `Settings`; `UISettings::ColorValuesChanged` → `PostToHookThread`. Desktop id from switch + decay |
| Path cache is not a poll | `EnsureButtonPathCached` from UVS = `ReportClicked` into `HandleClick` | Resolve on full bind + `OnPointerPressed` only; never from UVS |
| Identity-map keys need a live weak_ref | Raw `IUnknown*` is reused when Explorer reallocates a button | Erase the entry unless `weak.get() == this button` |
| Identity-keyed maps, not linear `weak.get()` | Four COM-resolving scans per UVS per button | `unordered_map<IUnknown*, …>`; keep the map, skip paint on unranked |
| No native reorder of untouched icons | `ClearButtonHighlight` healed z-order on every unranked UVS | Only restore `IconPanel` child order after we insert/remove our host |
| No `SizeChanged` without a drainable dispatcher | `RememberUiDispatcher` can fail; uninit never revokes that watch | Register the watch only if the dispatcher was recorded |
| No icon name fuzzy | English `" running"` / `" pinned"`, `LISTER`/`VSCODIUM` special cases, wrong glow | HWND / AUMID / path only. Preview unique-title dropped |
| No in-mod enable / debug toggles | Duplicates Windhawk’s mod on/off and Advanced logging | Drop `enabled` and `glowDebugLog`; use `Wh_Log` |
| Keep `-loleaut32` even if we do not call `Sys*` | WinRT `hresult_error` needs `SysFreeString` / `SysStringLen` | Drop `-lpropsys` if unused; do not drop oleaut32 |
| No `UpdateLayout` from `OnApplyTemplate` | Synchronous layout during measure is a XAML layout cycle | Return false + `SchedulePreviewFlyoutRefresh` at Low |
| Overlay sweep does not heal native z-order | `g_pendingOverlaySweep` re-opened the unranked reorder path | Sweep only removes `WhRecentFocusGlow` |
| Empty path cache is not permanent | Pinned icon: first resolve has no task item; Explorer reuses the same `TaskListButton` on launch; click `force` often runs before the process exists | Retry while path and AUMID are empty; throttle pinned-not-running |
| Pinned AutomationId is not a finished Win32 resolve | `appIdUpper` from `Appid: discord…` made `haveIdentity` true; launch never re-resolved; rank key is the image path | `resolvedWhileRunning`: one resolve on not-running → running; tick throttle otherwise |
| Do not deref ctor-map `taskItem` | HWND gone ⇒ native `ITaskItem` likely freed; `GetWindowFromTaskItem` UAF | Store HWND at ctor; raw pointer is compare-only (thumbnail-reorder) |
| UVS must not clear on overlay sweep | Desktop switch / decay set the flag then UVS blanked ranked icons until `ApplyAllHighlights` | Paint cached rank; the full bind is the sweep |
| Paint cache includes edge + size | SizeChanged updated `lastEdge` then `ApplyButtonHighlight` early-out | Key is rank + settings + accent + edge + panel size |
| Restore native z-order from a snapshot | Assumed `BackgroundElement` first / OverlayIcon above Icon | Save `weak_ref<UIElement>` in visual order (unnamed Styler children too) |
| Paint rank **-1** ≠ **0** | First UVS scored an unresolved button as 0 and skipped the full bind | Unknown identity stays -1 and schedules rebind; 0 only after a real resolve said “no rank” |
| Own `ScaleTransform` instance | `ClearValue` wiped `taskbar-dock-animation`. A `weak_ref` to the displaced transform goes null if Icon held the last strong ref. | Remember the object we set; keep the previous transform on the glow host Tag (tree-owned, like the preview plate brush); restore only while current == ours |
| YAML defaults are real | All-zero preview intensities must not be “unset” | Do not override user 0s after an in-place recompile |
| README is the catalog page | Users never see the repo README | Screenshots on `raw.githubusercontent.com`; no tester checklist |
| `lastHwnd` needs a PID | HWND values recycle; icon bind would follow the new owner | Store `lastPid`; `HwndMatchesStoredPid` before HWND identity |
| `ConfirmPreviewFocusNow` off the click thread | `HandleClick` + `ResolveAppIdentity` + inline `RunOnUiThread` stalled the taskbar | `WM_APP_PREVIEW_CLICK` + HWND/PID; worker resolves |
| Flyout refresh uses a stale card | Shared pending latch + oldest `g_trackedThumbViews` weak_ref | Coalesce onto one Low pass; prefer the card that scheduled it if still in a repeater |
| Repeater index ≠ `Thumbnails` index | Snap-group card extra in one collection shifts every later GetAt | Compare sizes; compact window ordinal or skip GetAt |
| Stale global `Thumbnails` collection | `g_TaskGroup_Thumbnails` is from the last `TargetItemKey`; refresh also runs from OnApplyTemplate / decay; no-DataContext made the agree-check vacuous | Clear the weak ref on the way **in** to `TargetItemKey`; DataContext first; GetAt only if it agrees with a DataContext HWND |
| Unique-title | Name-guess; untested on current Win11 (pass 1/2 fill) | Dropped. Stash: `stash/preview-unique-title.cpp` |
| Transient min-focus poll | Remaining=0 re-armed 200 ms forever while File Explorer / desktop held FG | Grace = min-focus (min 2 s) of 200 ms polls, then drop the candidate |
| File Explorer never ranked | `IsOwnExplorerProcess` + `EXPLORER.EXE` skipped folder windows | Allow `CabinetWClass` / `ExploreWClass`; tray/desktop stay `ShouldIgnoreHwnd` |
| Decay timer is not a heartbeat | 30 s tick with empty maps is wasted registry/COM work | Arm on first confirm; stop when every desktop map is empty |
| Path cache survives exe replace | Explorer reuses `TaskListButton`; cached `C:\A\foo.exe` bound any `foo.exe`; `PathAppearsOnTaskbar` filename match kept A ranked | Re-resolve when running and HWND dead or live path differs; exact path/AUMID only for “on the taskbar”; demote unmatched when that identity is gone, even if `seenOnTaskbar` |
| Hover-storm debounce reset | Each `WM_APP_REQUEST_APPLY_DEBOUNCED` restarted the 300 ms timer | Skip `SetTimer` if the full-rebind timer is already armed |
| Preview host re-parsed | `ApplyThumbnailHighlight` `ClearThumbnailHighlight` dropped the overlay host every paint | Reset plate + hide children; keep `WhRecentFocusThumbGlow` |
| `lastRunningTick` is not a heartbeat | 400 ms freshness dropped idle ranks on decay / settings / desktop switch; empty-rank apply skipped the UI snapshot | `observedRunning` until the UI sees not-running; grace only after that; `ApplyAllHighlights` snapshots `IsRunning` then recomputes, even with no ranks |
| Settings change cancelled min-focus | `WM_APP_SETTINGS_CHANGED` `KillTimer` left a valid pending candidate with no clock | Drop only an excluded candidate; re-arm remaining app/preview deadlines for an allowed one |
| Intensity × fill × opacity | Brush alpha and element Opacity both multiplied by rank `t` made 60% look ~36% | Rank intensity is Opacity (or native-plate brush alpha) once; fill/stroke setting is the other |
| Drain Low callback is not completion | Low `SetEvent` let unload proceed while `Completed` still ran mod code | Empty Low sentinel; `Completed` always signals; keep the op until after wait. `Completed` is set only once |
| Weak prior transform | Displacing `Icon.RenderTransform` dropped the last strong ref; restore used `ClearValue` | Glow host Tag holds the previous transform for the takeover |
| Win32 AUMID exclusion | Pending/timer/preview history used path+filename only; window AUMID exclusions did not drop them | Same `IsExcludedKey(..., appId)` for admission, pending, confirm, and history; resolve AUMID outside `g_stateMutex` |
| Dead path-cache row stays eligible | `observedRunning` survived after the button was destroyed; full bind never saw `IsRunning=false` | Erase null button weaks on the UI thread before eligibility. Do not `weak.get()` in `PathAppearsOnTaskbar` |
| Clear stores rank 0 before the rerank test | Closing a glowing button skipped `ScheduleRefreshAllHighlights`; decay compared only the rank count | Read the paint rank before `ClearButtonHighlight`. Decay compares the ordered rank keys |
| Second transform takeover restores the first | Host Tag was written only while unset, so a later owner was overwritten and the first snapshot came back | On each takeover, replace the Tag and the origin flags together. Restore only while the current transform is still ours |
| Due deadline re-arm skips grace | `remaining == 0` used `Immediate`, which drops a candidate while Alt-Tab still holds foreground | Resume a positive elapsed minimum with `FromTimer`. Keep `Immediate` for a zero minimum, promote skip, and preview click |
| Group HWND has no PID | A cached group handle matched a rank before path/AUMID, so a recycled HWND lit the old button | Store the PID captured with each group window and the sample. Match only while `HwndMatchesStoredPid` holds |
| Preview skip used any nonzero tick | A reused or decayed HWND skipped preview min-focus | `windowAlreadyTracked` is `IsWindowRecentForPreviewLocked` (live, PID, not decayed) |
| Plate restore wrote the first brush | Repaint put the saved brush back even after Styler replaced it. Zero fill installed a transparent brush | Restore only while the border still has our brush. Save the current owner on the next takeover. Zero fill leaves the native background |
| Full bind required the scheduling button | The Low callback returned when that button was already destroyed, so the dead-row prune never ran | The anchor only picks the dispatcher. The queued bind runs until unload |
| Empty-resolve cap stuck across launches | `resolvedWhileRunning` was set true and never cleared, so eight misses blocked later launches | Clear that flag and the attempt count on each new running episode |

---

Done in 0.9.x and not listed: UWP `APPID:` keys, preview plate brush restore,
Styler hover-plate z-order, settings snapshots, timer deadlines, transient
foreground, bounded vtable probe (fail closed, not a per-build proof), worker-first unload (a failed SizeChanged revoke is logged, not a certification that every subscription is gone), icon fuzzy removal,
identity-keyed UVS maps, no native reorder of untouched icons, nested settings
groups, `Wh_Log` instead of an in-mod debug toggle, `UISettings::ColorValuesChanged`,
filename-900 dropped, `ReportClicked` off UVS, empty-resolve retry, rank -1 vs 0,
exe-replace path-cache re-resolve, `PathAppearsOnTaskbar` exact-only, unmatched demote without filename steal,
`lastHwnd`+pid, replica/score prune, flyout Low coalesce, snap-group GetAt
guard, click confirm on the focus thread, idle decay timer, pinned→running
re-resolve, ctor-map HWND only, no UVS clear on overlay sweep, paint cache
edge+size, native z-order snapshot, DataContext-first preview bind,
unique-title dropped (stash), Thumbnails clear-on-retarget, bounded transient
min-focus, File Explorer folder windows, deadline-style full-rebind debounce,
preview overlay host reuse, window recency key is path or `APPID:` (not AFH
path), ring preview style removed (hybrid default), OverlayIcon raised after
snapshot restore (Thunderbird/Discord badge), `observedRunning` vs 400 ms
heartbeat, settings-change timer re-arm, linear rank intensity, drain
`Completed` barrier, glow-host Tag for prior icon transform, Win32 AUMID
exclusion on pending/preview history.

1. Composition shadow / true GPU outer glow if XAML halo stays clipped
   (optional polish; current bar/frame/plate is the product).
2. Stronger DataContext ↔ TaskItemThumbnail identity (TaskItem/DataContext is first; repeater GetAt fills holes).
3. Classic / non-XAML thumbnail path if still needed on some builds.
4. Multi-monitor secondary taskbars if weak refs only cover primary.
5. Per-desktop prune of deleted virtual desktop GUIDs beyond decay.
6. Per-monitor taskbar edge if a secondary bar can sit on a different side.
7. Test seam + `make release` concat (pure ranking/identity table tests).
8. Watch `ReportClicked` for jump-list
   / MRU side effects (once per button on full bind / press, not on hover).
