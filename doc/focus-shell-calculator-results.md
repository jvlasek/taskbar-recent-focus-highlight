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
