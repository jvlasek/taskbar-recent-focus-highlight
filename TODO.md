# Next release

Baseline: 0.11.1. This is a backlog, not a list of confirmed defects or a
commitment to ship every item in the next release.

## Known visual issue

- [x] **Hovering a rank-1 preview loses its whole-card plate.** 0.11.16:
  plate is our own Border above BackgroundBorder (hover changes the
  background underneath and shows through the partial fill). Needs a live
  check, with and without Taskbar Styler. Reported
  2026-11-15 on 0.11.3: the title-bar tint (ranks 2+) survives hover, but the
  rank-1 plate is replaced by Windows' own hover highlight. Cause: the plate
  tints the card's native `BackgroundBorder.Background`, which the card's
  PointerOver visual state sets too, so hover wins; the title tint lives in
  our own overlay host and is untouched. Fix: paint the plate as our own
  element instead, as taskbar-active-window-highlight does — a `Border`
  inserted right above `BackgroundBorder`, `IsHitTestVisible=False`, with
  Width/Height/Margin/alignment/CornerRadius bound to it (follows Styler
  themes). Same element can provide the "outline" style from the Explorer
  plan below. Decide whether hover should show on top of the plate (keep
  native hover visible, e.g. lower plate opacity) or the plate should win.
  Verify unload/settings change still restore any native brush we touched.

- [ ] **Closing a ranked preview does not repaint the remaining ranks.**
  Repro: multi-window flyout (Calculator, 12 windows), close the rank-2
  window from the flyout; the old rank 3 keeps its rank-3 tint instead of
  taking the rank-2 look. Log `manual-20261115-142933-523175`, 14:54:12-17:
  ranks are recomputed correctly (old rank 3 `A2A01B0` -> rank 2), but the
  repaint at 14:54:17 came from `OnDecayTimer` ("Preview decay: windows 15 ->
  14"), about 5 s after the close, not from the close itself. Nothing in the
  flyout refresh path fires when a card is removed, so the stale paint lasts
  until the next decay tick (up to 30 s), retarget or reopen. Fix: refresh
  the flyout when its window set shrinks (card removal / ItemsRepeater
  ElementClearing, or window destroy for a mapped HWND), plus the planned
  `put_ThumbnailSource` hook for cards that get rebound.
  **Update (user):** it stays wrong as long as the flyout is open, also after
  the logged 14:54:17 repaint (`rank 2 ... intensity=60`); only clicking the
  taskbar icon (retarget/reopen) fixes it. So the paint lands on the wrong
  card. Every sibling in that flyout resolved `how=repeater`, none
  `how=taskitem`: the DataContext/ctor map missed all 12 Calculator cards and
  only the index fallback (repeater index -> `Thumbnails.GetAt`) was used.
  After a removal the repeater can hold recycled/removed elements with stale
  indices, so the HWND-to-card pairing shifts even though the size check
  passes. Fix with the view-model resolve (card -> `ThumbnailSource` -> view
  model -> `ParentItem.WindowId`) as pass 0; it is per card and cannot shift.
  Also log why pass 1 misses (no DataContext vs no ctor-map entry).
  **0.11.2/0.11.3 status:** view-model resolve verified live (152/152 cards
  `how=viewmodel`, DataContext is null on these cards). Ranks after a close
  are now correct, but the 0.11.2 capture showed no refresh for 17 s after
  the close (`put_ThumbnailSource` does not fire on removal). 0.11.3 adds an
  `ItemsRepeater.ElementClearing` watch; needs a live check that the repaint
  follows the close immediately (`Preview: watching flyout repeater` line,
  then a `Preview resolve` right after the close).
  Whole session: 217 `how=repeater`, 97 `how=none`, **0 `how=taskitem`**,
  although the ctor hooks are installed and the map is full (12 maps for 12
  cards). So on this build the card DataContext is not the
  `TaskItemThumbnail` (likely its view model), and pass 1 never matches:
  every preview rank so far rested on the index fallback. This is probably
  the main preview-reliability bug, not only a close-time issue.

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
  **Implemented on this branch, needs live check:** startup logs
  `Preview resolve: view model on`; siblings should show `how=viewmodel`
  with `vm=` filled. Watch for `view model ... != ctor map` (disagreement)
  and the one-time `card DataContext class=` line (explains the old miss).
  Retest the close-ranked-preview bug above.
- [ ] Preview cards: hook `TaskItemThumbnailView::put_ThumbnailSource` so
  recycled cards repaint (suspected cause of stale/missing preview marks).
  **Implemented on this branch** (coalesced flyout refresh). Check whether
  closing a card fires it; if not, close still waits for the decay tick.
- [ ] Use `ActiveItem` as a third focus source next to WinEvents and the
  shell hook, if shadow logs show `MOD-MISSED` cases it would cover and no
  `HWND-MISMATCH` for UWP.
  **2026-11-15:** not needed so far. No `MOD-MISSED` in any capture,
  including a Calculator launch-key burst (`focus-20261115-180821-833502`:
  observer intervals of at least 1 s all confirmed; only one window flashed,
  so the flashing path has one sample). Second run
  `focus-20261115-181122-546017`: two activations without any foreground
  WinEvent, both caught via the shell hook. The only mismatches were
  owned dialogs, fixed in 0.11.5-0.11.7. Keep the shadow check; revisit if a
  real miss shows up. Respect per-taskbar apps lists: only the list that
  reported a window may clear it.
- [ ] Group windows via `TaskbarAppsList.Groups` items' `WindowId` instead of
  the `CTaskGroup::GetNumItems` offset probe. **0.11.8/0.11.9:** shadow
  comparison per button via `ITaskGroup.get_Items` (IIDs from
  windowsudk.winmd). First run: UWP differs only by CoreWindow (old) vs
  frame (public) — 0.11.9 compares after child->root; SOLIDWORKS had one
  extra old-path window (details logged from 0.11.9 on). 0.11.9 result: the
  only differences were UWP CoreWindow (old) vs frame (public) and a hidden
  SOLIDWORKS `swThumbnailWnd` tool window (its taskbar thumbnail proxy;
  Explorer does not count it). **0.11.10: public list is authoritative**;
  private path only as a logged fallback (`Button windows: FALLBACK`).
  Next, once logs show no fallbacks: drop GetTaskItemsArray /
  GetWindowFromTaskItem / the ReportClicked-sentinel native lookups (and the
  required symbols they need), and re-check UWP button identities
  (Settings / Store / Calculator must keep separate `APPID:` keys).
- [x] **Never Combine: one button = one window** (user, 2026-11-15).
  Verified on 0.11.15 (`manual-20261115-222407-856139`): 49 window buttons
  detected, ranks per window (Claude, Calculator window A, Calculator window
  B), each bound to one button. Still to check: combined mode unchanged
  after switching Never Combine off; "combine when full" mix.
  Observed on 0.11.10: both VS Code buttons ("2 running windows") lit, since
  each uncombined button listed the whole group and the path/AppId fallback
  matched. **0.11.11:** an uncombined button (own TaskItem inside a group of
  2+) lists only its own window; each window's app-level confirmation is
  kept; an app with uncombined windows ranks one slot per confirmed window
  (`perWindow`), matched only by exact window. Live check failed: the
  uncombined detection never fired — Never Combine buttons have no own
  TaskItem (same sample HWND, group=2). UWPSpy dump
  (`captures/uwspy-nevercombine`): each window button's AutomationId is
  `Window: 0x<hwnd>`, the group button's `Appid: <AUMID>`; nothing else
  differs (DataContext null on all). **0.11.13** takes the own window from
  that AutomationId, validated against the group's public Items. Still
  nothing split: diagnostics (0.11.12/0.11.14) showed group buttons have
  `isExpanded=1`, and window buttons have their own TaskItem but **no group
  view model** (one window). **0.11.15:** uncombined = `Window:` id that
  matches the button's own TaskItem window; diagnostics removed. With
  combining off, each button is its own unit and should rank on its own
  window, not light up with every button of the same app. Check what a
  Never Combine button's window list contains (whole group or its own
  window) and whether ranks then need to be per window for those buttons.
- [x] 0.11.4: windows with an explicit AUMID are keyed `APPID:…` (TC vs
  Lister are two apps now; requested 2026-11-15: one button = one app).
  Expect the TC `SPLIT` to disappear from shadow logs. Still open: apps
  whose AUMID is process-level only (not on the window) stay path-keyed.
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
