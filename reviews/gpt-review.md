# Fresh review of Taskbar Recent Focus Highlight

Reviewed 2026-09-18. **Recommendation: request changes before catalog submission.** The design has improved substantially, but ordinary re-focus can fail to promote apps, and the latest unload fix still has an asynchronous-completion race. These deserve attention before cosmetic simplification.

Scope: the local `taskbar-recent-focus-highlight.wh.cpp` v0.9.25 (7,634 lines), embedded catalog README/settings, repository README (284 lines), AGENTS (714 lines), relevant reference-mod identity code, and review-response history. Local HEAD: `3ec3ab1a5d729b2036369e0791ffeb636b436929`; mod Git blob: `e6b828941dd6fd9038c165652489d10ef99026d5`. The working tree was clean at the start.

The public PR head is `22bff1d554d71b1695733d777c166abeae04db8b`, also the commit identified by the latest Claude review. The local code includes subsequent fixes described in `review-response11.md`; this report reviews that local code, not an assumption that it is identical to the submitted PR. [PR and review history](https://github.com/ramensoftware/windhawk-mods/pull/5331), [latest Claude review](https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5701740290).

This was static review, with a small native Windows API probe for buffer-error behavior. I did not compile, inject, disable, or exercise the mod inside Explorer. Reproductions below are code-derived test cases unless explicitly identified as executed. Only this report was added.

All `.cpp` line references below refer to the local mod file. P1 means fix before release; P2 means a concrete correctness issue; P3 means lower-impact maintenance or polish. Compatibility concerns that need reproduction are separated from confirmed control-flow defects.

## Findings

### 1. [P1] Preview confirmation clears the app candidate before app promotion

**Location:** `.cpp:6541–6549`, `6785–6789`, `6943–6948`.

`OnPreviewMinFocusTimerElapsed` considers the app side complete whenever the app has *any historical nonzero timestamp*. That does not mean the current focus episode has been confirmed. It then clears the shared `g_pendingFocus`.

This breaks the default configuration:

1. Focus A long enough to confirm it, then B long enough to put B at rank 1.
2. Return to the previously confirmed window of A.
3. `SchedulePreviewConfirm(windowAlreadyTracked=true)` invokes preview confirmation synchronously.
4. A exists in `appFocusMap`, so preview confirmation clears `g_pendingFocus`.
5. The subsequent immediate app confirmation sees no pending candidate and returns. A does not become rank 1.

`alwaysWait` also breaks: when preview confirmation occurs before the app deadline, it can discard that deadline's candidate merely because the app was confirmed on a previous visit. `immediateTopN` has the same problem for previously tracked apps outside the top N.

**Recommendation:** track app/preview completion for the current focus episode explicitly, or separate their pending state. Historical map membership must not stand in for current-episode completion. This is a regression in the attempt to clean up idle pending state, not a reason to undo the idle-timer improvement entirely.

### 2. [P1] The new drain-result check still has a completion race that can hang unload

**Location:** `.cpp:5617–5633`, `5717–5722`, `5734–5735`.

There are two separate observations of the asynchronous operation:

1. `DispatcherOpWasQueued(drainOp)` observes `Started` and returns true.
2. The dispatcher finishes with result `false` because it is shutting down.
3. The subsequent `drainOp.Status() != Completed` condition is false, so no completion handler is attached.
4. `drainPosted` is still true, but the Low callback never ran. Nothing signals `done`; the infinite wait hangs.

This is exactly the asynchronous case that the round-11 fix needed to cover. Checking `Status()` before subscribing recreates a check/subscribe race. Microsoft documents both the false result during shutdown and that attaching `Completed` after completion still invokes the handler. [TryRunAsync contract](https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.coredispatcher.tryrunasync?view=winrt-26100), [Completed contract](https://learn.microsoft.com/en-us/uwp/api/windows.foundation.iasyncoperation-1.completed?view=winrt-26100).

**Recommendation:** use a single completion protocol that observes the final result without a pre-subscription status gate. Its ownership must also cover the completion delegate itself: the Low callback currently signals before the operation's completion handler necessarily finishes, so that handler is additional mod code whose lifetime needs accounting. Do not fix this with a timeout that permits unloading live callbacks.

Validation should inject transitions between the two observations: `Started → Completed(false)`, `Started → Completed(true)`, canceled/error, and already-completed operations.

### 3. [P2] App confirmation bypasses the supposedly independent preview deadline

**Location:** `.cpp:6644–6647`, `6670–6687`.

`alsoConfirmPreviewWindow` compares the configured app and preview durations. It does not check how long the *current window* has actually been focused. The same comparison decides whether pending state must survive for the preview timer.

With app minimum 8 seconds and preview minimum 1 second, immediately promoting a tracked app's previously unconfirmed window also stamps that window immediately. A second route needs no instant promotion: spend 7.8 seconds in one window of a new app, switch to another window of that app, and let the app deadline fire at 8 seconds. The second window gets preview recency after roughly 0.2 seconds because `1 <= 8`, despite its reset `previewStartTick`.

**Recommendation:** decide preview readiness from `previewStartTick` and its own deadline. App completion should leave an unfinished preview candidate alive regardless of the ordering of the configured durations. Explicit preview clicks can retain their documented immediate exception.

### 4. [P2] Same-PID confirmation can turn Explorer's desktop/taskbar into a confirmed app window

**Location:** `.cpp:6435–6465`, `6641–6668`; related transient handling at `6799–6812`.

`HandleForegroundChanged` correctly treats the desktop and taskbar as transient. However, when the timer fires, `StillPendingForeground` accepts a different foreground HWND from the same PID without checking `ShouldIgnoreHwnd` or `IsTransientForeground`.

File Explorer folder windows share Explorer's PID with the shell. Start an unconfirmed folder-window candidate, then focus the desktop or taskbar before its deadline. The timer can accept that shell HWND instead of entering the bounded transient branch. App confirmation stores the shell window's class and HWND against the Explorer image-path key; preview confirmation can stamp it as a window too. Folder icons can consequently lose their match because their class differs from the newly stored shell class.

**Recommendation:** validate that the replacement HWND is an eligible app window before accepting same-PID continuity. A transient same-process HWND must take the transient-wait path. Keep the legitimate same-app window transition behavior.

### 5. [P2] Closed or pinned-only apps can continue occupying top-N slots

**Location:** `.cpp:1557–1581`, `1600–1620`, `4289–4291`; cache pruning at `3950–3960`.

`PathAppearsOnTaskbar` checks stored path/AUMID strings, not current running presence. A pinned button retains its known path after the application exits. Dead button cache entries also remain until the cache exceeds 128 entries or that identity is otherwise revisited; pruning `g_trackedButtons` does not prune this cache.

Focus D, A, B, C with highlightCount=3, then close pinned C. The paint pass correctly leaves C dark once running grace ends, but `PathAppearsOnTaskbar(C)` remains true. C keeps rank 1 until decay, A/B retain weaker ranks, and D cannot fill the vacant visible slot. With decay disabled the stale rank can persist indefinitely.

**Recommendation:** keep retained identity/history distinct from evidence of a currently running taskbar app. Gather a live running-identity snapshot on the UI thread, then use that plain-data snapshot for rank eligibility. Preserve virtual-desktop history and do not resolve XAML weak references on the focus thread.

### 6. [P2] Zero intensity and zero title-background opacity still paint visible highlights

**Location:** `.cpp:3332–3344`, `3355–3366`, `3382–3394`, `5207–5228`, `5264–5268`, `5300–5308`.

The settings accept 0–100, but most render formulas have a nonzero floor:

| Style | Result at intensity 0 |
|---|---|
| Icon frame | First stroke alpha 100 with opacity 0.70 |
| Icon side bar | 55% of configured fill alpha, then opacity 0.85 |
| Preview native plate | 45% of configured fill alpha |
| Preview title bar | Alpha 90 with opacity 0.50 |
| Preview title background | Alpha at least 8 |

Title background also remains visible with `previews.fillOpacity=0`: `maxA` is at least 16. This is separate from the old settings-loader bug that replaced all-zero values with defaults; removing that override did not make the renderer honor zero.

**Recommendation:** define zero endpoints explicitly and consistently. Multiply the selected maximum strength by rank intensity, and make tint opacity zero suppress the title wash. Icon size boost can remain independently controlled; test opacity with size boost disabled to avoid conflating the two features.

### 7. [P2] Updating exclusions does not invalidate pending confirmation

**Location:** `.cpp:7600–7634`, `6475–6554`, `6562–6696`.

The settings callback removes excluded entries from existing maps, but does not invalidate `g_pendingFocus`. Neither timer confirmation path rechecks exclusions before inserting into the maps. A settings update that excludes a still-pending foreground app can therefore be followed by its timer inserting the excluded app/window again. Recompute does not independently filter exclusions.

This is most directly tested by applying the setting programmatically while the target remains foreground, rather than opening Windhawk's settings window and changing focus as a side effect.

There is a related existing-history gap for Win32 AUMID exclusions: `ResolveAppIdentity` checks the window AUMID explicitly, but the settings sweep tests only the path key and display name. `AppFocusInfo.appIdUpper` is available but unused there. Previously confirmed Win32 windows excluded by AUMID can retain their existing glow until another event removes it.

**Recommendation:** apply settings changes to pending state on its owning thread and revalidate exclusions at confirmation. Make the history sweep use the same supported identity forms as initial admission. Do not call shell property-store APIs while holding the state mutex.

### 8. [P2] The process-path retry loop cannot reliably grow past MAX_PATH

**Location:** `.cpp:1103–1125`.

After `ERROR_INSUFFICIENT_BUFFER`, the code exits if `n <= size`, and otherwise uses `n` as the required size. `QueryFullProcessImageNameW` documents the output length on success; it does not provide a required-size contract on buffer failure. [Microsoft API documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew).

I executed a native call against the review PowerShell process with a two-character buffer: it returned false, error 122 (`ERROR_INSUFFICIENT_BUFFER`), and left the length at 2. The mod's condition would therefore stop rather than grow. A process whose image path exceeds the initial 260-character buffer can remain unresolved, losing both focus admission and button matching.

**Recommendation:** explicitly grow the buffer on that error up to a chosen bound, or use a sufficiently large initial buffer. Do not rely on a failure output length as the allocation size. The probe validates error behavior, not a live long-path app inside Explorer.

### 9. [P2] Size-boost cleanup cannot restore a transform that painting overwrote

**Location:** `.cpp:3396–3404`, `4088–4123`.

The clear path now correctly refuses to clear another mod's *current* transform. But the apply path still replaces any existing `Icon.RenderTransform` and its origin without saving either. If Taskbar Styler or another mod has a persistent transform before the first ranked paint, size boost replaces it; clearing this mod's own transform then uses `ClearValue`, losing that pre-existing local transform.

**Recommendation:** either refrain from taking over an existing transform, or preserve and restore the previous transform/origin when this mod still owns the property. The default size boost is nonzero, so this is relevant to the advertised coexistence behavior, not only an obscure opt-in.

### 10. [P2] Focus-worker startup failure still reports successful initialization

**Location:** `.cpp:7091–7124`, `7133–7141`, `7195–7223`, `7518–7519`.

The updated initialization correctly propagates symbol-hook failures, but `StartWinEventHookThread` returns void. Event/thread/window-class/window creation failures only log; early worker failure still signals the ready event, and `Wh_ModInit` returns true. The event is also signaled before installing the foreground hook, whose failure only logs and continues.

The mod can consequently appear enabled while never tracking subsequent foreground changes. This is the same failure-reporting category as Claude's symbol-init finding, at a different initialization stage.

**Recommendation:** return a startup result covering the functionality needed for focus tracking, distinguish failure from readiness, and unwind acquired resources on failure. Preserve the legitimate late Taskbar.View load path.

## Additional concerns requiring targeted validation

These are not presented as reproduced Explorer crashes or reasons to add speculative fallback code.

- **Unload failure handling remains incomplete** (`.cpp:5699–5742`, `7549–7561`, `3034–3069`). Event creation/dispatch/revocation failures can return or log while unload continues. Revocation removes a watch from the registry before attempting to revoke its token; if revocation throws, an empty registry does not prove cleanup succeeded. Establish what each failure means for the continued existence of the dispatcher and event source. A failed queue is not itself proof that no handlers remain.
- **Multi-dispatcher safety is claimed but not actually enforced.** `ForEachLiveElementOnThisDispatcher` resolves `weak.get()` before checking the owning dispatcher (`.cpp:4126–4146`); pruning and watch revocation do similar cross-list scans. This is safe only under the practical single-XAML-thread assumption, which the review replies explicitly rely on. Record and verify that assumption, or partition entries by dispatcher before resolving them. I would not demand a new multi-dispatcher framework without evidence that a second thread occurs.
- **Cached edge detection misses changes without size changes** (`.cpp:3073–3097`, `3137–3175`). A left→right or bottom→top move can keep dimensions unchanged. `SizeChanged` then supplies no edge invalidation, and UVS uses the old edge indefinitely if Explorer reuses the panel. Test both moves with the actual taskbar-position mod. Do not assume the panel is always rebuilt.
- **Wide labeled buttons can defeat orientation state** (`.cpp:2813–2848`). `panelLooksVertical` wins even if `horizontalState` is true. A horizontally arranged but wide labeled IconPanel could be treated as vertical. Verify actual panel geometry with labels enabled; if the state is authoritative, geometry should be fallback only.
- **Native probing is bounded, not memory-safe** (`.cpp:3466–3475`, `3524–3545`, `3649–3673`). Scanning 32 pointer slots does not establish that an object contains 32 slots. Calling `GetNumItems` on fabricated storage assumes its implementation shape before validating the returned offset. The GetWindow fallback also invokes a concrete implementation on an unrecognized interface pointer. These are inherited private-ABI risks, not proven failures on current supported builds. Keep the assumptions explicit and validate against reference implementations/builds; a normal C++ `catch (...)` is not a general access-violation guard.
- **Cached thumbnail mappings lack their own PID capture** (`.cpp:577–582`, `4362–4367`). A live model holding an old HWND can pass `IsWindow` after handle reuse. The recency map's PID check protects old recency entries, but does not authenticate the model→HWND mapping if the replacement window has already received fresh recency. Add a handle-reuse test before treating the “cannot go stale” comment as a guarantee.

## Code quality and complexity

There are sound foundations here: exact process/AUMID identity, optional thumbnail hooks, immutable settings publication, cached accent, a dedicated message-pump thread, posted click confirmation, owned overlay names, and stopping the worker before draining UI. Removing fuzzy matching was the right simplification. I would retain those decisions.

The main maintainability problem is **implicit state coupling**. App and window tracking share one `PendingFocus`, and infer completion from map contents or configured durations. Identity retention, current taskbar presence, and whether a button was painted are also interleaved. Findings 1, 3, and 5 arise from those blurred responsibilities. Splitting the file alone would not fix them.

The 7,634-line size is high for the feature, but line count is not by itself a correctness defect. Roughly 1,400 lines cover icon visuals/layout, and roughly 1,300 cover preview tree handling/rendering/ranking. Private taskbar integration accounts for legitimate complexity; the repair and fallback layers account for much of the avoidable complexity.

Recommended simplifications, in priority order:

1. Make focus-episode transitions explicit and testable: app pending/completed, preview pending/completed, desktop, candidate identity, and deadlines. This would eliminate several duration comparisons and map-membership proxies.
2. Separate live running-button evidence from the retained identity cache and recency history. Use one captured desktop/settings snapshot throughout a paint/rank pass; `ApplyAllHighlights_UIThread` captures `deskId` but later mutates `CurrentDeskLocked()` again, and preview tick/sequence reads use separate locks.
3. Replace `Cand` plus `buttonTaken` bookkeeping with straightforward per-button first-match assignment if behavior is unchanged. Matching is now boolean; remnants of the scoring architecture remain.
4. Centralize style opacity mapping and setting clamping. Validation is repeated in the loader and renderers, yet zero semantics still differ across styles.
5. Simplify dispatcher ownership after measuring the real thread arrangement. Do not remove shutdown synchronization merely to reduce lines.
6. Treat native property/order restoration as ownership, not repair of arbitrary current state. `RestoreIconPanelNativeZOrder` is called on every non-cached ranked paint (`.cpp:3303`), not only insertion/removal. A snapshot retained from the first paint can also overwrite later legitimate Styler ordering.

Performance deserves measurement rather than blanket “lightweight” claims:

- Ranked UVS still schedules full binds (`.cpp:5942–5946`); debounce caps frequency but does not eliminate repeated whole-taskbar work during sustained hover.
- Full binds do shell/process work on the UI thread. The two-second stale-check throttle helps. `ProcessImagePathCacheScope` is a one-entry last-PID cache, not a per-pass PID map.
- Preview refresh performs repeated tree walks, model-map scans, per-window state locks, and unconditional visual resets. Native plate repaint also reparses a marker with `XamlReader::Load` each time, even though the main overlay host is retained.
- Demoted app entries with timestamp zero never reach the decay-erasure branch (`.cpp:1600–1603`). They accumulate, and `RecencyMapsEmptyLocked` keeps the decay timer alive because it tests container emptiness. This is a lower-impact resource/idle-work defect, not the reason to block release.

Broad exception swallowing is understandable around volatile XAML trees, but critical setup/teardown needs explicit success reporting. Optional visual failure and inability to prove safe unload should not have the same “log and continue” treatment.

## README and AGENTS review

The embedded catalog README is much more appropriate than the older developer-checklist version: it explains the feature, includes screenshots, and documents exact-match failure behavior. The repository README provides useful architecture context. AGENTS preserves valuable hard-won constraints, but has become a mixture of specification, historical incident log, and instructions for pleasing the reviewer.

Concrete corrections needed:

- **Removed title matching is still documented.** Repository `README.md:194–197` describes a unique-title fallback; `AGENTS.md:167–168` says missing hooks may fall back to title-only. Both contradict the current code and other paragraphs in the same documents.
- **Contradictory layout instructions.** `AGENTS.md:447` says “use span + transform,” while the earlier layout rules and implementation explicitly require Margin instead of RenderTransform.
- **Min-focus claims need the re-focus exception.** Embedded README `.cpp:40–41` promises short Alt+Tab does not change ranks, while the default promotion policy is supposed to promote tracked apps immediately. Once finding 1 is fixed, that promise will again visibly disagree with default behavior. Explain the initial admission wait versus the selected re-focus policy.
- **Settings descriptions overstate geometry behavior.** The side-bar roundness description says it uses the roundness setting, but `StyleGlowBarOnSide` always uses half-thickness. Native preview plates preserve their native corners rather than reading icon roundness; only the overlay fallback uses it.
- **Thread ownership table is inaccurate.** `AGENTS.md:353` says focus writes/UI reads recency state, but UI binding marks/demotes/recomputes entries, and the settings callback erases them. Document actual writers or change the ownership model later.
- **Retry policy contradicts the implementation.** AGENTS says an empty resolve is not permanent; the code caps empty attempts at eight. State the retry limit and reactivation triggers. `resolvedWhileRunning` is a lifetime flag, not an explicit running-state transition tracker.
- **Restoration claims are too strong.** “Own ScaleTransform” protects a later owner's current value but does not restore a prior owner (finding 9). The native-order snapshot is not automatically refreshed after an unranked period. Qualify coexistence claims with the combinations actually tested.
- **Shutdown wording should be precise.** Repository README's final runtime step implies waiting for UI and worker before clearing, while actual safe sequencing is stop worker, clear/revoke on UI, then drain. “Deterministic unload” in AGENTS is an intended invariant, not currently established evidence.

I would shorten AGENTS to a small set of authoritative invariants, the ownership table, and links to a separate regression matrix/history. The repeated decision tables and incident rows already disagree. In particular, “copy a cited catalog pattern” is useful precedent, not proof of API correctness: the review history itself illustrates how a suggested fix can need additional analysis. Optimize for maintainable invariants and reproducible behavior, not for avoiding another bot comment.

## Verification that would materially improve confidence

No repository test/build harness was found among tracked files. The extensive manual checklist is useful, but misses the combinations that expose the shared-pending-state bug. Add a small pure transition test seam while keeping the published Windhawk mod a single translation unit if needed; a wholesale build-system rewrite is unnecessary.

Prioritize these cases:

1. Default settings, A→B→A using the same confirmed windows: A must return to rank 1.
2. All three promotion modes, app already tracked/not tracked, window already tracked/new, with preview enabled/disabled.
3. App minimum greater/equal/less than preview minimum; switch windows just before the app deadline.
4. Explorer folder→desktop/taskbar before confirmation; no shell HWND/class enters app or preview recency.
5. Async drain completion races and cleanup rejection; verify every event source and delegate lifetime, not only whether the wait returns.
6. Close a top-ranked pinned app: a still-running lower-ranked app fills the slot; check with decay zero too.
7. Apply exclusions during pending focus and exclude existing Win32 history by AUMID.
8. Zero intensity across all styles and zero preview tint; disable size boost during opacity checks.
9. Long executable paths, same-named exes in different directories, recycled HWNDs, and partial/failed identity resolves.
10. Persistent pre-existing transforms, live Styler changes, same-size opposite-edge moves, labeled taskbars, and disable with a flyout open.

The most valuable next step is a focused correction of the confirmation state machine and async completion protocol, followed by these regression cases. Another round of isolated helper additions without those tests is likely to continue the review ping-pong.
