# Recheck of v0.9.27 against gpt-review-reply.md

Reviewed 2026-09-18. **Recommendation: request changes.** Several original findings are fixed, including the app/preview completion mix-up and the long-path retry. However, the new running-presence check introduces a major regression, and three other fixes remain incomplete.

Scope: the reply, the complete implementation/documentation diff against the first review's v0.9.25, and the surrounding callers, timers, rendering, and teardown paths. Reviewed mod Git blob: `45b2cab697356c31bd56f9b421f3ce4f4b8ec2ba` (7,819 lines). The implementation, README, and AGENTS already had uncommitted changes when this recheck began; I left those unchanged.

This is static review, supplemented by small in-memory models of the running-presence predicate, settings-message behavior, and zero-visual early return. Those models are not production-code tests or Explorer integration tests. I did not compile or inject the mod. All `.cpp` line references below identify the current local `taskbar-recent-focus-highlight.wh.cpp`.

## Actionable findings

### 1. [P1, new regression] Running apps expire from eligibility after 400 ms without a UI check

**Locations:** `.cpp:1570–1577`, `1623–1629`, `4315–4325`, `7100–7131`. Related: `ClearButtonRunningGrace` at `904–908` and desktop switching at `921–940`.

`lastRunningTick` records the last time `ButtonCountsAsRunning` observed a running button. It is updated during UI work, not continuously while the application remains running. The new `PathAppearsOnTaskbar` treats that observation as valid for only 400 ms. Silence from the UI is therefore interpreted as the application having stopped.

Concrete sequence with default settings:

1. Confirm A and B; their buttons and timestamps are current.
2. Leave both apps open and stop interacting with the taskbar.
3. The 30-second decay timer recomputes ranks. The apps have not reached their 30-minute decay deadline, but both running timestamps are older than 400 ms, so both are excluded.
4. `ApplyAllHighlights_UIThread` receives an empty rank list and returns through its clear-only branch **before** refreshing button running state.

The highlights can disappear while the applications are still running, and the empty-rank branch prevents that pass from repairing the presence snapshot. A later focus confirmation can recover them; ordinary idle use should not require that recovery.

Desktop switching has an even more direct route: it explicitly clears every `lastRunningTick`, then recomputes ranks. No app can pass the new predicate at that point, and the subsequent empty-rank apply again skips running-state collection. This undermines restoring the destination desktop's remembered ranks.

**Fix direction:** represent observed running state separately from the grace timestamp. Refresh current-desktop presence on the UI thread before using it to determine eligibility, including when there are currently no ranks. Use grace to tolerate an observed transition to not-running, not as a heartbeat that every running app must renew. Preserve history independently of current presence. Increasing the timeout merely postpones the same bug.

**Validation:** keep two ranked apps open without hovering for at least one decay tick; switch D1→D2→D1; then close a pinned top-ranked app and verify a lower-ranked running app fills its slot. The reduced predicate model confirmed that two live apps with 30-second-old observation timestamps produce zero eligible ranks and take the no-refresh path.

### 2. [P1, incomplete fix] The drain can return before its new completion delegate runs

**Locations:** `.cpp:5796–5814`, `5853–5874`.

Always subscribing to `Completed` fixes the specific check/subscribe hang from review 1. That part can be closed. The callback-lifetime issue called out in the same finding is still present.

For an operation that is initially `Started`, the code now installs two delegates from the mod image:

- The Low dispatcher callback signals `done` immediately.
- The operation's `Completed` delegate subsequently calls `DispatcherOpWasQueued` and handles failure.

On successful dispatch, the Low callback can signal the waiter before the async operation invokes its completion delegate. The waiter closes the event and proceeds through unload without waiting for that delegate. The completion handler still executes mod code even when its result is true and it does not call `SetEvent`. Thus a successful UI drain does not establish that all the code introduced by the drain protocol has finished.

Microsoft documents `Completed` as a completion delegate and explicitly supports attaching it after completion; that validates the removed subscription gate, but does not make the Low callback's earlier signal a completion barrier. [IAsyncOperation.Completed documentation](https://learn.microsoft.com/en-us/uwp/api/windows.foundation.iasyncoperation-1.completed?view=winrt-26100).

**Fix direction:** make final operation completion and delegate lifetime part of the unload handshake. Do not allow the Low callback alone to release the unload waiter while a completion callback remains outstanding. Ensure the operation/delegates and event remain owned until the protocol is finished, including failure paths. Keep the unbounded safety requirement; adding a timeout would not solve this.

**Validation:** test both `Started → Completed(false)` and `Started → Completed(true)`. In the success case, deliberately delay completion-handler execution after the Low callback has run and verify unload cannot finish first. This is a source-level lifetime finding, not a reproduced Explorer crash.

### 3. [P2, new regression] Every settings change cancels both pending timers, even for an allowed app

**Location:** `.cpp:7200–7210`; sender at `7816`.

`WM_APP_SETTINGS_CHANGED` unconditionally calls `CancelMinFocusTimer` and `CancelPreviewMinFocusTimer`. It then clears pending state only if the app is excluded. An ordinary color, thickness, intensity, or unrelated exclusion change therefore leaves an allowed candidate valid with neither timer armed.

If the target stays foreground, there need not be another foreground event to restart the timers. The current focus episode can fail to confirm, and the valid pending state also prevents idle detection. The in-memory control-flow model yielded exactly this state: `valid=true`, `excluded=false`, both timers false.

**Fix direction:** cancel/drop an excluded candidate, but preserve or re-arm unfinished deadlines for an allowed candidate on the focus thread. Changes to minimum-focus durations or preview enablement need an explicit policy, using the episode completion flags and elapsed time.

**Validation:** update an unrelated setting while a new app is partway through its minimum-focus interval, keeping the target foreground. It must still confirm. Test both app and preview deadlines; use a programmatic settings update so opening the settings UI does not obscure the case by changing focus.

### 4. [P2, incomplete fix] A weak reference does not preserve the transform being replaced

**Locations:** `.cpp:570`, `3461–3500`, `4214–4245`.

The new code saves the previous local transform as `weak_ref<Media::Transform>`. If the icon property holds the last strong reference to that transform, assigning this mod's scale releases it. The saved weak reference then resolves to null at cleanup, and the code falls back to `ClearValue`—the same loss of the previous transform identified in review 1.

That is an ordinary ownership arrangement: another component can create a transform, assign it to the icon, and retain no separate strong reference. The new restoration only works when some unrelated owner happens to keep the old transform alive. [C++/WinRT strong and weak reference semantics](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/weak-references).

**Fix direction:** either avoid replacing another owner's transform, or actually retain the previous value for the takeover's duration. An owned UI marker can keep the saved value associated with the UI tree, following the existing preview-brush approach. If retaining it elsewhere, explicitly respect dispatcher cleanup and process-shutdown lifetime rules; do not add a strong XAML reference to a normally destroyed global cache without considering those rules.

**Validation:** install a local transform with no owner other than `Icon.RenderTransform`; rank and unrank the icon; verify the original object/value and origin are restored. Also retain the check that a later owner's replacement is never overwritten during cleanup.

### 5. [P2, new regression] Zero intensity plus zero size boost skips removal of an existing boost

**Locations:** `.cpp:3347–3351`, `3437–3504`.

The new zero-visual early return occurs before the `sizeBoost == 0` branch that calls `ClearIconScaleIfOurs`. Hiding glow layers does not remove a scale previously applied to the native icon.

Reproduction without changing settings mid-focus:

1. Configure rank 1 with a nonzero size boost, and rank 2 with intensity 0 and size boost 0.
2. Confirm A so it receives rank 1's scale.
3. Confirm B so A becomes rank 2.
4. A hides its glow but retains the old scale. The code records the new paint-cache state, so subsequent identical paints can keep returning without fixing it.

**Fix direction:** reconcile or restore the owned icon transform before taking the zero-visual fast path. Test transitions into zero, not just an icon whose settings start at zero.

**Validation:** rank 1→rank 2 as above, plus changing an already-scaled rank to zero intensity/zero boost. The reduced branch model confirms cleanup is bypassed; actual visual behavior still needs live validation.

### 6. [P2, incomplete fix] Win32 AUMID exclusions still miss pending focus and existing preview history

**Locations:** `.cpp:6635`, `6727`, `7204–7207`, `7789–7810`; initial admission's additional AUMID check at `1498–1500`.

The app-history sweep now correctly checks `AppFocusInfo.appIdUpper`. But the other two parts of the original exclusion finding are not equivalent to initial admission:

- A Win32 pending candidate stores a path key and executable display name. The timer checks and new settings-message handler pass only those to `IsExcludedKey`; an exclusion containing its window AUMID cannot match. App confirmation reads `appIdUpper` later but still inserts without comparing that value against the exclusion list.
- Existing Win32 preview entries store the path key, not the window's AUMID. The preview-history sweep still tests only that key and its filename. Existing thumbnail recency therefore survives an AUMID-only exclusion even though app history is removed.

Hosted `APPID:` keys are not the problem here, and the new path/executable-name checks are useful. The remaining gap is specifically Win32 windows with a separate AUMID—the same identity form already supported by `ResolveAppIdentity`.

**Fix direction:** use the same exclusion identity information for admission, pending confirmation, and existing history. Capture or resolve missing identity outside the state mutex, then perform the protected state update. Do not narrow the supported identity forms silently.

**Validation:** a Win32 app with a custom window AUMID; exclude that AUMID while pending and after its app/preview history has been confirmed. Neither its icon nor its preview should regain or retain a highlight.

## Status of the first review's ten findings

“Fixed” below means the specific source-level defect is addressed, not that the whole feature passed live tests.

| Original finding | Recheck result |
|---|---|
| 1. Preview clears the app candidate | **Fixed for the reported sequence.** Per-episode flags replace historical map membership. Running-presence regression above is a separate issue. |
| 2. Drain completion race | **Partial.** Unconditional subscription fixes the missed-completion hang; delegate lifetime remains unaccounted for. |
| 3. App confirmation bypasses preview deadline | **Fixed for the reported paths.** App confirmation no longer stamps preview recency; unfinished preview state survives app completion. |
| 4. Explorer shell accepted through same PID | **Fixed for the reported replacement-HWND path.** Transient replacements are rejected before same-PID continuity is accepted. |
| 5. Closed pinned apps occupy slots | **Not acceptable as implemented.** Retained path alone no longer qualifies, but 400 ms freshness is not running state. |
| 6. Zero intensity/fill still paints | **Zero-alpha endpoints fixed.** The new early return leaves prior scaling behind; see finding 5. |
| 7. Exclusions versus pending/history | **Partial.** Path/name checks and app-history AUMID check added; timer cancellation regressed and Win32 AUMID coverage remains incomplete. |
| 8. Long process paths | **Fixed.** Explicit growth reaches the 32,768-character attempt without relying on failure output length. |
| 9. Restore previous icon transform | **Not fully fixed.** Weak storage cannot guarantee restoration of the displaced object. |
| 10. Focus-worker startup failure | **Fixed for the reported failures.** Foreground-hook installation precedes success signaling; event/thread/window/hook failures propagate through initialization. |

The ctor-map PID capture is also a useful improvement: reuse by a different process is rejected when the captured PID is nonzero. Same-process HWND reuse remains outside what an HWND+PID guard can prove; that is a limitation of the chosen identity, not a reason to reopen this as a new required change.

## Other changes and remaining qualifications

- **Edge detection:** authoritative orientation state now precedes geometry. Refreshing cached edge during a full bind also addresses stale opposite-edge placement once that bind runs. This is an improvement; live position-mod testing is still needed. The empty-rank branch skips that refresh, reinforcing the importance of fixing finding 1 first.
- **Revoke failures:** moving revocation outside the layout-watch mutex was already correct and is not disputed. It does not answer the prior concern that a failed revoke is logged after its watch has been removed from bookkeeping, while unload continues. That concern remains open; this pass does not turn it into a claim of a reproduced crash.
- **Preview timing:** the reply says every window waits its own interval and also mentions ranking “including if another app then grabs focus.” Qualify both statements. Previously tracked windows still take the deliberate immediate path in `SchedulePreviewConfirm`; leaving for another app before confirmation replaces the pending episode. The actual fix is narrower and good: app confirmation no longer substitutes for the preview deadline.
- **Opacity curve:** many new formulas multiply alpha by `t` and then element opacity by `t` again. Effective strength is approximately quadratic for icon bars/frames and fallback preview plates, but linear for native plates and title backgrounds. Zero works, but the reply's description of intensity as one proportional scale is not consistently true. Decide whether the changed visual ladder is intended; do not treat this as a release blocker by itself.
- **State flags:** `appConfirmed` and `previewConfirmed` improve ownership of successful completion. Timeout/abandonment is still implicit. For example, an already-completed app with an unfinished preview that exhausts transient grace can leave valid pending state behind after the preview timer stops. This remains an idle-state cleanup concern for the eventual state-transition tests.

## Documentation and complexity

The obsolete title fallback and contradictory “span + transform” instructions are corrected. README version and the targeted regression checklist are updated. The single-XAML-thread assumption is now explicit rather than disguised as dispatcher partitioning.

Several earlier documentation qualifications remain:

- The embedded README still says a short Alt+Tab cannot change ranks (`.cpp:40–41`), despite default immediate re-promotion of tracked apps.
- Side-bar roundness is still described as adjustable (`.cpp:182`), but the bar helper rounds by half-thickness. Native preview plate corners also do not use icon roundness.
- AGENTS now endorses `lastRunningTick` freshness as running presence and describes save/restore of the previous transform without the weak-reference caveat. Those statements reflect the attempted fixes, not their actual guarantees.
- The recency writer table, retry-limit wording, and broad restoration/unload claims identified in the first report still deserve cleanup rather than more duplicated invariant rows.

The patch adds 272 lines and removes 87 from the mod. Most additions have a reasonable purpose, especially explicit episode flags and startup result handling. The problem is not the extra 185 lines by itself: the new code still conflates observation freshness with state and weak identity with ownership. Small transition tests would provide more value now than another broad rewrite or another layer of defensive helpers. No new tracked automated test harness accompanies this patch.

## Suggested completion checks

1. Keep ranked apps open without taskbar interaction through the 30-second decay tick; switch desktops and return.
2. Delay async completion after the Low callback and prove unload waits for all mod delegates.
3. Change an unrelated setting during pending focus without changing the foreground target.
4. Restore a transform for which the icon was the sole owner; test rank transitions into zero boost/intensity.
5. Apply Win32 AUMID-only exclusions to pending focus and existing preview history.
6. Retain the passing-source regression cases from the first report: A→B→A, a window switch just before the app deadline, Explorer folder→desktop, long paths, and worker startup failure.

The original confirmation-state correction should be kept. The next patch should concentrate on the six findings above and demonstrate those transitions, rather than treating the whole first review as either resolved or needing to be redone.
