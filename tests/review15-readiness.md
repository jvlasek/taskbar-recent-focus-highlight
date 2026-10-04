# Review 15 preparation — 2026-10-04

Local source: 0.10.13. This housekeeping pass changed one lifetime comment,
not executable behavior or settings. No version increase was needed.

## Fresh checks

All passed:

- Full optimized x64 DLL compile/link using the installed Windhawk compiler,
  actual `windhawk_api.h` (without `WH_EDITING` stubs), and Engine 1.7.3.
  Output: ignored `tmp/review-0.10.13.dll`; not injected.
- `python tests/run-preview-identity-tests.py`
- `python tests/run-async-identity-tests.py`
- `python tests/run-foreground-recheck-tests.py`
- `python tests/run-dispatcher-tests.py`
- `python tests/run-icon-order-tests.py` — 30,240 placements and geometry checks.
- `python tests/run-review-helper-tests.py`
- `python -m unittest discover -s tests/uwspy -p test_highlight_harness.py -q`
  — 24 checks.
- `git diff --check`.

Live evidence and limits are recorded in
[`doc/investigation-notes.md`](../doc/investigation-notes.md). The recorded live
runs precede this comment-only housekeeping pass; they are not fresh runs of
the newly linked check DLL.

## Housekeeping

- README pending-validation text replaced with the actual scoped results.
- AGENTS version, asynchronous resolver descriptions, removed Edge reference
  and older follow-up wording corrected.
- Thumbnail getter comment now covers both constructor and native click use.
- Review response updated with tests and the bounded-recovery limitation.
- Temporary hosted-window/activation probes absent; intentional identity,
  preview and recovery logs retained. The standalone observer/harness is not
  part of the submitted mod.

## Submission handoff

The local files are prepared for review. This pass does not copy into the
separate windhawk-mods checkout, push either repository, or post a comment.
Publish the development documentation if referencing it, copy this exact
`.wh.cpp` into the submission checkout, then post
[`reviews/review-response15.md`](../reviews/review-response15.md).

The unresolved cause of the missing foreground event is a separate
investigation; this pass does not claim it has been identified or universally
worked around.
