Thanks for the review. Updated to 0.12.3:

**Must fix**
- **Unload race:** fixed as suggested. After the Low sentinel runs, `WaitUntilUiThreadLeftModCode` sends `WM_NULL` (`SendMessageTimeoutW`, repeated) to a window of that UI thread, so `Wh_ModUninit` returns only once the thread is back in its message loop, past the `Completed` delegate and the sentinel. The wait ends if the thread exits. Tested with repeated disable / enable.
- **Moved settings:** reverted. `highlightCount`, `minFocusSeconds`, `promoteMode` and `decayMinutes` keep their top-level ids again and the second transition is gone (`previews.style` stays). The page is reordered and relabelled instead: the four "Taskbar: …" settings first, then the Taskbar icons and Thumbnail previews groups, then the exclude list.

**Optional**
- Function-local heap `thread_local`s moved to namespace scope and emptied in the unload cleanup on each UI thread.
- `TaskListButton_UpdateVisualStates_Hook`: everything after the original call in one `try`/`catch`.
- Dead code removed: the `IAsyncAction` `DispatcherOpWasQueued` overload, the `WhRecentFocusThumbGlowL1` rectangle, the own `IVirtualDesktopManager` declaration, the no-op `ReleaseVdm()` in uninit. The CLSID constant stays (no dependency on a uuid library exporting it).
- Comments: references to documents outside the file and per-version history removed; comments give the reason only.
- `LOG_DEFERRED` / `DeferredLogScope` removed; the mod logs with plain `Wh_Log`. It was there for our performance measurements; only our separate perf build defers lines logged under a lock now.
- Settings texts: option labels match their descriptions, internal terms ("confirm", "recency map", "min-focus") replaced, stale references fixed.
- `GetLastInputInfo` being session-wide is intended: "used" means any input while the app is in front. It is now stated under Limitations in the README, including that watching a video without input lets the app decay.
