# Claude submission review (round 2) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5537851679

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.1 (post-round-1 fixes).

---

## Draft PR comment

Thanks for the second pass. Round 1 already took the unload timeouts, message-window class, COM-under-mutex, VDM apartment, cross-thread `SetTimer`, dispatcher list, accent cache, preview `GetAt` path, snap-group `IconsRepeater` check, `ScaleTransform` coexistence, preview-settings override, and README. This round is mostly leftover unload ordering plus a push to drop name heuristics. Responses below.

**1. Uninit order — agreed, will fix.**

Confirmed: `RunOnEachUiDispatcherAndWait` still runs before `StopWinEventHookThread`. `OnDecayTimer` / `OnVirtualDesktopSwitched` / `RequestApplyVisuals` / `RunOnUiThread` have no `g_unloading` early-out, so they can `TryRunAsync(Normal, …)` after the Low drain sentinel. That is a real post-`FreeLibrary` crash.

Will stop the focus thread first, then drain dispatchers, and add the missing guards on `RequestApplyVisuals`, `RunOnUiThread`, `OnDecayTimer`, and `OnVirtualDesktopSwitched`. `RequestApplyPreviewVisuals` already bails on `g_unloading`.

**2. Lost stop signal / INFINITE wait — agreed on the race, not on giving up the dispatcher wait.**

The `CreateThread` → first `GetMessage` window is real: `StartWinEventHookThread` returns before the worker has a queue, `PostThreadMessage` can fail with `ERROR_INVALID_THREAD_ID`, and `WaitForSingleObject(..., INFINITE)` then never returns.

Will add a manual-reset “ready” event, set after `CreateWindowExW` succeeds (and also on the early-fail paths so Uninit cannot wait forever on a dead thread), and wait for it in `StartWinEventHookThread`. Shutdown will `PostMessage(WM_APP_SHUTDOWN)` to the window once it exists, and still `PostThreadMessage` as a backup.

Will **not** put a timeout back on `RunOnEachUiDispatcherAndWait`. Round 1 required an unbounded wait because a timed-out drain leaves `SizeChanged` lambdas in the image and the next relayout crashes Explorer. A wedged taskbar UI thread already means Explorer is stuck; hanging Disable is better than a later crash. The worker-thread hang is the one we can close with the ready event.

**3. Strong `CoreDispatcher` globals — agreed, will fix.**

`CoreDispatcher` is agile (that is why we store it instead of `weak_ref<FrameworkElement>`), but `~vector` on process teardown still runs after XAML threads are gone. Will switch to `[[clang::no_destroy]] std::optional<std::vector<CoreDispatcher>>` and `reset()` in `Wh_ModUninit`, per the wiki sections cited.

**4. `ERROR_CLASS_ALREADY_EXISTS` — agreed, will treat as failure.**

Round 1 allowed that error so a leaked explorer.exe-attributed class would not block reload. The class is now registered with the **mod** module handle, so `ALREADY_EXISTS` means a previous instance’s `WndProc` is still the one in the class atom. Creating a window on that class is the dangling-WndProc crash. Will fail the thread instead of reusing it.

**5. Fuzzy name matching — partial agree; will stop using it for icon ranks.**

The English `" running"` / `" pinned"` strip and the app-specific `LISTER` / `TOTALCMD` / `VSCODIUM` / `STEAM` / `CALIBRE` noise list are a maintenance liability, and a wrong glow is worse than no glow. Icon assignment already has button → `ITaskItem` → HWND → path / AUMID. Will bind icons only on that exact identity (path, AUMID, HWND). No subsequence / initials / shared-prefix / name-cache second pass.

Will **keep** unique-title matching for **preview cards only**, and only when the optional `ItemsRepeater` + `Thumbnails.GetAt` path did not bind that card. That is a different problem (several windows of one app, ctor/GetAt miss on some builds). Identical titles stay unmatched. `ScoreTitleToAutomationName` stays for that fallback; the icon fuzzy helpers go.

If `taskbar.dll` identity hooks fail, icons simply will not glow. That is the intended failure mode.

**6. Re-resolve on every UVS / full rebind — agreed, will fix.**

`EnsureButtonPathCached(..., false)` is still called from `TrackButton_UIThread` (every `UpdateVisualStates`) and from `ApplyAllHighlights_UIThread` (every button). Debounce is 2 s if resolved, **250 ms** if not — so a pinned icon is a `ReportClicked` sentinel + `OpenProcess` several times a second while the pointer moves.

A path does not change for the life of the button. Will resolve only when the cache entry has no path yet (or `force=true` from `OnPointerPressed`). No periodic retry.

`EnsureIconPanelLayoutWatch` still runs `DetectTaskbarEdge` (visual states + geometry + `SHAppBarMessage`) on every UVS even when the panel is already watched. Will skip that when a watch already exists; `SizeChanged` already heals edge changes.

### Optional

Will take:

- Dead code: `WM_APP_REQUEST_APPLY` / `WM_APP_REQUEST_PREVIEW_APPLY` (never posted), `ApplyVisualHighlights()`, `GetCachedButtonPath()`, `g_taskbarDllHooked`.
- `#include <string_view>`.
- `GetModuleHandleW(L"taskbar.dll")` first; `LoadLibraryEx` only if it is not already loaded (avoid a leak on every reload).

Will skip for now:

- Dropping `glowDebugLog`. Windhawk’s toggle gates `Wh_Log` entirely; this setting only gates **verbose bind/preview-resolve** lines so a user who turns Windhawk logging on for another reason is not flooded. Happy to drop it if maintainers prefer one switch.
- `WindhawkUtils::StringSetting` — nice, not required for this round.
- `-loleaut32` — `PropVariantInit`/`PropVariantClear` are used for AUMID; will leave the linker flag unless a build shows it unused.
- Pre-checking the 300 ms debounce before `TryRunAsync` in `ScheduleRefreshAllHighlights` — cheap, can do with item 6.
- Moving `ConfirmPreviewFocusNow` off the UI thread, brush caching, running-grace, 180×120 placeholder — functionality notes, not this pass.

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| 1 | Valid | Swap Uninit order; `g_unloading` guards on apply/decay/desktop-switch/`RunOnUiThread` |
| 2 | Valid race; reject bounded UI drain | Ready event on worker; keep INFINITE dispatcher wait |
| 3 | Valid | `no_destroy` optional vector + `reset()` |
| 4 | Valid | Fail on `ERROR_CLASS_ALREADY_EXISTS` |
| 5 | Partial | Drop icon fuzzy bind; keep preview unique-title fallback |
| 6 | Valid | Resolve path once; skip `DetectTaskbarEdge` if already watched |
| Optional dead code | Valid | Remove unused messages/helpers |
| Optional glowDebugLog | Keep | Verbosity filter, not a second logging master |
| Optional StringSetting | Skip this pass | |
| Optional LoadLibraryEx leak | Take | GetModuleHandle first |
| Functionality notes | Skip this pass | |

Round-1 vs round-2 conflict: round 1 required INFINITE waits; round 2 wants a timeout on the UI drain. SizeChanged tokens make the timeout unsafe. Ready event fixes the worker hang without bringing timeouts back.
