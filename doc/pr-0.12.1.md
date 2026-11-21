# Taskbar Recent Focus Highlight 0.12.1

Update of the published 0.11.1. Development repository with tests, perf
results and notes: https://github.com/jvlasek/taskbar-recent-focus-highlight

## What was removed

- **Every `taskbar.dll` hook and private lookup.** 0.11.1 hooked or resolved
  `CTaskGroup::GetNumItems`, `CWindowTaskItem::GetWindow`,
  `CImmersiveTaskItem::GetAppWindow` / `GetThumbnailWindow`, four vtables,
  `CTaskListWnd::HandleClick` and three WindowsUdk constructors / vector
  helpers, and probed task-item array offsets. All gone. Button windows and
  preview cards now come from the public WindowsUdk `ITaskGroup` /
  `ITaskItem` / `ITaskItemThumbnail` interfaces.
- **The click hook** (`ICommandSource` / `HandleClick`) and the
  asynchronous identity resolver (worker-side process path / AUMID lookups
  with serials, deadlines and rechecks).
- **Path, AUMID and file-name matching** of taskbar buttons.
- **The icon size boost.** It was the only place the mod wrote a native
  element's property (`Icon.RenderTransform`, also used by dock-animation
  mods), and it needed about 200 lines to save and restore it. The mod now
  writes no property of any native element.
- **The side bar's halo** (a second, slightly larger translucent rectangle;
  XAML cannot blur it, so it showed as a hard-edged rim when magnified).
- Symbol count: 26 (Taskbar.View.dll + taskbar.dll) -> 12 (Taskbar.View.dll
  only). Lines: 8294 -> 8669, with the new features below included.

## Identity rule

An app is a taskbar button: the mod uses **Explorer's own AppId of the
button** (`ITaskGroup` / `ITaskItem::get_AppId`), read synchronously when
the button is painted. A window that no taskbar button lists is never
ranked. Recency is recorded per window and joined to its app by that AppId
at rank time, so a window whose button appears later (just opened, another
virtual desktop, mod loaded into a running Explorer) joins its app as soon
as the button is known.

User-visible consequences:

- `requireTaskbarButton` ("Only apps on the taskbar") is gone: it is always
  true now. A stored value is ignored.
- The exclude list also accepts an AppId (shown in the mod log as
  `Button identity: ... appId=`). This is the only way to tell apart two
  buttons of one exe (Total Commander and its Lister, UWP apps sharing a
  host).

## Recency: last use, not last switch

Decay (apps and previews) now counts from when an app was last *used*: the
last keyboard / mouse input while it was in front (`GetLastInputInfo`),
refreshed on the 30 s decay tick and when it loses the foreground. Before,
it counted from the switch, so an app used for longer than the decay time
without switching lost its highlight while in use. Away from the keyboard
the input time stops and everything ages out as before. Only a stretch in
front in which the window passed the minimum focus counts, so a short visit
cannot move an app up. No new setting; the decay settings' descriptions say
"since you last used it".

## Explorer processes without a taskbar

Windhawk loads the mod into every `explorer.exe`, also folder-window and
COM-server processes that never load `Taskbar.View.dll`. Before, it tracked
focus there too (hooks, timers, log lines duplicating the taskbar
process). Now the focus thread only creates its hidden window at init and
installs its hooks once the Taskbar.View hooks are in; a process without a
taskbar does nothing after init.

The mod's own elements no longer take up layout space: a plate used to ask
its button for the old size, so with a Taskbar Styler theme that shrinks the
buttons (BottomDensy) the highlighted ones stayed big. Plates, the side bar
and the preview overlays now have zero desired size at the same place.

The log is quieter: the ladder, the button bindings and the preview
siblings are printed only when they change, and repeating wait lines are
gone. New lines say why an app has no slot, how long ago a decayed app was
last used, which desktop the input switched to, and when the decay tick ran
late after sleep.

## Look

Icons (setting `icons.glowStyle`, ids unchanged):

- **Side bar** (`leftBar`, default): as before, without the halo.
- **Plate** (`full`, was "Full"): the button's own plate tinted, flush with
  `BackgroundElement` and at the full Fill opacity. Full sat at 92 % inside
  it at 60 % of the fill, almost where Windows draws its hover and
  attention plates, and paler than both.
- **Plate with accent** (`frame`, was "Frame"): the same plate at half the
  tint with an accent border (new `icons.glowAccentThickness`). Frame was an
  outline of the side bar's thickness above the glyph and read as a
  keyboard-focus ring.
- On a multi-window button both plates also tint Windows' stacked sliver
  (`MultiWindowElement`), so the stack reads `(O))` in the highlight colour.
  A ranked app that requests attention keeps Windows' red pill and red
  sliver on top of our plate.

Thumbnail previews (`previews.topStyle` / `restStyle`, ids unchanged):
Whole plate, Whole plate with accent (was "Outline"), **Title** (`titleBg`,
was "Title background": now a plate over the title row, 4 px in from the
card with the card's corners and even gaps, instead of a strip 8 px in with
tighter corners), **Title with accent** (new, `titleOutline`), Bar under
title, Not marked (ranks 2+).

## Settings

- **Transition:** 0.11.1 had one preview style, `previews.style`. It is
  split into `previews.topStyle` (rank 1) and `previews.restStyle` (ranks
  2+). If a 0.11.1 user had picked a non-default `previews.style` and the
  new keys are still at their defaults, the old choice is used for all
  ranks until the user first saves the settings; from then on the settings
  page applies. The state is kept in mod storage. (Per your note on
  Discord: no migration mechanism, so this is the mod's own.)
- **Removed, no transition needed:** `icons.glowRoundness` (fixed 28 %),
  `icons.glowLayers` (fixed 2), `icons.sizeBoostRank1..3` (feature removed),
  `requireTaskbarButton` (see above).
- **New:** preview styles per rank (`topStyle` / `restStyle`, Whole plate
  with accent and Title with accent styles, `None` for ranks 2+),
  `previews.outlineThickness` (shown as "Accent thickness"),
  `icons.glowAccentThickness`, `previews.singleWindow` (off by default).
  Ranks 4+ fade from the rank 3 intensity instead of repeating it.
- **Renamed in the UI only** (stored ids unchanged, so no transition): the
  icon and preview styles above; Thickness / Size are now "Side bar
  thickness / length", since they only apply to the side bar.

## Private surface that remains

Every item fails closed (no highlight rather than a guessed one):

- 12 `Taskbar.View.dll` symbols, 7 required and 5 optional (previews).
- `TaskListButton` layout `(IUnknown*)pThis + 3` and the thumbnail view's
  `*((IUnknown**)pThis + 1)`, as in other taskbar mods.
- Group capture: `TaskListGroupViewModel::IsMultiWindow` read through a
  hooked `consume_ITaskGroup::IsRunning`, the same technique as
  taskbar-volume-control-per-app. The log warns once if it never succeeds.
- WindowsUdk IIDs and leading vtable slots, from windowsudk.winmd.
- The `Window: 0x<hwnd>` AutomationId of uncombined (Never Combine) buttons,
  cross-checked against the button's own task item.
- XAML element names (`IconPanel`, `Icon`, `RunningIndicator`,
  `BackgroundElement`, `MultiWindowElement`, `BackgroundBorder`,
  `IconsRepeater`, title text).
- Public only: `RegisterShellHookWindow` (best effort), the
  `CurrentVirtualDesktop` registry value with a change notification,
  `IVirtualDesktopManager` and `GetLastInputInfo`.

## Performance

Quiet perf build of 0.11.50 (logging compiled out), 3 h 11 min of normal
use with idle stretches:

- Taskbar UI thread busy 0.005 % of the time (0.125 % in a shorter, busier
  run of an intermediate build before the performance work).
- Full pass over all buttons: 310 runs, average 0.98 ms, p95 under 2 ms.
- `UpdateVisualStates` hook (hover): average 39 us, p99 under 128 us.
- The focus worker never waited more than 6.6 us for a lock the UI thread
  holds.
- Outliers of 10-23 ms are single passes after long idle periods, spent
  touching XAML objects for the first time (most likely paged out), not
  lock waits.

## Known limitations

- **Loaded into a running Explorer:** Explorer reports a taskbar button only
  when its visual state changes. The first button that changes (hover,
  window switch) now brings in all buttons of that taskbar; until then the
  window focused at load is recorded but not ranked.
- **Apps that show documents as taskbar tabs** (SOLIDWORKS): their preview
  cards have no window behind them and stay unmarked. The icon is ranked
  normally.
- **Unload waits are unbounded.** `Wh_ModUninit` stops the focus thread,
  then waits until each taskbar UI thread has run a sentinel (or exited)
  before freeing state. A timeout would let a late XAML callback run into
  freed memory. A test mod that fails or delays `Wh_ModInit` /
  `Wh_ModUninit` on Windhawk 1.7.3 showed that a slow uninit only shows
  "Mod tasks in progress" and queues the re-enable, which is the safer
  failure.

## Related mod: taskbar-active-window-highlight

[taskbar-active-window-highlight](https://windhawk.net/mods/taskbar-active-window-highlight)
by EphremTil17 was merged while this update was in progress. Credit where
it is due: reading button windows and preview cards through the public
WindowsUdk `ITaskGroup` / `ITaskItem` / `ITaskItemThumbnail` interfaces
follows that mod, and replaced this mod's private taskbar.dll lookups. So
does drawing the preview plate / outline as an own `Border` instead of
tinting the card's native one, which keeps it visible on hover. The in-mod
README credits it.

The two still make sense as separate mods. That one marks the window that
is active right now, from Explorer's own active-item state, and is much
lighter. This one keeps a history: apps and windows that held focus for a
minimum time, ranked per virtual desktop with decay, on the taskbar icons
and inside multi-window flyouts. Someone who only wants the active window
marked in previews is better served by that mod, and this mod's README
points there. With the defaults they do not mark the same thing twice:
single-window previews are off here (`previews.singleWindow`), and the
Whole plate with accent style exists for users who want the same look for
rank 1.

## Testing

Live on Windows 11 26100-era builds through 0.11.2-0.12.1: combined and
Never Combine taskbars, multiple virtual desktops (including empty ones and
switches by clicking another desktop's flashing button), sleep / lock,
taskbar moved to the left and back, mod disable / enable, settings
transition from a 0.11.1 profile. The 0.12.1 styles were picked side by
side against the old ones on the live taskbar, including multi-window
buttons (layout from a UWPSpy dump) and a ranked app requesting attention.
Script tests in the development
repository cover identity, exclude list, rank derivation, notification
filtering, min-focus deadlines and the perf build.
