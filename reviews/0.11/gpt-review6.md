# GPT review 6 — v0.9.32

Reviewed local commit `dfb8121de042cb0826ed9e3c918e4854a29069b7` (`issues from review 12`), including the current C++ source, AGENTS.md, README.md, the latest changes addressing Claude's review, and the preceding fixes relevant to review 5. The source is 8,278 lines; its Git blob is `8d25b8832e2d73992bbedf565e7eb17966bca09e`.

**Verdict: the earlier functional fixes mostly hold, but I would not consider the unload issue resolved.** The new implementation improves the ordinary completion-subscription failure path, yet still permits unloading without a proven dispatcher drain. There is also a separate false-success path in how it checks the Low operation.

This is a static review, not an Explorer runtime or fault-injection test. No implementation files were changed.

## Findings

### 1. [P1] Failed Low-sentinel retries still end in High-only unload

**Locations:** `taskbar-recent-focus-highlight.wh.cpp:6097–6123`, `6143–6158`, and `8166–8172`.

The change correctly recognizes that High cleanup completion is not a queue drain, but its failure path still uses that as the final barrier:

1. High cleanup is accepted.
2. Posting the Low sentinel repeatedly fails or returns an unsuccessful operation.
3. The retry loop stops after about two seconds if High has finished, or after about thirty seconds while High remains pending.
4. The fallback waits for High alone, returns `false`, and `Wh_ModUninit` merely logs that result before continuing teardown and returning.

An older Normal/Low mod callback can remain queued behind High cleanup. Finishing High, or waiting an arbitrary additional interval, does not establish that the older callback has finished. Such a callback still needs the mod's code even if its first action is to check `g_unloading` and return. This preserves the central failure in Claude's suggested fallback, with a retry interval added before it.

Microsoft documents that High-priority work runs ahead of other work and that Low work waits for higher-priority work to clear. This ordering is precisely why High completion cannot substitute for the Low drain. See [CoreDispatcherPriority](https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.coredispatcherpriority?view=winrt-26100).

The same underlying problem exists in adjacent error exits: failure to create the event at lines 6061–6064 skips that dispatcher's cleanup, and the outer catch at lines 6156–6158 also returns an error that unload only logs. A failure to post new work does not prove that previously posted work or registered event handlers no longer exist.

**Required outcome:** unloading must be conditional on a demonstrated safe lifetime boundary, rather than on best-effort cleanup plus a diagnostic. That could involve a successfully executed drain, explicit accounting for outstanding callbacks and subscriptions, or a reliable proof that the dispatcher can no longer invoke them. A timeout, posting failure, or exception is not that proof. This is not a recommendation to add another retry layer or blindly spin forever on a dispatcher that has permanently terminated.

### 2. [P1] Low operation completion is accepted without checking whether its callback ran

**Locations:** `taskbar-recent-focus-highlight.wh.cpp:5955–5971`, `6012–6034`, and `6125–6138`.

`DispatcherOpWasQueued` treats `AsyncStatus::Started` as provisionally successful. `RunOnEachUiDispatcherAndWait` stores that answer in `lowQueued`, waits for the operation to finish, and then checks the final result of **High only**. It never validates the final status/result of Low.

A concrete control-flow sequence is:

1. High cleanup completes successfully.
2. Low is still `Started` when `lowQueued` is calculated, so the branch enters the wait.
3. Low finishes with `GetResults() == false`, or is canceled/errors without executing the sentinel.
4. The completion notification releases the wait. High's check passes, and this dispatcher is reported successful despite no executed drain sentinel.

This is not merely an invented return convention: Microsoft explicitly documents that `TryRunAsync` can complete successfully with a **false** result during dispatcher shutdown. See [CoreDispatcher.TryRunAsync](https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.coredispatcher.tryrunasync?view=winrt-26100). A terminal operation and a successfully executed callback are different facts. Dispatcher shutdown also needs its own lifetime argument; the false result alone does not provide one.

The polling fallback has a related weakness: an exception reading `Status()` breaks the loop at lines 6029–6030, and the void helper gives the caller no way to distinguish that from confirmed completion.

**Required outcome:** propagate a meaningful wait outcome and verify successful Low execution after waiting. Rejection, cancellation, error, and inability to establish completion must remain distinct from a successful drain. Fixing the final-result check alone is insufficient if its failure still falls through to the unsafe unload behavior in finding 1.

### 3. [P2, carried forward] Expired app history can still bypass minimum focus

**Locations:** `taskbar-recent-focus-highlight.wh.cpp:7383–7385`, `2746–2768`, and `7458`.

The preview equivalent is fixed, but app-level `alreadyTracked` still means only “an entry exists with a nonzero confirmation tick.” It does not check whether that tick has expired under `decayMinutes`.

With `immediateTracked`, returning briefly to an app after its decay deadline but before the periodic prune removes its entry skips the minimum-focus wait and refreshes its recency. Returning after the prune waits normally. Thus the same user action can behave differently according to the timing of the roughly thirty-second maintenance pass.

This is a smaller, existing issue, not a regression introduced by the latest unload changes. Apply the app's decay rule when deciding whether existing history qualifies for immediate promotion, as the preview path now does. If “tracked until physical pruning” is intentional product behavior, document that exception instead; it is otherwise inconsistent with treating decay as the end of recent history.

## Recheck of the previous functional fixes

| Area | Assessment |
| --- | --- |
| Reused HWND matching the wrong app | Addressed: captured identities now include PID, and matching/staleness checks validate it. |
| Stale or expired preview history skipping minimum focus | Addressed: the foreground path now uses `IsWindowRecentForPreviewLocked`, including liveness, PID, and decay. |
| Restoring a preview brush over a newer owner's brush | Addressed for the reported case: restoration checks that the current brush is still the mod's brush; prior unset and explicit-null values are distinguished. |
| A disappearing scheduling anchor dropping the global refresh | Addressed: the scheduled global refresh no longer requires the original weak anchor to survive. |
| Failed identity resolution remaining exhausted across a new running episode | Addressed: the running transition resets the retry state. |

The owned Taskbar DLL reference now has a shared release helper used on relevant initialization failures as well as unload. The unused completion-handler query and stale stash reference were also removed. These are sensible, contained cleanup changes.

## Quality, complexity, and documentation

The latest source changes are concentrated in shutdown and initialization cleanup, rather than another rewrite of app/thumbnail behavior. I did not identify another new functional regression in those unchanged paths during this pass. That is a static-review assessment, not a claim of exhaustive runtime coverage.

The main maintainability concern is now especially concrete: the unload code mixes attempted posting, provisional acceptance, operation completion, callback execution, cleanup success, and queue drainage behind booleans such as `highQueued`, `lowQueued`, and `allOk`. Those states are not interchangeable. More fallback branches have increased the amount of code without establishing the required safety invariant. A small, explicit lifecycle model would make this easier to reason about than further layers of retries and catches.

AGENTS.md correctly emphasizes stopping the worker before draining UI dispatchers and avoiding timeout-based unload. The implementation still has bounded retry paths that end without a drain, so the documented guarantee is stronger than the code. The source comment immediately above the helper also says High finishing is not the drain, while the fallback ultimately permits exactly that outcome. Update the documentation to match the eventual behavior once the lifetime issue is actually resolved.

Both taskbar and preview highlighting remain legitimate essential features of this mod. The findings do not justify removing either feature; the unsafe shutdown machinery is shared infrastructure that needs a coherent solution regardless of which visual feature uses it.

## Validation needed before calling unload fixed

Exercise successful cleanup and failure cases with work already queued: Low posting throws/rejects while High succeeds; Low begins pending then completes false/canceled/error; completion subscription fails; status inspection fails; event creation fails; and more than one recorded dispatcher participates. Verify callback and subscription lifetimes, not just the helper's return value or lack of an immediate crash.

Also check the carried-forward decay case by refocusing just after expiry but before the next prune. The review did not compile the mod, run these fault cases, or inject it into Explorer. `git diff --check` passed for the reviewed workspace.
