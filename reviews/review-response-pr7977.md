# Response to the windhawk-mods AI review of PR #7977 (0.12.2)

2026-11-26, branch `fix/pr-review` -> 0.12.3.

## Must fix

| Item | Change |
|---|---|
| Unload race: the sentinel's `Completed` delegate wakes `Wh_ModUninit` with `SetEvent` before it returns; the delegate and sentinel are then released, all in the mod image | Fixed as suggested: after `SentinelRan`, `WaitUntilUiThreadLeftModCode` sends `WM_NULL` (`SendMessageTimeoutW`, 100 ms, repeated) to a window of that thread (`EnumThreadWindows`); handled only back in the message loop. Ends if the thread exits. No window found: logged, one 20 ms pause. |
| Settings moved into `icons.*`: the page shows defaults, first save drops the old values | Reverted (ids from 0.11.1 again, `ApplyLegacyAppSettings` gone; `previews.style` transition kept). Layout improved without new ids: the four app-ranking settings first, labelled "Taskbar: …", then the two groups, then the exclude list. |

## Optional

| Item | Change |
|---|---|
| Function-local heap `thread_local`s leak per unload | Namespace scope, emptied by `ReleaseUiThreadLocals_UIThread` in the unload cleanup on each UI thread (seeded repeaters, last ladder / resolve log, last flyout key, image path cache, deferred log list). |
| `try`/`catch` in `TaskListButton_UpdateVisualStates_Hook` | Everything after the original call in one `try`. |
| Dead code | Removed: the `IAsyncAction` `DispatcherOpWasQueued` overload, the `WhRecentFocusThumbGlowL1` rectangle, the fallback `IVirtualDesktopManager` declaration, the no-op `ReleaseVdm()` in uninit. Kept: `kClsidVirtualDesktopManager` (no dependency on a uuid library exporting `CLSID_VirtualDesktopManager`). |
| References to documents outside the repo / version history in comments | Removed (about 100); comments keep the reason. Comment-only: the code with comments stripped is identical. |
| Settings text | `promoteMode` is "Taskbar: coming back to an app" with options matching the description; no "recency map", "confirm", "min-focus"; stale "below" / "Icons →" references fixed. |
| `LOG_DEFERRED` layer | Removed from the mod; kept only in the perf build, see below. |
| `GetLastInputInfo` is session-wide | Known trade-off: any input while the app is in front counts as its use (mouse movement on another monitor too). Per-window input needs low-level input hooks, which we avoid; counting media playback as use was considered and dropped (no per-app, reliable public signal). Now also under "Compatibility and limitations" in the README. |

## LOG_DEFERRED

Why it exists: `Wh_Log` with logging on is I/O; several lines are produced
while `g_stateMutex` (or another lock the taskbar thread takes to paint) is
held, so a slow log write would stall the taskbar. Inside a
`DeferredLogScope`, `LOG_DEFERRED` formats into a thread-local list that the
scope prints after the lock is released.

The review's point: with logging off, `Wh_Log` is nearly free, but
`LOG_DEFERRED` inside a scope still formats (1 KB buffer) and allocates a
string; and it is one more custom layer. Their alternative: copy values out
of the locked section, log after unlocking. In practice the deferred lines
are state changes (confirms, decays, identity changes), not per paint.
Decision (Jakub, 0.12.3): removed from the mod. It came from Fable review
2.5, prompted by one 18.3 ms worker lock hold in a quiet perf run, where
logging is off anyway; for users (logging off by default) `Wh_Log` under a
lock costs nothing. The mod now logs with plain `Wh_Log`. Only the perf
build defers lines logged under a timed lock (`PERF_LOG` in
`tests/perf/perf_header.inc`), so normal perf runs still exclude log I/O
from lock holds.
