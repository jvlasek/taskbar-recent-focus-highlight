# Shell activation replacement: Calculator, 2026-10-04 21:20

Evidence: `tests/uwspy/captures/focus-20261004-212038-881997`.
Experimental version: 0.10.15, no foreground recovery polling.
Times below are local UTC+02:00. Observer READY enabled all three channels;
STOP status was 0 and stderr was empty.

## The missing-event case reproduced and succeeded

Three first thumbnail activations in this recording have no corresponding
foreground WinEvent at activation, but both the independent observer and the
mod receive shell activation code 4 with target equal to actual foreground:

| HWND | Mod shell notification | Preview confirmation | Subsequent rank-1 glow log |
|---|---|---|---|
| `0xD02C2` | 21:20:48.876 | 21:20:48.877 | 21:20:58.316 |
| `0x68114C` | 21:20:59.934 | 21:20:59.936 | 21:21:00.542 |
| `0xA10AE0` | 21:21:09.952 | 21:21:09.953 | 21:21:10.739 |

Each has a preceding ExtendedUIClick/SwitchToItem trace. Later repeat
activations of the first two windows deliver ordinary foreground WinEvents
as well. This is evidence that the new input covers the observed gap, not
merely a run in which the original symptom failed to occur. The user also
reports that the visuals looked correct. Rank logs are the mod's own evidence,
not an independent pixel assertion.

## Background versus foreground

At startup, callbacks name `0xD02C2`, `0x68114C` and `0xA10AE0` while the
observer's actual foreground remains Terminal. They receive no preview
confirmation until their later activation. Other Calculator windows briefly
do become foreground during launch and receive preview confirmation then;
those are real observed foreground transitions, not background-only launches.
Null shell targets accompanying the Explorer flyout do not themselves stamp
window recency.

## Scope

This passes the core Calculator first-activation regression with polling
removed. Confirmation here is effectively immediate; this recording does not
validate a positive preview minimum or duplicate-event deadline timing.
It also does not establish disable/unload, desktop switching, deliberate
right-click/close controls, or full cross-build coverage. Native diagnostics
remain pending those checks. No source behavior changed for this analysis.

## Positive delay and right-click follow-up, 21:26

Evidence: `tests/uwspy/captures/focus-20261004-212625-787718`.
The short Calculator foreground episodes during launch at 21:26:29–30 produce
candidates but no Calculator preview confirmation. The positive delay filters
these episodes as intended.

At 21:26:54.515, Calculator `0x1BD0112` becomes a candidate; the shell duplicate
arrives at .527 and preview confirmation at 21:26:55.507, approximately one
second after the initial event. It does not restart a full second at the duplicate.

The second activation at 21:27:14.062 names `0x1340196`. Both the observer's
shell callback and its .072 sample show that frame actually foreground and
its CoreWindow `0x91622` holding keyboard focus. No foreground WinEvent for
this transition is recorded. The mod starts its candidate at .064 and confirms
at 21:27:15.055, approximately one second later. Brave becomes foreground at
21:27:17.875. The user reports a right-click produced a highlight; this second
interaction has ExtendedUIClick without SwitchToItem and is consistent with
that report, though the recorder does not log mouse buttons. The resulting
promotion has independent foreground/focus evidence, not just a click signal.

The user reports hover alone does not highlight; recorded flyout refreshes
retain existing recency ticks rather than adding further Calculator confirmations.
This run supports positive-delay handling and foreground validation for the
reported right-click interaction. It does not prove that every context-menu
interaction activates its owner, or complete unload/desktop-switch coverage.

## Win32, desktop and lifecycle follow-up

The diagnostic-build recordings (temporary traces still present) cover:

- `focus-20261004-213534-236251`: two VS Code previews confirm, followed by
  app confirmation at 21:36:25.273 after the candidate at 21:36:17.281.
- `focus-20261004-213808-319544`: desktop GUID changes from E9345F8A… to
  514F471C… at 21:39:04.748, with separate map/rank counts, then returns to
  E9345F8A… at 21:39:24.447. The user reports expected visuals.
- `focus-20261004-214031-286817`: three unloads complete, with two intervening
  successful shell registrations and resumed VS Code preview confirmations.

Final cleaned-build recording `focus-20261004-214505-364695` has no temporary
trace lines and its source line numbers match 0.11.0. The user reports keeping
highlighted flyouts open during delayed Windhawk exit and seeing highlights
removed correctly. Uninitialize starts at 21:45:55.458; the worker exits and
joins at .460; Uninitialize completes at .464 (about 6 ms). The independent
observer still sees Explorer PID 56152 and the same flyout HWND 0x50F56 at
21:46:01.196. STOP is successful; observer stderr is empty.

This supports a successful final-build flyout-open unload without an Explorer
restart. Windhawk exit unloads all enabled mods. The recording does not include
restarting Windhawk afterward. This is targeted manual coverage, not exhaustive
multi-monitor/theme/build coverage or proof of behavior under hung dispatchers.
