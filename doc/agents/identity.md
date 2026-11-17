# Identity: which taskbar button is which app

Read before changing button capture, app keys, rank matching, Never Combine
handling or the exclude list. Design note: `doc/appid-identity-design.md`.

Focus tracking and taskbar UI live in **two different worlds**. Since 0.11.33
they are bridged by **Explorer's own AppId**, the key Explorer groups taskbar
buttons by (one taskbar button = one app). Design note:
`doc/appid-identity-design.md`.

## World B — taskbar XAML buttons (captured first)

Highlights are applied to `Taskbar.TaskListButton` elements inside
`Taskbar.View.dll`. Each full pass (`ApplyAllHighlights_UIThread`) calls
`EnsureButtonIdentity` for every live button: a **synchronous** capture
(`CaptureButtonResolveData`, ~10 µs) of the button's public WindowsUdk
`ITaskItem` / `ITaskGroup`:

| Field | Source | Role |
|--------|--------|------|
| AppId | `ITaskGroup::get_AppId` (else `ITaskItem::get_AppId`) | **Button identity** |
| Windows | `ITaskGroup::get_Items` → `ITaskItem::get_WindowId` | Window → AppId map, grace, per-window slots |
| Uncombined | AutomationId `Window: 0x<hwnd>` + own TaskItem | Never Combine: one window per button |

Every captured window is published to the **window → AppId map**
(`PublishWindowAppIds`, `g_windowAppId`). A changed AppId or window set resets
the cached paint rank. A button Explorer recycles for another app simply
reports the new AppId. No worker round trip, serials, deadlines or rechecks
(the 0.10.3–0.11.31 asynchronous resolver is gone).

## World A — focus

`EVENT_SYSTEM_FOREGROUND` / shell activation give an `HWND`, normalized
(child → root, owned dialog → highest visible same-process owner; UWP content
→ its frame). `ResolveAppIdentity` then:

1. resolves process path, window AUMID, display name (logs, **exclude list**);
2. checks the exclude list (names, paths, AUMIDs — unchanged setting);
3. looks the window up in the window → AppId map: key = `APPID:<AppId>`.

A window that no captured button lists yet (just opened, or the mod just
loaded: Explorer reports a button only when its visual state updates) gets a
provisional key `WINDOW:<hwnd>` and a full pass is requested. Its recency is
kept and moved to `APPID:<AppId>` by `AttributeProvisionalAppsLocked` (start of
every rank recompute) once a button lists the window. Provisional keys are
never ranked, so a window that never gets a button (tray widgets, popups)
just decays. This replaced the `requireTaskbarButton` setting (removed in
0.11.33) and the 0.11.33–0.11.36 retry timer.

Examples (shadow logs 2026-11-16): TC `{6D809377-…}\TOTALCMD\TOTALCMD64.EXE`,
Lister `LISTER{CECFE544-…}`, VS Code `MICROSOFT.VISUALSTUDIOCODE`, Terminal
`MICROSOFT.WINDOWSTERMINALPREVIEW_8WEKYB3D8BBWE!APP`, Discord
`COM.SQUIRREL.DISCORD.DISCORD` — stable across app updates, unlike the old
versioned exe-path keys.

## App matching

`IdentityMatchesRank`: rank key AppId == button AppId. Per-window slots
(uncombined buttons) match only the button listing exactly that window
(HWND + PID). Pinned-only icons: no highlight (`IsRunning == false`).
`AppAppearsOnTaskbar`: a rank without a running (or just-closing) button with
its AppId keeps its history but takes no slot. If capture is unavailable,
**do not glow** — a wrong icon is worse than none.

## Known gaps (app matching)

| Gap | Impact |
|-----|--------|
| Taskbar.View symbols missing | No identity, no glow (fail closed) |
| Taskbar tabs (SOLIDWORKS documents) | Preview cards unmarked (see README) |

## Never Combine / uncombined buttons

- A button is **uncombined** when its AutomationId is `Window: 0x<hwnd>` and
  that window is the window of the button's own TaskItem
  (`GetButtonAutomationWindow`, `CaptureButtonResolveData`). It then lists only
  that one window. Combined buttons carry `Appid: <AUMID>`.
- Uncombined windows are kept per UI thread in `g_uncombinedWindowsByThread`.
  Each full pass replaces its own thread's set from the buttons it shows, so
  switching Never Combine off merges per-window ranks back into one app rank
  (0.11.35). Readers use the union of all threads.
- `RecomputeRanksForDesktopLocked`: an app that has any confirmed window with
  its own button is split into one rank unit per confirmed window
  (`perWindow`, from `windowAppFocus`). Such a unit matches only the button
  that lists exactly that HWND + PID.
- An app-level rank (window not confirmed on its own yet) lights only the
  uncombined button of the app's last focused window, not every button of the
  app (0.11.34).
- Recycled buttons: the taskbar `ItemsRepeater` keeps recycled
  `TaskListButton`s parented, parked off-screen, with their last identity.
  `IsRealizedTaskbarButton` (`GetElementIndex >= 0`) excludes them from the
  full pass and clears their chrome (0.11.26).

## Running state and grace

- `ButtonCountsAsRunning` returns the observed `IsRunning`, never the grace
  result. A stopped button is never painted because its closing HWND is
  still alive.
- `observedRunning` is the last UI snapshot, sticky until the UI sees
  not-running. It is not a heartbeat: idle apps must not drop because nothing
  refreshed a tick. A virtual-desktop switch clears it
  (`ClearButtonRunningGrace`); the next full pass snapshots again.
- Grace (`kIsRunningGraceMs`, 400 ms) only keeps rank **eligibility** across a
  not-running flicker while a captured window of the button is still alive
  (HWND + PID). When grace applies, another full pass is scheduled so its
  expiry is observed without another `UpdateVisualStates`.
- `AppAppearsOnTaskbar` is called under `g_stateMutex`, also from the focus
  worker. It reads the cache only and must not `weak.get()`. Rows whose button
  is dead are erased on the UI thread first
  (`PruneDeadButtonPathCache_UIThread`).
- Every full pass snapshots `IsRunning` for every shown button **before**
  recomputing ranks, also when the rank list is empty.

## Rules when changing this

1. Match by Explorer AppId equality, plus HWND + PID for per-window units.
   No automation names, window titles, process names or per-app special
   cases. Missing identity means no highlight: a wrong icon is worse than none.
2. The capture is synchronous on the UI thread and keeps value data only. No
   XAML object or task-item pointer outlives it.
3. Every stored HWND is paired with the PID captured with it; check
   `HwndMatchesStoredPid` before trusting the handle (handles are recycled).
4. Maps keyed by an element's `IUnknown*` must validate the stored `weak_ref`
   against the live element and drop the row otherwise (heap addresses are
   reused).
5. `UpdateVisualStates` paints the cached rank only. Identity is re-captured
   in the full pass. Cached paint rank: **-1** unknown, **0** unranked, **>0**
   rank. Never write 0 while identity is unknown; schedule the full pass.
6. The WindowsUdk interfaces are reached by IID-gated `QueryInterface`; a
   failure means no identity. IIDs and vtable slots come from
   `windowsudk.winmd`; declare only the leading slots that are called.
7. Exclude list: process names, paths, window AUMIDs and Explorer AppIds,
   case-insensitive. Use the same `IsExcludedKey` for admission, pending
   focus, confirmation and stored history.

## Private dependencies (keep this list current)

- `Taskbar.View.dll` symbols in `HookTaskbarViewDllSymbols` (required:
  `TaskListButton::get_IsRunning`, `UpdateVisualStates`, the two
  `TryGetItemFromContainer` instantiations, `TaskListWindowViewModel::
  get_TaskItem`, `TaskListGroupViewModel::IsMultiWindow`,
  `ITaskGroup::IsRunning`; the thumbnail symbols are optional).
- Implementation layouts: `(IUnknown*)pThis + 3` for `TaskListButton`,
  `*((IUnknown**)pThis + 1)` for `TaskItemThumbnailView`.
- Group capture: `IsMultiWindow` called on `(void**)groupViewModel - 1` while
  the hooked `ITaskGroup::IsRunning` records the group
  (adapted from taskbar-volume-control-per-app).
- WindowsUdk IIDs / slots (`ITaskItem`, `ITaskGroup`, `ITaskItemThumbnail*`).
- AutomationId text `Window: 0x<hwnd>`.
- XAML element names (`IconPanel`, `Icon`, `RunningIndicator`,
  `BackgroundElement`, `BackgroundBorder`, `IconsRepeater`, title text blocks).
- `RegisterShellHookWindow` (best effort) and the registry value
  `CurrentVirtualDesktop`.
