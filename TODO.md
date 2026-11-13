# Next release

Baseline: 0.11.1. This is a backlog, not a list of confirmed defects or a
commitment to ship every item in the next release.

## Known visual issue

- [ ] Investigate hover behavior with full-pane highlighting. Capture the
  affected style/theme, active and inactive states, native child order and
  ZIndex, and before/during/after-hover screenshots before choosing a fix.
  Preserve native glyphs, badges, progress and running indicators; move or
  repaint only our overlays. Include Taskbar Styler full-cell hover plates.

## Automated regression framework and dedicated VM image

- [ ] Plan a reproducible Windows VM image and extend the existing real-input
  harness around the scenarios below. Preserve independent foreground/identity
  observations, versioned fixtures, repeatable setup and logs/screenshots.
  Document monitor, GPU and theme scenarios requiring physical-machine checks.
  Include static ABI investigation alongside automation, not as a claimed
  consequence of passing runtime tests.

- [ ] Establish a Windows-build compatibility baseline, including 26H2.
  Record OS build, Taskbar.View.dll/taskbar.dll versions and hashes, and
  resolved required/optional symbols alongside live test results. Compare
  pre/post-update binaries; identical files do not prove identical behavior
  when feature activation changes the executed paths.
- [ ] Audit private task-item ABI assumptions against that baseline:
  QueryViaVtable, task-item array offsets, interface projections, and the
  immersive GetThumbnailWindow getter. Bounded probing is not proof of safety
  on another build. Prefer known symbol-resolved interfaces where evidence
  supports them; keep missing identity fail-closed and add no guessing fallback.
- [ ] Validate 0.11.1 grouped-window close behavior: close the sampled window
  while siblings remain, including same-process and separate-process groups.
  Check identity continuity, eventual async resolution, and slot release when
  the last window closes. The current continuity rule covers captured live
  same-process group members, not every grouping arrangement.
- [ ] Complete live checks of the current shell-notification focus path:
  Win32 and Calculator activation, positive minimum-focus deadlines, hover
  and background-attention controls, desktop switching, and unload/re-enable.
  Record degraded coverage if shell registration fails; foreground WinEvents
  alone have a documented Calculator activation gap.
- [ ] Exercise secondary monitors, taskbar edge/size relayout, Never Combine,
  snap-group cards, and desktop history restoration. Existing harness coverage
  does not establish all of these behaviors.
- [ ] Check preview rank-2/rank-3 opacity and icon frame/full placement across
  native and Styler themes. Membership checks are not pixel-level visual tests.

## Optional enhancement

- [ ] Evaluate Composition DropShadow for true glow only after correctness
  work. Current layered glow can clip at large spread; confirm layout,
  coexistence and unload behavior before adopting a new rendering mechanism.

## Release gate

- Keep runtime notes and generated diagrams consistent with implementation.
- Run relevant existing checks and record live passes and unverified behavior.
- After a version increase, commit its source, documentation and checks locally
  before handing over the version. Do not push without a request.
