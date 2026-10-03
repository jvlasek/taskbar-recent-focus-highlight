# Taskbar badge/recency test harness

## Highlight and thumbnail activation harness

`highlight_harness.py` discovers the test group's taskbar button by exact AppId
through UWPSpy and Windows UI Automation, then uses real mouse hover/click input.
Other desktop apps can remain open. Coordinates come from live UIA physical
screen bounds; XAML root-relative bounds are never used directly as mouse
coordinates. Ambiguous roots, mismatched geometry, occlusion and changed hit
targets stop the run. No fixed taskbar slot or English Calculator title is used.

Attach UWPSpy to Explorer yourself and leave its inspectors open. Use combined
taskbar buttons for the flyout tests. Enable this mod, set preview count to 3
(or pass the actual value with `--top`, less than `--count`), and use nonzero
intensity/fill. Default assertions expect Plate + title background (`plateTitle`);
use `--preview-style other` for titleBar/titleBg/plate membership-only checks.
`--focus-seconds` must exceed BOTH configured focus thresholds; keep decay long
enough for the run. Settings are never changed by the harness.

From this directory, after building the updated fixture with `build.ps1`:

```powershell
python .\highlight_harness.py --app win32 --launch attention --windhawk-log
python .\highlight_harness.py --app calculator --windhawk-log
```

Run these separately. Close the previous run's A test windows or Calculators
yourself first; the harness leaves its windows open for inspection and never
terminates existing applications. Calculator mode requires no pre-existing
Calculator windows, because otherwise the group contains unrelated cards.
Four windows are created by default. Press Enter at the preparation prompt,
then leave mouse/keyboard alone. Hold Escape to abort; Ctrl+C also works when
the console has focus. Input is never sent to a Calculator until its foreground
HWND belongs to the discovered test set and its PID still matches.

Previous A fixture windows are rejected before launching a new Win32 run.
Pointer movement uses `SendInput` with absolute virtual-desktop coordinates,
including a short movement inside the button, and verifies the resulting cursor
position. `SetCursorPos` alone did not reliably trigger Explorer's hover preview
in live testing. Rejected movement or a displaced cursor interrupts the run.
Flyout opening moves the pointer into the monitor work area, then rediscovers
and re-enters the button. It waits for matching thumbnails for up to five
seconds after the initial hover delay (individual inspection calls also take
time). Post-hover snapshots do not reset the pointer. `thumbnail_discovery`
records unfiltered XAML names, matches and UIA counts to distinguish a missing
flyout from a recognition mismatch.

The Win32 variants `--launch background`, `--launch minimized`, and
`--launch attention` distinguish background display, minimized display and
minimized display requesting taskbar attention. All share one AppId to form a
multi-window group. The first flyout must have no highlights before activation.
Calculator uses real packaged-app launches, then minimizes the discovered
windows. Windows may have activated them at launch: this mode does NOT claim
to test a guaranteed never-focused launch or force Calculator attention.

Calibration clicks each card and associates it with the actual foreground HWND,
independently of the mod's identity mapping. Calculator receives 111/222/333/444
only after activation, so screenshots can corroborate card identity. Then each
cycle hovers a card for five seconds, verifies no recency promotion, activates
it, and checks the resulting membership. The pointer stays on the card while
the post-hover inspection runs; returning to the taskbar would change the
interaction being tested. This deliberately exercises the delayed-click gap in
0.10.13 and may fail there. Warm-up also checks the first activation of initially
untracked Win32 windows. Later Calculator cycles exercise already tracked ones.

Each activated app must have icon glow after the configured hold. Combined
flyouts assert top-N window membership; `plateTitle` also asserts which window
gets rank 1's plate. Exact rank-2/rank-3 colour intensity and pixel aesthetics
are not judged. Screenshots/XAML and labelled steps go to `run.jsonl`; the
independent foreground observer always writes `foreground.jsonl`. Optional
Windhawk logs use the existing collector. Classification follows the badge
harness: pass, fail, disrupted input/focus, or inconclusive discovery/evidence.
The UIA helper has bounded process timeouts. Hold intervals check both actual
foreground and delivered events; the standalone observer retains additional
samples for diagnosis. This cannot guarantee detection of every sub-sample
external interference.

Additional modes:

```powershell
# Set Windows taskbar combining to Never manually first.
python .\highlight_harness.py --app win32 --grouping separated
# Guided move of all test windows to a different virtual desktop between rounds.
python .\highlight_harness.py --app win32 --desktop-rounds 2
```

Separated mode checks actual activation and that all same-app buttons get icon
glow; there is no multi-window flyout to assert. Desktop rounds record and
require distinct desktop GUIDs, rediscover the buttons, and establish fresh
history. They test operation on another desktop, not yet return-to-desktop
history restoration or windows split across desktops. Snap-group preview cards
are not supported by this scenario; unexpected extra cards are inconclusive.
For secondary taskbar duplicates, provide `--tree` (UWPSpy tree) and
`--taskbar-hwnd` (native taskbar HWND, decimal or 0x-prefixed).

Validation: fixture built with VS 2022; seven offline assertion/geometry/input
guard tests passed; read-only taskbar discovery was checked on the real desktop.
Full input-driven runs still require live validation. No Explorer restart,
injection, mod toggle, taskbar setting change or desktop creation is automated.

```powershell
python -m unittest discover -s . -p test_highlight_harness.py -v
```

This suite belongs to **whawk-lru**. UWPSpy provides inspection/IPC and its
reusable Python client; the native test apps, scenario controller, regression
fixtures, and transport tests live here.

Run commands below from `whawk-lru/tests/uwspy`.
Set `UWPSPY_ROOT` to the UWPSpy checkout/worktree containing `tools/uwspy_cli.py`:

```powershell
$env:UWPSPY_ROOT = 'C:\Users\jvlas\source\repos\UWPSpy\watcher'
.\build.ps1
```

If the variable is unset, scripts default to the sibling `UWPSpy/watcher`
worktree next to `whawk-lru`. Native build projects accept `/p:UwpSpyRoot=...`.
The client is loaded from that checkout; there is no duplicate client copy here.

`build.ps1` builds A.exe through D.exe and the IPC transport fixture. Build the
UWPSpy solution as Release/x64 first; the fixture uses its generated WinRT
headers and compiles the actual `UWPSpy/ipc.cpp` from the selected checkout.
The four child executables share source but have distinct paths and
AppUserModelIDs, so both the mod and Windows treat them as separate apps.

Manually run UWPSpy's new launcher, attach to Explorer, and leave the inspectors
open. If an older DLL remains loaded, save recordings and restart Explorer
before attaching the new build. No build/test script restarts Explorer, injects
UWPSpy, or changes mod settings.

The harness discovers the pipe automatically on every run. With multiple
sessions it offers a numbered choice; select the Explorer session.
You can still supply `--endpoint` explicitly.

For other CLI commands, PowerShell can capture the output directly (no `$()`
required). This example requires exactly one session:

```powershell
$pipes = @(python "$env:UWPSPY_ROOT\tools\uwspy_cli.py" endpoints)
if ($LASTEXITCODE -ne 0 -or $pipes.Count -ne 1) { throw 'Expected one UWPSpy session' }
$endpoint = $pipes[0]
```

See `tools/README.md` in UWPSpy for generic CLI commands and protocol details.

## Badge recreation scenario

Use a quiet desktop. Set the mod's highlight count to 1, 2, or 3 and pass the
matching `--top`; set `--focus-seconds` longer than its minimum-focus setting.
Ensure the child apps are not excluded and that decay will not expire ranks
during this short scenario. The harness does not read or alter mod settings.
Keep taskbar buttons visible, avoid auto-hide, and keep inspector windows off
the screenshot area. Record other relevant mods/settings in your run notes.

```powershell
python .\harness.py --mode enabled --top 3 --focus-seconds 10 --cycles 3 --screenshots --output C:\captures\runs
```

The harness launches A/B/C/D and asks for Enter after preparation. It then finds
the buttons by exact AppUserModelID (not window title) and starts a watch on A. Click A when prompted. All later focus
changes are requested through the currently foreground test child, and actual
foreground HWNDs/events are checked. Leave input alone until the result prints.
If multiple taskbars yield multiple matches, `--tree` may disambiguate them;
if ambiguity remains within a tree the run is inconclusive rather than picking
an arbitrary monitor. The four children close after the run; inspectors remain.

Each cycle focuses A, shows and clears its badge, focuses B/C/D long enough to
promote them, verifies A left the top N, then recreates A's badge. It checks
highlight membership (after enough known focus history), badge presence and
ordering, and foreground interference. All four buttons are captured before
badge recreation. Exact rank opacity/intensity and every possible rendering
fault are NOT asserted. A passing list-order check cannot prove the screenshot
looks correct; review the PNG when investigating composition issues.

For the clean comparison, disable the mod and restart Explorer yourself,
reattach UWPSpy and run with `--mode disabled`; the new endpoint is discovered automatically.
That mode asserts the test buttons have no mod glow hosts.

`run.jsonl` contains labels, command acknowledgements, actual foreground events,
observations, capture filenames, and watch notifications. `result.json` reports:

- `pass`: this scenario's assertions held;
- `fail`: a badge/highlight assertion failed while monitored preconditions held;
- `disrupted`: unexpected foreground, rejected focus, child exit, or interruption;
- `inconclusive`: inspection errors, ambiguity, watcher limits/errors, or missing evidence.

Configuration mismatches can cause apparent failures; retain the recorded
configuration. Polling changes are evidence, not an expected exact count of
internal XAML mutations. Watch polling remains two seconds and may miss short
transitions. Explicit `get` calls are used for state checks; the foreground
hook continuously records actual foreground transitions. A focus hold is
measured from verified focus, with a configurable duration to satisfy the mod's
minimum. This is not automatic discovery of the mod's focus timing.

## Development checks

After `build.ps1`, run:

```powershell
python -m unittest discover -s . -p test_ipc.py -v
```

The fixture creates two hidden message windows on separate threads in its own
process. Tests cover UI routing, Unicode, fragmented/malformed requests,
abandoned requests, bounded event gaps, timeouts, and shutdown with an idle
client. They do not attach to Explorer or change focus. The small text fixtures
are reduced ordering/property excerpts from recordings 1, 8, and 9 of the
2026-09-26 badge reproduction, with unrelated properties removed.

`bin/`, native intermediate directories, Python caches, and `test-runs/` are
ignored. Run outputs can also be placed outside this repository with `--output`.

Discovery failures include a `button_discovery` entry in `run.jsonl` with the
observed names and IDs. Close older A/B/C/D test apps before starting a new run.

Button discovery excludes collapsed/empty elements and rectangles wholly above
or left of the XAML root (including parked repeater elements retaining old IDs).
It retries missing buttons for five seconds. Exported coordinates here are
root-relative, so this does not exclude monitors with negative desktop origins.
`button_discovery` records rectangles and `layout_eligible` for every candidate.

## Optional Windhawk debug log

Add `--windhawk-log` to the scenario command. Before running, set this mod's
Advanced / Debug logging to **Mod logs** (or Detailed debug logs for engine
troubleshooting), and close Windhawk's **Show log output**, DebugView, or other
collectors. The harness does not change Windhawk settings.

```powershell
python .\harness.py --mode enabled --top 3 --focus-seconds 10 --cycles 10 --screenshots --windhawk-log --output captures
```

The bundled DbgViewMini is located automatically under Program Files/Windhawk;
use `--dbgview 'X:\portable\...\DbgViewMini.exe'` for another installation.
It runs without a console window, captures local-session messages matching
`*[WH]*`, and stops when the run finishes. This includes other Windhawk mods or
processes emitting that prefix, not exclusively this mod or Explorer.
A unique probe confirms collection before test apps start. Collector conflicts,
startup failure, or loss of capture make an otherwise passing run inconclusive.
Existing viewers are never terminated by the harness.

`windhawk.log` preserves the collector's UTF-8 output (including its timestamps,
PIDs and process names). `windhawk.jsonl` adds UTC and monotonic receipt times,
the current scenario label and a probe flag. Labels describe receipt time, not
exact execution order; buffering/scheduling can cross a step boundary. The
probe proves the collector works, not that mod logging is enabled. No history
from before collection is available. Logging may affect timing; compare runs
without it when investigating timing-sensitive failures.

Collector lifecycle/label/error tests (no Explorer attachment needed):

```powershell
python -m unittest discover -s . -p test_windhawk_log.py -v
```


The visibility assertion recognizes collapsed hosts from historical diagnostic
captures. Current .42 removes unranked hosts; disabled controls reject any
remaining host. For current release checks see [final validation](../badge-final-validation.md).

### Same-button unload regression

The harness option `--test-unload` (requires `--mode enabled`) runs the enabled
cycles, then pauses for manual mod disable while retaining A/B/C/D, the original
UWPSpy element references, and the Explorer session. Do not restart Explorer,
reattach UWPSpy, or close the apps during this pause. Press Enter after disabling;
the harness verifies all glow hosts are gone, then asks you to click A again and
repeats the cycles with disabled expectations. Evidence labels use
`post-unload-`. Foreground monitoring allows human input only at the phase
boundary and resumes after A is activated. PASS requires both phases to pass.
A separate disabled run creates fresh buttons and does not test delayed damage
to the previously highlighted buttons. `--cycles` applies to each phase.

From `tests/uwspy`:

```powershell
python .\harness.py --mode enabled --test-unload --top 3 --focus-seconds 10 --cycles 10 --screenshots --windhawk-log --output captures
```


Badge-absent unload variant: add `--unload-badge absent --test-unload`.
The harness clears and verifies A's badge before the manual disable pause,
verifies absence again after disable, then checks the first recreation before
running the disabled cycles. Default unload behavior keeps the badge present.
`--cycles 5` runs five cycles in each phase; keep the same Explorer and apps
across the pause as above. No mod rebuild is required for this harness option.


Repeated lifecycle test: `--test-unload --unload-rounds 3` runs three
(enabled cycles -> manual disable -> disabled cycles) pairs. Before pairs 2/3,
the harness pauses for manual re-enable, then asks for A activation again.
All phases retain the original windows and element references. `--cycles 5`
means five cycles per phase (30 total with three pairs). Use
`--unload-badge absent` for each badge-absent unload boundary. The final state
is disabled. Labels include the round number; no settings are changed by code.
`test_lifecycle.py` checks phase ordering and reference preservation without
interacting with Explorer.

## Manual log recording

For Settings/Calculator close/reopen testing, no harness or UWPSpy is needed.
Enable the mod's **Mod logs**, and close Windhawk's log viewer and DebugView.
From the repository root:

```powershell
python .\tests\uwspy\record_windhawk.py
```

Wait for **Recording ready**, reproduce both a successful and failed reopen,
then return to the terminal and press **Ctrl+C**. The printed evidence folder
under `tests/uwspy/captures/manual-...` contains `windhawk.log` and
`windhawk.jsonl`. Record which app failed and the approximate time. Collection
starts now; it cannot recover earlier messages. `--output` sets the parent
folder, and `--dbgview` selects a portable DbgViewMini installation.

Version 0.10.6 `Identity` messages show running transitions and each request's
queue, worker, result, delivery, and acceptance. `sameTarget=0`, `live=0`, or
`timely=0` explain rejection; `published=0` can mean supersession or unload.
A successful probe verifies the collector, not that mod logging is enabled.

### Combined foreground + Windhawk recording

From the repository root, run:

```powershell
python .\tests\uwspy\record_focus.py
```

This automatically builds `foreground_observer.c` using Windhawk's bundled
compiler when needed, then starts the standalone observer and the same Windhawk
collector used above. Wait for **Both recordings ready**. Enable Mod logs and
close other debug viewers/recorders first. Use existing Calculator windows to
avoid the remapped launch key's occasional browser activation.

One **Ctrl+C** in this terminal stops both children. The observer stops cleanly
when the launcher closes its input pipe; it needs no second console. The existing
Windhawk collector is stopped and its reader drained as in `record_windhawk.py`.
Startup/runtime failures also clean up both. `--seconds 10` provides an automatic
stop; `--compiler`, `--dbgview`, and `--output` override the defaults.

The printed `captures/focus-...` evidence directory contains `windhawk.log`,
`windhawk.jsonl`, `foreground.jsonl`, and `foreground-stderr.log`. Foreground
records carry UTC time and system uptime milliseconds. `EVENT` includes the
notified HWND and original 32-bit WinEvent timestamp; `SAMPLE` observes foreground
and keyboard focus every 50 ms but writes only changes (plus the initial state).
Both include current foreground, PID/TID, class, root, and GUI-thread focus data.
`READY`/`STOP` mark the observer lifetime. No titles, key contents, injection,
activation, or application modifications are involved. Queries are sequential
snapshots, not atomic; sampling can miss transitions shorter than 50 ms.

Compare event arrival with sampled state and the mod's temporary activation logs.
A missing event alone does not establish whether Windows omitted it or a listener
missed delivery. A successful recording is not a test of the mod's correctness.
