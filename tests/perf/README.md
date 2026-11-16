# Perf build (timing regression check)

Run before every release and compare with the previous release's numbers.

1. `python tests/perf/make_perf_build.py` writes
   `tmp/perf/taskbar-recent-focus-highlight.perf.wh.cpp`: a copy of the mod
   with timers around the entry points listed in `make_perf_build.py`
   (same mod id, version `…-perf`). Never commit or submit it.
2. Paste it into Windhawk's editor, compile, set the mod's logging to
   **Mod logs**, and record: `python tests/uwspy/record_windhawk.py`.
3. Use the taskbar normally for a while (switch apps, hover icons and
   previews, open/close windows, change virtual desktops). Then disable the
   mod once: that logs the since-load summary.
   For a release baseline use `--quiet` (writes `….perf-quiet.wh.cpp`):
   every other log line becomes a no-op, so the numbers exclude the mod's
   own logging. Compare quiet with quiet, normal with normal.
4. Keep the `PERF` lines (e.g. `findstr PERF windhawk.log`) with the release
   notes in `tests/perf/results/<version>.txt`.

Reading the lines:

- `PERF interval|since-load: span=… uiBusy=…ms (…%) workerBusy=…ms` — time
  spent in the outermost instrumented scope per thread kind. `uiBusy` is what
  Explorer's taskbar thread spends inside the mod; the worker never blocks the
  taskbar.
- `PERF … <Slot>: n total avg p50 p95 p99 max` — per entry point. Times are
  inclusive (a nested instrumented call is counted in its caller too).
  Hook slots (UpdateVisualStates, ButtonPointerPressed, ThumbOnApplyTemplate,
  ThumbPutSource, FlyoutTargetItemKey) start after Explorer's original
  function returns, so they are the mod's own cost only. Results taken
  before 2026-11-16 22:30 (0.11.30, first 0.11.31 runs) include Explorer's
  work in those slots and in uiBusy.
  Percentiles are upper bounds of power-of-two microsecond buckets.
- Logging itself is not timed except where an entry point logs inside its
  scope (most do, with Mod logs on), so compare runs with the same logging
  level.

If an anchor stops matching after a refactor, the script stops with an
error: update the anchor and keep the slot name, so numbers stay comparable.
`tests/run-perf-build-tests.py` checks the generator and the timing code.
