# History: superseded designs and removed mechanisms

Not a description of the current code. Read it only when you are about to
revive something (a fallback, a private lookup, a second resolver) and want to
know why it was removed. The complete guide as it was before the split
(0.11.37) is in Git: `git show a2ac126:AGENTS.md`.

Short timeline:

- 0.9.x: fuzzy automation-name matching removed; unload hardened over twelve
  review rounds; icon overlays moved to an own host (no native styling).
- 0.10.3–0.11.31: asynchronous button identity (worker resolved process path /
  AUMID, serials and deadlines). Removed in 0.11.33.
- 0.11.2–0.11.19: preview cards resolved through Explorer's card view model;
  constructor-map and repeater-index passes removed.
- 0.11.18–0.11.20: every taskbar.dll hook and private lookup removed
  (`ReportClicked` sentinel, `QueryViaVtable`, task-item array offsets,
  `HandleClick`, `GetThumbnailWindow`); tag `before_cleanup` = 0.11.17.
- 0.11.33–0.11.37: identity = Explorer's AppId, captured synchronously;
  `requireTaskbarButton` and the `OnPointerPressed` hook removed.
- 0.11.38: Fable review cleanup; size boost, glow roundness / layers and
  outline fill opacity settings removed; logs written after `g_stateMutex`
  is released (`DeferredLogScope`).
- 0.11.39: Fable review perf items: no hover-triggered full pass for known
  ranks, edge once per pass, `QueueFullPass` coalescing, flyout retarget
  clears only on a new key, decay prunes other desktops in separate holds,
  custom colour parsed once, accent read from the subscribed `UISettings`.
- 0.11.40: source split into `src/NN-*.cpp` parts, joined by
  `tools/build_mod.py`; tests read the parts.
- 0.11.41: empty rank list: two retry sweeps after it empties, not one per
  decay tick forever.
- 0.11.42: Fable review phase 4 (subset): repeater seeding, PID in the
  window → AppId map, `HSHELL_WINDOWDESTROYED`, confirm-time desktop, AppId
  re-lookup at confirm, group-capture warning. Provisional keys kept (see
  identity.md).
- 0.11.43: re-seed repeaters after a desktop switch or an unknown focused
  window.
- 0.11.44: delayed re-seed after a switch, one 300 ms AppId retry at app
  confirm; group-capture warning only after 20 misses with no hit.
- 0.11.45: Fable review phase 5: `MinFocusTrack` (one deadline / Alt-Tab
  grace machine for app and preview confirms), `FindButtonStateLocked`,
  renames (`ButtonState`, `g_buttonStates`, `g_buttonStateMutex`,
  `ButtonCapture`, `CaptureButton`), `EnumName` tables for settings and log
  names, `ClampedIntSetting` / `EnumSetting`, `AppIdentity` struct,
  single-loop full-pass binding, `MarkButtonUnranked`. Not done: one
  per-window recency model (4.3), a generic event-watch container (unload
  path, left as is), a catch-logging helper.
- 0.11.46: one per-window recency model (Fable 4.3): `windowAppFocus` is the
  record, `DeriveAppsFromWindowsLocked` builds `appFocusMap` by current
  AppId at each recompute; provisional app keys, `AttributeProvisionalAppsLocked`
  and the confirm-time AppId retry removed.
- 0.11.47: `doc/code/state-machines.md` observations 1-3: same-window
  pending survives WINDOW: -> APPID:, app confirm completes a given-up
  preview track, desktop change checked before the transient early return
  and in the decay tick, both through `OnVirtualDesktopSwitched`.
- 0.11.48: desktop re-check timer (`kDesktopRecheckTimerId`, 0.4 / 1.6 /
  2.8 s after a foreground change). Live 2026-11-18: clicking Claude's
  flashing button on an empty desktop switched back, the focus event came
  before the registry update, and the mod kept the empty desktop (no glows)
  for 15 s until the decay tick.
- 0.11.49: replaced by a `RegNotifyChangeKeyValue` watch on
  `HKCU\...\Explorer\VirtualDesktops` in the focus thread's wait loop
  (`MsgWaitForMultipleObjectsEx`); the per-foreground read is only the
  fallback when the watch fails, the decay tick keeps its read.
- 0.11.50: `LOCKAPP.EXE` is shell UI (sleep capture: the lock screen was
  confirmed as focus on wake); `OnVirtualDesktopSwitched(reason)` logs
  `Desktop switch (<reason>)`.
- 0.12.0: windhawk-mods submission; in-mod README / settings wording for
  ranks 4+ and "highlight" (not "boost"). PR text: `doc/pr-0.12.1.md` (was `pr-0.12.0.md`).
- 0.12.1 (after the 26 h capture manual-20261120-154409-086197): decay from
  last use; focus tracking waits for the Taskbar.View hooks
  (`ActivateFocusTracking`), so folder-window explorer.exe processes stay
  idle; the log prints ladder / bindings / preview siblings only on change and
  gained `Hidden`, `Decayed … key … last use`, input-desktop name and
  late-tick lines (testing.md).
- 0.12.2 (Gemini 4 Neon pre-submission review, `reviews/neon.md`; the
  windhawk-mods submission is now 0.12.2, PR text `doc/pr-0.12.2.md`):
  accent from `WM_SETTINGCHANGE` / `WM_DWMCOLORIZATIONCOLORCHANGED` instead
  of `UISettings::ColorValuesChanged`; transient foregrounds keep the
  last-use stretch; same-app window switch re-confirms; destroyed window in
  front stamped and folded; signed `GetLastInputInfo` difference; no
  `Wh_Log` under any mutex; `RememberUiDispatcher` gates; PID 0 fails
  closed; exclusion sweep on the worker; init no longer fails for "nothing
  to paint"; smaller one-liners. Not taken: see `reviews/review-response-neon.md`.

## Review lessons about removed mechanisms

| Lesson | Why it bit us | Rule |
|--------|----------------|------|
| Path cache is not a poll | `EnsureButtonPathCached` from UVS = `ReportClicked` into `HandleClick` | Resolve on full bind + `OnPointerPressed` only; never from UVS |
| Empty path cache is not permanent | Pinned icon: first resolve has no task item; Explorer reuses the same `TaskListButton` on launch; click `force` often runs before the process exists | Retry while path and AUMID are empty; throttle pinned-not-running |
| Pinned AutomationId is not a finished Win32 resolve | `appIdUpper` from `Appid: discord…` made `haveIdentity` true; launch never re-resolved; rank key is the image path | `resolvedWhileRunning`: one resolve on not-running → running; tick throttle otherwise |
| Do not deref ctor-map `taskItem` | HWND gone ⇒ native `ITaskItem` likely freed; `GetWindowFromTaskItem` UAF | Store HWND at ctor; raw pointer is compare-only (thumbnail-reorder) |
| Native order snapshots removed | Earlier snapshots and native ZIndex normalization were abandoned | 0.9.42 preserves native relative order and properties; explicit host insertion fixes the reproduced deferred-index asymmetry |
| `ConfirmPreviewFocusNow` off the click thread | `HandleClick` + `ResolveAppIdentity` + inline `RunOnUiThread` stalled the taskbar | `WM_APP_PREVIEW_CLICK` + HWND/PID; worker resolves |
| Repeater index ≠ `Thumbnails` index | Snap-group card extra in one collection shifts every later GetAt | Compare sizes; compact window ordinal or skip GetAt |
| Stale global `Thumbnails` collection | `g_TaskGroup_Thumbnails` is from the last `TargetItemKey`; refresh also runs from OnApplyTemplate / decay; no-DataContext made the agree-check vacuous | Clear the weak ref on the way **in** to `TargetItemKey`; DataContext first; GetAt only if it agrees with a DataContext HWND |
| Unique-title | Name-guess; untested on current Win11 (pass 1/2 fill) | Dropped. Stash: `stash/preview-unique-title.cpp` |
| Path cache survives exe replace | Explorer reuses `TaskListButton`; cached `C:\A\foo.exe` bound any `foo.exe`; `PathAppearsOnTaskbar` filename match kept A ranked | Re-resolve when running and HWND dead or live path differs; exact path/AUMID only for “on the taskbar”; demote unmatched when that identity is gone, even if `seenOnTaskbar` |
| Plate restore wrote the first brush | Repaint put the saved brush back even after Styler replaced it. Zero fill installed a transparent brush | Restore only while the border still has our brush. Save the current owner on the next takeover. Zero fill leaves the native background |
| Empty-resolve cap stuck across launches | `resolvedWhileRunning` was set true and never cleared, so eight misses blocked later launches | Clear that flag and the attempt count on each new running episode |

## Old "done" list and future-work list (0.9.x era; the backlog is `TODO.md`)

Done in 0.9.x and not listed: UWP `APPID:` keys, preview plate brush restore,
Styler hover-plate z-order, settings snapshots, timer deadlines, transient
foreground, bounded vtable probe (fail closed, not a per-build proof), worker-first unload (a failed SizeChanged revoke is logged, not a certification that every subscription is gone), icon fuzzy removal,
identity-keyed UVS maps, no native reorder of untouched icons, nested settings
groups, `Wh_Log` instead of an in-mod debug toggle, `UISettings::ColorValuesChanged`,
filename-900 dropped, `ReportClicked` off UVS, empty-resolve retry, rank -1 vs 0,
exe-replace path-cache re-resolve, `PathAppearsOnTaskbar` exact-only, unmatched demote without filename steal,
`lastHwnd`+pid, replica/score prune, flyout Low coalesce, snap-group GetAt
guard, click confirm on the focus thread, idle decay timer, pinned→running
re-resolve, ctor-map HWND only, no UVS clear on overlay sweep, paint cache
edge+size, explicit host insertion with native-order preservation (0.9.42), DataContext-first preview bind,
unique-title dropped (stash), Thumbnails clear-on-retarget, bounded transient
min-focus, File Explorer folder windows, deadline-style full-rebind debounce,
preview overlay host reuse, window recency key is path or `APPID:` (not AFH
path), ring preview style removed (hybrid default), native badge-order repair
removed in 0.9.36 after the repeated badge recreation regression, `observedRunning` vs 400 ms
heartbeat, settings-change timer re-arm, linear rank intensity, drain
`Completed` barrier, glow-host Tag for prior icon transform, Win32 AUMID
exclusion on pending/preview history.

1. Composition shadow / true GPU outer glow if XAML halo stays clipped
   (optional polish; current bar/frame/plate is the product).
2. Stronger DataContext ↔ TaskItemThumbnail identity (TaskItem/DataContext is first; repeater GetAt fills holes).
3. Classic / non-XAML thumbnail path if still needed on some builds.
4. Multi-monitor secondary taskbars if weak refs only cover primary.
5. Per-desktop prune of deleted virtual desktop GUIDs beyond decay.
6. Per-monitor taskbar edge if a secondary bar can sit on a different side.
7. Test seam + `make release` concat (pure ranking/identity table tests).
8. Watch `ReportClicked` for jump-list
   / MRU side effects (once per button on full bind / press, not on hover).

## 0.10.0 submission polish

The embedded source comment links to the public development repository; avoid
relative references to files absent from windhawk-mods. The stored roundness
key remains icons.glowRoundness, with the UI label Previews: Roundness (%).
Geometry/placement helpers use the upstream Chromium-based formatter style.

## Review follow-up (0.10.2)

Late view-hook installation shares HandleLoadedModuleIfTaskbarView between
LoadLibraryExW and AfterInit. Atomically claim g_taskbarViewHookAttempted before
hooking; failures are not retried on subsequent loads of the same view module.
The init path is serialized before loader hooks activate. The process-image
cache releases its string allocation at outermost scope exit. Side layers are
1–2, with only L0/L1 rectangles. The later 0.10.3 change below completes the asynchronous click-path follow-up.

## Asynchronous button identity (0.10.3 — superseded in 0.11.33 by synchronous AppId capture)

This replaces the earlier synchronous full-bind/press metadata and stale-check
implementation. EnsureButtonPathCached captures task item/group HWNDs and PIDs
on the UI thread, then queues plain ButtonResolveData on the existing focus
worker. No XAML or task-item reference crosses that queue. Worker results are
stored under the button-cache mutex by globally unique request serial; the
worker never resolves weak XAML references. The UI consumes a result only after
recapturing the button's HWND/PID, running state, AutomationId and group list.
A replaced cache row or newer click invalidates the earlier serial.

Clicks force a fresh request, not a synchronous metadata lookup. Full refreshes
share the resolver. One queued request per button, maximum 128 plus one in
flight. Five-second deadlines apply before work and when accepting results;
they do not cancel synchronous Windows APIs. Completion repaints must not
start routine refresh requests (allowIdentityRefresh=false), avoiding a
result/requery loop. A changed target may queue its replacement. Expired
same-target results cool down until a later normal refresh.

Keep confirmed recency while async resolution is pending: absence of a cached
identity is not proof of a tray-only app. RecomputeRanksForDesktopLocked still
requires exact running-button identity before showing a rank. The old confirm
lambda which immediately demoted unresolved apps was removed. Uninit joins the
worker, clears its value-only queue, then performs the existing UI drain.

ShouldIgnoreHwnd now uses GetWindowTitle().empty() for tool-style windows;
GetWindowTextLengthW is also forbidden there because it sends same-process
messages. tests/run-async-identity-tests.py exercises production queue, worker
and UI-result logic with controlled fixtures; it does not emulate XAML.

## Identity diagnostics (0.10.6)

Identity log lines correlate button identity and request serial across queue,
worker, result publication/dispatch and UI acceptance. Acceptance logs expose
target changes, HWND/PID liveness and deadline rejection; supersession has its
own line. Running-state transitions log only on change. Do not add metadata
queries solely for logging. `tests/uwspy/record_windhawk.py` wraps the existing
collector for manual sessions without test apps or UWPSpy. It probes startup,
checks collector health, and saves timestamped evidence until Ctrl+C.

## Thumbnail identity (0.10.11)

At construction, immersive thumbnail mappings use the optional symbol-resolved
CImmersiveTaskItem::GetThumbnailWindow with the live ITaskItem interface pointer.
On the tested build this provides the same ApplicationFrameWindow tracked by
focus, even before the content HWND exists. Ordinary window task items keep the
existing GetWindow lookup. The captured HWND/PID is validated on later reads.
Missing immersive thumbnail getter returns no match; do not substitute the app
content HWND. Taskbar icon identity still uses GetAppWindow for app metadata.

This replaces the 0.10.7–0.10.10 preview workarounds and diagnostics: no thumbnail
root normalization, hosted association cache, attachment polling/timer, or
frame-child enumeration remains. Existing async identity logs and the manual
recorder remain. Regression test: `python tests/run-preview-identity-tests.py`.
Targeted Win32 and Calculator live tests passed; see doc/investigation-notes.md
for evidence and coverage limits. GIMP has earlier manual coverage, not a fresh
automated validation of this version. Do not infer complete build coverage.

## Review follow-up (0.11.1)

Identity-completion repaints are coalesced per UI dispatcher. Its queued flag
clears before painting, so results published during a pass may queue another;
a slow dispatcher retains its own flag independently. Dispatch rejection clears
the flag for retry. Results remain cached for ordinary full passes if an accepted
callback never executes. Existing worker-stop-before-dispatcher-drain ordering
is unchanged.

When a grouped Win32 button changes its sampled HWND, keep its identity while
refreshing only if the new HWND/PID is a live member of the previously captured
group, the process matches the old sample, and the cached path is not a shared
UWP host. Unknown windows, process changes, pinned-only transitions and hosted
apps still invalidate the old identity. This deliberately does not use PID-only
matching or guarantee continuity across separate-process groups.

Window titles call InternalGetWindowText directly. Shell registration failure
preserves the foreground WinEvent path, with a clear degraded-mode log. Missing
shell events can therefore leave the known Calculator activation gap on such a
system; there is no claim of equivalent coverage in degraded mode.

Checks: `tests/run-identity-repaint-tests.py`, `tests/run-async-identity-tests.py`
and `tests/run-shell-activation-tests.py`. Live grouped-window close/remaining
icon behavior still needs validation on 0.11.1.

## Explicit preview insertion (0.9.46)

Use `InsertAt(children.Size(), element)` for preview host creation, bringing
that host to the front, and native-plate marker creation. These also modify
native panel collections; do not use Append. Deferred children in these
specific panels have not been demonstrated, but explicit insertion keeps
notifications balanced with removal. The source guard rejects remaining
Append calls throughout this mod. Live .45 results are not a .46 preview test.
