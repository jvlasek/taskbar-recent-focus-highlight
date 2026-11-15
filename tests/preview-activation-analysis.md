# First Calculator activation: independent observation

Evidence: `tests/uwspy/captures/focus-20261002-160114-719232`.
Mod diagnostic version: 0.10.12. Observer: `foreground_observer.c`.
User reproduced after repeated keyboard launches, with some windows flashing.
"Keyboard launch" means the dedicated **Launch Calculator** key of a Microsoft
Natural keyboard (remapped from a WWW key on this machine), not Win+R or Start
search. Pinned/right-click launches did not reproduce in this session.

**Re-test 2026-11-15 (0.11.7, `focus-20261115-180821-833502`):** the same
launch-key burst with flashing windows, first activations from the flyout
(some after a pause). Observer: 23 Calculator foreground intervals across 13
frames; all 11 intervals of at least 1 s were confirmed by the mod, the other
12 were shorter than the preview minimum. Shadow ActiveItem check: no
`MOD-MISSED`. The first-activation gap did not reproduce with the shell-hook
activation path in place. **Coverage caveat (user):** only one Calculator
flashed on launch in this run, so the flashing first-activation path has a
single sample; repeat until several windows flash before calling it fixed.

**Second re-test 2026-11-15 (0.11.7, `focus-20261115-181122-546017`):**
launch key pressed while clicking elsewhere to keep focus away. 21 Calculator
foreground intervals across 18 frames; every interval of at least 1 s was
confirmed. Two activations reproduced the original gap: frames `A2044` and
`52A36` came to the front with **no** `EVENT_SYSTEM_FOREGROUND` (observer saw
`52A36` only by sampling), but each had a shell `HSHELL_WINDOWACTIVATED` (4),
and the mod took both from the shell-hook path (`52A36`: candidate 18:12:24.050,
confirmed 1 s later). No shadow `MOD-MISSED`. Conclusion: the gap still
exists in Windows; the 0.11.0 shell activation path covers it.

Times below are local UTC+02:00; foreground.jsonl stores UTC.

| Time | Independent observer | Mod |
|---|---|---|
| 16:01:29.946 | Foreground EVENT for Explorer flyout `072258A` | Receives same event, treats it as transient |
| 16:01:30.112 | SAMPLE: foreground `0DC19B8` (Calculator frame), keyboard focus `00B2312` (CoreWindow) | No foreground event for this frame; no confirmation |
| 16:01:30.904 | EVENT: foreground returns to `00D20DE` | Receives event |
| 16:01:31.754 | — | Calculator `0DC19B8` still resolves but has tick=0, rank=0 |
| 16:01:35.419 | EVENT: Calculator `0DC19B8` becomes foreground on second activation | Receives and accepts same HWND |
| 16:01:35.425 | — | Confirms Calculator preview recency |
| 16:01:35.493 | SAMPLE: same frame and CoreWindow keyboard focus as first activation | — |
| 16:01:37.143 | — | Same Calculator is rank 1, tick=793758156 |

The independent observer records no matching foreground EVENT on the first
activation, although its sample verifies the actual foreground and keyboard
focus. Thus this is not merely mod filtering, a hidden painted highlight, or a
missing thumbnail HWND. Both out-of-context listeners miss notification of this
transition. This does not prove which Windows component omitted or suppressed
notification, nor that flashing itself causes it. No click-hook diagnostic was
emitted for the interaction either.

The first foreground interval lasted about 0.8 seconds; preview minimum in this
recording is zero. The second activation receives an event and immediately ranks
the exact same frame. The app's independent eight-second minimum is unrelated.

Repair implemented in 0.10.13: a short, bounded foreground reconciliation
after a taskbar/flyout foreground transition, on the existing focus worker.
If a real app has taken foreground, feed that actual HWND through normal focus
handling and normal minimum-focus/exclusion rules. Do not stamp the previous
pending app or infer use from clearing the red attention tint. Avoid permanent
polling or reviving the hosted-window association cache.

The worker checks every 100 ms for at most two seconds after the shell event;
normal app events cancel the check. Controlled production-helper tests cover
recovery, cancellation, stale messages, timeout, unload and timer failure.
Compilation with Windhawk 1.7.3 passed. Live validation is still pending:
repeat the first activation of flashing Calculator windows, including a long
pause in the flyout before clicking, and test a positive preview minimum.
The bounded window cannot recover every missing event (in particular a click
after it expires without another shell signal). Do not describe it as a
universal replacement for foreground events.
