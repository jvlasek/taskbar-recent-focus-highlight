# 0.9.42 final visual/lifecycle validation

Status: automated checks/build recorded in the handoff; live checks pending.
Do not describe .42 as visually validated or submit its draft review response
as completed validation until the following checks are performed.

1. Recompile/load .42, restart Explorer, attach UWPSpy. Use the same settings
   as the .41 baseline (leftBar, top 3, 8-second minimum; harness hold 10s).
2. Run five enabled badge cycles. Confirm screenshots, not only PASS.
3. Repeat for frame, full and bottomBar. Verify glyph readability, badge front,
   expected bar/frame/plate placement, hover and active/inactive running pills.
4. Switch between all four styles while test/ordinary apps remain open. Confirm
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
