# Claude submission review (round 11) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5701740290

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.25.

---

## Draft PR comment

Thanks — two real bugs, then the rest.

**1. `TryRunAsync` bool-cast — agreed, that would hang uninit.**

`static_cast<bool>(TryRunAsync(...))` only tests that the operation object is non-null. `posted` / `drainPosted` / `any` were unconditionally true, so a dispatcher that refuses the work never took the `!drainPosted` `SetEvent` path and `WaitForSingleObject(..., INFINITE)` could block Disable.

We now read “was it queued?” from `Status()` / `GetResults()` (same pattern as `taskbar-vertical`). For the uninit Low drain, a `Completed` handler also `SetEvent`s if the work never ran. The same check is on `SchedulePreviewFlyoutRefresh` (a failed queue used to leave the flyout-refresh latch set forever) and `ScheduleRefreshAllHighlights`.

**2. Init stays loaded when hooks fail — agreed, with one caveat.**

- `taskbar.dll` identity hooks fail → `return FALSE` (Windhawk shows the error and reloads on the next settings change).
- `Taskbar.View.dll` is already loaded but its hooks fail → `return FALSE`.
- `g_taskbarViewDllLoaded` is set only **after** a successful View hook, so `Wh_ModAfterInit` / `LoadLibraryExW` can retry.

We do **not** `return FALSE` when Taskbar.View is simply not loaded yet. Explorer often loads it after `Wh_ModInit`; that is the same late-load path as `taskbar-thumbnail-reorder` (`Taskbar view module not loaded yet` + `LoadLibraryExW`). Failing init there would make the mod never attach on a normal Explorer start.

**3. Title-based matching — dropped.**

Pass 3 (unique-title HWND) and the title-score tick copy (old pass 4) are gone. Preview cards that miss TaskItem / repeater GetAt stay unmarked. Code is in `stash/preview-unique-title.cpp` if we need it later. On current Win11, pass 1/2 already fill every card we test.

**4. Line count.**

- Identity scoring is now `bool IdentityMatchesRank` (it only ever returned 0 or 1000).
- Per-dispatcher fan-out stays. Primary and secondary taskbars share one Explorer UI thread; collapsing to a single dispatcher without a second thread to drain would be speculative (same as last round’s `HasThreadAccess` note).

### Optional

- **Icons thickness/roundness on previews.** Agreed — called out in the Previews → Style description (title-bar thickness / plate corners).
- **`CLSCTX_INPROC_SERVER`.** Agreed.
- **Log volume.** Agreed — dropped the per-button dump; rank list stays.
- **`TargetItemKey` flag if original throws.** Agreed — RAII reset.

### Functionality notes

- **Native `IconPanel` reorder.** Side bar still has to sit above a Taskbar Styler hover plate and under OverlayIcon; a purely additive overlay does not express those three constraints. Repair layer stays; OverlayIcon is raised after snapshot restore (Thunderbird).
- **Identity on the UI thread.** Full bind only, now also throttled for the stale `OpenProcess` check. Not moving that COM onto the focus thread this round (would re-introduce cross-thread XAML).
- **Uninit INFINITE.** Intentional. Finding 1 was the hang; a timeout with a live thread is worse.
- **Registry desktop id lag.** Known; next FOREGROUND / desktop-switch refreshes it.
