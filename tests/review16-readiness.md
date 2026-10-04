# 0.11.0 review preparation — 2026-10-04

Shell activation replaces the 0.10.13 bounded foreground polling recovery.
The 0.10.14/0.10.15 native probes and three optional private hooks are removed.
The independent observer is a separate test tool and remains available.

Final checks:

- Optimized x64 Windhawk 1.7.3 compile/link, actual API (no WH_EDITING stubs).
  Output `tmp/review-0.11.0.dll`; not injected.
- `python tests/run-shell-activation-tests.py`
- `python tests/run-preview-identity-tests.py`
- `python tests/run-async-identity-tests.py`
- `python tests/run-dispatcher-tests.py`
- `python tests/run-icon-order-tests.py` (30,240 placements)
- `python tests/run-review-helper-tests.py`
- `python -m unittest discover -s tests/uwspy -p test_highlight_harness.py -q`
  (24 checks)
- `git diff --check`

All pass. The runtime SVG was regenerated for the release.

Live 0.10.15 evidence: `doc/focus-shell-calculator-results.md`. The release
removes diagnostics without changing that event/recency path. Additional diagnostic-build Win32, desktop-switch and disable/re-enable
recordings passed; the final 0.11.0 flyout-open delayed-exit recording also
passed. See the results document for paths and precise build distinctions.
Final-build restart after that exit is not captured. Controlled tests are not proof of live Explorer dispatcher behavior.
Review reply: `reviews/review-response16.md`; does not claim those missing runs.
