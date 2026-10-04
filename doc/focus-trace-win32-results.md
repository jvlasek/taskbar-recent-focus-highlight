# VS Code trace: 2026-10-04 20:52

Evidence: `tests/uwspy/captures/focus-20261004-205213-653287`.
Diagnostic source: 0.10.14 on `codex/focus-activation-tracing`.
Times below are local UTC+02:00; observer JSON stores UTC.

Observer READY confirms all three notification channels and 50 ms sampling.
STOP status is 0; stderr is empty. The recording includes 21 foreground
WinEvents, 36 keyboard-focus WinEvents and 15 ordinary shell activation
messages (code 4). No rude-app activation message occurs.

## Ordinary activation comparison

Four observed Code activation episodes have both a foreground WinEvent and a
shell activation message naming the actual foreground window:

| Time | HWND / PID | Foreground event | Shell message |
|---|---|---|---|
| 20:52:17 | `0x7196E` / 45252 | .149 | .161 |
| 20:52:34 | `0x14C167E` / 79972 | .004 | .056 |
| 20:52:39 | `0x10E12A4` / 79972 | .658 | .671 |
| 20:52:51 | `0x10E12A4` / 79972 | .644 | .656 |

The PID change is consistent with the reported update/relaunch. Native
HandleTaskActivated follows each episode with target matching foreground.
ExtendedUIClick and SwitchToItem appear together for the first, third and
fourth episodes. The second has ExtendedUIClick entries without a recorded
SwitchToItem; the trace alone does not identify that branch's purpose.
Preview confirmation occurs in all four episodes. No `Foreground reconciled`
message appears in this recording.

Unlike the first Calculator activation in
[the previous recording](focus-trace-calculator-results.md), these Code
activations did not need the alternate shell signal to fill a missing
foreground event. Shell notification nevertheless agrees with foreground
state in both kinds of observed activation.

## Startup event targets are not current-state guarantees

Between 20:52:20.240 and 20:52:21.999, foreground-event callbacks name a dialog
and several new Code windows, while the actual foreground snapshot remains
Terminal `0x105A0`. No matching shell activation of those windows appears in
that interval. Event-to-callback tick differences are 0 ms for five of these
six events and 16 ms for the other, at the recorded clock's granularity.

This is not evidence of a long callback backlog, but it does not distinguish
very brief transitions from other event-generation behavior. Do not call the
events false or claim a Windows root cause from these snapshots. It does
reinforce the need to validate current foreground instead of unconditionally
promoting a notification's target HWND.

## Conclusion and remaining controls

Shell activation remains a promising additional event-driven input, with
evidence from the missing-event Calculator case and this Win32 comparison.
Neither recording establishes complete coverage or explains why the first
Calculator foreground WinEvent was absent.

Before changing production behavior, record deliberate hover-only,
thumbnail right-click, thumbnail close, keyboard invocation and Alt-Tab
controls, with their order noted. These check that merely displaying or
interacting with a preview does not become a false recency promotion, and
that duplicate signals do not reset focus deadlines. Null shell targets and
background native activation notifications must not be treated as an app
activation on their own.

No mod behavior changed for this analysis; the experiment remains separate
from main. Validation was inspection of the observer/native trace and
`git diff --check`; no binary rebuild is needed for this documentation change.
