# GPT review 8 — v0.9.35

Reviewed commit `db4a880` and the current local source. This was a targeted source recheck of shutdown, focus/preview timers, ranking and pruning, identity matching, visual restoration, and thumbnail binding, building on the previous reviews. It was not a fresh line-by-line audit of every unchanged helper. No implementation files were changed.

## Assessment

I found no newly demonstrated critical defect in the paths examined, and no evidence identifying the cause of the earlier Explorer crash. Two low-priority idle-maintenance defects are concrete enough to record. Neither warrants another shutdown rewrite or a claim that the mod is broadly unstable.

The retained-thread-handle patch still passes the compiler syntax check and the existing dispatcher harness. Those checks cover compilation and controlled dispatcher outcomes, not every aspect of Explorer's lifetime or visuals. The user's subsequent successful unload and enable/disable tests are useful additional evidence, but do not explain the first crash.

## Findings

### [P3] An abandoned preview wait can remain pending after its timer stops

Location: `taskbar-recent-focus-highlight.wh.cpp:6996–7005`; idle predicate at `1104`.

When preview transient grace expires, `OnPreviewMinFocusTimerElapsed` returns without completing or clearing the preview side of `g_pendingFocus`. The timer callback has already killed its one-shot timer. If the app side was previously confirmed, there need not be an app timer left to clear the episode either.

A concrete sequence is an immediate app confirmation with a positive preview minimum, followed by focusing a transient shell window before preview confirmation and staying there beyond its grace period. The app side is done, the preview side gives up, but `g_pendingFocus.valid` remains true. After the recency maps expire, `StopDecayTimerIfIdle` still cannot stop maintenance because it sees pending focus.

Impact: unnecessary 30-second maintenance and potentially UI refresh requests while otherwise idle. A later ordinary foreground episode can replace the stale pending state. This is not an unbounded 200-ms polling loop and is not a crash finding.

Suggested eventual correction: explicitly finish the abandoned preview side, preserve an app side that is genuinely still waiting, and clear the episode once neither side has work left. Use an explicit completion/abandonment decision rather than resetting the whole shared pending structure indiscriminately.

### [P3] Zero-timestamp demotion entries cannot age out

Locations: `taskbar-recent-focus-highlight.wh.cpp:4614–4615`, `1675–1678`, and `1079`.

The unmatched-app demotion path sets `lastConfirmedFocusTick` to zero but leaves the map entry. Ranking skips zero-timestamp entries before the decay/erase check, so those entries cannot subsequently expire through that pruning path. `RecencyMapsEmptyLocked` nevertheless counts them as history because the map is nonempty.

Once an app is demoted this way, and unless it is later confirmed again or removed by another path, it can keep the decay timer running even after all useful app/window history is gone. Multiple distinct demoted keys can also remain in the map for the session.

Impact: stale bookkeeping and avoidable periodic work; these entries do not become visible ranks merely by remaining in the map. This is an existing issue, not a regression from v0.9.35's dispatcher changes.

Suggested eventual correction: decide whether a zero-timestamp entry still has a useful purpose. If not, erase it on demotion or during pruning. If it must remain, separate that bookkeeping from the predicate that decides whether recency maintenance is needed.

## What held up in this pass

- UI-thread identity is retained with a real handle; the code does not reopen a potentially reused ID or accept a failed OpenThread call as termination.
- The normal shutdown sequence remains worker join, High cleanup, then successful Low completion. Thread termination is checked independently, including while an operation is pending.
- Rejected operations and dispatcher exceptions do not authorize immediate unload. The documented still-live/rejecting-dispatcher limitation remains.
- App promotion checks decay rather than mere map membership. Preview eligibility checks its separate recency rules.
- Captured button HWNDs include PID validation. Icon matching does not reintroduce filename/title guessing.
- Thumbnail resolution remains DataContext/model first, repeater mapping for holes, with duplicate HWND and disagreement checks.
- Icon scale restoration occurs before the glow host holding the saved transform is removed. Native preview brush restoration checks ownership before overwriting the current brush.

## Existing limits, not new findings

The rare shutdown cases still need runtime evidence. In particular, a live thread with a permanently rejecting dispatcher intentionally blocks unload. Event-subscription revocation failures and the exact lifetime of completion delegates are not fully established by the fake-dispatcher harness. These are previously discussed assurance limits; this pass did not reproduce a failure in either one and does not label them as newly proven crash bugs.

The earlier crash report identifies an exception and loaded modules, but supplies no call stack or retained dump. It does not justify blaming this mod, exonerating it, or attributing the crash to not restarting Explorer after an update.

## Validation and recommendation

- Windhawk compiler `-fsyntax-only`: passed.
- `python tests/run-dispatcher-tests.py`: passed.
- `git diff --check`: passed.
- No new Explorer injection or forced unload was performed during this review.

Keep the current version under ordinary-use testing with crash capture available. The two idle-maintenance items are suitable for a small, separately tested cleanup patch; they are not reasons to suspend normal testing or expand the shutdown architecture. If changed, add focused state-transition tests rather than starting another broad speculative rewrite.

The review count alone is not a reliability metric. Several recent rounds revisited successive changes to the same shutdown problem. That is a reason to demand reproducible evidence and narrower patches, not a reason to keep inventing hypothetical blockers until a review produces no observations.
