# Calculator trace: 2026-10-04 16:22

Evidence: `tests/uwspy/captures/focus-20261004-162259-923827`.
Diagnostic source: 0.10.14 on `codex/focus-activation-tracing`.
Times below are local UTC+02:00; observer JSON stores UTC.

Observer READY confirms foreground, keyboard-focus and shell-activation
channels registered. STOP status is 0; stderr is empty. Native probe coverage
initialization was not captured, but ExtendedUIClick, SwitchToItem and
HandleTaskActivated all appear in the trace. No HandleClick probe appears.

## First activation: foreground event missing, shell message present

Target: Calculator host `0x44128E`, PID 20120; content focus `0x1CE1B50`, PID 48852.

| Time | Evidence |
|---|---|
| 16:23:18.668 | Foreground WinEvent names Explorer flyout `0x511EB8` |
| 16:23:18.808–.811 | ExtendedUIClick → SwitchToItem → returns S_OK; native entry/exit snapshots still show Explorer foreground |
| 16:23:18.814 | Keyboard-focus callback names Explorer InputSite `0xF70612`, but its actual-state snapshot already shows Calculator foreground and content keyboard focus |
| 16:23:18.815 | **SHELL event 4 names Calculator `0x44128E`**, with that same frame actually foreground |
| 16:23:18.854 | Independent sample confirms Calculator foreground/content focus |
| 16:23:18.858 | HandleTaskActivated names Calculator; target matches actual foreground |
| 16:23:18.888 | Existing bounded recovery reconciles Calculator |

There is no foreground WinEvent for this Calculator transition in the
observer recording. The native click route prediction is confirmed in this
interaction: ExtendedUIClick and SwitchToItem ran, not our HandleClick entry.
The request returned before foreground completed, so return alone must not be
treated as completed activation. Cross-process timestamp/tick granularity is
not sufficient to infer sub-millisecond ordering.

## Second activation: normal foreground event present

At 16:23:36.093–.095, the same ExtendedUIClick/SwitchToItem route runs for the
same frame. This time the observer receives foreground WinEvent 3 at .095,
shell activation 4 at .112, and focus events for the frame and content. Native
HandleTaskActivated follows at .162. This gives a same-window comparison in
one recording, rather than a comparison between unrelated apps.

## Negative evidence: native activation is not sufficient by itself

At 16:23:07.114 and .862, HandleTaskActivated names Calculator targets
`0x4D2278` and `0x1E212C` while actual foreground remains ChatGPT `0x3414AC`.
Unconditionally promoting its task-item HWND would rank background launches.
Likewise, keyboard-focus callback HWND is not always the actual foreground
window: the first activation includes a delayed Explorer InputSite event while
Calculator is foreground. Always distinguish event target from sampled state.

## Implication and next comparison

The shell-activation notification is now a concrete event-driven candidate,
observed precisely when foreground WinEvent failed. It could let us feed the
actual foreground through normal focus handling without adding more private
Explorer hooks. This run does not establish general shell-message coverage.

Before choosing a production change, collect the planned Win32, hover-only,
right-click, close, keyboard invocation and Alt-Tab controls with this same
diagnostic build. Compare shell event target, actual foreground and existing
WinEvent delivery; examine duplicate notifications and null shell targets.
Both ordinary and rude-app shell activation codes must be considered (only
ordinary code 4 occurred in this recording).

No behavior changed after this result. The reason Windows omitted/did not
deliver the foreground event remains unknown. We have evidence of an alternate
signal, not a complete root-cause explanation or a validated replacement yet.
