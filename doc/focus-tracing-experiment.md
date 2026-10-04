# Native activation tracing experiment (historical)

The experiment concluded with 0.11.0: shell notifications retained, polling
and temporary native probes removed. Instructions below describe the diagnostic
0.10.14/0.10.15 builds, not the current release. Live coverage is recorded in
[Calculator results](focus-shell-calculator-results.md).

Branch: `codex/focus-activation-tracing`. Experimental mod: **0.10.15**.
Baseline main: 0.10.13. Version 0.10.14 collected the initial native traces.
Version 0.10.15 adds shell activation notifications and removes the bounded
foreground polling recovery. Thresholds and identity rules remain unchanged;
duplicate notifications preserve the pending preview deadline.
Do not publish this version to the Windhawk catalog.

## Validate the event-driven replacement

Load 0.10.15 and use the same `python .\record_focus.py` recorder below.
A startup log must say `Shell activation notifications registered (no foreground polling)`.
Registration failure fails focus-worker startup; there is no silent polling fallback.

1. Launch Calculator in the background, including the keyboard/minimize case.
   Merely launching or flashing must not earn recency. Leave the flyout open
   for more than five seconds, then activate a previously unused window once.
   Leave it focused past the preview minimum; reopen and check its highlight.
   Repeat with several windows. This is the missing-event regression case.
2. Repeat with ordinary Win32 windows. Verify app and preview thresholds
   independently; closely spaced duplicate events must not extend the wait.
3. Hover only, right-click/dismiss, and close a disposable thumbnail; note the
   action order. No background window may gain recency merely from these actions.
   Close can legitimately activate another window: compare actual foreground.
4. Test Alt-Tab, keyboard thumbnail invocation and virtual desktop switching.
5. Disable/re-enable, then repeat activation. Test unload with the flyout open
   using the established delayed/manual procedure; do not disrupt unrelated work.

The temporary native probes remain only to correlate results. Shell registration
uses RegisterShellHookWindow, whose Microsoft documentation notes desktop scope
and a compatibility caveat; this is measured coverage, not a promise across builds:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registershellhookwindow


## What is recorded

Temporary `TEMP_FOCUS_TRACE` logs record paired entry/exit snapshots for:

- `HandleClick` — existing hook, excluding synthetic identity queries.
- `HandleExtendedUIClick` — candidate XAML thumbnail action route.
- `SwitchToItem` — narrower switch request.
- `_HandleTaskActivated` — common shell activation-notification candidate.

Each pair carries a sequence number, monotonic tick, current TID, target
HWND/PID, and actual foreground/active/focus windows. Capture target identity
only on entry while the task item is supplied by Explorer. Exit uses saved
value data, never dereferences that task item again. No extra process paths,
AUMIDs, window titles or XAML are queried for tracing. Hooks preserve the
original call and result. Getter exceptions do not prevent the original call.
Optional-symbol coverage is logged at initialization; a missing probe is not
evidence that its route never ran.

The standalone observer records foreground WinEvents, keyboard-focus
WinEvents and shell activation messages, plus its existing 50 ms actual-state
sampling. It fails startup if any required notification channel cannot be
registered. It does not inject code or change focus. Shell messages arrive on
a hidden, nonactivating top-level window, destroyed at shutdown.

Tracing adds work and may alter timing. A non-reproduction is not proof of a
fix. Close/context-menu/hover controls are essential: an event is not assumed
to indicate completed activation merely because its name contains “activate”.

## Run

1. Copy the branch's `.wh.cpp` into your local Windhawk editor and compile/load
   it. Verify **0.10.15**. Foreground recovery polling is removed;
   this experiment has no new settings.
2. Enable **Mod logs** and close other debug collectors. From `tests/uwspy`:

   ```powershell
   python .\record_focus.py
   ```

   The observer rebuilds automatically. After it reports ready, disable and
   re-enable the mod once with flyouts closed so the recording includes
   `TEMP_FOCUS_TRACE coverage extendedClick=1 switchToItem=1 taskActivated=1`.
   If any probe is zero, send the recording before interpreting absent events.
   This is a manual setup step; the recorder does not toggle the mod.

3. First recording: use the familiar Calculator keyboard launches to obtain
   previously untouched flashing windows. Open the flyout, wait at least five
   seconds, click one, type a distinctive digit, wait three seconds, then reopen
   the flyout. Observe before clicking again. If it fails, do a second activation
   of that same window for comparison. Stop with Ctrl+C and share the path.
4. Separate control recording: ordinary Win32 thumbnail activation, hover-only,
   thumbnail right-click, thumbnail close, keyboard thumbnail invocation and
   Alt-Tab. Describe the order when sharing it. Normal keyboard-focus events
   inside an app must not be treated as app switches.

Files remain `foreground.jsonl`, `windhawk.log` / `.jsonl` and observer stderr.
`EVENT` 3 = foreground; `EVENT` 32773 = keyboard focus; `SHELL` 4 or 32772 =
shell activation. `SAMPLE` reflects actual state. Use `tick_ms` versus native
`tick` to correlate on the same machine; text logs use local time, JSON UTC.
Nested calls have separate sequence numbers and a shared thread ID. The
recorder does not capture screenshots.

## Original questions for 0.10.14

- Does the failing thumbnail interaction reach ExtendedUIClick/SwitchToItem
  while bypassing HandleClick?
- Does `_HandleTaskActivated` identify the presented HWND, and when does it
  run relative to actual foreground and keyboard focus?
- Do keyboard-focus or shell messages arrive when the foreground event does
  not? Do controls produce misleading notifications without activation?
- Which signal covers both app and preview recency, including keyboard
  activation? Would it remove the need for bounded recovery or cover one route?

The Calculator and Win32 timelines supported the 0.10.15 shell experiment.
Do not promote windows merely because a native probe fired.
A later merge should remove temporary
probes or explicitly retain selected diagnostics, with fresh checks.

## Historical validation: 0.10.14

Full Windhawk 1.7.3 compile/link passed. The observer compiled with warnings as
errors. `tests/run-focus-trace-tests.py` exercised the actual probe wrappers:
original calls/arguments/results, no post-original item query, getter exception
and unload guards, and absence of recency/timer mutations. Recorder lifecycle
tests also passed. Live hook delivery in Explorer remains unverified until the
user-run recording. No experimental DLL was injected by the agent.

The read-only observer smoke test registered all three channels and stopped
cleanly on stdin EOF. Preview identity, asynchronous identity and bounded
foreground-recovery regression suites passed, as did all 24 highlight-harness
checks. No test input was sent to applications during these checks.

## Validation: 0.10.15

Full Windhawk 1.7.3 optimized x64 compile/link passed. Production-helper tests
pass for shell filtering, stale/background/null targets, normalization, unload
guard, duplicate preview deadlines and sibling transitions. Registration order
and cleanup paths have source assertions, not a simulated Windows lifecycle.
Existing native trace, preview identity and async identity suites pass.
The old recovery helper tests were replaced because that implementation is gone.

The mod logs `TEMP_FOCUS_TRACE shell-notification` for activation messages;
compare its target and foreground snapshot with the independent observer.
Live Explorer delivery, positive-threshold timing and disable/unload still need
the user-run checks above. No experimental DLL was injected by the agent.
