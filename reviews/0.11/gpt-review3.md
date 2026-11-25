# Third review: v0.9.29

**Assessment: substantial progress; four narrower P2 findings remain.** I did not identify a new P1 in this pass. The previous idle-running regression and the specific premature Low-sentinel signal have been addressed. The remaining concerns chiefly involve invalidating state when a button disappears or another owner changes a property.

Reviewed the changes from v0.9.27 (`87a849e`) through the current working file, plus the affected callers and README/AGENTS changes. Current HEAD is `cbac0b25f55ebe89b259eaefa7d4488d35d86b77`; the reviewed mod blob is `f6166686a39a5b09642ca2530196d435c3e45ad0`, v0.9.29, 8,038 lines. This includes the existing uncommitted `FrameworkElement host{nullptr}` correction. There was no new reply to review.

This is static review with reduced in-memory control-flow models, not a build or Explorer integration test. Source references below are to the current `taskbar-recent-focus-highlight.wh.cpp`. I changed only this report.

## Findings

### 1. [P2] A removed button can remain permanently “observed running” in the identity cache

**Locations:** `.cpp:1600–1607`, `3574–3588`, `4093–4107`, `4357–4387`.

Separating `observedRunning` from the 400 ms timestamp fixes the idle-app regression. However, that Boolean is only reset by observing a live button as not-running or by clearing state on desktop switch. Removing a dead weak reference from `g_trackedButtons` does not remove its corresponding `g_buttonPathCache` entry.

When an unpinned button disappears without a final observed `IsRunning=false`, its cached `observedRunning=true` survives. Full binds enumerate only live buttons, so they never update that entry. The general dead-entry sweep in the identity cache only runs above 128 entries, and lookup-based cleanup requires revisiting the same identity. `PathAppearsOnTaskbar` consequently continues accepting the old path/AUMID even though there is no live button.

The closed app can still occupy a top-N slot until decay or another cleanup trigger; with decay disabled it can persist indefinitely. The reduced predicate model confirms that an entry with a dead weak reference and `observedRunning=true` remains eligible regardless of timestamp age.

**Fix direction:** reconcile identity-cache membership with live tracked buttons on the owning UI thread before rank eligibility is computed. Alternatively, remove the corresponding cache entry when pruning a dead tracked button. Avoid introducing off-thread XAML weak-reference resolution or restoring the heartbeat approach.

**Validation:** rank an unpinned app, remove its button without delivering a final not-running observation, and run a full bind with fewer than 128 cache entries. Its history may remain, but it must not qualify as currently on the taskbar. Include a still-running fourth app to verify the vacant slot is filled.

### 2. [P2] Clearing a closed button suppresses the full rebind intended to fill its slot

**Locations:** `.cpp:6053–6069`, `2624`, `6168–6173`; related fallback at `7288–7294`.

The new not-running branch reads the cached rank *after* calling `ClearButtonHighlight`. That clear writes paint rank 0. For the usual case—a highlighted button with chrome—the subsequent `cached > 0` condition is therefore false and `ScheduleRefreshAllHighlights` is skipped. The surrounding UVS hook also reads the now-zero rank and skips its own refresh unless a sweep was already pending.

The closed button loses its glow, but the remaining buttons need not be reranked. This is observable when a background app closes or hides to the tray while another app remains foreground: there need not be a new foreground event to repair the ranking.

The decay pass is not a reliable fallback. It compares only the number of ranks. If the list changes from `[A, B, C]` to `[A, C, D]`, both counts are 3 and no repaint is requested. C can retain its old intensity and D can remain unmarked until some unrelated full bind occurs.

**Fix direction:** capture the old rank or running transition before clearing, then request one debounced full bind when eligibility changes. For decay-driven changes, compare ordered rank identities or an equivalent generation—not only vector length.

**Validation:** keep A foreground, let background pinned B stop after running grace has elapsed, and leave C/D running. Without any further focus or hover input, the visible set and intensity order must become A/C/D. The reduced models confirm both the lost positive-rank test and the unchanged-count repaint omission.

### 3. [P2] A second transform takeover restores the first owner's stale value

**Locations:** `.cpp:3510–3548`, `4272–4306`.

Keeping the displaced transform alive on the glow host fixes the weak-reference ownership problem. The remaining issue is the `Tag`-is-unset condition: it preserves the first saved transform even when this mod subsequently detects that the current transform belongs to someone else and takes ownership again.

Sequence:

1. The icon has transform T0. This mod stores T0 on its host and applies its scale.
2. Another mod or a live Styler update replaces the scale with T1.
3. A rank/settings change forces a non-cached paint. `alreadyOurs` is false, but the host Tag still contains T0, so T1 is not saved.
4. This mod overwrites T1 with another scale.
5. Clearing the highlight restores T0, losing the newer legitimate T1. The origin may meanwhile come from T1, because that field is updated separately.

**Fix direction:** when taking over a property that is no longer this mod's, either yield to its current owner or replace the saved transform/origin snapshot as one unit. Clear obsolete saved state when there is no prior local transform. Continue restoring only while the currently installed transform is this mod's own instance.

**Validation:** use two distinct externally supplied transforms before and during highlighting, force a repaint, then clear. The second takeover must not restore the first snapshot. The ordinary single-takeover/sole-owner case is now fixed.

### 4. [P2] Settings rearming can discard a candidate that is still within transient grace

**Locations:** `.cpp:7017–7036`, `7070–7076`, `6855–6896`, `7398–7399`.

The settings handler now correctly preserves allowed candidates and calls the deadline helpers. But an elapsed positive-duration deadline is routed to `MinFocusConfirmMode::Immediate`. That mode bypasses the transient-grace branch, which is restricted to `FromTimer`.

For example, an app's minimum focus has elapsed while the Alt+Tab switcher holds foreground, and its timer is correctly polling within the bounded grace interval. Apply an unrelated setting while that state persists. `EnsurePendingAppTimer` sees zero remaining time and calls immediate confirmation. Foreground does not match, the transient branch is skipped, and the app candidate is cleared even though its grace has not expired.

The equivalent preview helper also uses Immediate after an elapsed positive deadline. Existing scheduled work can mask that path, but it is not a faithful resumption of deadline processing.

**Fix direction:** for an already-due positive-duration deadline, resume the normal timer/deadline semantics, including transient handling. Reserve Immediate for intentional bypasses such as zero minimum focus, configured instant promotion, or explicit preview clicks.

**Validation:** change an unrelated setting programmatically while a pending app is in its post-deadline Alt+Tab grace, then return to the target before grace expires. It should still confirm. Also retain the ordinary pre-deadline settings-change test that this patch fixes.

## Status of the six findings from review 2

| Previous finding | Current assessment |
|---|---|
| Running observations expire after 400 ms | **Reported regression fixed.** Observed running is persistent, and full binds collect presence before recomputing even with empty ranks. Dead-entry invalidation remains a separate gap. |
| Low callback signals before completion delegate | **Specific ordering defect fixed.** Low is empty; completion signals; the operation remains owned through the wait. Failure handling and live unload stress still need validation. |
| All settings changes cancel both timers | **Reported cancellation fixed.** Allowed pending state is rearmed. The due-deadline/transient case above remains. |
| Weak reference loses previous transform | **Single-takeover case fixed.** The host Tag owns the saved transform, and clear restores it before removing the host. Repeated ownership changes need the correction above. |
| Zero intensity/boost skips old scale cleanup | **Fixed.** The early-return branch now restores the owned scale before caching the new state. |
| Win32 AUMID missing from pending/preview exclusions | **Reported identity-coverage gap fixed.** AUMID is captured in pending/window history, passed into the shared exclusion helper, and considered in confirmation and history sweeps. |

The linear-opacity change is also consistent across the inspected icon and preview paint paths: intensity is no longer multiplied into both brush alpha and element opacity. No need to reopen the old quadratic-opacity observation.

“Fixed” here closes the specific source-level finding, not every possible concurrency or compatibility case. In particular, I am not reopening the completion finding merely because event signaling is followed by a callback epilogue; the concrete extra callback that could run after the old signal has been accounted for. The previously documented rejection/revoke-failure paths still deserve fault testing before claiming unload safety is proven.

## Documentation and maintenance notes

- AGENTS now accurately distinguishes observed running from a freshness heartbeat. Add the missing lifetime rule when addressing finding 1: presence must not survive the button that supplied it.
- `AGENTS.md:707` says to detach the completion handler; the current code does not do so. Remove that claim or explain the intended lifetime protocol. Do not blindly implement a second assignment to `Completed`: its API contract permits setting that property only once. [Microsoft's Completed contract](https://learn.microsoft.com/en-us/uwp/api/windows.foundation.iasyncoperation-1.completed?view=winrt-26100).
- The embedded README still overstates the minimum-focus guarantee for short Alt+Tab under the default immediate re-promotion mode. The side-bar roundness description also still differs from its fixed half-thickness rounding.
- The new AUMID backfill queries every saved window on every settings change, although stored nonempty IDs are retained and only empty ones are backfilled. This is optional cleanup: gather only entries that need backfill, or remove the migration-like path if fresh module initialization always guarantees populated history. Avoid adding more shell calls without an actual missing-data case.
- The mod grew another 219 net lines since review 2. Explicit state is preferable to hidden coupling, but there is still no tracked automated transition harness. The two rank-cache findings above are good candidates for small tests with fake button observations rather than more manual-only checklist rows.

## Why a simple feature became this large

The original requirement—remember three apps and make them easy to find—is small. Its recency logic does not require thousands of lines. The expensive part is that Windows does not supply a public “paint this taskbar button for this process” interface. This implementation crosses from foreground HWNDs into private taskband objects and then into Explorer's XAML tree. Version-specific symbols, element recycling, UI-thread affinity, and safe DLL unload are real integration costs.

But not all of the present complexity is unavoidable. The current product also includes independent per-window preview ranking, several icon and preview styles, all four taskbar edges, icon scaling, virtual-desktop history, hosted-app identity, transient-focus policy, and restoration of other mods' visuals. The latest transform issues are a direct cost of size boost; much of the thumbnail machinery serves a separate feature from finding three app icons.

For the original use case, a deliberately smaller variant could keep exact identity matching, one owned icon marker, the three-app recency list, necessary desktop/running filtering, and robust teardown. Preview ranking and native-icon scaling could be omitted. That would remove meaningful state and restoration obligations. Merely disabling those settings in this existing file would not remove their maintenance burden.

I would not start that rewrite during this repair pass. First close the remaining correctness gaps and validate the core behavior. Then decide whether the extra features justify their ongoing cost. The useful dividing line is essential private-taskbar integration versus optional product scope—not whether the feature sounds simple in one sentence.

## Focused acceptance checks

1. Rank four apps; remove an unpinned top-three button without a final running-state observation. The fourth app fills the slot.
2. Keep the foreground fixed; close/hide a ranked background app. The remaining icons rerank without hover or another focus event, including when the rank count stays three.
3. Change another mod's transform while highlighting is active, force repaint, then clear. Restore the latest displaced value.
4. Change settings while a pending candidate is inside transient grace; return before expiry and confirm normally.
5. Retain the previous checks: idle through a decay tick, return to another desktop's history, A→B→A, independent preview deadlines, zero visual transitions, AUMID exclusions, and disable with a flyout open.

These are smaller remaining problems than the previous pass. The fixes should mostly repair invalidation and ownership at existing boundaries, not require another broad layer of fallback behavior.
