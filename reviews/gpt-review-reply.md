# Reply to gpt-review.md (v0.9.27)

Thanks — this was more useful on the confirmation state machine than the catalog bot. P1s and the P2s we agreed to are in **v0.9.27**.

## P1

**1. Preview confirm cleared the app candidate.** Agreed. `lastConfirmedFocusTick > 0` is history, not this visit. `PendingFocus` now has `appConfirmed` / `previewConfirmed` for the current episode. Preview confirm no longer drops pending just because the app was ranked before. Default A→B→A can promote A again.

**2. Drain completion race.** Agreed. Gating `Completed` on `Status() != Completed` skipped the handler when the op flipped `Started → Completed(false)` between the two observations. We always subscribe `Completed` (it still runs if already finished). No unload timeout.

## P2 (this drop)

**3. Preview vs app clocks.** Agreed, decoupled. App confirm does not stamp the current HWND. Each window waits out `previewStartTick` (default 1s). Clicks stay immediate. A new Lister window is not stamped by the 8s app timer after a 0.2s switch; it still ranks after its own preview wait (or a click), including if another app then grabs focus.

**4. Same-PID desktop/tray vs File Explorer.** Agreed. `StillPendingForeground` rejects `IsTransientForeground` replacements. Folder-window candidates take the bounded transient path instead of confirming Progman/tray as Explorer.

**5. Closed pinned / relocated exe occupying a slot.** Agreed — seen with a pinned tool that moved on update. `PathAppearsOnTaskbar` now requires a **running** button (`lastRunningTick` within the 400ms grace), not merely a path on a pinned icon. `RecomputeRanks` uses that every pass, not a sticky `seenOnTaskbar`.

**6. Intensity / fillOpacity 0.** Agreed. Rank intensity scales the peak with no additive floor. `previews.fillOpacity=0` suppresses plate/title wash. Icon size boost is independent (intensity 0 + boost still scales).

**7. Exclude list vs pending.** Agreed. Settings sweep also matches `AppFocusInfo.appIdUpper` (no property-store under the mutex). `WM_APP_SETTINGS_CHANGED` drops an excluded `g_pendingFocus` on the focus thread. Both min-focus confirm paths re-check the exclude list before inserting.

**8. `QueryFullProcessImageNameW`.** Agreed. On `ERROR_INSUFFICIENT_BUFFER` we double the buffer up to 32k; we do not treat `n` as a required size.

**9. Size boost vs another mod’s transform.** Agreed. Before the first own `ScaleTransform` we stash the previous local transform/origin (weak). Clear restores that if it is still live, otherwise `ClearValue` only our instance.

**10. Worker start reported success.** Agreed. Ready is signaled after the FOREGROUND hook is installed. `StartWinEventHookThread` returns false if the message window/hook never comes up; `Wh_ModInit` returns `FALSE`. Late Taskbar.View load is unchanged.

## Additional concerns

- **Unload revoke vs map.** SizeChanged revoke still happens off the layout-watch mutex (COM/XAML must not run under that lock). Failed revoke is logged; we will not time out a drain.
- **Multi-dispatcher.** Documented: one Explorer UI thread. `weak.get()` then `HasThreadAccess` is a skip, not a partition. No second-thread framework without evidence.
- **Same-size opposite-edge move.** Full bind (`ApplyAllHighlights`) now refreshes `DetectTaskbarEdge` into the layout-watch cache. UVS still uses the cache (not per-hover detect).
- **Labeled / wide IconPanel.** VisualState `VerticalOrientation` / `HorizontalOrientation` wins; geometry is fallback only.
- **Vtable probe.** Still bounded fail-closed; not an AV guard. Unchanged.
- **Ctor-map HWND recycle.** Mapping now stores PID at ctor; `HwndFromMappingEntry` rejects PID mismatch.

## Docs

Unique-title leftovers in README/AGENTS (title fallback / “title-only”) are gone. Layout instruction is Margin, not RenderTransform. `PathAppearsOnTaskbar` docs include the running-button requirement.

## Not in this drop

GPT’s longer-term list (explicit episode types as a separate test seam, per-pass identity snapshot, `Cand` cleanup, measuring hover-bind cost) is still follow-up. We did not add a timeout on unload or a multi-dispatcher overlay engine.
