# Claude submission review (round 11) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5701740290

`review-response11.md` was written against v0.9.25 and was not posted. The code since then is v0.9.31. The draft below is the comment to post with that file. The unload answer is the part that had to change: the Low callback no longer signals the waiter.

---

## Draft PR comment

Thanks — two real bugs, then the rest. This is v0.9.31 (later than the reviewed `22bff1d`).

**1. `TryRunAsync` bool-cast — agreed, that would hang uninit.**

`static_cast<bool>(TryRunAsync(...))` only tests that the operation object is non-null. We read “was it queued?” from `Status()` / `GetResults()` (same pattern as `taskbar-vertical`), including `IAsyncAction` when the SDK returns that instead of `IAsyncOperation<bool>`.

For the uninit drain the Low callback is empty and does not signal. `Completed` is the barrier: it always `SetEvent`s, and the operation is kept alive until after `WaitForSingleObject`. Signaling from the Low callback let unload return while `Completed` was still inside the mod. `Completed` is assigned once. If the operation cannot be subscribed, we signal immediately and record the failure.

The same queued-check is on `RunOnUiThread`, `SchedulePreviewFlyoutRefresh` (a failed queue clears the flyout latch), and `ScheduleRefreshAllHighlights`.

**2. Init stays loaded when hooks fail — agreed, with one caveat.**

- `taskbar.dll` identity hooks fail → `return FALSE` (Windhawk shows the error and reloads on the next settings change).
- `Taskbar.View.dll` is already loaded but its hooks fail → `return FALSE`.
- `g_taskbarViewDllLoaded` is set only after a successful View hook, so `Wh_ModAfterInit` / `LoadLibraryExW` can retry.

We do not `return FALSE` when Taskbar.View is simply not loaded yet. Explorer often loads it after `Wh_ModInit`. That is the same late-load path as `taskbar-thumbnail-reorder` (`Taskbar view module not loaded yet` + `LoadLibraryExW`). Failing init there would make the mod never attach on a normal Explorer start.

**3. Title-based matching — dropped.**

Unique-title HWND assignment and the title-score path are gone. A preview card matches TaskItem/DataContext first, then repeater `GetAt` for holes only. A card that misses both stays unmarked. The removed code is in `stash/preview-unique-title.cpp`.

**4. Line count.**

- Identity scoring is `bool IdentityMatchesRank` (it only ever returned 0 or 1000).
- Per-dispatcher fan-out stays. Primary and secondary taskbars share one Explorer UI thread; collapsing to a single dispatcher without a second thread to drain would be speculative.

### Optional

- **Icons thickness/roundness on previews.** Called out under Previews → Style. Title-bar thickness follows Icons → Thickness. The native preview card keeps its own corners; the overlay plate fallback uses Icons → Roundness. Side and edge icon bars do not use Roundness.
- **`CLSCTX_INPROC_SERVER`.** Agreed.
- **Log volume.** Agreed — dropped the per-button dump. The rank list log stays.
- **`TargetItemKey` flag if original throws.** Agreed — RAII reset. The hook also clears the captured thumbnails collection on entry.

### Functionality notes

- **Native `IconPanel` reorder.** The side bar has to sit above a Taskbar Styler hover plate and under OverlayIcon. A purely additive overlay does not express those constraints. The repair layer stays. OverlayIcon is raised after the snapshot restore.
- **Identity on the UI thread.** Full bind and press only, throttled, including the stale-path check. Not moving that COM onto the focus thread (that would resolve XAML off the UI thread).
- **Uninit `INFINITE`.** Intentional. Finding 1 was the hang. A timeout with a live thread or a live `SizeChanged` is worse.
- **Registry desktop id lag.** Known. The next foreground event or desktop switch refreshes it.

/ai-review
