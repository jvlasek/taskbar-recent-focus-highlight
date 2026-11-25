# Fourth review: local checkout unchanged since review 3

**Assessment: the four P2 findings from `gpt-review3.md` remain open. No new implementation was available to assess.**

Reviewed on 2026-09-21. Local HEAD is `f518798f78eb3e62c5cb9730e99bd9c7d757317b`; the working tree was clean before this report. The C++ file's Git blob is `f6166686a39a5b09642ca2530196d435c3e45ad0`, exactly the blob recorded in review 3 (v0.9.29).

The commit since the previous review adds `gpt-review3.md` and commits the `FrameworkElement host{nullptr}` initialization correction. That correction was already present as an uncommitted change when review 3 was performed. It is therefore not a new fix relative to the reviewed source. README and AGENTS have no committed changes in this interval.

## Rechecked findings

1. **[P2] Dead buttons can retain taskbar eligibility.** `PathAppearsOnTaskbar` still accepts cached `observedRunning` at lines 1600–1607; the full-bind snapshot at 4357–4387 only updates live buttons. The dead-entry invalidation gap reported previously is unchanged. A removed app can continue occupying a top-N slot.
2. **[P2] Closing a highlighted button can suppress reranking.** At lines 6053–6060, `ClearButtonHighlight` still runs before reading the cached rank, so its rank-zero write suppresses the positive-rank refresh condition. The fallback at 7288 still compares rank counts rather than ordered identities, missing a three-to-three replacement.
3. **[P2] Repeated transform takeover can restore an obsolete value.** At lines 3510–3545, takeover still only saves the current transform when the host Tag is unset, while updating the origin separately. A second external transform can be replaced and later restored to the first snapshot.
4. **[P2] Rearming an elapsed deadline can bypass transient grace.** At lines 7031–7035 and 7070–7074, elapsed positive-duration deadlines still use `Immediate`. The previously described settings-change path can therefore bypass the normal timer-mode transient handling.

The detailed trigger sequences, recommended corrections, and acceptance checks in [review 3](gpt-review3.md) still apply. These are existing findings, not four newly discovered defects. No additional P1 is claimed by this focused recheck.

## Scope and quality

Taskbar app highlights and per-window preview-flyout highlights are both essential product features, as clarified by the user. The earlier suggestion to omit preview ranking is withdrawn. Any future simplification should preserve both and target duplicated state, invalidation rules, ownership, and testability in their implementation.

The previous documentation observations also remain unchanged, including the AGENTS claim that the completion handler is detached and the overly broad minimum-focus wording. These do not constitute new regressions.

## Verification limits

This was a local source recheck: Git history/diff, exact source-blob comparison, and rereading the flagged branches. It was not a fresh exhaustive audit of identical source. No build or live Explorer test was run, and no remote branch was fetched or compared. Fixes made in another checkout, an unsaved editor buffer, or a remote branch are outside this snapshot.

Only this report was created; no implementation or project documentation was changed.
