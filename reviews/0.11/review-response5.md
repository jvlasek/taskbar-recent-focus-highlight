# Claude submission review (round 5) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5581599719

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.4.

---

## Draft PR comment

Thanks — two real holes in the identity cache from last round’s “don’t resolve on UVS” change. Both agreed; both are in.

**1. Failed resolve cached forever — agreed, fixed.**

Confirmed: `resolveAttempted` is set even when `pathUpper` / `appIdUpper` stay empty (pinned, no task item). The same `TaskListButton` is reused when the app launches, so we never try again except `OnPointerPressed` `force` — and that click often happens *before* the process exists.

`lastResolveTick` is now the retry throttle. Empty identity is retried; a successful path/AUMID is still once-per-button. A pinned-not-running miss is not retried every paint (`kUnresolvedRetryMs`), but a button that has become `IsRunning` retries immediately so launch-from-pinned does not wait out the throttle.

**2. First UVS stamps rank 0 with no identity — agreed, fixed.**

Confirmed: `TrackButton` no longer resolves, `FindRankForButton` scores an empty identity as 0, we stored that 0, and the UVS hook then skipped `ScheduleRefreshAllHighlights`. Distinguished “unknown” from “rank 0”: if the cache has neither path nor AUMID we leave rank at -1 and schedule the debounced full bind (which does call `EnsureButtonPathCached`).

### Optional / functionality notes

Skipping this pass (surface-area / token budget). The dead `replica` branch, `PathAppearsOnTaskbar` shadow, `lastHwnd` pid, TargetItemKey latch, per-card flyout resolve, and click-path `ConfirmPreviewFocusNow` are all noted; none change the two bugs above.

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| 1 | Valid | Retry empty identity; throttle pinned misses; running empty retries immediately |
| 2 | Valid | Don’t write rank 0 without path/AUMID; schedule full bind; UVS treats rank -1 as “need rebind” |
| Optionals | Skip | Dead replica branch, rename `id`, lastHwnd pid |
| Fn notes | Skip | TargetItemKey latch, coalesce OnApplyTemplate, snap-group index, click-path resolve, decay timer |

`kUnresolvedRetryMs` = 2000. UVS no longer calls `EnsureButtonPathCached`, so the throttle only bounds full-bind retries on pinned icons. `IsRunning` bypasses it so click-then-process-start still resolves on the next bind.
