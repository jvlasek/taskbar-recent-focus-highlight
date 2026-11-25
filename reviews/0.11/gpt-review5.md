# Full review 5 — v0.9.30

**Assessment: the four previous corrections are present and fix their specific reported paths. Five P2 issues remain in the broader implementation. No new P1 is established by this pass; this is not an unload-safety certification.**

Reviewed on 2026-09-22 against the local working tree: initial HEAD `f518798f78eb3e62c5cb9730e99bd9c7d757317b`, C++ blob `241714d59408413616165f73cf5bc8143c56e2e2`, 8,081 lines, version 0.9.30. The CPP, AGENTS, and README initially had uncommitted changes; these were reviewed and preserved. During the review, another actor committed them as `7c36861` (0.9.30 fixes from gpt-review). Final hash checks confirmed all three reviewed files remained unchanged. References below are one-based lines in `taskbar-recent-focus-highlight.wh.cpp` at that blob.

I reviewed the mod's ranking, focus timers, identity resolution, icon and preview painting, hooks, settings, and initialization/shutdown, alongside README, AGENTS, previous reviews, and relevant local reference-mod patterns. The on-disk `gpt-review-reply.md` still describes v0.9.27; I found no newer reply file. The actual v0.9.30 changes, rather than that older reply's claims, are the basis of this assessment. No remote branch or current PR discussion was compared in this pass.

Both **app highlights and per-window preview-flyout highlights are essential features**. None of the recommendations below requires removing either.

## Findings

### 1. [P2] Cached group HWNDs can bind an unrelated app after handle reuse

**Locations:** 550–556, 2063–2086, 3897–3917, 3970–3978, 4009–4028.

The rank's `lastHwnd` has a PID guard, but the button's `groupHwnds` contains only bare handles. `IdentityMatchesRank` accepts equality with any cached group handle before comparing paths or AUMIDs. Its PID check proves that the rank still describes its current window; it does not prove that this window still belongs to the button whose group was cached earlier.

Concrete sequence:

1. Button A resolves a group containing windows H1 and H2, with H1 as its sample.
2. H2 closes; H1 remains alive. H2's handle is later reused for a window belonging to app B, which receives a valid B rank and PID.
3. A full bind checks A's sample H1, finds its image path unchanged, and retains A's old group list.
4. B's current `lastHwnd` equals the stale H2 entry in A. The function returns true before noticing that A and B have different paths. A can receive B's highlight, potentially alongside B's actual button.

This can survive full binds because sample validation does not refresh group membership while H1 remains valid. Microsoft explicitly documents that window handles can be recycled to identify another window. [IsWindow documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindow).

**Correction:** retain and validate the captured PID for each cached group member, or invalidate/rebuild membership before using it as authoritative evidence. Apply consistent lifetime checks to cached sample evidence too. The existing rank-side PID check alone is insufficient. Do not introduce name matching.

**Acceptance:** preserve A's first window, close its second, and model/reproduce reuse of the second handle by B with a different PID. Only B's button may match B's rank. A small identity-table test can make this deterministic without relying on the OS to recycle a particular handle during a manual run.

### 2. [P2] Preview instant promotion treats a stale HWND record as a confirmed current window

**Locations:** 7193–7205, 7279, 7109–7120; existing correct eligibility helper at 1796–1818.

`windowAlreadyTracked` is computed solely from map membership and a nonzero tick. It does not check the saved PID or preview decay. `SchedulePreviewConfirm(true)` then bypasses the preview minimum and immediately stamps the new foreground window.

If an old preview window closes and its handle is reused by another process before the next pruning pass, a genuinely new window receives immediate confirmation using the old window's membership. `StampWindowRecencyLocked` does detect the PID mismatch and removes the old row, but that happens **after the decision to skip the wait**; it immediately creates a newly confirmed row. The old timestamp is not copied, yet the new window still bypasses its required minimum focus.

An expired row awaiting the 30-second prune similarly qualifies for immediate promotion. The existing `IsWindowRecentForPreviewLocked` already checks PID and decay, but is not used for this decision. App `alreadyTracked` also omits decay, so the equivalent expiry-boundary behavior deserves the same treatment for app promotion.

**Correction:** make the preview skip decision using current, non-decayed window identity, preferably the existing helper. Use a consistent definition of “still tracked” for app promotion as well.

**Acceptance:** seed a nonzero preview record for HWND H/PID P1, then deliver foreground H/PID P2 with a positive preview minimum. No stamp occurs until the new deadline. Repeat with an expired record for a still-live window just before the next decay tick.

### 3. [P2] Preview plate restoration overwrites a newer owner's background

**Locations:** 4991–5017, 5181–5182, 5430–5448.

Icon scaling now checks ownership before restoring, but preview plate cleanup does not. Its marker saves only the prior brush; it does not retain the exact brush installed by this mod or verify that the same target border still owns it. Every repaint starts by restoring that old brush unconditionally.

Example: the border starts with brush T0; the mod saves T0 and installs its tint; a Styler update installs T1 while the flyout is open. Clearing or repainting this card writes T0 over T1. Repainting then saves T0 again, so subsequent clear still loses the new style. A replaced native border found under the same view can also receive a snapshot taken from its predecessor.

This contradicts the advertised preservation of other mods' preview styling. It is a separate native-property ownership problem from the now-fixed icon-transform takeover.

**Correction:** associate restoration state with the target border and installed brush, restore only while the current value is still owned by this mod, and adopt the latest displaced value when deliberately taking ownership again. Preserve the difference between an unset local value and an explicitly null local value. An owned overlay is another option where native-property preservation cannot be established.

**Acceptance:** while a plate-highlighted flyout is open, replace its native brush externally, then clear, change rank, and disable. The externally installed brush must survive. Also test a template with no local background and one with a local null background.

**Related zero-setting edge:** `previews.fillOpacity=0` still replaces the native background with a transparent brush at 5443–5446. It removes the tint but also hides the prior background for the duration of the highlight. For zero tint, leave the native background intact after restoring any owned plate. This is distinct from the previously fixed nonzero opacity floor.

### 4. [P2] A refresh queued by a disappearing button can be discarded before pruning runs

**Locations:** 3599–3611, 4365–4367, 6141–6169, 7299–7336.

The new dead-cache pruning correctly runs before eligibility **when a full bind executes**. However, the Low-priority full-bind callback is still tied to the scheduling button's lifetime: `!weak.get()` returns without running the bind or posting a replacement request.

An unpinned button can schedule the refresh during its final visual updates and be destroyed before Low-priority work runs. Its dead cache entry then survives because the cleanup that would remove it was just cancelled. If its last observed running state was true, decay computes the same ordered keys from the same stale cache and does not request a UI pass. With no further foreground/hover/settings activity and app decay disabled, the vacant slot can remain occupied indefinitely. If a final not-running observation did occur, decay can repair eligibility later, but the intended immediate reranking is still lost.

This is not a claim that the new pruning loop fails. It is a missing guarantee that the loop will run after the event it is intended to repair. There is also no direct destruction-triggered refresh for a button that disappears without a final observation.

**Correction:** once work is queued on the dispatcher, a global full bind should not require that particular button to remain alive. Use the dispatcher lifetime plus the unload guard, or reschedule through the established focus-thread/dispatcher mechanism when the anchor disappears. Ensure removal has a reliable invalidation trigger; do not restore the 400 ms heartbeat eligibility rule or add off-thread XAML access.

**Acceptance:** disable app decay, keep A foreground and C/D running, queue a refresh from ranked unpinned B, then destroy B before executing the Low callback. Without another hover/focus event, the visible ranks must become A/C/D. Also cover disappearance without a final `IsRunning=false` observation.

### 5. [P2] The empty-resolution retry cap never resets for a new running episode

**Locations:** 728–730, 2011–2032, 3940–3953, 4074–4078, 4102–4107.

After eight empty path/AUMID resolutions, a button stops resolving unless forced by a pointer press or another bypass. The comment promises another attempt when a pinned button becomes running. That bypass depends on `!resolvedWhileRunning`, but the field is initialized false and only ever assigned true. Observing the button as not-running never resets it or its retry budget.

A reused pinned button that exhausted its retries while running remains permanently capped across later close/relaunch cycles. Launching the app through a shortcut, Start, or another process does not invoke this button's force-on-pointer-press path. Even if taskband identity is now available, full binds keep skipping the lookup and the app remains unhighlighted until an unrelated forced resolve occurs.

**Correction:** track the running transition explicitly and reset the per-episode resolution state/retry budget on that transition. Decide whether capped running failures need a bounded recovery path as well. Retain throttling; the goal is recovery, not probing on every hover.

**Acceptance:** model eight running resolution misses, observe not-running, then relaunch the same button without pointer input after making identity available. The next eligible full bind must attempt resolution and recover.

## Status of the previous four findings

| Review 3/4 finding | v0.9.30 assessment |
|---|---|
| Dead identity-cache rows survive a full bind | **Specific case fixed.** `CollectLiveButtonsOnThisDispatcher` prunes dead identity rows before recomputation. Finding 4 concerns delivery of that bind, not the pruning implementation. |
| Clear erases the rank before deciding to refresh; decay compares only counts | **Fixed.** The old rank is captured first, and decay compares ordered keys. |
| Second icon-transform takeover restores the first snapshot | **Fixed for the reported takeover.** Each takeover replaces the saved transform and origin flags; obsolete Tag state is cleared. |
| Due positive deadlines are rearmed as Immediate | **Fixed.** Both helpers now use FromTimer for elapsed positive minima, preserving transient handling. |

The embedded README now explains immediate tracked-app re-promotion, bar roundness accurately says the bars ignore it, and native-versus-fallback preview corners are distinguished. AGENTS no longer tells contributors to assign/detach the completion handler a second time. These are useful corrections.

## Quality, complexity, and catalog-review concerns

The latest repair is reasonably contained: 43 net C++ lines, using existing state and dispatch mechanisms rather than adding another timer or fallback identity heuristic. The architecture has worthwhile boundaries: immutable settings, separate app/window histories, exact identity matching, a focus message pump, UI-thread painting, and owned overlays.

The remaining maintenance problem is inconsistent enforcement of those boundaries. “Window identity” is HWND+PID in some structures and bare HWND in others. “Previously confirmed” sometimes means a live non-decayed record and sometimes any nonzero tick. Ownership checks are stronger for icon transforms than preview brushes. These inconsistencies explain why local fixes leave neighboring paths exposed.

Actionable improvements that preserve both core features:

- **Centralize identity and eligibility semantics.** Reuse the existing live-preview helper; give cached group members the same lifetime information as ranked windows. Avoid another special-case matching fallback.
- **Separate invalidation from repaint.** A full global bind should be requested because membership, identity, ranks, or layout became invalid, not because any ranked icon happened to repaint. The UVS hook at 6197–6200 still schedules a full bind for every positive rank. Debouncing limits its frequency but does not make hover-only work a cached paint: with continued events it keeps scanning all buttons and periodically validating image paths. Treat this as a performance concern to measure, not a claim of a measured slowdown.
- **Reduce redundant bind work.** The app-confirm callback at 6995–6999 resolves/snapshots all buttons, then invokes `ApplyAllHighlights_UIThread`, which does another collection and resolution/snapshot pass. The new pruning helper participates in both. One explicit presence snapshot would be easier to reason about and cheaper.
- **Add a small transition test seam.** Keep the distributable single translation unit if desired. Tests of copied predicates are not enough long-term; exercise the actual ranking/identity/deadline helpers with fake observations. The failure sequences above are better regression tests than further expansion of the manual checklist.
- **Trim misleading historical state and comments.** `seenOnTaskbar`, the candidate-assignment bookkeeping, and title fields/comments should be audited for remaining functional purpose. Do not delete compatibility code merely to hit a line target, but avoid retaining explanatory machinery for behaviors already removed.

8,081 lines is substantial for an Explorer-injected mod, but size alone does not make preview ranking expendable. The right simplification target is duplicated state and inconsistent lifetime rules while keeping both app discovery and per-flyout window discovery intact.

## Lifecycle and compatibility: remaining validation limits

The worker-first shutdown and completion-based Low drain remain improvements. I did not find a reason to reopen the specific completion-order bug already closed in the previous pass.

However, the pre-existing failure paths still prevent a blanket claim of safe unload: `RevokeIconPanelLayoutWatchesOnThisDispatcher` removes records before revocation and logs exceptions (3102–3133), while `Wh_ModUninit` logs cleanup failure and continues (7965–7977). A live event subscription surviving a failed revoke would still reference mod code. These are carried-forward fault-testing concerns, not newly demonstrated normal-path crashes. Test dispatcher rejection, revoke failure, and disabling during hover/flyout activity before claiming the unload invariant is proven. A timeout is not a remedy.

The bounded native-object probes are still private-ABI assumptions, not memory-validity checks. The local volume-control reference uses the same general native bridge; copying that pattern does not prove compatibility with every Windows build. Optional preview symbols appropriately preserve icon functionality when missing, but the user should still be able to tell when essential preview functionality is unavailable.

## Documentation follow-ups

- AGENTS calls native probing “Explorer-safe” and lists “deterministic unload” among completed work; those claims need qualification consistent with the failure paths above.
- AGENTS says not to hold `g_layoutWatchMutex` across dispatcher calls, but the revoke collection loop calls `panel.Dispatcher()` and `HasThreadAccess()` while holding it (3101–3111). Either bring the implementation into line with the rule or narrow the rule with a justified threading model; no deadlock is asserted here without a concrete reentrant path.
- The future-work list still calls repeater GetAt “primary,” whereas the implementation and main matching section prefer DataContext/TaskItem first.
- README's preview identity table says HWND+title; actual recency identity is HWND+PID. `WindowFocusInfo.windowTitle` still says “fallback match” despite the removal of title matching.
- “Optionally” in the README can describe a user-facing toggle, but should not imply that preview ranking is secondary product scope. Both features are required for this review's acceptance.

## Validation performed and recommended

Performed source tracing, exact blob identification, diff review, symbol/call-site searches, and `git diff --check` (passed). Reduced in-memory models reproduced the decision failures for stale group handles, the preview skip predicate, brush restoration, capped retries, and a cancelled global bind. These models check the described control flow; they do not execute the C++ or prove Windows/XAML behavior.

No build or Explorer injection/runtime test was run. The repository has no standalone build/test harness, and this review does not alter or reload the user's mod. Preserve the existing live checks for A→B→A, independent app/preview minima, three same-app windows, same-title windows, snap-group cards, desktop return, zero intensities, and unload with an open flyout, in addition to the five acceptance sequences above.

Only `gpt-review5.md` was created. Implementation, README, AGENTS, and pre-existing review files were left unchanged.
