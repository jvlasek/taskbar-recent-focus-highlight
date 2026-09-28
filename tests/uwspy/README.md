# Taskbar badge/recency test harness

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

Discover the endpoint:

```powershell
python "$env:UWPSPY_ROOT\tools\uwspy_cli.py" endpoints
$endpoint = '\\.\pipe\UWPSpy-1234-{paste-the-actual-session-guid}'
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
python .\harness.py --endpoint $endpoint --mode enabled --top 3 --focus-seconds 10 --cycles 3 --screenshots --output C:\captures\runs
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
reattach UWPSpy, select the new endpoint, and run with `--mode disabled`.
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
