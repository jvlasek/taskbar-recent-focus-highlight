# Catalog review: what the reviewers check, and lessons not to regress

Read before preparing a windhawk-mods PR or answering a review round, and
when a change touches unload, threading or the hover path. The rules
themselves live in the topic files; this file is the reviewers' view and the
incident log. Rows about mechanisms that no longer exist are in `history.md`.
Latest overall review: `reviews/fable.md` (0.11.37).

PR #5331 is two-stage: Claude (`/ai-review`, you run it) then a human
(`/ready-for-reviewer`). The AI re-reads the **whole** `.wh.cpp` every round.
Required items must be fixed or declined in the PR comment with a reason.
Optionals/functionality notes are “your call” — it will still re-mention
size, leftover heuristics, and English strings until you explicitly park them.

It is not reviewing visual taste (4px vs 8px wash). It is reviewing whether
Explorer can survive Disable, whether the taskbar UI thread stays cheap, and
whether the mod is a catalog citizen (copy existing mods, fail closed, small
surface). A “fix” that adds a new poll, global, or name-match becomes the
next round’s required finding.

## Invariants (re-checked every round)

1. **Unload is total.** When `Wh_ModUninit` returns, no thread, `WndProc`,
   `SizeChanged`, or `TryRunAsync` lambda may still live in the image.
   Unbounded waits. **Stop the worker first**, then drain UI. Ready event
   before `Start` returns. Mod `hInstance` for the message class;
   `ERROR_CLASS_ALREADY_EXISTS` is fatal. No strong XAML in CRT-destroyed
   globals. `TryRunAsync` “queued?” is `GetResults()` / `Status()`, not
   `static_cast<bool>(op)` (that hang uninit). Cite: `taskbar-clock-customization`
   join, `taskbar-vertical` TryRunAsync, wiki “Global objects and process
   shutdown”. Identity/View symbol hook failure at init is `return FALSE`
   except Taskbar.View not loaded yet (LoadLibrary retry).
2. **Own your threads and apartments.** `SetTimer` only on the HWND’s owner.
   COM (`IVirtualDesktopManager`, `SHGetPropertyStoreForWindow`) on one STA,
   never under a mutex the UI thread takes to paint. No
   `weak_ref<FrameworkElement>::get()` off the UI thread. Capture agile
   `CoreDispatcher` on the UI thread. Primary and secondary taskbars share
   one Explorer UI thread; `HasThreadAccess` after `weak.get()` is a skip,
   not a second-thread partition.
3. **Do not tax the shell UI thread.** `UpdateVisualStates` is hover-hot.
   No `ReportClicked`, `EnumWindows`, property-store, `UISettings`, or
   registry on that path. Coalesce flyout work to one Low pass. Cache
   accent/desktop id. Debounce with a deadline (skip re-arm), not a reset
   that can postpone forever. Cite: `taskbar-volume-control-per-app` only
   probes identity from real input.
4. **Do not guess identity from UI strings.** Automation names are
   localized. No English `" running"` / `" pinned"` as logic, no `LISTER`
   special cases, no filename-900, no fuzzy initials. Explorer AppId /
   HWND only. Preview unique-title is gone (`stash/preview-unique-title.cpp`).
   Wrong glow is worse than none.
   Preview cards: Explorer's own card view model only.
5. **Do not fight native template / other mods.** Own-named overlays.
   Don’t `ClearValue` native fills/visibility. Don’t reorder `IconPanel`
   native children or rewrite their ZIndex. Position only our host with
   explicit InsertAt (including at end); remove it when unranked and on unload. Don’t wipe another mod’s `ScaleTransform`. Plate tints
   save/restore the previous brush.
6. **Catalog packaging.** The YAML README is the store page (screenshots on
   `raw.githubusercontent.com`, no tester checklist, no “see the repo
   README”). YAML defaults are real — a user `0` is not “unset”. No in-mod
   enable/debug toggle (`Wh_Log` + Windhawk Advanced). Nested setting
   groups. Keep `-loleaut32`. Thumbnail hooks `optional` so older builds
   still load.
7. **Surface area.** Line count is a finding. Prefer delete or copy a
   cited catalog pattern over another helper/fallback. Don’t add a probe
   to close a bug (that probe becomes the next required item).

## How to spend fewer rounds

- Close every **required** item in the same reply: patch or a short “won’t
  — because …”. Silence is treated as unaddressed.
- While closing a finding, do not introduce a new global, timer, or
  string-match. The next `/ai-review` will treat that as new work.
- Copy the mod they cited (clock uninit, volume-per-app click probe,
  thumbnail-reorder GetAt, audio-scroll `RegisterClass`, accent-color-sync
  YAML) instead of inventing a variant.
- Fail closed (no glow) rather than a new heuristic.
- Unique-title is dropped (fail closed on preview holes).
- Optionals: take the cheap ones or say skip. Size complaints: don’t add
  a helper for a one-token bug (`CornerRadius{4,4,4,4}` not `{4}`).

Incident-level rows below are the concrete hits from those invariants.
Do not regress them; do not grow the table instead of following the
buckets.

## Lessons (do not regress)

These were flagged on ramensoftware/windhawk-mods PR #5331. The pattern is:
Explorer-injected mods must be unload-safe, keep COM/XAML on the right thread,
and must not guess identity from localized UI strings.

| Lesson | Why it bit us | Rule |
|--------|----------------|------|
| Uninit **worker first**, then UI drain | Drain then stop: decay/`RequestApplyVisuals` `TryRunAsync` after the Low sentinel → `FreeLibrary` then crash | `g_unloading`; stop focus thread; then `RunOnEachUiDispatcherAndWait` |
| Unbounded drain, not a timeout | Timed-out drain leaves `SizeChanged` in the image | INFINITE wait for dispatcher join **and** focus thread |
| Ready event before `Start` returns | `PostThreadMessage` fails until the worker has a queue → INFINITE hang on Disable | Manual-reset event after `CreateWindowExW` (and on every early-fail path) |
| Mod `hInstance` for the message class | `GetModuleHandle(nullptr)` is explorer.exe; leaked class + dangling `WndProc` | `GetModuleHandleExW(FROM_ADDRESS \| UNCHANGED_REFCOUNT)` |
| `ERROR_CLASS_ALREADY_EXISTS` is fatal | Reusing a previous instance’s class jumps into unmapped memory | Fail the thread; do not `CreateWindow` on a stale class |
| No strong XAML in CRT-destroyed globals | Process teardown runs `~vector<CoreDispatcher>` after XAML threads are dead | `[[clang::no_destroy]] optional<…>` + `reset()` in Uninit |
| No `weak_ref<FrameworkElement>::get()` off UI | Last-ref `~FrameworkElement` on the worker/uninit thread | Store agile `CoreDispatcher` captured on the UI thread |
| No `SetTimer` cross-thread | HWND belongs to the focus thread; debounce never armed | `PostMessage` → `SetTimer` in `WndProc` |
| No COM under `g_stateMutex` | UI takes that mutex to paint; hung `SHGetPropertyStoreForWindow` freezes the taskbar | Resolve class/AUMID/path **then** lock |
| VDM on the focus thread only | STA `CoCreate` on one thread, use/release on another | Registry first; VDM fallback on the worker; UI reads cached GUID |
| Accent / desktop id not per-paint | `UISettings` + registry on every UVS; broadcast never reaches `HWND_MESSAGE` | Cache accent in `std::atomic<uint32_t>` outside `Settings`; `UISettings::ColorValuesChanged` → `PostToHookThread`. Desktop id from switch + decay |
| Identity-map keys need a live weak_ref | Raw `IUnknown*` is reused when Explorer reallocates a button | Erase the entry unless `weak.get() == this button` |
| Identity-keyed maps, not linear `weak.get()` | Four COM-resolving scans per UVS per button | `unordered_map<IUnknown*, …>`; keep the map, skip paint on unranked |
| No native collection reordering | Historical clears/paints healed native z-order | Move/remove only the owned host. Use explicit InsertAt even at end so insertion/removal notify deferred bookkeeping |
| No `SizeChanged` without a drainable dispatcher | `RememberUiDispatcher` can fail; uninit never revokes that watch | Register the watch only if the dispatcher was recorded |
| No icon name fuzzy | English `" running"` / `" pinned"`, `LISTER`/`VSCODIUM` special cases, wrong glow | Explorer AppId / HWND only. Preview unique-title dropped |
| No in-mod enable / debug toggles | Duplicates Windhawk’s mod on/off and Advanced logging | Drop `enabled` and `glowDebugLog`; use `Wh_Log` |
| Keep `-loleaut32` even if we do not call `Sys*` | WinRT `hresult_error` needs `SysFreeString` / `SysStringLen` | Drop `-lpropsys` if unused; do not drop oleaut32 |
| No `UpdateLayout` from `OnApplyTemplate` | Synchronous layout during measure is a XAML layout cycle | Return false + `SchedulePreviewFlyoutRefresh` at Low |
| Overlay sweep does not heal native z-order | `g_pendingOverlaySweep` re-opened the unranked reorder path | Sweep only removes `WhRecentFocusGlow` |
| UVS must not clear on overlay sweep | Desktop switch / decay set the flag then UVS blanked ranked icons until `ApplyAllHighlights` | Paint cached rank; the full bind is the sweep |
| Paint cache includes edge + size | SizeChanged updated `lastEdge` then `ApplyButtonHighlight` early-out | Key is rank + settings + accent + edge + panel size |
| Paint rank **-1** ≠ **0** | First UVS scored an unresolved button as 0 and skipped the full bind | Unknown identity stays -1 and schedules rebind; 0 only after a real resolve said “no rank” |
| Own `ScaleTransform` instance | `ClearValue` wiped `taskbar-dock-animation`. A `weak_ref` to the displaced transform goes null if Icon held the last strong ref. | Remember the object we set; keep the previous transform on the glow host Tag (tree-owned, like the preview plate brush); restore only while current == ours |
| YAML defaults are real | All-zero preview intensities must not be “unset” | Do not override user 0s after an in-place recompile |
| README is the catalog page | Users never see the repo README | Screenshots on `raw.githubusercontent.com`; no tester checklist |
| `lastHwnd` needs a PID | HWND values recycle; icon bind would follow the new owner | Store `lastPid`; `HwndMatchesStoredPid` before HWND identity |
| Flyout refresh uses a stale card | Shared pending latch + oldest `g_trackedThumbViews` weak_ref | Coalesce onto one Low pass; prefer the card that scheduled it if still in a repeater |
| Transient min-focus poll | Remaining=0 re-armed 200 ms forever while File Explorer / desktop held FG | Grace = min-focus (min 2 s) of 200 ms polls, then drop the candidate |
| File Explorer never ranked | `IsOwnExplorerProcess` + `EXPLORER.EXE` skipped folder windows | Allow `CabinetWClass` / `ExploreWClass`; tray/desktop stay `ShouldIgnoreHwnd` |
| Decay timer is not a heartbeat | 30 s tick with empty maps is wasted registry/COM work | Arm on first confirm; stop when every desktop map is empty |
| Hover-storm debounce reset | Each `WM_APP_REQUEST_APPLY_DEBOUNCED` restarted the 300 ms timer | Skip `SetTimer` if the full-rebind timer is already armed |
| Preview host re-parsed | `ApplyThumbnailHighlight` `ClearThumbnailHighlight` dropped the overlay host every paint | Reset plate + hide children; keep `WhRecentFocusThumbGlow` |
| `lastRunningTick` is not a heartbeat | 400 ms freshness dropped idle ranks on decay / settings / desktop switch; empty-rank apply skipped the UI snapshot | `observedRunning` until the UI sees not-running; grace only after that; `ApplyAllHighlights` snapshots `IsRunning` then recomputes, even with no ranks |
| Settings change cancelled min-focus | `WM_APP_SETTINGS_CHANGED` `KillTimer` left a valid pending candidate with no clock | Drop only an excluded candidate; re-arm remaining app/preview deadlines for an allowed one |
| Intensity × fill × opacity | Brush alpha and element Opacity both multiplied by rank `t` made 60% look ~36% | Rank intensity is Opacity (or native-plate brush alpha) once; fill/stroke setting is the other |
| Drain Low callback is not completion | Low `SetEvent` let unload proceed while `Completed` still ran mod code | Unload returns only after the Low sentinel callback ran, or the original UI thread's retained handle is signaled. A terminal Low op with `GetResults()==false`, High completion, a timeout, or a failed status read is not that proof |
| Weak prior transform | Displacing `Icon.RenderTransform` dropped the last strong ref; restore used `ClearValue` | Glow host Tag holds the previous transform for the takeover |
| Win32 AUMID exclusion | Pending/timer/preview history used path+filename only; window AUMID exclusions did not drop them | Same `IsExcludedKey(..., appId)` for admission, pending, confirm, and history; resolve AUMID outside `g_stateMutex` |
| Dead path-cache row stays eligible | `observedRunning` survived after the button was destroyed; full bind never saw `IsRunning=false` | Erase null button weaks on the UI thread before eligibility. Do not `weak.get()` in `AppAppearsOnTaskbar` |
| Clear stores rank 0 before the rerank test | Closing a glowing button skipped `ScheduleRefreshAllHighlights`; decay compared only the rank count | Read the paint rank before `ClearButtonHighlight`. Decay compares the ordered rank keys |
| Second transform takeover restores the first | Host Tag was written only while unset, so a later owner was overwritten and the first snapshot came back | On each takeover, replace the Tag and the origin flags together. Restore only while the current transform is still ours |
| Due deadline re-arm skips grace | `remaining == 0` used `Immediate`, which drops a candidate while Alt-Tab still holds foreground | Resume a positive elapsed minimum with `FromTimer`. Keep `Immediate` for a zero minimum, promote skip, and preview click |
| Group HWND has no PID | A cached group handle matched a rank before path/AUMID, so a recycled HWND lit the old button | Store the PID captured with each group window and the sample. Match only while `HwndMatchesStoredPid` holds |
| Preview skip used any nonzero tick | A reused or decayed HWND skipped preview min-focus | `windowAlreadyTracked` is `IsWindowRecentForPreviewLocked` (live, PID, not decayed) |
| Full bind required the scheduling button | The Low callback returned when that button was already destroyed, so the dead-row prune never ran | The anchor only picks the dispatcher. The queued bind runs until unload |
