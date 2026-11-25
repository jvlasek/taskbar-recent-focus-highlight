# Comprehensive Pre-Publish Review of v0.12.1

Reviewer: Gemini 4 Neon, 2026-11-25.  
Source: `src/00-metadata.cpp`–`src/21-entry-points.cpp` joined into `taskbar-recent-focus-highlight.wh.cpp` at commit `1feda77` (8,730 lines across 22 parts).

## Scope & Method

- Read all 22 source parts in `src/` (every line), `AGENTS.md`, every topic file under `doc/agents/` (including `history.md` and `review-lessons.md`), `doc/code/state-machines.md`, `doc/appid-identity-design.md`, `doc/investigation-notes.md`, `doc/generate-runtime-flow.py`, `doc/pr-0.12.1.md`, `TODO.md`, `README.md`, `reviews/fable.md`, and the four reference mods in `example/`.
- Verified build parity and ran the offline Python checks (`run-build-tests.py`, `run-appid-identity-tests.py`, `run-layout-footprint-tests.py`, `run-icon-order-tests.py`, `run-shell-activation-tests.py`, `run-review-helper-tests.py` — all passed).
- **Nothing was compiled against Windhawk/Explorer or run live.** Every finding below comes from static analysis of the code, ABI assumptions, and state transitions. Where a finding depends on Windows runtime behavior (e.g. Taskbar Styler themes or multi-monitor layouts), that boundary is stated explicitly.

---

## Executive Summary

The evolution from 0.11.37 (Fable's review) to 0.12.1 is an impressive piece of engineering. Removing all `taskbar.dll` hooks, moving to synchronous Explorer AppId capture, deriving app records from per-window focus records, unifying app and preview deadlines into `MinFocusTrack`, gating focus tracking on `Taskbar.View.dll` so non-taskbar `explorer.exe` processes stay idle, and making every overlay zero-desired-size have eliminated whole classes of historical fragility.

That said, I found **several real bugs and subtle edge cases** — mostly in the new 0.12.1 features (last-use tracking), in unload/dispatcher invariants that Windhawk's `/ai-review` specifically audits, in the `ITaskGroup` capture hook shared with other mods, and in taskbar edge detection under Never Combine / `taskbar-icon-size`. None require architectural rewrites; all of the pre-publish items are small, surgical fixes.

### Triage Before Publishing 0.12.1

| Priority | Item | Where | Why it matters |
|---|---|---|---|
| **Fix before publish** | **1.1** Last-use (`g_inFrontHwnd`) breaks after Alt+Tab / Start / Lock, on `HSHELL_WINDOWDESTROYED`, and on same-app window switches | `src/18-focus-promotion.cpp` | Actively used app can decay while in use — directly undermines 0.12.1's headline feature |
| **Fix before publish** | **1.2** `LastInputTick64` 32-bit unsigned underflow on clock race (`lii.dwTime > now`) | `src/18-focus-promotion.cpp:452` | Returns 0 or 49.7 days in the past (especially on machines with >49.7 d uptime) |
| **Fix before publish** | **1.3** Non-RAII `g_captureTaskGroup` + raw un-AddRef'd `ITaskGroup` pointer | `src/12-button-identity.cpp:200` | Exception bricks `ITaskGroup::IsRunning` for Explorer & `taskbar-volume-control-per-app`; raw pointer outlives `groupViewModel` |
| **Fix before publish** | **1.4** Unload/dispatcher gaps (`WatchRepeaterElementClearing_UIThread`, `Schedule*`, `put_ThumbnailSource_Hook`, `UISettings::ColorValuesChanged`, `ObserveDispatcherOp`) | `src/07-colors.cpp`, `src/13-previews.cpp`, `src/14-preview-overlay.cpp`, `src/16-ui-dispatch.cpp`, `src/17-taskbar-view-hooks.cpp` | High probability of being flagged by Windhawk `/ai-review` under the "Unload is total" invariant |
| **Fix before publish** | **1.5** `HwndMatchesStoredPid` / `LookupWindowAppId` fail open when `pid == 0` | `src/02-state.cpp:99`, `src/04-helpers.cpp:209`, `src/06-window-recency.cpp:45` | Violates fail-closed / handle-recycle rule |
| **Fix before publish** | **1.6** `EnsureVdm()` returns raw STA `g_vdm` off the hook thread; `Wh_ModSettingsChanged` calls `SHGetPropertyStoreForWindow` without guaranteed COM init | `src/03-virtual-desktops.cpp:41`, `src/21-entry-points.cpp:151` | Apartment safety + silent exclude-list miss |
| **Fix before publish** | **1.7** Small verified bugs: `t <= 0` plate cache thrash, `EnsureButtonIdentity` null deref, retargeted exclusion leak, `HandleLoadedModuleIfTaskbarView` missing `g_unloading` check, `Wh_Log` under locks | `src/06`, `src/08`, `src/11`, `src/12`, `src/17`, `src/18` | Easy, low-risk hardening |
| **Fix before publish** | **5.1** Version/doc sync (`doc/investigation-notes.md`, `doc/generate-runtime-flow.py` / `.svg`, `doc/code/state-machines.md`, `README.md`, orphaned comments) | `doc/`, `README.md`, `src/02`, `src/13` | Required by `AGENTS.md` version-sync rule; avoids misleading reviewers |
| **Should fix (or 0.12.2)** | **2.2 / 4.3** `DetectTaskbarEdge` `pw > ph + 4.0` trap with Never Combine / `taskbar-labels` / `taskbar-icon-size`, and single `passEdge` across multi-monitor taskbars | `src/10-taskbar-edge-layout.cpp:88`, `src/12-button-identity.cpp:645` | Can flip side bar onto the native running pill on short/wide horizontal buttons |
| **Should fix (or 0.12.2)** | **1.8 / 2.1** `TaskListButton_IsRunning` calls `produce<..., ITaskListButton>` with `as<IUnknown>()`; `Wh_ModInit` returning `FALSE` on `highlightCount=0 && !previews` | `src/08-identity-matching.cpp:14`, `src/21-entry-points.cpp:12` | Future-build ABI safety + settings UX trap |
| **Post-0.12.1** | `TaskbarAppsList` + `ITaskItem3::SystemRelativeLastActivatedTime`; `WTSRegisterSessionNotification`; replace `XamlReader::Load`; per-thread `ownerThreadId` on watch maps | Various | Architectural improvements with trade-offs documented below |

---

## 1. Bugs & Safety Hazards

### 1.1 Last-use tracking (`g_inFrontHwnd` / `g_inFrontSince`) has four state-machine breaks

The 0.12.1 "last use" feature (`src/18-focus-promotion.cpp:448-545`) is a major UX improvement, and the guard `tick >= g_inFrontSince` is clever (short visits below min-focus cannot bump recency). However, tracing it across the rest of the focus engine reveals four concrete failure modes:

1. **Transient foreground permanently detaches `g_inFrontHwnd` from the active app.**  
   `HandleForegroundChanged` calls `NoteForegroundForLastUse(hWnd)` at `src/18-focus-promotion.cpp:554` **before** checking `IsTransientForeground(hWnd)` at L565.
   - Suppose the user is working in confirmed App A (`g_inFrontHwnd = A, g_inFrontSince = t0`).
   - At `t = 10 min`, the user opens Alt+Tab, Start Menu, Search, Clock flyout, IME, or locks the PC (`LOCKAPP.EXE`), and then dismisses it back to App A.
   - `NoteForegroundForLastUse(T)` stamps App A at `t = 10 min`, then sets `g_inFrontHwnd = T`.
   - As `doc/agents/recency.md:75` documents, *"the landed app often does not get a second `EVENT_SYSTEM_FOREGROUND`"* (the exact reason transient grace exists!).
   - Result: `g_inFrontHwnd` remains `T`. On every subsequent 30 s decay tick, `RefreshInFrontLastUse()` (`src/18-focus-promotion.cpp:536`) checks `if (!fg || fg != g_inFrontHwnd) return;` — `fg` is `A`, `g_inFrontHwnd` is `T` — and **returns early forever**. App A decays after 30 minutes while the user is actively typing in it.
   - The exact same break occurs when a brand-new App A confirms via the 200 ms transient-grace poll after a brief Alt+Tab: `g_inFrontHwnd` is stuck on `MultitaskingViewFrame`, not `A`.

2. **`HandleWindowDestroyed` does not clear `g_inFrontHwnd`.**  
   In `src/18-focus-promotion.cpp:857-901`, destroying `g_inFrontHwnd` leaves the stale handle stored. If that HWND is recycled before another foreground event, `NoteForegroundForLastUse` hits `if (hwnd == g_inFrontHwnd) return;` and keeps the dead window's `g_inFrontSince`.

3. **Same-app window switch while `appConfirmed == true` leaves the new window out of `windowAppFocus`.**  
   Suppose `promoteMode = ImmediateTracked` (default) and `previewMinFocusSeconds = 1` (default). User focuses window `A1` of App A -> app confirms immediately (`appConfirmed = true`, `windowAppFocus[A1]` stamped), while `g_pendingFocus` stays `valid = true` waiting 1 s for `previewConfirmed`. Within that 1 s, the user switches to `A2` (same AppId).  
   `HandleForegroundChanged` enters `sameAppPending` (`src/18-focus-promotion.cpp:672-693`): `g_pendingFocus.hwnd` changes to `A2`, `hwndChanged = true`, `previewConfirmed = false`, **but `appConfirmed` stays `true`**! `EnsurePendingAppTimer()` immediately returns because `g_pendingFocus.appConfirmed` is true (`L386`).  
   Consequence: `windowAppFocus[A2]` is **never created**. Under Never Combine, `A2` gets no per-window slot; under combined mode, `g_inFrontHwnd` is `A2`, so `RefreshInFrontLastUse()` never finds `A2` in `windowAppFocus` and App A decays while the user works in `A2`.

4. **Last-use stamp lost if the window closes before the next recompute.**  
   `NoteForegroundForLastUse` updates `desk.windowAppFocus[hwnd].tick = at`, not `desk.appFocusMap`. If the user switches away from App A (stamping `windowAppFocus[A]` with `at`) and closes `A` before the next `RecomputeRanksForDesktopLocked` (e.g. within 30 s, or closes `A` directly via taskbar close while it's in front so `HandleWindowDestroyed` runs before `NoteForegroundForLastUse`), `HandleWindowDestroyed` erases `desk.windowAppFocus[A]` (`L872`), so `desk.appFocusMap[key].lastConfirmedFocusTick` never receives `at`.

**Suggested fixes:**
- Keep `g_inFrontHwnd` focused on the last **real** (non-transient) foreground window, or track a separate `g_inFrontConfirmed = bool` / re-sync `g_inFrontHwnd` on confirm (`OnMinFocusTimerElapsed` / `OnPreviewMinFocusTimerElapsed`) when `confirmHwnd` is still foreground. Specifically:
  - When `IsTransientForeground(hWnd)` is true, stamp `g_inFrontHwnd` with `LastInputTick64(now)` (so we don't credit idle time spent on the lock screen or Start menu) and set a `g_inFrontPaused = true` flag (or only overwrite `g_inFrontHwnd` if the transient is `LOCKAPP.EXE` / a real leave). When `RefreshInFrontLastUse()` sees `fg == g_inFrontHwnd` again (user returned without a second foreground event), resume refreshing it — using `at > g_inFrontPausedAt` so time *while* on LockApp isn't credited, only fresh input after returning!
  - Better yet, in `OnMinFocusTimerElapsed` / `OnPreviewMinFocusTimerElapsed`, if `confirmHwnd == NormalizeFocusHwnd(GetForegroundWindow())`, ensure `g_inFrontHwnd = confirmHwnd` and `g_inFrontSince <= now`.
- In `HandleWindowDestroyed(hwnd)`: if `g_inFrontHwnd == hwnd`, stamp last use (while `windowAppFocus[hwnd]` still exists) and fold `w.tick` into `appFocusMap[w.key]` if `w.tick > app.lastConfirmedFocusTick`, then clear `g_inFrontHwnd = nullptr; g_inFrontSince = 0;`.
- In `HandleForegroundChanged` (`sameAppPending` + `hwndChanged` + `g_pendingFocus.appConfirmed`): stamp `windowAppFocus[hWnd]` (or reset `appConfirmed = false` and let `ShouldSkipAppMinFocus` re-confirm it) so `A2` always gets its own `windowAppFocus` record.

---

### 1.2 `LastInputTick64` 32-bit unsigned underflow on clock race

In `src/18-focus-promotion.cpp:452-462`:
```cpp
ULONGLONG LastInputTick64(ULONGLONG now) {
    LASTINPUTINFO lii{};
    lii.cbSize = sizeof(lii);
    if (!GetLastInputInfo(&lii)) {
        return 0;
    }
    // dwTime is GetTickCount (32-bit); the difference survives wrap.
    const ULONGLONG ago = static_cast<DWORD>(static_cast<DWORD>(now) -
                                              lii.dwTime);
    return ago < now ? now - ago : 0;
}
```
In `NoteForegroundForLastUse` (`L513-515`), `now = GetTickCount64()` is captured *before* `GetLastInputInfo(&lii)`. If a keystroke or mouse event happens between those two calls (or if `GetLastInputInfo`'s tick is 1 tick = 15.6 ms ahead of `now`), `lii.dwTime > static_cast<DWORD>(now)`.

Because the subtraction is unsigned 32-bit, `static_cast<DWORD>(now) - lii.dwTime` wraps to `~0xFFFFFFFF` (~49.7 days):
- On machines with uptime `< 49.7 days`: `ago < now` is false -> returns `0` (update silently skipped).
- On machines with uptime `> 49.7 days` (common with Windows Fast Startup / hibernate): `ago < now` is **true** -> returns `now - 49.7 days` (and logs `"used until 4294967s ago"` if it ever moved anything).

**Fix:** Capture `now = GetTickCount64()` *after* `GetLastInputInfo(&lii)` (so `now >= lii.dwTime` in the normal case), and still interpret the 32-bit difference as signed:
```cpp
const int32_t diff = static_cast<int32_t>(static_cast<DWORD>(now) - lii.dwTime);
if (diff <= 0) {
    return now;  // input happened right now (clock race)
}
const ULONGLONG ago = static_cast<uint32_t>(diff);
return ago < now ? now - ago : 0;
```
(This handles idle spans up to ~24.85 days cleanly and never underflows on a clock race.)

---

### 1.3 `GetWindowsUdkTaskGroupFromTaskListButton`: non-RAII hijack flag + un-AddRef'd `TaskGroup` pointer

In `src/12-button-identity.cpp:172-180` and `200-232`:
```cpp
bool WINAPI ITaskGroup_IsRunning_Hook(void* pThis) {
    if (g_captureTaskGroup) {
        g_capturedTaskGroup = *(void**)pThis;
        return false;
    }
    ...
}

void* GetWindowsUdkTaskGroupFromTaskListButton(UIElement element) {
    ...
    winrt::com_ptr<IUnknown> groupViewModel;
    TryGetItemFromContainer_TaskListGroupViewModel_Original(
        groupViewModel.put_void(), &element);
    if (!groupViewModel) {
        return nullptr;
    }
    g_capturedTaskGroup = nullptr;
    g_captureTaskGroup = true;
    TaskListGroupViewModel_IsMultiWindow_Original((void**)groupViewModel.get() - 1);
    g_captureTaskGroup = false;
    ...
    return g_capturedTaskGroup;
}
```
Two concrete safety hazards here (inherited from `taskbar-volume-control-per-app`):

1. **Exception leaks `g_captureTaskGroup = true` permanently on the taskbar UI thread.**  
   `CaptureButton` wraps the call in `try { ... } catch (...)` (`L255`), so if `IsMultiWindow_Original` throws a WinRT/C++ exception before reaching `L215`, `g_captureTaskGroup` stays `true` on that UI thread **forever**. Every subsequent `ITaskGroup::IsRunning` call by Explorer itself (and by `taskbar-volume-control-per-app`) will then return `false`, breaking running indicators across the entire taskbar until Explorer restarts.
2. **Dangling COM pointer lifetime.**  
   `g_capturedTaskGroup` is stored as a raw `void*` without `AddRef`. Then `groupViewModel` goes out of scope at `L231`, releasing the view model before the caller (`CaptureButton` at `L273-275`) calls `QueryInterface` on `windowsUdkTaskGroup`. If the button was concurrently torn down or recycled and `groupViewModel` held the last reference to the `TaskGroup`, `windowsUdkTaskGroup` is a use-after-free.

**Fix:** Return `winrt::com_ptr<::IUnknown>` (calling `copy_from_abi` / `AddRef` inside `ITaskGroup_IsRunning_Hook` while `pThis` is guaranteed live, and clearing `g_capturedTaskGroup` on exit) and guard `g_captureTaskGroup` with a 4-line RAII scope guard.

---

### 1.4 Unload & Dispatcher Invariant Gaps (Likely `/ai-review` Findings)

Your unload discipline is already among the best in the Windhawk ecosystem, which is why the remaining small holes stand out — Windhawk's `/ai-review` checks these invariants every round (`doc/agents/review-lessons.md:23-38`):

1. **Subscribing/posting without `RememberUiDispatcher` succeeding.**  
   `doc/agents/review-lessons.md:110` states the rule: *"No `SizeChanged` without a drainable dispatcher — Register the watch only if the dispatcher was recorded."*
   - `EnsureIconPanelLayoutWatch` (`src/10-taskbar-edge-layout.cpp:460`) obeys this.
   - **`WatchRepeaterElementClearing_UIThread`** (`src/13-previews.cpp:243`) does **not** call `RememberUiDispatcher(repeater)` at all before subscribing `repeater.ElementClearing(...)`.
   - **`TaskItemThumbnailView_put_ThumbnailSource_Hook`** (`src/17-taskbar-view-hooks.cpp:210-216`) calls `SchedulePreviewFlyoutRefresh(card)` without calling `TrackThumbView_UIThread(card)` or `RememberUiDispatcher(card)`.
   - **`TrackThumbView_UIThread`** (`src/13-previews.cpp:402`) calls `RememberUiDispatcher(thumbView)` and ignores its `bool` return value.
   - **`SchedulePreviewFlyoutRefresh`** (`src/14-preview-overlay.cpp:391`) and **`ScheduleRefreshAllHighlights`** (`src/17-taskbar-view-hooks.cpp:108`) post Low callbacks to `dispatcherAnchor.Dispatcher()` without verifying `RememberUiDispatcher(dispatcherAnchor)` succeeded.  
   **Fix:** Gate `WatchRepeaterElementClearing_UIThread`, `SchedulePreviewFlyoutRefresh`, and `ScheduleRefreshAllHighlights` with `if (!RememberUiDispatcher(el)) return;`, and call `TrackThumbView_UIThread(card)` in `put_ThumbnailSource_Hook`.

2. **`UISettings::ColorValuesChanged` fires on a WinRT threadpool thread and cannot be drained on unload.**  
   In `src/07-colors.cpp:94-95`, `ColorValuesChanged` registers a lambda invoked on an MTA/threadpool thread. Revoking the event token in `UnsubscribeAccentChanges()` (`L106`) removes future notifications, **not** an invocation that WinRT's `EventSource` already snapshotted and is executing concurrently on a threadpool thread. If Windows changes theme/accent right as `Wh_ModUninit` finishes, that threadpool thread can still be inside the mod's `.text` section after `FreeLibrary`.  
   **Clean fix (see §2.2.1):** Since 0.11.0 the worker window is a real top-level `WS_POPUP` window, not `HWND_MESSAGE` — it receives `WM_SETTINGCHANGE` (`L"ImmersiveColorSet"`) and `WM_DWMCOLORIZATIONCOLORCHANGED` directly in `HookThreadWndProc`, with **zero** WinRT event delegates and **zero** unload hazard!

3. **`ObserveDispatcherOp` early return on `ThreadExited` while `Completed` delegate is attached.**  
   In `src/16-ui-dispatch.cpp:95-107`:
   ```cpp
   if (subscribed) {
       HANDLE waits[] = {done, ui.thread.get()};
       for (;;) {
           const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
           if (result == WAIT_OBJECT_0 + 1) {
               return DispatcherOpEnd::ThreadExited;
           }
   ```
   The comment at `L105` says *"Do not close the event while its completion delegate can run."* Yet if `ui.thread` exits (`WAIT_OBJECT_0 + 1`), `ObserveDispatcherOp` returns immediately, `RunOnEachUiDispatcherAndWait` closes `winrt::handle done` (`L191`), and `Wh_ModUninit` returns — even if the `IAsyncOperation` transitioned to `Canceled`/`Error` and queued its `Completed` delegate `[done](...) { SetEvent(done); }` on a COM/WinRT rundown thread.  
   (On Windows, `CoreDispatcher` usually cancels operations inline on thread shutdown before the thread terminates, so this window is very narrow, but worth knowing: polling `op.Status()` every 20 ms during unload — which `ObserveDispatcherOp` already implements at `L109-127`! — or waiting for `done` if `op.Status() == Started` avoids attaching any DLL-resident delegate to `op.Completed` at all.)

4. **Cross-thread `weak_ref::get()` and wrong-thread revoke-under-lock in watch revocation.**  
   `doc/agents/threading-unload.md:97` rule #4 forbids resolving `weak_ref<FrameworkElement>` off its owning UI thread. Yet:
   - `RevokeIconPanelLayoutWatchesOnThisDispatcher` (`src/10-taskbar-edge-layout.cpp:354`), `RevokeRepeaterWatchesOnThisDispatcher` (`src/13-previews.cpp:325`), `EnsureIconPanelLayoutWatch` (`10:551`), `WatchRepeaterElementClearing_UIThread` (`13:257`), `ForEachLiveElementOnThisDispatcher` (`12:461`), `PruneTrackedButtons_UIThread` (`11:151`), and `PruneDeadButtonStates_UIThread` (`11:180`) all call `weak.get()` on *every* entry in global maps before checking `HasThreadAccess()`.
   - Worse, in `RevokeIconPanelLayoutWatchesOnThisDispatcher` (`src/10-taskbar-edge-layout.cpp:371-386`), the `keep` vector holds watches belonging to **other** dispatchers (`onThisThread == false`), and on a map collision (`L375`) it calls `panel.SizeChanged(token)` on the **wrong thread while holding `g_layoutWatchMutex`**.
   - On current Win11 builds, primary and secondary taskbars share one UI thread, so this is dormant today; storing `DWORD ownerThreadId = GetCurrentThreadId();` in each watch/tracked entry lets every UI thread skip foreign entries **before** touching `weak.get()` (and eliminates the `keep` re-insertion path completely).

---

### 1.5 `HwndMatchesStoredPid` and `LookupWindowAppId` fail open when `pid == 0`

In `src/04-helpers.cpp:209-219`:
```cpp
bool HwndMatchesStoredPid(HWND hwnd, DWORD storedPid) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }
    if (!storedPid) {
        return true;   // <-- fails OPEN!
    }
    const DWORD pid = PidFromHwnd(hwnd);
    return pid != 0 && pid == storedPid;
}
```
And in `src/02-state.cpp:90, 105`:
- `PublishWindowAppIds` stores `WindowAppIdEntry{appIdUpper, WindowPid(h)}`. If `h` died between `CollectUdkGroupWindows` and `PublishWindowAppIds`, `WindowPid(h)` returns `0`. Later, if the OS recycles that HWND to a different process, `LookupWindowAppId` checks `if (it->second.pid && ...)` -> skips the PID check and returns the **wrong app's AppId**!
- Same in `StampWindowRecencyLocked` (`src/06-window-recency.cpp:45,56`) and `CaptureButton` (`src/12-button-identity.cpp:252`): a dying window gets stored with `pid = 0`, disabling recycle protection forever for that handle.

**Fix:**
- In `HwndMatchesStoredPid`: change `if (!storedPid) return true;` to `if (!storedPid) return false;`.
- In `PublishWindowAppIds`, `CaptureButton::rememberWindow`, and `StampWindowRecencyLocked`: if `pid == 0`, skip inserting the dead/inaccessible window.
- In `LookupWindowAppId`: require `it->second.pid != 0 && WindowPid(hwnd) == it->second.pid`.

---

### 1.6 COM Apartment & Exclusion Issues (`EnsureVdm` and `Wh_ModSettingsChanged`)

1. **`EnsureVdm()` returns raw STA `g_vdm` off the hook thread** (`src/03-virtual-desktops.cpp:38-44`):
   ```cpp
   if (!OnHookThread()) {
       std::lock_guard<std::mutex> lock(g_vdmMutex);
       return g_vdm;  // <-- contradicts the comment above it!
   }
   ```
   Currently every caller happens to be on the hook thread, so nothing hits this today, but returning an STA COM pointer across apartments is a latent trap. Return `nullptr` when `!OnHookThread()`.

2. **`Wh_ModSettingsChanged` calls `SHGetPropertyStoreForWindow` on the Windhawk thread** (`src/21-entry-points.cpp:150-155`):
   - `SHGetPropertyStoreForWindow` requires COM to be initialized on the calling thread; Windhawk does not guarantee COM initialization on the settings-change callback thread (if `CoInitializeEx` wasn't called there, `SHGetPropertyStoreForWindow` returns `CO_E_NOTINITIALIZED` and newly excluded Win32 AUMIDs silently survive).
   - `windowHwnds` also collects duplicates (any HWND in both `windowFocusMap` and `windowAppFocus` is queried twice).
   - `desk.appFocusMap` (`L162-172`) is checked *without* consulting `windowAppIds[it->second.lastHwnd]`. If the user originally had an empty exclude list (so `ExclusionAumid` was skipped at focus time), closed the window or had a distinct Win32 AUMID, and later added that AUMID to `excludedPrograms`, `appFocusMap` can miss it.
   - **Fix:** Move the exclusion sweep (or AUMID resolution) into the `WM_APP_SETTINGS_CHANGED` handler on the focus worker (which is already STA-initialized and owns `g_desktopMaps`), and deduplicate HWNDs using an `unordered_set`.

---

### 1.7 Smaller Verified Bugs

1. **Paint cache thrash when `glowIntensityRankN == 0` on Plate / PlateAccent styles** (`src/11-button-paint.cpp:105-109`):  
   When `t <= 0.0`, `ApplyButtonHighlight` already created `WhRecentFocusGlow`, calls `HideAllGlowLayers(host)`, and stores `SetCachedPaintState(..., plateGeometry = 0)`. On the next `UpdateVisualStates`, `sameGeometry` checks `painted.plateGeometry (0) == plateShapes.signature (non-zero)` -> **always false**, repainting on every single hover/title update! Just call `ClearButtonHighlight(button)` when `t <= 0.0` (matching `ApplyThumbnailHighlight` at `src/15-preview-paint.cpp:40-43`), before creating `host`.
2. **Unchecked dereference of `FindButtonStateLocked(button, true)`** (`src/12-button-identity.cpp:365`):  
   `auto& e = *FindButtonStateLocked(button, true);` will crash if `InspectableIdentity(button)` returns `nullptr` on the second call after `CaptureButton`. Check `if (!state) return {};` (as `SetCachedPaintState` already does at `L441`).
3. **Retargeted exclusion fails to clear `g_pendingFocus`** (`src/18-focus-promotion.cpp:270-284`):  
   `L270` overwrites `pending.hwnd = confirmHwnd;`, then `L280` checks `if (g_pendingFocus.hwnd == pending.hwnd) g_pendingFocus = {};`. If `StillPendingForeground` retargeted to a sibling window of the same app (`confirmHwnd != g_pendingFocus.hwnd`), `L280` is false and `g_pendingFocus` is left valid with no timer. Keep `const HWND originalPendingHwnd = pending.hwnd;` for the comparison.
4. **`HandleLoadedModuleIfTaskbarView` doesn't check `g_unloading`** (`src/17-taskbar-view-hooks.cpp:362`):  
   Add `if (g_unloading.load()) return;` before claiming `g_taskbarViewHookAttempted` or calling `Wh_ApplyHookOperations()`.
5. **`Wh_Log` still called while holding mutexes:**  
   Fable 2.5 introduced `DeferredLogScope` for `g_stateMutex`, but several mutex-held `Wh_Log` calls remain (including locks shared with the worker):
   - `src/06-window-recency.cpp:49` (`StampWindowRecencyLocked` — holds `g_stateMutex`; change to `LOG_DEFERRED`).
   - `src/08-identity-matching.cpp:38` (`ButtonCountsAsRunning` — holds `g_buttonStateMutex`, which the worker takes in `AppAppearsOnTaskbar`).
   - `src/12-button-identity.cpp:374` (`EnsureButtonIdentity` — holds `g_buttonStateMutex`).
   - `src/12-button-identity.cpp:311` and `670` (`CaptureButton`, `ApplyAllHighlights_UIThread` — hold `g_uncombinedMutex`, which the worker takes in `RecomputeRanksForDesktopLocked`).
   - `src/02-state.cpp:410` (`RememberUiDispatcher` — holds `g_dispatchersMutex`).
6. **`FindThumbnailBackgroundBorder` fallback can match our own `WhRecentFocusThumbCard`** (`src/13-previews.cpp:553`):  
   In the fallback branch (when no child is named `"BackgroundBorder"`), skip `child.Name() == kThumbCardOverlayName` so `PaintCardOverlay` cannot bind an overlay to itself.
7. **`TransformToVisual(host)` on freshly created preview host** (`src/15-preview-paint.cpp:170, 195`):  
   Unlike `ReadPlateShapes` (`src/09-glow-host.cpp:341`), which deliberately avoids `TransformToVisual(host)` because a newly inserted host hasn't been arranged yet, `ApplyThumbnailHighlight` calls `GetTitleBottomRelative(title, host, ...)` and `bb.TransformToVisual(host)` immediately after `EnsureThumbOverlayHost(panel)` creates `host`. Transforming relative to `panelFe` (already arranged; `host` stretches to fill `panel` with zero margin) makes first paint match subsequent paints.
8. **`HoverFlyoutModel_TargetItemKey_Hook` clears/schedules on repeated empty keys** (`src/17-taskbar-view-hooks.cpp:342-349`):  
   `if (key.empty() || key != lastKey)` clears all tracked thumbnails and schedules a Low refresh even when `key.empty() && lastKey.empty()`. If `key.empty()`, clear only when `!lastKey.empty()`, and skip `SchedulePreviewFlyoutRefresh` when `key.empty()`.

---

### 1.8 ABI & Lifecycle Traps Worth Fixing Soon

1. **`TaskListButton_IsRunning` relies on `ITaskListButton` being interface #0** (`src/08-identity-matching.cpp:14-17`):  
   `TaskListButton_get_IsRunning_Original` is `winrt::impl::produce<TaskListButton, ITaskListButton>::get_IsRunning`, whose `this` pointer is the `ITaskListButton*` subobject. Passing `taskListButtonElement.as<IUnknown>()` only works because `ITaskListButton` happens to be the first interface in `implements<TaskListButton, ITaskListButton, ...>` (offset 0). Compare with `ResolveHwndFromCardViewModel` (`src/13-previews.cpp:92`), which properly QIs `IID_Wudk_ITaskItemThumbnailView` first. Querying `IID_Taskbar_ITaskListButton` (or verifying it against `Taskbar.View.dll`'s `guid_v`) makes `TaskListButton_IsRunning` fail-closed if Microsoft ever inserts a newer default interface before `ITaskListButton`.
2. **`Wh_ModInit` returning `FALSE` on `highlightCount == 0 && !previewHighlightEnabled`** (`src/21-entry-points.cpp:12-17`):  
   As your own `TODO.md:236` notes, returning `FALSE` from `Wh_ModInit` is silent in Windhawk (the mod toggle stays ON, `Wh_ModSettingsChanged` is never called). If a user sets `highlightCount=0` and previews off, restarts Explorer, and later sets `highlightCount=3`, the mod stays dead until manually toggled off/on. Staying loaded (just doing no painting while both are disabled) avoids this UX dead end.

---

## 2. Windows & Explorer Interfaces — Audit & Opportunities

### 2.1 Audit of Interfaces Currently Used

| Subsystem | Interface(s) Used | Documented? | Verdict |
|---|---|---|---|
| **Foreground focus** | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, OUTOFCONTEXT)` + `RegisterShellHookWindow` (`HSHELL_WINDOWACTIVATED`, `HSHELL_RUDEAPPACTIVATED`, `HSHELL_WINDOWDESTROYED`) + `GetForegroundWindow` | Public Win32 | **Proper.** Empirically justified by the Calculator WinEvent gap; shell hook degrades cleanly; no polling. |
| **Window metadata** | `InternalGetWindowText`, `GetClassNameW`, `GetWindowLong`, `GetAncestor`, `GetWindow(GW_OWNER)`, `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `QueryFullProcessImageNameW`, `SHGetPropertyStoreForWindow(PKEY_AppUserModel_ID)` | Public Win32 (`InternalGetWindowText` documented WinUser) | **Proper.** `InternalGetWindowText` avoids same-process `WM_GETTEXT` deadlocks; AUMID skipped when exclude list is empty. Fix COM init in `Wh_ModSettingsChanged` (§1.6). |
| **Last use (idle)** | `GetLastInputInfo` | Public Win32 | **Proper.** Zero-overhead, no input hooks. Fix 32-bit signed diff (§1.2) and transient stickiness (§1.1). |
| **Virtual desktops** | `HKCU\...\Explorer\VirtualDesktops\CurrentVirtualDesktop` + `RegNotifyChangeKeyValue` + `EVENT_SYSTEM_DESKTOPSWITCH` + `IVirtualDesktopManager` | Public Win32/COM (reg value semi-documented) | **Good compromise.** Avoids fragile undocumented `IVirtualDesktopManagerInternal`. See §2.2.3–2.2.4 for immediate-promote lag and lock/unlock distinction. |
| **Accent color** | `Windows.UI.ViewManagement.UISettings` + `ColorValuesChanged` | Public WinRT | **Suboptimal for unload.** Replace subscription with `WM_SETTINGCHANGE` / `WM_DWMCOLORIZATIONCOLORCHANGED` (§2.2.1). Keep `UISettings::GetColorValue` for reading the exact UWP accent. |
| **Taskbar button identity** | `Taskbar.View.dll` symbols + `WindowsUdk.UI.Shell` (`ITaskItem`, `ITaskGroup`, `IVectorView<TaskItem>`) + `AutomationId` (`Window: 0x...`) | Private symbols + public WindowsUdk WinMD (IID-gated) | **Strong fail-closed design.** Biggest drop in private surface since 0.11.1. Fix `IsMultiWindow`/`IsRunning` RAII+AddRef (§1.3) and `ITaskListButton` QI (§1.8). |
| **Preview card identity** | `Taskbar.View.dll` optional symbols + `WindowsUdk` `ITaskItemThumbnail` -> `ParentItem.WindowId` + WinUI 2 `ItemsRepeater.ElementClearing` | Private optional symbols + public WindowsUdk + WinUI 2 | **Best available.** 100% per-card, reorder-safe, fail-closed, optional. |
| **Taskbar edge** | `VisualStateManager` + `RunningIndicator` alignment + `IconPanel` aspect ratio + `SHAppBarMessage(ABM_GETTASKBARPOS)` | Public WinRT + Win32 | **Fragile heuristic.** `pw > ph + 4.0` misclassifies wide/short horizontal buttons (§2.2.2). |
| **UI thread marshaling** | Agile `CoreDispatcher::TryRunAsync` + duplicated `SYNCHRONIZE` thread handle + High cleanup / Low sentinel | Public WinRT + Win32 | **Sound.** Tightest unload barrier short of subclassing `Shell_TrayWnd`. Fix `RememberUiDispatcher` gating (§1.4). |

### 2.2 Interfaces That Can Help (Without Regressing History)

#### 2.2.1 `WM_SETTINGCHANGE` (`"ImmersiveColorSet"`) + `WM_DWMCOLORIZATIONCOLORCHANGED` for Accent Color
`doc/agents/review-lessons.md:106` notes that broadcast messages were avoided because *"broadcast never reaches `HWND_MESSAGE`"*. That was true in 0.9.x–0.10.x, **before** 0.11.0 replaced the message-only window with a real hidden top-level `WS_POPUP` window (`src/19-focus-thread.cpp:365`) for `RegisterShellHookWindow`!

A hidden top-level `WS_POPUP` window **does** receive both `WM_SETTINGCHANGE` (with `lParam && _wcsicmp((PCWSTR)lParam, L"ImmersiveColorSet") == 0`) and `WM_DWMCOLORIZATIONCOLORCHANGED` directly in `HookThreadWndProc` on the worker thread:
- Completely removes `g_uiSettings` (`[[clang::no_destroy]]`), `g_colorValuesChangedToken`, `SubscribeAccentChanges()`, `UnsubscribeAccentChanges()`, and the undrainable WinRT threadpool callback (§1.4.2).
- `RefreshCachedAccent()` still reads the exact accent via a local `UISettings{}.GetColorValue(UIColorType::Accent)` on demand (only when the message arrives or settings load — zero hot-path cost).

#### 2.2.2 `XamlRoot::Size()` / `GetWindowRect(Shell_TrayWnd)` for Taskbar Edge Detection
`DetectTaskbarEdge` (`src/10-taskbar-edge-layout.cpp:88-179`) currently includes `const bool panelLooksVertical = pw > ph + 4.0;` (`L128`), assuming a wider-than-tall button means a vertical taskbar (`48×32`). That assumption breaks on:
- **Never Combine / `taskbar-labels`** on a horizontal taskbar (`160×48` buttons -> `pw > ph + 4.0` is true!).
- **`taskbar-icon-size`** with compact height (`TaskbarHeight = 32..36`, `TaskbarButtonWidth = 44` -> `pw > ph + 4.0` is true!).

Today, `IsInVisualState(..., L"HorizontalOrientation")` at `L157` masks this on stock Win11 *only* when `OrientationStates` has already transitioned. If a theme, a build change, or first-paint state leaves `CurrentState()` null, the side bar flips onto the bottom edge right over the running pill.

**Better documented interface:**
1. Orientation of the **taskbar strip itself** (never fooled by button aspect ratio): `iconPanel.XamlRoot().Size()` (WinRT Win10 1903+, public) or `Taskbar.TaskbarFrame` `ActualWidth` vs `ActualHeight` — a horizontal taskbar is ~1920×48 (`width > height`), a vertical taskbar is ~48×1080 (`height > width`).
2. Physical edge per monitor (fixes multi-monitor `passEdge` where `SHAppBarMessage` only reports the primary taskbar): find the `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` via `GetWindowRect` + `MonitorFromWindow(..., MONITOR_DEFAULTTONEAREST)` + `GetMonitorInfoW`, and compare `rcTray` against `mi.rcMonitor` (touches top/bottom/left/right edge). Cache per `XamlRoot` (one per monitor's taskbar), not one `passEdge` for all monitors.

#### 2.2.3 `WTSRegisterSessionNotification` for Lock / Unlock
Currently, lock/unlock is inferred indirectly from `EVENT_SYSTEM_DESKTOPSWITCH` (which fires for UAC, Ctrl+Alt+Del, screen saver, and lock — identically) plus `OpenInputDesktop` for logging (`src/19-focus-thread.cpp:27-39`), and `LOCKAPP.EXE` is filtered by process name (`src/04-helpers.cpp:484`).

If you ever want clean semantics for last-use across lock/sleep without colliding with UAC prompts, `WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION)` (public Win32, `wtsapi32.dll`, works on your existing `WS_POPUP` worker window, unregistered via `WTSUnRegisterSessionNotification` on worker exit) delivers `WM_WTSSESSION_CHANGE` with exact `WTS_SESSION_LOCK` and `WTS_SESSION_UNLOCK` notifications. Not required for 0.12.1 (keeping `-lwtsapi32` out keeps the link list minimal), but the right documented API if lock-screen edge cases ever recur.

#### 2.2.4 `IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop` on Immediate Promote
Right now, `ConfirmDesktopId` (`src/03-virtual-desktops.cpp:134-148`) only catches registry lag when `minFocusSeconds > 0` gives the registry time to flush before confirmation. Under the **default** `promoteMode = ImmediateTracked`, clicking a flashing button or activating a window on Desktop 2 can deliver `EVENT_SYSTEM_FOREGROUND` *before* Explorer writes `CurrentVirtualDesktop` to the registry:
- `HandleForegroundChanged` runs `OnMinFocusTimerElapsed(Immediate)` immediately.
- `ConfirmDesktopId` reads the still-stale registry GUID (Desktop 1) and credits the Desktop 2 window to **Desktop 1**.
- Moments later, `DesktopKeyWatch` fires and switches to Desktop 2 (without the newly promoted rank!).

**Small improvement using an interface you already have:** Before crediting an `Immediate` confirm to `g_currentDesktopId`, `IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop(hwnd, &onCurrent)` (already created on the worker!) can verify whether `hwnd` is actually on the current desktop; and `TryGetWindowDesktopId(hwnd, &winDesk)` (already written at `src/03-virtual-desktops.cpp:71`!) can be tried first when it succeeds inside `explorer.exe`.  
*(Caveat: on some Windows 10/11 builds, `GetWindowDesktopId` fails with `E_ACCESSDENIED` / `TYPE_E_ELEMENTNOTFOUND` for windows of other or elevated processes, and returns a sentinel/null GUID for windows pinned to all desktops — so it must remain an advisory/best-effort enhancement with `g_currentDesktopId` as the fallback, verified live.)*

#### 2.2.5 Constructing Own XAML Hosts in Code vs `XamlReader::Load`
Still open from Fable 3.4: `EnsureGlowHost` (`src/09-glow-host.cpp:212`, 1 `Grid` + 1 `Rectangle`) and `EnsureThumbOverlayHost` (`src/14-preview-overlay.cpp:187`, 1 `Grid` + 3 `Rectangle`s + 1 `Border`) parse XML strings with `Markup::XamlReader::Load`. Constructing those 2–5 elements directly via C++/WinRT constructors (as `PaintNativeContour` and `PaintCardOverlay` already do!) avoids XML parsing on first paint, removes `<winrt/Windows.UI.Xaml.Markup.h>`, and is ~15 lines shorter. Low priority, safe post-0.12.1 cleanup.

### 2.3 Interfaces Evaluated and Deliberately NOT Recommended (Respecting History)

- **Undocumented `IVirtualDesktopManagerInternal` / `IVirtualDesktopNotification`:** Tempting for lag-free desktop switches, but Microsoft changes their IIDs and vtable layouts frequently across Windows 11 cumulative updates. Your public `RegNotifyChangeKeyValue` + `IVirtualDesktopManager` design is far better for long-term maintenance.
- **Global input hooks (`WH_KEYBOARD_LL` / `WH_MOUSE_LL`) or `RIDEV_INPUTSINK` for per-window last-use:** Would distinguish scrolling an inactive window from typing in the active window, but adds system-wide input-path overhead and is a red flag in Windhawk catalog review. `GetLastInputInfo` is the right trade-off.
- **Any `taskbar.dll` symbols / `CTaskGroup` / `HandleClick` / `ReportClicked`:** Rightly gone in 0.11.18–0.11.20 (`doc/agents/history.md`). Do not revive.
- **`WindowsUdk.UI.Shell.TaskbarAppsList` + `ITaskItem3::SystemRelativeLastActivatedTime` (from `TODO.md:100,254` and Fable 3.2):** Worth keeping on the **post-0.12.1** roadmap, not rushing into 0.12.1. Moving to `TaskbarAppsList` would delete the `IsMultiWindow`/`IsRunning` capture trick, eliminate cold-start wait after enabling in a running Explorer, and allow optional seeding from Explorer's own last-activation timestamps. The cost is hooking `TaskbarController` (another private symbol) and still waiting for a `TaskbarController` callback on hot-load unless you also traverse the visual tree. Right decision to keep it out of 0.12.1.
- **Composition `DropShadow`:** Still properly parked in `TODO.md:271` (clipping + unload lifetime complexity).

---

## 3. State Machines — Explicit, Implicit & Cross-Machine Audit

`doc/code/state-machines.md` is an unusually helpful artifact, but its snapshot is **v0.11.46**. Between 0.11.47 and 0.12.1, **three new implicit state machines** were introduced without being added to `state-machines.md`, and several cross-machine edges have subtle bugs.

### 3.1 Three New Undocumented Implicit State Machines (0.11.49–0.12.1)

#### Machine 15 (NEW, implicit): In-Front "Last Use" Stretch (`g_inFrontHwnd`, `g_inFrontSince`)
Lives on the focus worker (`src/18-focus-promotion.cpp:448-545`).

Current (buggy on transient FG) behavior:
```mermaid
stateDiagram-v2
    direction LR
    state "None" as None
    state "Tracking app A\n(g_inFrontHwnd=A, since=t0)" as TrackA
    state "Stuck on transient T\n(g_inFrontHwnd=T)" as StuckT
    [*] --> None
    None --> TrackA : FG = A
    TrackA --> TrackA : decay tick (fg==A)\nrefresh last use
    TrackA --> StuckT : FG = Alt-Tab / Start / Lock (T)\nstamps A, overwrites hwnd=T
    StuckT --> StuckT : user returns to A w/o FG event;\ndecay tick sees fg(A) != T -> SKIP!
    StuckT --> TrackA : only if Windows sends FG = A
```
In addition to the transient-FG stall above (§1.1.1), this machine has no transition out of `Tracking(A)` on `HSHELL_WINDOWDESTROYED(A)` (§1.1.2), and `Tracking(A2)` is ineffective if `A2` was entered via `sameAppPending` while `appConfirmed` was already true (§1.1.3).

#### Machine 16 (NEW, implicit): Virtual Desktop Registry Watch (`DesktopKeyWatch`, `g_desktopKeyWatchActive`)
Lives on the focus worker (`src/19-focus-thread.cpp:184-214, 287-295, 394-416`).
- States: `Closed` -> `Armed` (`RegNotifyChangeKeyValue`) -> (event signaled) -> re-arm -> `Armed` or `Lost` (falls back to per-FG registry read in `HandleForegroundChanged`).
- **Minor loop-priority issue (`src/19-focus-thread.cpp:401-412`):** In `MsgWaitForMultipleObjectsEx`, `WAIT_OBJECT_0` (the registry event) is checked before `PeekMessageW` and executes `continue;`. If the registry key is being written rapidly (or right during shutdown), `WM_APP_SHUTDOWN` sits in the message queue until the event stops firing. Checking `if (g_unloading.load()) break;` or pumping messages after handling the event eliminates any starvation edge case.

#### Machine 17 (NEW, implicit): Deferred Focus-Tracking Activation (`g_taskbarViewHookAttempted`, `g_taskbarViewHooked`, `g_focusTrackingActive`)
Spans Windhawk init, `LoadLibraryExW_Hook`, `Wh_ModAfterInit`, and the focus worker (`src/17-taskbar-view-hooks.cpp:362`, `src/19-focus-thread.cpp:7, 235-329, 385`).
- States: `WaitingForTaskbarView` -> `HooksInstalled (g_taskbarViewHooked=true)` -> `TrackingActive` (or `Degraded` if `SetWinEventHook` / `RegisterShellHookWindow` fails) -> `Deactivated`.
- **Race audit:** The handshake between `g_hookThreadHwnd.store(..., release)` + `g_taskbarViewHooked.load()` in the worker and `g_taskbarViewHooked = true; PostToHookThread(WM_APP_TASKBAR_READY);` in `HandleLoadedModuleIfTaskbarView` is **sound** (if both fire, `ActivateFocusTracking` is idempotent via `g_focusTrackingActive`). Only missing piece: check `g_unloading.load()` at the top of `HandleLoadedModuleIfTaskbarView` (§1.7.4).

### 3.2 Cross-Machine Gaps in the Existing 14 Machines

| Machines | Gap / Edge Case | Severity | Location |
|---|---|---|---|
| **1 + 2** (`g_pendingFocus` + `MinFocusTrack`) | Same-app switch `A1 -> A2` while `appConfirmed=true, previewConfirmed=false` never creates `windowAppFocus[A2]` | Medium | `src/18-focus-promotion.cpp:672-693` |
| **1 + 2** | Retargeted exclusion in `OnMinFocusTimerElapsed` compares against overwritten `pending.hwnd` instead of original `g_pendingFocus.hwnd`, leaving `g_pendingFocus` valid with no timer | Low-Med | `src/18-focus-promotion.cpp:270, 280` |
| **2 + 8 + 16** (Immediate promote + desktop lag) | `ImmediateTracked` confirms synchronously before `RegNotifyChangeKeyValue` fires after a desktop switch -> credits new desktop's window to old desktop | Medium | `src/18-focus-promotion.cpp:743`, `src/03-virtual-desktops.cpp:134` |
| **5 + 6** (Button paint cache + UVS) | `t <= 0` stores `plateGeometry = 0`, causing permanent paint-cache miss on every `UpdateVisualStates` for Plate styles | Low-Med | `src/11-button-paint.cpp:105-109` |
| **5 + 6** (Repeater seeding) | `SeedButtonsFromRepeater_UIThread` keeps `static thread_local unordered_set<void*> seededRepeaters` without `weak_ref` validation (reused address after monitor/DPI teardown won't re-seed) and can insert `nullptr` | Low | `src/12-button-identity.cpp:554-559` |
| **6** (Dual debounce + sweep) | `g_pendingOverlaySweep` is a single `atomic<bool>` cleared by the first UI dispatcher that finishes (use a generation counter like `g_buttonSeedGeneration` for multi-dispatcher parity). Also, if `QueueFullPass` sees a pass already queued, `ScheduleRefreshAllHighlights` can avoid arming a redundant 300 ms worker debounce timer | Low | `src/12-button-identity.cpp:742, 769`, `src/17-taskbar-view-hooks.cpp:113` |
| **7** (Flyout refresh) | `g_previewFlyoutRefreshPending` is `thread_local`, set before `DispatcherTryRun(anchor.Dispatcher(), ...)`. If `anchor` ever came from a foreign dispatcher, the calling thread's `Pending` flag would stick `true` forever. Guard with `dispatcher.HasThreadAccess()`. | Low | `src/14-preview-overlay.cpp:402-415` |
| **11** (Mod lifecycle) | Returning `FALSE` from `Wh_ModInit` when `highlightCount==0 && !previews` leaves mod silently dead across later settings saves | Low-Med | `src/21-entry-points.cpp:12-17` |
| **12** (Unload drain) | `ObserveDispatcherOp` returns on `ThreadExited` while `Completed` delegate is attached (§1.4.3). Also, adding a periodic `"UI drain: still waiting on thread %u (%us)"` log (every 5 s, already noted in `TODO.md:238` and Fable 3.5) makes any stuck unload diagnosable | Medium | `src/16-ui-dispatch.cpp:95-108` |
| **14** (XAML event watches) | `ElementClearing` subscribed without `RememberUiDispatcher` check (§1.4.1); cross-thread `weak.get()` and wrong-thread revoke-under-lock in `keep` (§1.4.4) | Medium | `src/10-taskbar-edge-layout.cpp:342`, `src/13-previews.cpp:243` |

---

## 4. Interactions with Other Windhawk Mods & Shell Extensions

Checked against the actual implementations in `example/` and common Windows 11 Windhawk taskbar mods.

### Compatibility Summary

| Mod / Tool | Shared Surface | Status | Key Risk / Note |
|---|---|---|---|
| **Windows 11 Taskbar Styler** (`windows-11-taskbar-styler`) | `IconPanel`, `BackgroundElement`, `RunningIndicator`, `MultiWindowElement`, `BackgroundBorder` | **Mostly great; 3 subtle edge cases** | Own zero-footprint elements avoid `BottomDensy` sizing bugs and hover wipes. Watch out for un-named `> Border` Styler selectors hitting `WhRecentFocusThumbCard`, positional `[N]` selectors, and custom `Canvas.ZIndex` themes (details below). |
| **Taskbar Volume Control Per-App** (`taskbar-volume-control-per-app`) | `consume_ITaskGroup::IsRunning` hook, `IsMultiWindow`, `LoadLibraryExW` | **Safe normally; high blast radius on exception** | Windhawk chains `IsRunning` hooks cleanly when both behave. Fix non-RAII `g_captureTaskGroup` (§1.3) so an exception in our mod can never break `taskbar-volume-control-per-app` or Explorer. |
| **Taskbar Height and Icon Size** (`taskbar-icon-size`) | `TaskListButton::UpdateVisualStates` hook, `IconPanel` geometry | **Minor orientation hazard** | Hook chains cleanly; `SizeChanged` tracks resizes. Compact heights (e.g. 32–36 px tall × 44 px wide) trigger `pw > ph + 4.0` in `DetectTaskbarEdge` if `HorizontalOrientation` visual state is absent (§2.2.2). |
| **Taskbar Labels** (`taskbar-labels`) & Native **Never Combine** | `TaskListLabeledButtonPanel`, wide buttons (`~160×48`) | **Minor orientation hazard** | Same `pw > ph + 4.0` trap in `DetectTaskbarEdge`. Side bar on `Left` clears labels nicely; Plate follows wide `BackgroundElement` cleanly. |
| **Taskbar Active Window Highlight** (`taskbar-active-window-highlight`) | `TaskItemThumbnailView::OnApplyTemplate`, `put_ThumbnailSource`, `BackgroundBorder` sibling | **Safe (no crash); visual overlap if both enabled** | Both insert an own `Border` at `BackgroundBorder + 1`, doubling the tint/border on the active preview card. README already guides users clearly. |
| **Taskbar Thumbnail Size** (`taskbar-thumbnail-size`) | `TaskItemThumbnailView::OnApplyTemplate` | **Good synergy** | Our `OnApplyTemplate` defers to Low priority after layout, reading post-resize `BackgroundBorder` bounds. Very small `maxHeight` (<70 px) can trigger `cardH * 0.38` clamp in `TitleBar` style (`src/15-preview-paint.cpp:256`). |
| **Taskbar Thumbnail Reorder** (`taskbar-thumbnail-reorder`) | `HoverFlyoutModel::TargetItemKey`, flyout `ItemsRepeater` | **Excellent synergy** | View-model HWND resolution + `put_ThumbnailSource` + `ElementClearing` is completely immune to card reordering. |
| **Taskbar on Top** (`taskbar-on-top`) / **Vertical Taskbar** (`taskbar-vertical`) | Taskbar edge per monitor, `MultiWindowElement` | **Multi-monitor `passEdge` gap** | Single `passEdge` per pass (`src/12-button-identity.cpp:645`) assumes all monitors share one edge. Vertical multi-window stack sliver clip (`clipX`) fails closed if stack sticks out vertically rather than to the right. |
| **Taskbar Dock Animation** (`taskbar-dock-animation`) | `Icon.RenderTransform` | **100% compatible** | Removing `icons.sizeBoostRank*` in 0.11.38 eliminated the only conflict. |
| **ExplorerPatcher / StartAllBack** (Win10 taskbar) | Replaces `Taskbar.View.dll` with classic Win32 taskbar | **Fails closed cleanly** | Focus tracking stays idle (`Taskbar.View.dll` never loads); zero overhead, zero crash. Worth one bullet under README Limitations ("Windows 11 XAML taskbar only"). |

### Detailed Taskbar Styler Notes (from `example/windows-11-taskbar-styler.wh.cpp`)

1. **Un-named `> Border` selectors vs `WhRecentFocusThumbCard`:**  
   In `example/windows-11-taskbar-styler.wh.cpp:683, 988`, real Styler themes use un-named type selectors like `TaskListLabeledButtonPanel@RunningIndicatorStates > Border`. On taskbar buttons, our `WhRecentFocusGlow` is a `Grid` (not a `Border`), so `> Border` does **not** match it — great!  
   On **thumbnail previews**, however, `PaintCardOverlay` (`src/14-preview-overlay.cpp:59, 88`) inserts `WhRecentFocusThumbCard` as a direct `Border` child of the card's root `Grid`. Any Styler theme targeting `Taskbar.TaskItemThumbnailView > Grid > Border` (without `#BackgroundBorder`) will style our overlay Border alongside the native background. Wrapping `WhRecentFocusThumbCard` inside a `Grid` (or placing it inside `WhRecentFocusThumbGlow` when `BackgroundBorder` is a direct child of the root panel) would insulate it from `> Border` selectors, though keeping direct `Border` parity with `taskbar-active-window-highlight` is also reasonable.
2. **Positional child selectors (`[N]`):**  
   Styler supports numeric child indices (`Class#Name[2]`, `example/windows-11-taskbar-styler.wh.cpp:194`). Inserting `WhRecentFocusGlow` before `Icon` shifts indices of later children by +1 while ranked. Most Styler themes use `#Name` selectors on `IconPanel` children (`#BackgroundElement`, `#RunningIndicator`, `#MultiWindowElement`, `#LabelControl`), so this rarely hits in practice, and you cannot avoid it without either giving up z-order behind `Icon` or rewriting native `Canvas.ZIndex` (which breaks more things). Keep as-is.
3. **Themes that set `Canvas.ZIndex` on `BackgroundElement`:**  
   Because you rightly never touch native `Canvas.ZIndex`, a theme that sets `Canvas.ZIndex=1` on `BackgroundElement` will paint over `WhRecentFocusGlow` (default `ZIndex=0`). Reading `Canvas::GetZIndex(bg)` and setting `Canvas::SetZIndex(host, bgZ)` on *our own* `WhRecentFocusGlow` respects native ZIndex while keeping zero native mutations.
4. **Themes where `RunningIndicator` is collapsed at rest and full-cell on hover:**  
   `ReadPlateShapes` checks `RunningIndicatorLooksLikeHoverPlate` -> `ReadNativeGlowShape` (which requires `Visibility::Visible`). If a theme toggles `RunningIndicator` from `Collapsed` to `Visible` full-cell on hover, the plate will switch from `BackgroundElement` (with stack sliver) to `RunningIndicator` (without stack sliver) while hovered; `plateShapes.signature` catches it so it won't get stuck after hover ends.
5. **RTL Windows locales:**  
   In `PaintNativeContour` (`src/09-glow-host.cpp:501`), `clipX = shape.x + shape.w - stack.x` assumes `MultiWindowElement` extends past the right edge of `BackgroundElement`. On RTL Windows (where it extends to the left), `clipX >= stack.w`, so the stack sliver cleanly fails closed (no glitch, just un-tinted sliver).

---

## 5. Pre-Publish Packaging, Doc Sync & Code Hygiene

### 5.1 Documentation & Version Sync Drift (`AGENTS.md` Rule)

`AGENTS.md` requires keeping `@version`, `AGENTS.md`, `README.md`, `doc/investigation-notes.md`, and `doc/runtime-flow.svg` (`doc/generate-runtime-flow.py`) in sync on a version bump:

- **`doc/investigation-notes.md`**: Header still says *"Updated 2026-10-06. Describes mod 0.11.1"* and still describes path-keyed apps, `QueueButtonResolve`, `ResolveOneButtonOnFocusThread`, `CImmersiveTaskItem::GetThumbnailWindow`, and `GetAppWindow` (all removed in 0.11.18–0.11.33).
- **`doc/generate-runtime-flow.py` & `doc/runtime-flow.svg`**: Still labelled *"mod 0.11.1 · architecture checked 2026-10-06"* and diagrams the deleted async worker resolver (`QueueButtonResolve`) and `GetAppWindow`.
- **`doc/code/state-machines.md`**: Header still says `v0.11.46`; missing Machines 15–17 (last use, registry watch, deferred focus activation) and still shows Machine 8 without `RegNotifyChangeKeyValue`.
- **`README.md:54-55`**: Still says *"preview plate restores Styler tints"* (already fixed in `src/00-metadata.cpp`, still stale in repo `README.md`).
- **`src/00-metadata.cpp` (Windhawk catalog README)**: Very clean! Worth adding one short bullet under `## Limitations` stating **"Windows 11 native taskbar only"** (classic Win10 taskbar replacements like ExplorerPatcher's Win10 taskbar / StartAllBack are unsupported and stay untouched).

### 5.2 Small Dead Code & Micro-Perf Cleanups

- `src/02-state.cpp:495-496`: Orphaned comment (`// Marker: we tinted native BackgroundBorder...`) with no constant below it (leftover from 0.11.38 deletion of `kThumbNativeStyleMarker`).
- `src/13-previews.cpp:218-221` and `355-396`: Orphaned comment referencing `outSourceIndex` and `GetAt`; `CollectRepeaterThumbnailViews`'s `outSourceIndex` parameter is only ever passed `nullptr` (`src/15-preview-paint.cpp:297`) and can be removed.
- `src/09-glow-host.cpp:160-171`: `ClearButtonHighlight` calls `GetIconPanel(button)` twice (once directly, once inside `ButtonHasOurChrome(button)`).
- `src/11-button-paint.cpp:33, 63`: `ApplyButtonHighlight` already found `existingHost` at L33, then calls `ButtonHasOurChrome(button)` at L63 (another `GetIconPanel` + `FindChildByName` walk) instead of checking `existingHost != nullptr`.
- `src/09-glow-host.cpp:115-134`: `EnsureGlowHostZOrder` runs on every cached `UpdateVisualStates` hit and heap-allocates `std::vector<GlowOrderChild>`. Tracking the first `aboveHost` native index in a single `uint32_t` scalar avoids the allocation on the hover-hot path.
- `src/09-glow-host.cpp:478`: When `!accent` (`Plate` style), pass `nullptr` to `b.BorderBrush(...)` instead of allocating an unused `SolidColorBrush{strokeColor}`.
- `src/21-entry-points.cpp:99-122` (`Wh_ModUninit`): For parity with the other global maps cleared on unload, also clear `g_windowAppId`, `g_uncombinedWindowsByThread`, `g_fullPassQueuedThreads`, and reset `g_inFrontHwnd = nullptr; g_inFrontSince = 0;` (useful for tests or any non-unloaded module path).

---

## 6. Recommended Pre-Publish Sequence

If you want the cleanest, lowest-risk path to publishing 0.12.1 (or 0.12.2) to `windhawk-mods`:

1. **Fix the 0.12.1 last-use bugs (§1.1, §1.2):** transient FG stickiness, `HandleWindowDestroyed` clear + fold, `sameAppPending` window-switch `windowAppFocus` stamp, and `LastInputTick64` signed 32-bit diff. Add unit tests in `tests/run-appid-identity-tests.py` or a small last-use check for these exact transitions.
2. **Fix the unload & hook-safety items (§1.3, §1.4, §1.5, §1.6, §1.7):**
   - RAII `g_captureTaskGroup` + `com_ptr<IUnknown>` return for `ITaskGroup`.
   - Gate `WatchRepeaterElementClearing_UIThread` and `Schedule*` on `RememberUiDispatcher`, and track `put_ThumbnailSource_Hook` cards.
   - Replace `UISettings::ColorValuesChanged` with `WM_SETTINGCHANGE` (`ImmersiveColorSet`) + `WM_DWMCOLORIZATIONCOLORCHANGED` on the worker's `WS_POPUP` window (smaller code *and* closes the WinRT threadpool unload window!).
   - Fail closed on `pid == 0` in `HwndMatchesStoredPid` / `LookupWindowAppId` / publishers.
   - Return `nullptr` from `EnsureVdm()` off the hook thread; move `Wh_ModSettingsChanged` exclusion sweep onto the STA worker (or guard COM init) and include `appFocusMap`.
   - Fix the 4–5 one-line bugs (`t <= 0` plate clear, `EnsureButtonIdentity` null check, retargeted exclusion `originalPendingHwnd`, `HandleLoadedModuleIfTaskbarView` `g_unloading` check, `LOG_DEFERRED` in `StampWindowRecencyLocked`, `Wh_Log` outside `g_buttonStateMutex`/`g_uncombinedMutex`).
3. **Sync docs & rebuild (§5.1):** Update `doc/investigation-notes.md`, `doc/generate-runtime-flow.py` + `doc/runtime-flow.svg`, `doc/code/state-machines.md`, and `README.md`, then run `python tools/build_mod.py` and the full test suite (`tests/run-*.py`, including `run-dispatcher-tests.py` and `run-perf-build-tests.py`).
4. **Live smoke-check after the fixes:**
   - Work in an app for >decay (or test with `decayMinutes = 1`), press Alt+Tab / open Start Menu / lock screen and return without switching apps -> verify it does **not** decay while typing.
   - Test with `taskbar-icon-size` (compact height) and Never Combine -> verify side bar stays on the left edge (especially if you also include the `XamlRoot().Size()` fix from §2.2.2).
   - Change Windows system accent color with the mod running -> verify highlight color updates immediately, then disable/re-enable the mod.
