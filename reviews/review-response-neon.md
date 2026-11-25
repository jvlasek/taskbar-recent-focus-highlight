# Response to the Gemini 4 Neon review (`neon.md`)

2026-11-25, branch `fix/neon-review`. The submission becomes **0.12.2**.
Every finding was checked against the code at `1feda77` first.

## Done in 0.12.2

| Item | Verdict | Change |
|---|---|---|
| 1.1.1 transient foreground moves the last-use stretch | Real | Transient windows (`IsTransientForeground`) no longer start a stretch; back on the same window, use keeps counting with or without a new foreground event |
| 1.1.2 destroyed window in front not cleared | Real, unlikely | Stamped with the last input, folded into its app, `g_inFrontHwnd` cleared |
| 1.1.3 same-app switch after app confirm leaves the new window without a record | Real, narrow (switch within the preview wait) | `appConfirmed` cleared on a window change, so the new window is confirmed |
| 1.1.4 close before recompute loses the stamp | Real, minor | Fold at destroy (same change as 1.1.2) |
| 1.2 `LastInputTick64` race | Race real; the stated effect is not: either result was earlier than the record's tick, so nothing moved | Signed 32-bit difference, input after `now` = `now` |
| 1.3 capture flag without RAII, group without a reference | Theoretical (needs Explorer to throw through our hook) | Scope guard; the group is returned with its own reference |
| 1.4.1 posts / subscriptions without `RememberUiDispatcher` | Rule violation, dormant (flyout shares the taskbar thread) | Gated: `ElementClearing` watch, thumbnail tracking, both `Schedule*`; `put_ThumbnailSource` tracks the card |
| 1.4.2 / 2.2.1 `ColorValuesChanged` callback cannot be drained | Real; the window has been top-level since 0.11.0 | Window messages `WM_SETTINGCHANGE("ImmersiveColorSet")` / `WM_DWMCOLORIZATIONCOLORCHANGED`, 250 ms timer; no WinRT event |
| 1.5 stored PID 0 matches anything | Real, low | PID 0 never matches; windows already gone are not stored |
| 1.6 `EnsureVdm` off-thread, settings sweep COM on Windhawk's thread | Latent / plausible | `EnsureVdm` returns nullptr off the worker; `DropExcludedRecords` runs on the worker (STA), deduplicated, app records checked by their last window's AUMID |
| 1.7.1 plate cache miss at intensity 0 | Real (only with a 0 % rank) | Cache state stores the plate signature |
| 1.7.2 null dereference | Not reachable today | Checked |
| 1.7.3 retargeted exclusion keeps the pending focus | Real, harmless | Compared with the original window |
| 1.7.4 hooking during unload | Theoretical | `g_unloading` checked |
| 1.7.5 `Wh_Log` under locks | Real (seven places, two shared with the worker) | `LOG_DEFERRED` under `DeferredLogScope` everywhere; `run-review-helper-tests.py` scans for it |
| 1.7.6 fallback could pick our own card overlay | Real, only without a named `BackgroundBorder` | Skipped by name |
| 1.7.8 repeated empty flyout key | Real, minor | Empty key clears once, schedules nothing |
| 1.8.2 init fails for "nothing to paint" | Real UX trap | Mod stays loaded and paints nothing |
| 3.1 registry events hold back the worker's messages | Theoretical | Messages are pumped after every registry signal |
| 5.1 doc drift | Real | `investigation-notes.md`, runtime-flow diagram, `state-machines.md` (machine 15, registry watch), README, code breakdown |
| 5.2 small cleanups | Fine | Dead comments and `outSourceIndex` removed, one `GetIconPanel` per clear, cached-host check, no border brush without accent, uninit clears the remaining maps |

New test: `tests/run-last-use-tests.py`.

## Not taken (or later)

- **1.7.7 `TransformToVisual` against the panel instead of the fresh
  host.** Since 0.12.1 the overlays have zero desired size with offsetting
  margins, so host and panel coordinates are no longer the same thing; it
  needs a live look at the first paint before changing.
- **1.8.1 query `ITaskListButton` before `get_IsRunning`.** Its IID is
  private (Taskbar.View.dll); the symbol already names
  `produce<TaskListButton, ITaskListButton>`. Not worth guessing.
- **1.4.3 `ObserveDispatcherOp` after thread exit, 1.4.4 per-thread owner
  on watch entries.** Dormant while all taskbars share one UI thread; a
  design change for 0.12.x.
- **2.2.2 / 4.3 taskbar edge from the button's shape.** Only reached when
  neither orientation visual state is set; trusting the running pill's
  alignment first is a 0.12.x item, with a per-monitor edge.
- **2.2.3 `WTSRegisterSessionNotification`, 2.2.4 VDM check on immediate
  promote, 2.2.5 code-built hosts instead of `XamlReader`, `TaskbarAppsList`
  seeding.** Later; each needs live verification.
- **3.2 remaining rows** (`seededRepeaters` validation, overlay sweep
  generation, redundant debounce timer): low, later. The flyout pending flag
  is now guarded by the dispatcher gate (`RememberUiDispatcher` requires
  thread access).
- **§4 Styler notes** (`> Border` selectors, `ZIndex` themes, RTL): no
  report so far; README already covers the side-by-side look with Taskbar
  Active Window Highlight.

## Live checks before merging

1. Windhawk compile + `run-dispatcher-tests.py`.
2. Accent: change the Windows accent colour with the mod loaded (highlight
   follows within a second; one `System accent updated` line), then disable
   and re-enable the mod.
3. Last use with `decayMinutes = 1`: keep typing in one app, Alt+Tab and
   come back, open and close Start, lock and unlock; it must keep its rank.
4. Exclusion: add a ranked app to the exclude list; it loses its highlight
   (`Settings: dropped N records`).
5. Set highlight count 0 and previews off, restart Explorer, set it back:
   highlights return without toggling the mod.
