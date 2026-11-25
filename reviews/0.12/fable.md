# Review of 0.11.37 before the 0.12.0 PR

Reviewer: Claude Fable 5.2, 2026-11-17. Source: `taskbar-recent-focus-highlight.wh.cpp`
at `c606087` (8,243 lines). Line numbers (`L1234`) refer to that commit.

## How this review was done

- Read the whole mod source, `doc/appid-identity-design.md`, `TODO.md`, the PR #5331
  review thread (`tmp/review-page/reviews-extracted.txt`), the 0.11.37 quiet perf
  results and the three newest 0.11.3x captures.
- **Nothing was compiled or run.** Every finding is from reading the code. Where I
  suspect a behaviour bug I say how to confirm it; none of them is reproduced.
- I did not re-verify the WindowsUdk IIDs / vtable slots against `windowsudk.winmd`
  (that folder is not connected in this session).

## Summary

The AppId rewrite is the right move and it shows: identity is now one synchronous
capture (about 4 us per button in your perf run), matching is string equality, and the
whole async resolver, the taskbar.dll hooks and the click hook are gone. The unload
path, thread confinement of COM objects, and the fail-closed interface checks (IID
gated `QueryInterface`) are in good shape and I found no new unload hazard.

What is left is mostly **residue of the three earlier designs**. The code still carries
data, names, comments and whole code paths from the path-keyed and async eras, and a
few subsystems are more elaborate than what they now have to do. The file would be
easier to get through human review, and safer to maintain, about 1,000-1,500 lines
shorter, without losing a feature.

Before the PR I would do these, in this order:

1. Confirm or rule out the closed-app slot issue (2.1). It is the one finding that may
   be user-visible in normal use.
2. Fix the user-facing README text that still describes the old identity (1.1).
3. Remove the dead code listed in 1.2. The previous reviewers flagged dead code in
   three rounds, so it will be noticed.
4. Stop querying the window AUMID when the exclude list is empty (1.3).
5. Decide on the `icons.glowRoundness` setting, which now does almost nothing (1.4).

Everything in sections 3-6 is improvement material and can follow the PR.

---

## 1. Fix before the PR

### 1.1 The mod README still describes the old identity model

- L78-80: "Icon matching uses the process path / AppUserModelID from the taskband".
  It is Explorer's AppId of the button now. Suggested wording: "An icon is matched by
  the application ID Explorer itself groups taskbar buttons by, not by its label or
  process name. A window that has no taskbar button is never ranked."
- L76-77: "preview plate tints are restored when the highlight is cleared" refers to
  tinting the native `BackgroundBorder`, which stopped in 0.11.16 (own overlay border).
- L140-150 (`excludedPrograms`): say that an Explorer AppId is accepted too, and how a
  user finds it (the `Button identity: ... appId=` log line), since that is now the
  only spelling that separates two buttons of one exe.
- The changelog needs the removed `requireTaskbarButton` setting (already on your list).

### 1.2 Dead or redundant code left by the rewrites

All of these are safe deletions as far as I can tell:

| What | Where | Why it can go |
|---|---|---|
| `ClearThumbnailNativeStyles`, `kThumbNativeStyleMarker`, the `PropertySet` bag | L870-872, L5238-5282, L5290, L5303, L5310 | Nothing creates the marker element any more (plate is an own `Border` since 0.11.16). About 55 lines. |
| `AppFocusInfo::seenOnTaskbar` | L523, L1880, L4500-4506, L4560 | Written in four places, never read. The write at L4500 takes `g_stateMutex` once per bound button in every full pass for nothing. |
| `AppFocusInfo::classUpper` | L516, L7010, L7038 | Stored, never read. |
| `MakeAppKey` and the "legacy key" | L1457-1472, L1677, L1694 | `IsExcludedKey` already receives the path and the window AUMID separately; the legacy key adds nothing. `PathFromAppKey`'s path branch goes with it. |
| Demotion block in the full pass | L4534-4566 | A rank is only in the list if `AppAppearsOnTaskbar` was true microseconds earlier in the same pass, so `!AppAppearsOnTaskbar` cannot be true here except by a 400 ms grace expiring between the two calls. If it ever fired it would zero the confirmed tick (history loss). |
| `Scored::how`, `Scored::viaViewModel`, `ResolveHow`, `usedHwnds` | L5919-5977 | One resolve pass is left; these described three. |
| `FindRankForButton(..., requireRunning)` | L2388-2396 | Only called with `true`. |
| `IsUwpHostFileName` gate in `ResolveAppIdentity` | L1673-1675 | With AppId identity a frame without AUMID simply has no button and is never ranked. |
| "multiple generations of hosts" / "unexpected nesting" loops | L2597-2621, L5002-5024, L5294-5311 | The hosts are created in exactly one place each; the three-parent removal in `ClearThumbnailHighlight` guards against states the current code cannot produce. |

Stale names and comments that will mislead the next reader (and the human reviewer):

- `ButtonPathCacheEntry` / `g_buttonPathCache` / `g_buttonPathMutex` hold no path.
  `ButtonResolveData` / `CaptureButtonResolveData` resolve nothing asynchronously; the
  comment at L3949 ("Slow metadata reads are queued") is wrong now.
- L513 ("APPID:... or uppercase image path"), L1776-1784 (two stacked comment blocks,
  the first about `PathAppearsOnTaskbar`), L2349 and L4470 ("Path / AUMID / HWND
  only"), L7074-7075 ("Async identity results trigger a new eligibility pass"),
  L4792-4795 (orphaned comment above the repeater section), L2870 ("Caller must hold
  g_stateMutex ... or we take it": it takes it, so a caller holding it would deadlock).
- `tests/run-*.py` extract functions by signature text, so renames need the tests
  updated in the same commit.

### 1.3 Window AUMID is still read on every focus episode, only for the exclude list

`GetWindowAppUserModelId` (`SHGetPropertyStoreForWindow`, cross-process) runs in
`ResolveAppIdentity` (L1671), in `OnPreviewMinFocusTimerElapsed` (L6822, L6877), in
`OnMinFocusTimerElapsed` (L6934, L7012) and for every tracked window in
`Wh_ModSettingsChanged` (L8196). That is three to five calls per focus episode (five
for a window that has no AUMID, which is most Win32 windows), and each timer handler
runs `IsExcludedKey` twice. Since 0.11.33 the AUMID is used for nothing
but exclusion, and the default exclude list is empty.

Suggestion: read the AUMID only when `settings->excludedPrograms` is non-empty, decide
exclusion once when the pending focus is created, and re-check only if the confirmed
window differs from the pending one. That removes most of the cross-process work from
the worker and about 60 lines from the two timer handlers.

### 1.4 `icons.glowRoundness` is nearly a no-op

It is declared under Icons, labelled "Previews: Roundness (%)", and its only use is the
corner radius of the fallback plate (L5770-5771), which is painted only when a card has
no `BackgroundBorder`. On current builds it changes nothing a user can see. Remove it
(a removed setting needs no transition; the stored value is ignored) or wire it to
something visible. Also drop `round=%d%%` from the icon log line at L3657.

---

## 2. Suspected behaviour issues (please confirm live)

### 2.1 A closed app may keep a rank slot until decay

`AppAppearsOnTaskbar` (L1785) counts any cache entry with `observedRunning` set. That
flag is only updated when `ButtonCountsAsRunning` runs for the button. Since 0.11.26,
recycled buttons are removed from `live` before the snapshot loop (L4388-4397), so
their entries are never refreshed, but they stay in `g_buttonPathCache` because the
parked element is still alive (the weak ref resolves).

If Explorer parks the button of a closed, unpinned app without a final
`UpdateVisualStates` that reports `IsRunning=false` (your own comment at L3686 says
this often does not happen), the entry keeps `observedRunning=true` and the old AppId.
The app then still passes `AppAppearsOnTaskbar`, keeps its place in the top N, binds
to no button (`UNMATCHED rank`), and the next app is not promoted until the decay
time (30 minutes by default) or until Explorer reuses that element.

How to confirm: have four apps in history with `highlightCount` 3, close the rank 2
app (not pinned), and check whether the fourth app gets a highlight and whether the
log shows a repeating `UNMATCHED rank`.

Fix, either or both:
- In the parked branch (L4393) reset the entry: `observedRunning=false`,
  `lastRunningTick=0`.
- Make `AppAppearsOnTaskbar` also require a live window in the entry
  (`HwndMatchesStoredPid` over `groupWindows`). It is a few `IsWindow` calls and makes
  the check independent of what Explorer reports.

### 2.2 A grouped icon blinks when its first window closes

`RefreshButtonHighlight` (L6384-6397) clears the highlight and schedules a Low
priority full pass when the cached sample window is gone. For a group whose first
window closed while siblings remain, that is a visible off/on. 0.11.1 had
`CanRetainGroupIdentity` for this; it went away with the async code. Identity is cheap
now, so call `EnsureButtonIdentity(button)` right there and re-evaluate the rank
inline with `FindRankForButton`, instead of clearing and waiting.

### 2.3 Focus can be credited to the previous virtual desktop

`PendingFocus::desktopId` is `g_currentDesktopId`, taken from the registry value read
a moment earlier (L7181, L7304). The previous reviews recorded the registry lag as a
known limitation. The exact answer for "which desktop does this focus belong to" is
available from the public API you already wrap: `IVirtualDesktopManager::
GetWindowDesktopId(hWnd)` for the focused window, on the worker. I would use that for
the pending record (fall back to the registry value when it fails or returns
`GUID_NULL`) and keep the registry only for "which desktop is the taskbar showing".

### 2.4 `g_windowAppId` is not validated against PID and never re-keys recency

- `LookupWindowAppId` (L588) trusts the HWND alone, while the rest of the mod pairs
  every stored HWND with its PID. Store `CapturedWindow` and check it on lookup. Exact
  HWND reuse is rare, so this is low priority.
- An app that sets its AUMID after its window is shown changes group. The map is
  overwritten at the next capture, but recency already stored under the old
  `APPID:` key stays there. `AttributeProvisionalAppsLocked` handles only `WINDOW:`
  keys. See 4.3 for a model where both cases are one rule.

### 2.5 Worker holds `g_stateMutex` across everything, including logging

Your 0.11.37 quiet run shows one `WorkerStateLockHold` of 18.3 ms (in the decay
timer). The UI thread takes the same mutex in every full pass and, for a button
whose rank is not known yet, in the `UpdateVisualStates` path (L6427), so a hold like that is four frames at 240 Hz if it
coincides. One sample, probably the worker being preempted, but two things make it
more likely than it needs to be:

- `Wh_Log` is called inside the lock in many places (L1012, L1058, L1828, L1872,
  L7064, L6905). With logging on, that is `OutputDebugString` under a lock the UI
  thread needs. Format into locals, log after unlocking.
- The decay pass recomputes every desktop in one hold (L7359-7383). Only the current
  desktop affects paint; the others can be pruned in separate short holds.

`RefreshButtonHighlight` also shows a 13.4 ms maximum that is not inside
`ApplyButtonHighlight` (max 0.64 ms). The perf build times waits on `g_stateMutex`
only. I would add the same wait/hold timing to `g_buttonPathMutex`, which the worker
takes inside `AppAppearsOnTaskbar`, before concluding it is noise.

---

## 3. Windows and Explorer interfacing

### 3.1 What the mod depends on now (good state)

For the PR text, this is the complete list of non-public assumptions I found. It is
short, and every item fails closed:

- Twelve `Taskbar.View.dll` symbols (L6592-6658): seven required, five optional.
- `TaskListButton` implementation layout: `(IUnknown*)pThis + 3` (L6367) and
  `*((IUnknown**)pThis + 1)` for the thumbnail view (L6539). Same as other mods.
- Group capture: `TaskListGroupViewModel::IsMultiWindow` called with
  `(void**)vm - 1`, reading the group through a hooked
  `consume_ITaskGroup::IsRunning` (L3900-3947). Shared with
  taskbar-volume-control-per-app, so it breaks and gets fixed together with that mod.
- WindowsUdk IIDs and leading vtable slots (L3737-3773, L4587-4600).
- `AutomationId` text `Window: 0x<hwnd>` for uncombined buttons (L2323).
- XAML element names (`IconPanel`, `Icon`, `RunningIndicator`, `BackgroundElement`,
  `BackgroundBorder`, `IconsRepeater`, title text block names).
- `RegisterShellHookWindow` (best effort) and the `CurrentVirtualDesktop` registry
  value (with the public COM fallback).

Two small hardening points:

- If the group capture stops working (`g_capturedTaskGroup` null for every button
  with a group view model), nothing says so. Log it once; today the visible symptom
  would be "second window of an app is never ranked", which is hard to trace.
- The `Window: 0x...` parse is the only string format the mod depends on. Your 0.11.14
  diagnostics found a structural signal (own `TaskItem`, no group view model). If a
  combined single-window button always has a group view model, that signal can be
  primary and the string only a cross-check. Worth one logged comparison run.

### 3.2 Learning windows from buttons vs from Explorer's apps list

The window to AppId map is filled from button captures, so the mod knows a window
only after its button went through `UpdateVisualStates` and a full pass. That is the
source of the provisional `WINDOW:` keys, of `AttributeProvisionalAppsLocked`, of the
"request a full pass because this window has no button yet" path (L7222-7226), and of
the open TODO item about ranks appearing late after the mod is loaded into a running
Explorer.

Two ways to shrink that, cheapest first:

1. **Seed all buttons from the first one.** `IsRealizedTaskbarButton` (L4347) already
   finds the button's `ItemsRepeater`. On the first `UpdateVisualStates` after load,
   walk that repeater with `TryGetElement(i)` and track every realized
   `TaskListButton`. Then one state change on any button captures the whole taskbar,
   with no new symbol and no new hook. This probably closes the TODO item in about 20
   lines.
2. **Get onto the taskbar thread at load.** The usual Windhawk idiom is a
   `WH_CALLWNDPROC` hook plus `SendMessage` to `Shell_TrayWnd` and each
   `Shell_SecondaryTrayWnd`.
   It is synchronous and needs no dispatcher (`RunFromWindowThread`, as in your
   `example/taskbar-volume-control-per-app.wh.cpp`). Once on that thread you still
   need the taskbar's XAML root; the mods I remember get it through taskbar.dll
   symbols (I did not check this against the current catalog), which you just got
   rid of. So I would only do this if option 1 turns out not to be enough.

Longer term, your own note in `TODO.md` is the real fix: read groups, items, `AppId`
and `WindowId` from `TaskbarAppsList` (which the removed shadow branch already
reached), independent of which buttons are realized. That would delete the
provisional-key machinery outright. `SystemRelativeLastActivatedTime` from
`ITaskItem3` would then be a good **seed** at load (ranks present immediately after
enabling the mod), with your min-focus rule taking over from the first real focus.

### 3.3 Focus tracking

- WinEvent plus shell hook with a "is it really foreground" check (L7410-7419) is a
  sound combination, and making the shell hook optional was right.
- The shell hook also delivers `HSHELL_WINDOWDESTROYED`. Use it to drop the window
  from `windowFocusMap`, `windowAppFocus` and `g_windowAppId` and to request a repaint
  immediately. Today a closed window is cleaned up lazily (30 s decay tick, button
  running state, or `ElementClearing` if a flyout is open). It would also be a second
  fix for 2.1.
- `HandleForegroundChanged` (188 lines) takes `g_stateMutex` several separate times
  on one pass through it (inside `RefreshCurrentDesktopId`, at L7235 and L7270, inside
  `ShouldSkipAppMinFocus`, plus the early-exit branches at L7172, L7186, L7211). The worker
  is the only writer of pending state, so it is correct, but one snapshot under one
  lock, then decisions, then one write, would be shorter and easier to prove.
- `QuerySystemAccentColor` (L2121) creates a new `UISettings` on every refresh while
  `g_uiSettings` is already there on the same thread. Use the existing one.

### 3.4 XAML

- Good: own overlay elements only, `InsertAt` everywhere, no native child reordering,
  `IsHitTestVisible=False`, geometry sanity checks before assigning.
- The icon size boost replaces `Icon.RenderTransform` and needs about 200 lines to save
  and restore whatever was there (L3577-3654, L4209-4313, four cache fields, the host
  `Tag`). It is the only place the mod writes a native element's property, it competes
  with the dock-animation mods for the same property, and a 3-10 % bitmap scale blurs
  the icon. Options: (a) remove the feature, (b) default it to 0 so the code only runs
  for people who ask for it, (c) try `UIElement.Scale` with `CenterPoint` instead,
  which composes with `RenderTransform` and needs no save/restore. For (c) check first
  whether Explorer animates the icon's composition visual (press bounce); facade
  properties and direct visual animation of the same property do not mix.
- The two hosts are built with `XamlReader::Load` (L2646, L5383). Constructing a
  `Grid` and two `Rectangle`s in code is shorter and avoids the parser on first paint.
- `ParseHexColor` runs on every paint through `ResolveGlowBaseColor` (L2190). Parse
  once in `LoadSettings` and keep a `Color` in `Settings`.

### 3.5 Unload

I agree with keeping the unbounded waits, and the ordering in `Wh_ModUninit`
(worker first, then drain, then state) is correct. The drain (`ObserveDispatcherOp`,
`DrainOneUiDispatcher`, the two `DispatcherOpWasQueued` overloads, thread handles;
about 200 lines) is the most intricate code in the file relative to how rarely it
runs. I would not change it for 0.12.0. Two notes for later:

- The follow-up you listed (a periodic "still waiting on thread N" log line) is worth
  doing; a silent stuck unload is the only failure mode left.
- If the worker ever stops posting delegates to the UI thread (for example by posting
  a registered window message to the taskbar window and handling it in a subclass),
  only the UI thread's own Low priority posts would need draining, and the dispatcher
  list with duplicated thread handles could go. That is a redesign, not a cleanup.

---

## 4. Complexity

Current largest functions: `ApplyButtonHighlight` 222 lines, `LoadSettings` 214,
`ApplyThumbnailHighlight` 213, `RefreshThumbnailFlyout_UIThread` 198,
`HandleForegroundChanged` 188, `ApplyAllHighlights_UIThread` 185,
`OnMinFocusTimerElapsed` 160. The file has 12 mutexes, 95 `catch (...)` blocks and 97
`Wh_Log` calls. Rough savings are my estimates.

### 4.1 Two copies of the min-focus state machine (about -150 lines)

`OnMinFocusTimerElapsed` / `OnPreviewMinFocusTimerElapsed`, `EnsurePendingAppTimer` /
`EnsurePendingPreviewTimer`, and the paired fields in `PendingFocus`
(`focusStartTick`/`previewStartTick`, `transientRetryStartTick`/`preview...`,
`appConfirmed`/`previewConfirmed`) are the same machine twice: deadline check,
still-foreground check, transient grace, confirm. Make it one small struct
(`start`, `retryStart`, `confirmed`, timer id, seconds getter) with one
`OnDeadline(track)` and two confirm callbacks.

### 4.2 Button state: one accessor, and keep the worker out of it (about -150 lines)

Nine places repeat "find entry by pointer, compare weak ref, erase or create stub"
(`GetCachedButtonIdentity`, `GetCachedPaintState`, `SetCachedPaintState`,
`RememberOurIconScale`, `ClearIconScaleIfOurs` twice, `ApplyButtonHighlight` twice,
`ButtonCountsAsRunning`), each under `g_buttonPathMutex`. A single
`ButtonState* FindButtonState(button, create)` removes most of that.

The mutex exists only because the worker reads the cache in `AppAppearsOnTaskbar`.
Everything else is UI-thread data. If each full pass publishes a small snapshot
("AppIds with a running button", per UI thread, like `g_uncombinedWindowsByThread`),
the per-button state needs no lock at all, the `UpdateVisualStates` path stops taking
a cross-thread mutex four to six times per call, and `GetCachedButtonIdentity` stops
copying a vector of windows on every hover.

### 4.3 One recency model instead of three stores (design direction)

There are three overlapping stores per desktop: `appFocusMap` (per app key),
`windowAppFocus` (per window, app-level confirm, for Never Combine) and
`windowFocusMap` (per window, preview-level confirm). Around them sit provisional
keys, attribution, the per-window split in `RecomputeRanksForDesktopLocked`, and
`perWindow` matching.

If the app-level record were per window only (`hwnd -> {pid, tick, title}`), and the
app were derived at rank time from the current window to AppId map, then:

- a window without a button is simply not ranked yet (no `WINDOW:` keys, no
  attribution step);
- a window whose AppId changes follows it automatically (2.4);
- combined mode ranks by the newest window per AppId, Never Combine ranks the windows
  directly, and switching between them needs no special case.

Cost: an app whose windows are all closed has no history, so relaunching it within
the decay time would wait the min-focus time again instead of promoting immediately.
A small `appId -> lastTick` map keeps that behaviour if you want it. This is a larger
change, so I would only take it up together with 3.2.

### 4.4 Smaller structural items

- **`LoadSettings`** (about -130 lines): `int ClampedInt(PCWSTR name, int lo, int hi)`
  and a tiny string-to-enum table replace 30 if-pairs. The settings log line then
  fits next to it.
- **`ResolveAppIdentity`** has seven out-parameters (L1617). Return a struct.
- **Event watches**: `IconPanelLayoutWatch` and `RepeaterClearingWatch` each have
  their own take / keep / revoke-on-this-dispatcher code (L3232-3290, L4889-4927).
  One generic "weak element + revoker, revoke those owned by this thread" container
  serves both (about -80 lines).
- **`RefreshButtonHighlight`** repeats the same five-line "identity empty: schedule,
  else store rank 0" tail three times (L6410-6417, L6433-6440, L6450-6456).
- **Full pass matching** (L4472-4511): with equality matching a button matches at
  most one rank, so `Cand`, `rankTaken`, `buttonTaken` reduce to one loop that calls
  `IdentityMatchesRank` per running button.
- **Preview resolve** takes `g_stateMutex` twice per card (`tickFor`, `seqFor`,
  L5934-5954) and calls `SettingsSnap()` four times. One lock, one snapshot.
- **`catch (...)`**: most of the 95 blocks swallow silently, and many wrap calls that
  cannot throw. A helper that logs `winrt::to_hresult()` with a tag keeps the safety
  and makes a future Explorer change visible in the log instead of invisible.
- **Enum names**: five `...Name(enum)` switch functions; arrays indexed by the enum
  are a third of the size.

### 4.5 Development layout

Windhawk needs one file, the repository does not. You already generate perf variants
with `make_perf_build.py`. Splitting the source into a handful of parts (settings,
recency, identity, icon paint, preview paint, dispatcher, worker, entry points) that a
script concatenates into the `.wh.cpp` would let the tests include real code instead
of cutting functions out of the text by signature, and would make reviews of a single
area possible. Optional, but at this size it pays for itself.

---

## 5. Performance

From `tests/perf/results/0.11.37-quiet.txt` (399 s): UI thread busy 0.125 %, which is
fine overall. The one number close to a frame is the full pass:
`ApplyAllHighlights` n=255, average 1.37 ms, p95 under 4.1 ms, maximum 3.7 ms. A
240 Hz frame is 4.2 ms.

- Identity capture is not the cost: 9,245 captures at 3.9 us is about 0.15 ms of each
  pass (36 buttons on average). The rest is per-button XAML work. My first suspect is
  `RefreshCachedTaskbarEdge` -> `DetectTaskbarEdge` for **every button in every pass**
  (L4406-4408): two visual state group enumerations, a `RunningIndicator` search and
  several layout reads each. The edge is a property of the taskbar, not the button,
  and the `SizeChanged` watch already refreshes it. Detect once per pass (or not at
  all in the pass) and add a perf slot to confirm.
- Parked buttons get `ClearButtonHighlight` in every pass (L4393), each with an
  `IconPanel` lookup and a child search. Your Never Combine capture (0.11.34) shows 49
  of them. Skip the ones whose cached paint rank is already 0.
- **Are hover-triggered full passes still needed?** The `UpdateVisualStates` hook
  schedules a full pass whenever the hovered button has a rank (L6520-6524). The
  comment says this restores "siblings whose visuals Windows reset". That was true
  when the mod styled `BackgroundElement`; the overlays are own elements now and
  Explorer does not touch them. If a test confirms it, schedule only for unknown rank
  (`rank < 0`) or a pending sweep. That should remove most of the 255 passes.
- Requests from the worker (`RequestApplyVisuals`) bypass the 300 ms debounce, which
  only covers UI-thread requests: in the newest 0.11.37 capture 8 of 40 passes ran
  within 300 ms of the previous one. A per-dispatcher "pass already queued" flag (the
  pattern 0.11.1 used for identity results) would coalesce both sources.
- `FlyoutTargetItemKey` fired 184 times and each call clears every tracked thumbnail
  (three descendant searches per card). Clear only cards that carry our elements, or
  only when the key actually changed.
- Minor: `TrackButton_UIThread` creates a new weak reference and scans the dispatcher
  list on every `UpdateVisualStates` (L3703-3713). Check for an existing entry first.

---

## 6. Functionality suggestions

1. **Ranks right after enabling the mod**: seed from the first button's repeater (3.2).
   This is the most noticeable rough edge for a new user trying the mod.
2. **Persist recency across an Explorer restart or mod update.** AppIds are stable
   strings now, which they were not when keys were paths and window handles. Saving
   the top few `(AppId, age)` pairs per desktop to mod storage on unload (and perhaps
   every few minutes) is small and would make highlights survive a restart.
3. **Closed windows free their slot immediately** (`HSHELL_WINDOWDESTROYED`, 3.3).
4. **Ranks beyond 3** all use the rank 3 intensity, while the count can be set to 16.
   Either interpolate down from rank 3 or cap the count at what is distinguishable
   (the description already says 1-6).
5. **Settings surface**: 32 settings is a lot for a highlight. Candidates to remove or
   merge: `glowRoundness` (1.4), `glowLayers` (two values, side bar only), three
   `sizeBoostRank*` (one value, or none, see 3.4), `outlineFillOpacity` (could follow
   `fillOpacity`). Removal needs no transition under your compatibility rule.
6. **Exclude list by AppId**: add one log line per newly seen app that prints the
   three accepted spellings (exe name, path, AppId), so users can copy one.
7. **ARM64**: the mod is `x86-64` only. If the symbols resolve on ARM64 Explorer it
   may only need the `@architecture` line and a test; if not, say so in the README.

---

## 7. For the PR description

- Lead with what was removed: every taskbar.dll hook and private offset, the click
  hook, the async resolver, fuzzy and path matching. Then the dependency list from
  3.1, so the maintainer sees the whole private surface in one place.
- State the identity rule in one sentence (Explorer's AppId of the button; no button,
  no rank) and the two user-visible consequences: `requireTaskbarButton` is gone, and
  the exclude list accepts an AppId.
- Settings: the `previews.style` -> `topStyle`/`restStyle` transition and that it ends
  at the first save.
- Perf: the since-load line from the quiet run and the full-pass numbers.
- Known limitations: ranks appear after the first taskbar state change when loaded
  into a running Explorer (if not fixed by then), SOLIDWORKS-style document cards,
  registry desktop lag (if 2.3 is not done), unbounded unload waits and why.
- Mention the line count change against the merged version; the earlier reviews
  asked for it to go down.

---

## 8. Small items

- L8090: indentation of `return FALSE;`.
- L829-834: `WM_APP + 6` is skipped (leftover numbering).
- L1195: `ToUpper` uses `LOCALE_NAME_USER_DEFAULT`. For identifiers
  `LOCALE_NAME_INVARIANT` states the intent. As far as I know the result is the same,
  because `LCMAP_UPPERCASE` without `LCMAP_LINGUISTIC_CASING` uses file-system casing.
- L5749, L5765: inner `fillA` shadows the outer one; the name `usedNative` now means
  "painted with the card overlay".
- L3875-3884: the public/empty tally was a migration aid; it can go with the PR.
- `AGENTS.md` is 78 KB. Agents read it on every session; an index plus per-topic files
  would cost less context and go stale more slowly.
