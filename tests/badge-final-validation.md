# 0.9.45 final visual/lifecycle validation

Status: ready for review with the following evidence and limits (2026-09-30).

- Current-version five-cycle enabled run passed: `20260930-022800-951939`.
- User reports style switching, hover, initial placement and multi-window
  flyout smoke checks passed. An initial switch from the removed style did
  not behave as expected; cause unconfirmed, no special migration implemented.
- User reports the same Explorer process has run since .42 without recurrence.
- Both three-round lifecycle variants passed on .41. They have not been rerun
  on .45; user elected to proceed based on the isolated fix and subsequent use.
- Broader theme, taskbar relayout and multimonitor coverage remains unverified.

The checklist below is retained for further regression testing, not as a claim
that every item has been completed or a requirement to delay review.

1. Recompile/load .45, restart Explorer, attach UWPSpy. Use the same settings
   as the .41 baseline (leftBar, top 3, 8-second minimum; harness hold 10s).
2. Run five enabled badge cycles. Confirm screenshots, not only PASS.
3. Repeat for frame and full. Verify glyph readability, badge front,
   expected bar/frame/plate placement, hover and active/inactive running pills.
   Frame must have one contour; Full must follow the same native bounds.
   Check the very first highlight after clear/reload: Frame/Full must already
   be centered before a later focus/hover repaint, with visible native rounding. Check square and circular
   Styler backgrounds. Verify a saved bottomBar setting now displays Side.
4. Switch between all three styles while test/ordinary apps remain open. Confirm
   old visuals disappear, native pills return, and unranked icons are untouched.
5. With the user's usual Taskbar Styler theme, repeat hover and badge checks.
   Native custom ZIndex is intentionally not overridden. Record the theme.
6. Exercise supported taskbar orientation/icon-size changes and check geometry.
   Smoke-test multi-window flyouts; their code did not change.
7. Repeat same-button lifecycle tests with badges absent and present. Never
   restart Explorer between phases. Final mod state is disabled.

From tests/uwspy, the lifecycle command is:

```powershell
python .\harness.py --mode enabled --test-unload --unload-rounds 3 --unload-badge absent --top 3 --focus-seconds 10 --cycles 5 --screenshots --windhawk-log --output captures
```

Repeat with `--unload-badge present`, enabling the mod first. For a short
per-style run omit `--test-unload`, `--unload-rounds`, and `--unload-badge`.
Keep captures and note style/theme/orientation because the harness does not
read the mod's settings. A failed assertion or visible defect blocks submission.
