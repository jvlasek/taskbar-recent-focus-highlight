# Testing, logs and performance

Read before handing over a version, and when a behaviour needs a live check.
Automated checks do not cover XAML; record what was verified live and what
was not.

## Automated checks

`tests/run-*.py` cut production functions out of the mod source (`src/*.cpp`
joined, via `tests/mod_source.py`) by signature
and compile them with stubs, so a rename needs the test updated in the same
commit.

- `run-appid-identity-tests.py`: AppId identity, per-window derivation, exclude
  list, rank 4+ intensity.
- `run-preview-identity-tests.py`: card → window resolve.
- `run-shell-activation-tests.py`: focus notification path.
- `run-last-use-tests.py`: last use — `GetLastInputInfo` wrap / race, the stretch in front across transient windows, leave stamps, destroyed window in front, same-app re-confirm (0.12.2).
- `run-icon-order-tests.py`: glow host placement planner and geometry.
- `run-layout-footprint-tests.py`: overlay chrome has zero desired size (plates, side bar, preview overlays) and unchanged placement.
- `run-dispatcher-tests.py`: unload drain (Windows + g++ only).
- `run-perf-build-tests.py`, `run-review-helper-tests.py`.

`tests/uwspy/README.md` is the authoritative harness guide: external UWPSpy
client, native A/B/C/D apps, automatic pipe discovery, screenshots, optional
Windhawk logs, and same-button unload/reload phases. Do not restart Explorer,
reattach UWPSpy or close children between lifecycle phases. Do not automate
mod enable/disable or Explorer injection. Keep user captures intact.

- `tests/run-icon-order-tests.py` extracts the production host placement planner
  into C++ checks: 30,240 placements, native order preservation, idempotence,
  and missing anchors. It also rejects Append in host creation/positioning and
  native ZIndex writes. This is not a XAML integration test.
- `test_ipc.py`: IPC fixture, captured badge order, discovery and visibility.
- `test_windhawk_log.py`: collector lifecycle, labels and conflict reporting.
- `test_lifecycle.py`: phase order and preservation of original references.
- `tests/badge-static-analysis.md`: binary/source analysis and historical results.
- `tests/badge-final-validation.md`: current release-candidate live checklist.

## Performance

Windhawk users often have high-refresh screens: keep taskbar UI-thread work
well under a frame (4.2 ms at 240 Hz). `python tests/perf/make_perf_build.py
[--quiet]` writes `tmp/perf/*.perf[-quiet].wh.cpp` (never committed). Hook
slots are timed after Explorer's original call; `g_stateMutex` wait and hold
are timed per thread. Results per release go to `tests/perf/results/`. Run
before every release; details in `tests/perf/README.md`.

## Logs

All lines are plain `Wh_Log` (Windhawk Advanced tab: Mod logs / Detailed debug
logs). The format must be a string literal. Check the loaded version first
(`init 0.11.x`). `python tests/uwspy/record_windhawk.py` records a manual
session to `tests/uwspy/captures/manual-*/windhawk.log`; `record_focus.py`
adds an independent foreground observer.

| Substring | Meaning |
|-----------|---------|
| `Focus candidate:` / `Confirmed focus:` | App recency |
| `Preview focus confirmed:` | Window recency |
| `Current virtual desktop:` / `Desktop switch (<reason>):` | Per-desktop recency; reason = registry, `input desktop: <name>` (`Default` = back on the user desktop, `not accessible (lock / UAC)` = secure desktop; 0.12.1), foreground (watch fallback), decay tick |
| `Button identity:` | A button's Explorer AppId changed (first capture, reuse) |
| `Identity state:` / `Running state read: N running, M not running` | A known button's running state changed. Buttons read for the first time, or again after a desktop / lock switch reset them, are only counted in the `Running state read` summary of the next full pass (0.12.2; before, ~20 `Identity state` lines per lock or unlock) |
| `Window … belongs to APPID:…` | A window record joined (or changed) its app at a recompute |
| `Button windows:` / `Uncombined windows` | Never Combine detection |
| `ApplyAllHighlights:` / `rank list[` / `bind rank` | Full pass. Since 0.12.1 `rank list[` / `bind rank` (and `no ranks — cleared`) are printed only when the ladder or a binding differs from that UI thread's last pass; otherwise the summary ends `(ladder unchanged)`. A `Glow rank` line still follows its `bind rank` line |
| `UNMATCHED rank` | A ranked app bound to no shown button |
| `Preview resolve:` / `sibling[` / `unresolved:` | Per-card window (`how=viewmodel`) and why a card did not resolve. `sibling[` lines only when the set, a rank or a failure changed (else `(unchanged)`, 0.12.1) |
| `HWND recycled` | Preview map dropped a reused handle (PID mismatch) |
| `transient FG, waiting on` / `gave up waiting through transient … (after Ns)` | Min-focus grace: first line once per wait (no 200 ms repeats since 0.12.1), the second if it expired and the candidate was dropped |
| `Settings: dropped N records of excluded apps` / `Nothing to paint yet` | 0.12.2: exclusion sweep on the focus thread after a settings change; init with highlightCount 0 and previews off (the mod stays loaded). Since 0.12.2 `Identity state`, `Button identity`, `Button windows` and `Uncombined windows` are written after their lock, so they carry the deferred `(Function)` prefix |
| `Last use: hwnd=… left, used until Ns ago (app\|preview\|app+preview)` | The window that left the foreground was stamped; `preview` alone = a visit shorter than the app minimum focus (0.12.1) |
| `Decayed: <name> key=… (last use N.N min ago)` | App record dropped; the age should be ≥ the decay time (+ up to one 30 s tick) |
| `Hidden (no running button):` / `Shown again (running button):` | App keeps its history but has no slot, or got it back. A burst right after `Desktop switch` is the cleared running grace, until the next full pass |
| `Decay tick late by N min` | Sleep (GetTickCount64 counts it) or a stalled worker; idle records decay at once |
| `Focus tracking waits for the taskbar` / `Focus tracking started` | Process without / with Taskbar.View hooks; a waiting process logs nothing else (0.12.1) |
| `Decay timer armed` / `Decay timer stopped` | 30 s decay tick started or idled |
| `Hooked Taskbar.View.dll` / `Preview resolve: view model on` | Symbols found |
| `no dispatcher anchor` | Before the first button is seen (logged once) |
| `IconPanel relayout:` | Button size or taskbar edge changed |
| `ERROR: UI cleanup did not finish` / `watches not revoked` / `focus thread wait failed` | Unload handshake failed; Explorer may crash |

## Live checklist

Identity and ranks
- App min-focus 0–1 s and default; re-focus A after B (default promote): A
  returns to rank 1.
- Rapid Alt+Tab between two unranked apps, then rest on one: it still
  confirms after min-focus. Click File Explorer or sit on the desktop during
  another app's min-focus: no endless 200 ms polling.
- Total Commander + Lister: focusing Lister highlights Lister, not Commander.
- UWP: Calculator and Settings get separate ranks; Windows Security does not
  copy Settings. File Explorer folder windows are ranked; desktop, taskbar
  and Alt+Tab are not.
- Launch a pinned app (one click): its icon is highlighted after min-focus.
  A button that appears while ranks exist picks up its rank without waiting
  for the next Alt+Tab.
- Close a ranked app, pinned and unpinned: its slot frees for the next
  running app, also when the visible count stays at `highlightCount`.
- Never Combine on, off and "combine when full": one button per window ranks
  per window; switching back merges.
- Two ranked apps idle through a 30 s decay tick: highlights stay.
- Two virtual desktops: a highlight on D1 does not remain on a
  pinned-not-running icon on D2; D1 ranks return after switching back.
- Exclude an app (name, path, AUMID, AppId) while pending and after confirm:
  icon and preview recency both drop.

Previews
- Three windows of one app: ranks 1 > 2 > 3 in that flyout only; two windows
  with the same title; a new window needs its own preview min-focus.
- Hover two multi-window apps in sequence: recycled cards re-rank, one Low
  pass per flyout, not per card. A flyout with a snap-group card: no shifted
  marks. Close a ranked window from the flyout: remaining ranks repaint at
  once.
- Explorer start with preview min-focus 0: the worker does not spin.

Visuals
- Taskbar on each edge: the side bar does not cover the native running pill;
  unranked icons keep their dots after a relayout.
- Hover storm on ranked icons: highlights stay, no full-pass log spam.
- Taskbar Styler: hover a ranked icon, the side bar stays; preview plate and
  outline follow the theme's card shape.
- Change colour or intensity while ranked: highlights stay; 100/80/60 reads
  as linear. Accent change without a settings save updates the colour.
- Change a setting during a pending min-focus: the app still confirms.

Unload
- Disable while a flyout is open and while hovering: all chrome gone, no
  Explorer crash, repeated disable/enable in one Explorer session.

## Real-input harness (`tests/uwspy/`)

`tests/uwspy/highlight_harness.py` owns the scenario; UWPSpy remains an inspection
dependency. `highlight_uia.ps1` is a read-only UIA geometry/hit-test helper.
Use exact AppId for discovery, physical UIA bounds for taskbar buttons and
UWPSpy IPC `screen_rect` (the screenshot helper) for thumbnail input, and independently
observed foreground HWND/PID for card association. Do not infer HWND identity
from the mod's own recency logs. Ambiguous geometry, changed targets, unexpected
foreground and recycled handles must stop/classify the run, not be guessed away.
Never interpret root-relative XAML coordinates as desktop coordinates.

Background/minimized/attention fixtures are controlled Win32 cases. Real
Calculator launches can activate at startup, so calibration is explicit and
must not be described as never-focused launch coverage. Digits are sent only
after verifying an owned foreground window. The tests leave their windows open.
Separated-button and guided desktop rounds have narrower assertions documented
in tests/uwspy/README.md; they do not prove snap-group handling or desktop-history
restoration. Tests: `python -m unittest discover -s tests/uwspy -p
test_highlight_harness.py -v`. No mod version increase is needed for harness-only
changes. Live input runs require user preparation; do not disrupt their desktop
merely to turn an unverified scenario into a claimed pass.

Calculator combined-mode harness checks are retrospective: capture before
click, identify the target from the actual foreground HWND/PID, check saved
membership and rank-1 plate against pre-click history, then update history.
Never carry Calculator XAML card identities across flyout reopenings or use
the mod's mappings as the test oracle. Calibration and each cycle must cover
every owned HWND. Hover checks require stable cards within that opening.
These are per-click checks, not simultaneous sibling identity validation.
`--calculator-startup-seconds` defaults to 2; use 0 for early-minimize/blank
preview stress. The delay is not a guarantee of content readiness.
