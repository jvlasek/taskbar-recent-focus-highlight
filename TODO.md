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

## Explorer-side state (from taskbar-active-window-highlight, PR #7421)

Explorer already knows the active window and each window's app; we rebuild
both. Prefer its state where evidence supports it. Order matters: shadow
first, then the low-risk preview fixes, then identity.

- [ ] **Shadow AppId diagnostics (branch `shadow-appid-diagnostics`).**
  Log-only. Hooks `TaskbarController::OnAppsListActiveItemChanged` and
  records Explorer's active `WindowId` + `AppId`. Log lines start with
  `Shadow AppId:` / `Shadow focus:`. Run about a week with normal use,
  including Total Commander + Lister, Calculator, Terminal, Settings,
  VSCodium/Windhawk, Never Combine and a second monitor. Read:
  - `SPLIT` — one mod key, several AppIds. Expected for TC/Lister (agreed:
    they should be two apps with two slots). Any other split: inspect.
  - `MERGE` — several mod keys, one AppId. Blocks the switch for that app
    until explained.
  - `MOD-MISSED` — Explorer activation we never accepted (Calculator
    WinEvent gap?). `HWND-MISMATCH` — same PID, different HWND (UWP frame vs
    content?). `EXPLORER-OTHER` — we accepted a window Explorer does not
    call active (owned dialogs / tool windows; check owner/tool fields).
  - `no mod key` — Explorer active window our resolver rejects.
- [ ] Preview cards: resolve HWND via card -> `ThumbnailSource` -> view model
  -> `TaskItemThumbnail.ParentItem.WindowId` as pass 0; keep ctor map and
  GetAt as fallbacks until logs show they are never needed.
- [ ] Preview cards: hook `TaskItemThumbnailView::put_ThumbnailSource` so
  recycled cards repaint (suspected cause of stale/missing preview marks).
- [ ] Use `ActiveItem` as a third focus source next to WinEvents and the
  shell hook, if shadow logs show `MOD-MISSED` cases it would cover and no
  `HWND-MISMATCH` for UWP. Respect per-taskbar apps lists: only the list that
  reported a window may clear it.
- [ ] Group windows via `TaskbarAppsList.Groups` items' `WindowId` instead of
  the `CTaskGroup::GetNumItems` offset probe.
- [ ] Switch app keys to `TaskGroup.AppId` only after shadow logs are clean.
  Keep window-level (WindowId) matching first so Never Combine buttons of one
  AppId do not all light up. Plan exclude-list migration (keys change from
  paths/AUMIDs).
- [ ] Optional: "outline" preview style — own Border above `BackgroundBorder`,
  geometry/corner radius bound to it, `{ThemeResource SystemAccentColor}`.
- [ ] Unload: replace `INFINITE` waits (focus thread stop, dispatcher drain)
  with bounded waits.
- [ ] Active-only preview mode + single-window flyouts: not planned while
  taskbar-active-window-highlight covers that use case.

## Optional enhancement

- [ ] Evaluate Composition DropShadow for true glow only after correctness
  work. Current layered glow can clip at large spread; confirm layout,
  coexistence and unload behavior before adopting a new rendering mechanism.

## Release gate

- Keep runtime notes and generated diagrams consistent with implementation.
- Run relevant existing checks and record live passes and unverified behavior.
- After a version increase, commit its source, documentation and checks locally
  before handing over the version. Do not push without a request.
