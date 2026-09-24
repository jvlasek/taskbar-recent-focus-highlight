# GPT review 7 — v0.9.33

Reviewed local commit `ba3d13ba5c6b1b785726e0bd8df11d2038cbc848` (`review12-gpt1`), source blob `a71714317013df24d6de1498e8f4fa9d76cdf43e`. The C++ file is 8,327 lines.

This pass examines every source change since review 6, the surrounding dispatcher/unload and recency code, and AGENTS.md/README.md. It builds on the preceding functional review; it is not a claim that every unchanged line received a new independent audit. No implementation changes were made.

**Assessment:** the three specific review-6 findings have targeted fixes. The ordinary unload path is substantially clearer. Two shutdown error paths still need attention before treating the new lifetime guarantees as established. Neither finding concerns ordinary taskbar or flyout ranking.

## Findings

### 1. [P1] Dispatcher property failures can escape the unload entry point

**Locations:** `taskbar-recent-focus-highlight.wh.cpp:6102`, `6121`, `6197`, and `8215`.

`DrainOneUiDispatcher` calls `dispatcher.HasThreadAccess()` before entering any exception handler. `PauseDispatcherAttempt` calls it outside its own try block as well. The previous per-dispatcher catch was removed from `RunOnEachUiDispatcherAndWait`, and `Wh_ModUninit` does not catch either call.

If a retained dispatcher becomes disconnected and that property access throws, execution never reaches `ExceptionMeansDispatcherGone`. This can happen on the initial access, or on a retry after a posting/status failure. The exception instead escapes the drain and the mod's unload function, skipping the remaining cleanup. Whether the host catches it or the process terminates depends on host behavior; this review does not assume a host exception boundary makes interrupted cleanup safe.

This is specifically an exception-coverage defect in the newly introduced shutdown handling, not a claim that ordinary `HasThreadAccess` calls commonly fail. The implementation explicitly intends to tolerate dispatcher disconnection, but only classifies errors from `TryRunAsync`. The property and pumping paths are outside that policy; `ProcessEvents` errors are swallowed without classification too.

**Suggested correction:** make dispatcher access, posting, and pumping participate in one explicit outcome/error policy. Preserve the underlying error where needed. Do not fix this by restoring a broad catch that merely logs and allows unloading without a lifetime boundary.

**Verification:** inject a disconnected-object error on the initial property access and on the retry-time access after an unsuccessful post. Neither case should escape `Wh_ModUninit`, nor should an unrelated error be interpreted as proof of safe unload.

### 2. [P2] A dispatcher that rejects work without throwing can strand unload indefinitely

**Locations:** `taskbar-recent-focus-highlight.wh.cpp:6141–6155` and `6158–6174`.

The new loop has only two successful exit routes: a Low sentinel runs, or `TryRunAsync` throws one of the selected “dispatcher gone” HRESULTs. However, dispatcher shutdown has a documented non-exception result: `TryRunAsync` completes successfully and returns `false`. See [Microsoft's TryRunAsync documentation](https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.coredispatcher.tryrunasync?view=winrt-26100).

For a retained dispatcher that continues returning completed-false operations, the control flow is deterministic:

1. `ObserveDispatcherOp` returns `NotRun`.
2. The High branch drops the operation and retries, or the Low branch retries after its unsuccessful observation.
3. Neither branch obtains further shutdown/lifetime evidence, so neither can reach `DispatcherGone`.
4. Unload keeps posting and sleeping every 20 ms, even though there is no accepted callback being waited out.

There is no documented guarantee in that API contract that these false results eventually become one of the chosen exceptions. I have not reproduced persistent rejection in Explorer; this is a control-flow liveness problem for a documented shutdown response, not a claim that every disable hangs.

The High `Unknown` branch has a related limitation: it deliberately retains the same operation indefinitely, but thrown status/result errors have already been reduced to `Unknown`. If those reads keep failing, no new posting call occurs and the “dispatcher gone” classification is never reached through that route either.

**Suggested correction:** establish how dispatcher termination and outstanding mod callback lifetimes are determined independently of a post eventually succeeding or throwing. Carry enough failure information through operation observation to support that policy. Waiting without a timeout is appropriate for genuinely outstanding work, but repeated rejection is a different state that needs an explicit lifecycle decision.

Do **not** restore the two-/thirty-second fallback or treat `false` as a successful drain. That would reintroduce review 6's safety defect. If indefinite blocking on persistent rejection is an intentional last-resort safety policy, document it as such and narrow the claim that the routine handles a dispatcher being gone.

**Verification:** use a dispatcher substitute that returns completed-false for every post, both before High cleanup and after High cleanup succeeds. Also exercise persistent status-read failure on High. The test should demonstrate the intended safe lifetime policy rather than merely observe an endless retry.

## Review-6 closure

| Previous finding | Current assessment |
| --- | --- |
| Timed retries eventually permit High-only unload | The specific fallback is removed. High completion alone no longer returns from the normal drain state machine. The remaining issue is handling shutdown failures, as above. |
| Low terminal status counted as a successful drain | Fixed in the ordinary observation path: `ObserveDispatcherOp` requires `Completed` plus a true result for `Ran`; false/canceled/error are not accepted. Status-read failure becomes `Unknown`. |
| Expired app history bypasses minimum focus before pruning | Fixed at lines 7426–7434 using the existing decay helpers. This also preserves zero-decay behavior. The settings explanation and AGENTS.md were updated. |

Event creation failure now selects polling instead of skipping that dispatcher's cleanup. Retaining the High operation on an uncertain observation is also a sensible attempt to avoid posting duplicate cleanup while it may still be running.

The earlier fixes for captured HWND/PID identity, preview recency eligibility, brush ownership, refresh-anchor lifetime, and resolve retries are unchanged by this patch. I found no new regression in those fixes in this incremental pass.

## Quality and remaining assurance limits

The explicit `DispatcherOpEnd` states are easier to reason about than the previous provisional `queued` booleans. Separating observation from orchestration is useful. The change still adds another 49 net source lines, primarily to exceptional shutdown handling; the concern is whether those paths form a complete, tested lifecycle policy, rather than the line count by itself.

`HresultMeansDispatcherGone` also deserves a precise justification before its comment is treated as a proof: an object being closed/disconnected must be connected to the lifetime of all previously accepted delegates and event subscriptions. This pass has not established that guarantee for every listed HRESULT, and I am not presenting an unproven crash scenario for each code as separate findings. Similarly, the existing SizeChanged revocation code catches failures and discards the local watch records; a Low sentinel establishes queue ordering, not independently successful event revocation. This was an existing limitation, not introduced by this patch.

The new same-thread `ProcessEvents` path needs an actual reentrancy test if supported. If Windhawk always invokes this unload off the relevant UI thread, documenting that host contract may be simpler than maintaining an untested alternate path.

Small documentation cleanup: AGENTS.md now has a stranded fragment, “a drain — leftover `SizeChanged` lambdas crash Explorer,” after “Never unload on a best-effort failure.” README's “Immediate if in map” label is also less precise than the updated settings explanation: it now means unexpired confirmed history.

Both highlighting features remain appropriate parts of this mod. These findings call for resolving shared shutdown behavior, not removing preview highlighting or adding more identity heuristics.

## Validation performed

- Read the complete source diff from review 6 and traced its callers, retry branches, cleanup path, and decay helpers.
- Checked the relevant Microsoft API contract for shutdown rejection.
- `git diff --check` passed; the workspace was clean before this report.
- No compilation, Explorer injection, shutdown fault injection, or runtime validation was performed. The conditional failure sequences above are derived from the source, not reported test executions.

Before another review round, the highest-value evidence would be a small deterministic test of these dispatcher outcomes plus a real disable/re-enable test while a flyout is open. That would resolve more uncertainty than adding another fallback based only on comments.
