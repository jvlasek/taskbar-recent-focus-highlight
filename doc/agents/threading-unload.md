# Threads, shared state, locks and unload

Read before adding a thread, timer, lock, global, event subscription or
dispatcher post, and before touching `Wh_ModInit` / `Wh_ModUninit`.

## Overview

```
┌──────────────────────────────────────────────────────────────┐
│ Focus worker (hidden top-level window + GetMessage loop)     │
│  • SetWinEventHook FOREGROUND + DESKTOPSWITCH, OUTOFCONTEXT  │
│  • SHELLHOOK activation notifications (best effort)          │
│  • app min-focus, preview min-focus, decay, rebind timers    │
│  • g_desktopMaps[desktopGuid]: app + window recency          │
│  • RequestApplyVisuals / RequestApplyPreviewVisuals          │
└────────────────────────────┬─────────────────────────────────┘
                             │ CoreDispatcher.TryRunAsync
          ┌──────────────────┴──────────────────┐
          ▼                                     ▼
┌──────────────────────────┐      ┌──────────────────────────────┐
│ Taskbar UI — icons       │      │ Taskbar UI — thumbnails      │
│ TaskListButton hooks     │      │ TaskItemThumbnailView hooks  │
│ identity cache, glow host│      │ view-model resolve, overlays │
└──────────────────────────┘      └──────────────────────────────┘
```

- **Why a worker thread:** out-of-context WinEvents, the shell hook and the
  timers need a message pump (since 0.11.49 `MsgWaitForMultipleObjectsEx`
  that also waits on the VirtualDesktops registry-change event; the key is
  closed when the thread exits), and every cross-process read (process path,
  window AUMID, registry) stays off the taskbar paint path.
- **Why marshal to the UI thread:** XAML runs on its tree's dispatcher.
  `RunOnUiThread` posts to the agile `CoreDispatcher`s recorded by
  `RememberUiDispatcher` when a button, panel or card is first seen on its own
  thread. Before the first one is seen it logs "no dispatcher anchor" once and
  does nothing; the first `UpdateVisualStates` applies. Do not answer a failed
  post by posting to the worker again (that once self-posted forever and
  starved `WM_TIMER`).
- **Why hook `UpdateVisualStates`:** it registers buttons and repaints the
  **cached** rank. The full pass (`ApplyAllHighlights_UIThread`: capture
  identity, snapshot running state, recompute, paint) is debounced (300 ms
  plus a trailing timer on the worker), not run per hover.
- `LoadLibraryExW` hook covers a late `Taskbar.View.dll` load; the hook
  install is claimed once with `g_taskbarViewHookAttempted.exchange(true)`.
- **Focus tracking waits for the taskbar (0.12.1).** Windhawk loads the mod
  into every `explorer.exe`, including folder-window / COM-server processes
  that never load `Taskbar.View.dll`. The worker always starts (its window
  must exist so the shutdown post cannot be lost), but installs the shell
  hook, both WinEvent hooks and the registry watch (and reacts to accent
  broadcasts) only in `ActivateFocusTracking`: at worker start when `g_taskbarViewHooked`
  is already set, otherwise on `WM_APP_TASKBAR_READY`, posted after
  `Wh_ApplyHookOperations` succeeds in the `LoadLibraryExW` path. Until then
  `HandleForegroundNotification` ignores posts (`g_focusTrackingActive`), so
  such a process logs only init and "Focus tracking waits for the taskbar".
  Capture manual-20261120-154409-086197 had seven of them logging the same
  focus events as the taskbar process. A failed `SetWinEventHook` is now an
  `ERROR` line, not a failed init.

## Shared state

| Object | Guard | Owner / rule |
|--------|-------|--------------|
| `g_desktopMaps`, `g_currentDesktopId`, `g_pendingFocus` | `g_stateMutex` | Worker writes; UI reads, and recomputes ranks, under the lock |
| `g_windowAppId` (window → Explorer AppId) | `g_windowAppIdMutex` | UI capture writes; worker reads |
| `g_uncombinedWindowsByThread` | `g_uncombinedMutex` | One set per UI thread, written by that thread; readers use the union |
| `g_buttonStates` (identity, running state, paint cache, icon scale) | `g_buttonStateMutex` | UI. Keyed by `IUnknown*`, validated by `weak_ref`. Only `AppAppearsOnTaskbar` reads it off the UI thread |
| `g_trackedButtons`, `g_trackedThumbViews` | `g_buttonsMutex`, `g_thumbViewsMutex` | UI. Weak refs; never `weak.get()` off the UI thread |
| `g_layoutWatches` (`SizeChanged`), `g_repeaterWatches` (`ElementClearing`) | `g_layoutWatchMutex`, `g_repeaterWatchMutex` | UI. Registered only if `RememberUiDispatcher` recorded the thread; revoked on that dispatcher at unload |
| `g_uiDispatchers` | `g_dispatchersMutex` | `[[clang::no_destroy]] optional<vector<shared_ptr<UiDispatcher>>>`: dispatcher plus a real `SYNCHRONIZE` thread handle captured on the UI thread. `reset()` in Uninit |
| `g_settingsPtr` (`SettingsSnap`) | `g_settingsMutex` | Whole immutable object published by `LoadSettings` (`generation` bumped). Never mutate in place |
| `g_cachedAccent` | atomic packed ARGB | Written on the worker: settings load, and `WM_SETTINGCHANGE("ImmersiveColorSet")` / `WM_DWMCOLORIZATIONCOLORCHANGED` to its window (250 ms timer, 0.12.2). Not inside `Settings`. No WinRT event subscription (a thread-pool callback cannot be drained on unload) |
| `g_vdm` | `g_vdmMutex` | Created, used and released on the worker only |
| `g_hookThreadHwnd`, `g_hookThreadId` | atomics | Worker writes; others `PostMessage`. `SetTimer` / `KillTimer` only on the worker |
| `g_taskbarViewHooked` | atomic | Set once the Taskbar.View hooks succeed (init or `LoadLibraryExW`); read by the worker at start |
| `g_focusTracking`, `g_focusTrackingActive` | worker only | Hooks / watch installed by `ActivateFocusTracking`, removed by `DeactivateFocusTracking` at worker exit |
| `g_unloading` | atomic | Any thread |

**Lock order:** `g_stateMutex` first, then any one of `g_buttonStateMutex`,
`g_windowAppIdMutex`, `g_uncombinedMutex`, `g_settingsMutex`. Never the
reverse, and never publish settings while holding `g_stateMutex`.

**Not under a lock:** XAML calls, `Dispatcher` calls, COM and cross-process
reads (`SHGetPropertyStoreForWindow`, `GetProcessImagePath`). Resolve first,
then lock and assign. The UI thread takes `g_stateMutex` to paint. `Wh_Log`
under a lock is fine: with logging off it does nothing, and the mod logs
only state changes there. 0.12.2 deferred such lines with a
`DeferredLogScope` / `LOG_DEFERRED` layer; 0.12.3 removed it (windhawk-mods
review: it formatted and allocated even with logging off). Only the perf
build defers them (`PERF_LOG`, flushed when the last timed lock of the
thread is released), so normal perf runs do not count log I/O as lock hold.

## Rules

1. `SetTimer` only from the thread that owns the window: post a message and
   arm the timer in the `WndProc`. Debounce with a deadline (skip re-arming an
   armed timer), not a reset that a hover storm can postpone forever.
2. `KillTimer` does not remove a `WM_TIMER` that is already queued; handlers
   re-check the current candidate's deadline.
3. No strong XAML or `CoreDispatcher` reference in a global with a CRT
   destructor (`Wh_ModUninit` does not run at process exit). Weak refs are
   fine; strong ones need `[[clang::no_destroy]]` plus an explicit reset.
4. Never resolve a `weak_ref<FrameworkElement>` off its UI thread: the
   temporary strong reference can become the last one and destroy the element
   on the wrong thread.
5. The worker's window class is registered with the **mod's** module handle
   (`GetModuleHandleExW`, `FROM_ADDRESS | UNCHANGED_REFCOUNT`) and unregistered
   on every exit path. `ERROR_CLASS_ALREADY_EXISTS` is a hard failure (the
   stale class points into an unmapped image).
6. `StartWinEventHookThread` returns only after the worker signalled its ready
   event, so the shutdown post cannot be lost.
7. try/catch around XAML in hooks; a hook must never throw into Explorer
   (`TaskListButton_UpdateVisualStates_Hook` wraps everything after the
   original call since 0.12.3).
12. No heap-owning `thread_local` inside a function on a UI thread. Declare it
    at namespace scope and empty it in `ReleaseUiThreadLocals_UIThread` (the
    unload cleanup on that thread): a thread_local destructor runs only at
    thread exit, after the module is gone, so it leaked on every unload
    (0.12.3, AI review).
8. No WinRT event subscription whose handler can run on a thread-pool thread
   (`UISettings::ColorValuesChanged` until 0.12.2): revoking it does not wait
   for a call already running. Use a window message to the worker instead.
9. Subscribe to an element's event, post to its dispatcher or track it only
   after `RememberUiDispatcher(element)` returned true
   (`WatchRepeaterElementClearing_UIThread`, `TrackThumbView_UIThread`,
   `SchedulePreviewFlyoutRefresh`, `ScheduleRefreshAllHighlights`, 0.12.2);
   otherwise the unload drain never visits that thread.
10. A stored PID of 0 matches nothing (`HwndMatchesStoredPid`, 0.12.2): a
    window already gone when it was recorded is not stored at all.
11. COM that needs an apartment runs on the worker (an STA), not on
    Windhawk's settings thread: `DropExcludedRecords` handles a settings
    change there (0.12.2). `EnsureVdm` returns nullptr off the worker.

## Unload (`Wh_ModUninit`)

When it returns, no thread, `WndProc`, event handler or queued dispatcher
callback of this mod may still exist, because Windhawk frees the image.

Order:

1. `g_unloading = true`.
2. **Stop the worker first** (`WM_APP_SHUTDOWN` plus a thread message, then an
   INFINITE join). After this nothing can post new UI work.
3. Drain every recorded dispatcher (`RunOnEachUiDispatcherAndWait`): a High
   priority cleanup (clear icon and preview chrome, revoke `SizeChanged` and
   `ElementClearing` owned by that thread), then a Low priority sentinel.
4. Clear state and release the dispatcher records.

Drain rules:

- Return only when the Low sentinel **ran** (`Completed` with
  `GetResults() == true`) or the UI thread's retained handle is signalled.
  A rejected post, a High completion, a failed status read, a canceled or
  errored operation, or elapsed time is not proof.
- After the Low sentinel ran, wait until its thread is back in its message
  loop (`WaitUntilUiThreadLeftModCode`, 0.12.3): `SendMessageTimeout(WM_NULL)`
  to a window of that thread, repeated until handled or the thread exits.
  `Completed` wakes the waiter from inside our delegate, before the delegate
  and the sentinel have returned and been released (windhawk-mods AI review,
  PR #7977). No window found: logged, one 20 ms pause.
- `Completed` is the barrier, not a signal from inside the callback. If the
  event or the subscription fails, poll the same operation while watching the
  thread handle. Never drop an operation because its status cannot be read.
- "Was it queued?" is `GetResults()` / `Status()`, never
  `static_cast<bool>(op)`.
- A later `OpenThread` failure or a reused thread id proves nothing; that is
  why the handle is captured up front.

**No timeouts (decided 2026-11-16).** A timeout can unload the image under a
queued callback. Windhawk 1.7.3 has no uninit timeout: it shows "Mod tasks in
progress — Uninitializing…" while uninit runs and queues a re-enable until it
returns, so a stuck drain is visible and recovers when the UI thread does.
Deadline plus module pin was considered and rejected (the pinned DLL would
come back with stale globals). A failed `Wh_ModInit` is silent in the UI and
`Wh_ModUninit` is not called for it, so init failure paths clean up
themselves. Check: `python tests/run-dispatcher-tests.py` (Windows, g++).
