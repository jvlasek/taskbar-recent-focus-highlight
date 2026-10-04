# Native activation tracing experiment

Branch: `codex/focus-activation-tracing`. Diagnostic mod: **0.10.14**.
Baseline main: 0.10.13. No intended changes to focus thresholds, click
confirmation, identity matching, or the existing bounded recovery behavior.
Do not publish this version to the Windhawk catalog.

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
   it. Verify **0.10.14**. The current recovery remains enabled as the control;
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

## Questions before changing behavior

- Does the failing thumbnail interaction reach ExtendedUIClick/SwitchToItem
  while bypassing HandleClick?
- Does `_HandleTaskActivated` identify the presented HWND, and when does it
  run relative to actual foreground and keyboard focus?
- Do keyboard-focus or shell messages arrive when the foreground event does
  not? Do controls produce misleading notifications without activation?
- Which signal covers both app and preview recency, including keyboard
  activation? Would it remove the need for bounded recovery or cover one route?

Do not remove recovery or promote windows merely because a probe fired.
Choose a fix after comparing timelines. A later merge should remove temporary
probes or explicitly retain selected diagnostics, with fresh checks.

## Validation

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
