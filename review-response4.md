# Claude submission review (round 4) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5569454349

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.3 (post-round-3: identity maps, paint early-out, nested settings, no in-mod enable/debug).

---

## Draft PR comment

Thanks for the fourth pass. Round 3 keyed the caches by COM identity, early-outed paint on `(rank, generation)`, cached the taskbar edge, refused `SizeChanged` when the dispatcher cannot be recorded, and switched to nested settings. Five concrete leftover bugs below; all agreed.

**1. Accent handlers on a message-only window — agreed, will fix.**

Confirmed: `CreateWindowExW(..., HWND_MESSAGE, ...)` so `WM_DWMCOLORIZATIONCOLORCHANGED` / `WM_SETTINGCHANGE` never arrive. `WM_APP_REFRESH_ACCENT` only runs from `LoadSettings`. Default `glowColor: accent` therefore stays stale until a settings touch or reload.

Will subscribe to `UISettings::ColorValuesChanged` (same pattern as taskbar-styler), `PostToHookThread(WM_APP_REFRESH_ACCENT)` from that callback so `RefreshCachedAccent` stays on the focus-thread STA, revoke the token in `Wh_ModUninit`, and drop the two broadcast `case`s.

**2. Recycled `IUnknown*` cache keys — agreed, will fix.**

The comment already says the weak_ref is the liveness token, but `EnsureButtonPathCached` returns on key hit without checking `it->second.button.get() == button`. Explorer can reuse a heap address for a new `TaskListButton`; the new icon then inherits path / AUMID / HWND / `lastPaintRank`. Same hole in `GetCachedPaintState` / `SetCachedPaintState` / `GetCachedButtonIdentity` / `CachedTaskbarEdge`.

Will erase the entry when the weak_ref is dead or is a different object, on every identity-map lookup (path cache, paint state, layout-watch edge), not only the resolve path.

**3. `RestoreNativeRunningIndicator` — agreed, leftover, will delete.**

We no longer collapse `RunningIndicator` (only our own glow / thumb rectangles set `Collapsed`). The restore path can un-collapse a Styler or shell local value, and if `RunningIndicatorStates` is missing it force-sets `Visible` on a pinned icon. Will drop `RestoreNativeRunningIndicator` / `RunningIndicatorHasLocalCollapsed` and their call sites. We'll also do a dead-code sweep in this pass so similar leftovers don't sit around.

**4. `UpdateLayout()` inside `OnApplyTemplate` — agreed, will delete.**

`GetTitleBottomRelative` already returns `false` and `ScheduleThumbnailRelayout` redoes placement at Low after the pass. The `title.UpdateLayout()` call is the layout-cycle risk; it goes.

**5. Overlay sweep still reorders native children — agreed, will fix.**

The hover early-out is gated on `!g_pendingOverlaySweep`, and that flag is set on every desktop switch and rank-changing decay, so `ClearButtonHighlight` still hits `RestoreIconPanelNativeZOrder` on buttons with no chrome. Sweep should only remove leftover `WhRecentFocusGlow`. Z-order heal stays on the insert/remove-our-host path.

### Optional

- **`taskbar.dll` ref.** `GetModuleHandle` first; `LoadLibraryEx` only if it is not already loaded. Will keep the `HMODULE` and `FreeLibrary` in `Wh_ModUninit` **only if we were the ones who `LoadLibraryEx`'d**. Explorer's own ref is left alone.
- **`RefreshCachedAccent` vs `LoadSettings`.** Small window, but real. Will store the accent in its own `std::atomic<uint32_t>` (packed ARGB) outside `Settings`, so an accent event cannot republish a stale settings snapshot.
- **`BarLengthForSide` vestigial args.** Length used to depend on the `IconPanel` box, the taskbar edge, and a per-rank scale. Rank is now opacity-only and length is the glow-host cell, so `iconPanel`, `edge`, and `rankLenScale` are leftovers. Will drop them.
- **`HasVisualState` → `IsInVisualState`.** Agreed — it tests `CurrentState()`, not “does this state exist in the group.”
- **`-loleaut32`.** We dropped it last round and the link failed: WinRT `hresult_error` needs `SysFreeString` / `SysStringLen` from oleaut32 even though this file never calls them. Will keep `-loleaut32` and put that reason on the `@compilerOptions` line so it is not dropped again. `-lpropsys` stays off.

### Functionality notes

- **`ReportClicked` from UVS.** Valid concern. `TrackButton_UIThread` (every first `UpdateVisualStates` per button) still calls `EnsureButtonPathCached`, which drives the sentinel into `CTaskListWnd::HandleClick`. Volume-per-app only does that on a real scroll/click. Will stop resolving from UVS: identity resolve on full bind (`ApplyAllHighlights`) and `OnPointerPressed` only. First glow after a new button can wait for the 300 ms coalesced rebind; that is better than entering HandleClick on hover. If anything still looks like a flyout dismiss / jump-list side effect after that, we'll say so.
- **Score 900 (same file name, different folder).** This was the last filename tier after we dropped fuzzy names. Exact path (1000) already wins when both buttons exist, so two live `python.exe` folders stay distinct. The leftover failure is: ranked `C:\A\python.exe` is gone, `C:\B\python.exe` is on the bar → 900 lights B. That is a guess. Will drop 900; missing exact path / AUMID / HWND → no icon glow.
- **`g_thumbRelayoutPending` is process-wide.** The deferred callback already re-runs `RefreshThumbnailFlyout_UIThread` for the whole flyout, so sibling cards in the *same* hover are not left behind. The real hole is a second flyout (or a fast re-hover) while pending/depth is set. Will make the pending flag per dispatcher (or clear it at the end of the pass and allow one follow-up if titles are still unmeasured).
- **`TargetItemKey` vs stale cards.** That hook is the “hover target changed” signal, but the repeater may not have swapped realized views yet — new HWNDs on old cards, then `OnApplyTemplate` corrects it (a flash). Will clear our thumb chrome immediately and schedule the refresh at Low (same path as title relayout) so we do not paint the new app onto the previous flyout's cards.
- **Line count.** Preview unique-title stays as last-resort when GetAt misses (identical titles unmatched). Not cutting that this pass; 900 going away is the icon-side leftover.

Happy to have a human check on a real taskbar: accent change without touching settings; two `python.exe` folders; hover two multi-window apps in sequence; enable/disable while a flyout is open.

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| 1 | Valid | `UISettings::ColorValuesChanged` → `WM_APP_REFRESH_ACCENT`; drop broadcast cases; revoke token on uninit; `no_destroy` optional for `UISettings` |
| 2 | Valid | Weak-ref identity check + erase on recycle; all maps (path, paint, layout watch), not only `EnsureButtonPathCached` |
| 3 | Valid leftover | Delete restore helpers; dead-code sweep this pass |
| 4 | Valid | Delete `title.UpdateLayout()` |
| 5 | Valid | Sweep must not call `RestoreIconPanelNativeZOrder` without chrome |
| Opt taskbar.dll | Take | `loadedByUs` + `FreeLibrary` only then |
| Opt accent atomic | Take | Packed ARGB outside `Settings` |
| Opt BarLengthForSide | Take | Drop dead params — leftover from rank-scaled / IconPanel-based length |
| Opt rename | Take | `IsInVisualState` |
| Opt oleaut32 | **Keep** + comment | WinRT `hresult_error`; we already hit the link error |
| Fn ReportClicked | Take as a change | Resolve off UVS; bind + press only |
| Fn score 900 | Drop | Last filename guess |
| Fn thumb pending | Partial agree | Same-flyout OK (whole-flyout refresh); per-dispatcher pending |
| Fn TargetItemKey flash | Take | Clear chrome + Low schedule |
| Fn 7.5k | Skip cut | Unique-title stays |

### Optional 3 (BarLengthForSide) in plain language

The bar used to be sized from the native `IconPanel`, rotated from the detected taskbar edge, and shortened per rank (`rankLenScale`). Product rule later became: length is `%` of the **glow host** inner box, same for every rank; rank is opacity. The three parameters were never removed. Dead arguments, not a behaviour mystery.

### What to test on a real Windhawk install (if you want)

1. Default accent colour: change Windows accent, **do not** open mod settings — glow should follow (`ColorValuesChanged`).
2. Two folders of the same exe: only the focused path glows; the namesake stays dark (after 900 is gone).
3. Hover app A (2+ windows) then app B (2+ windows) quickly: no flash of A's ranks on B's titles.
4. Reload/enable: taskbar should not dismiss flyouts or shuffle jump lists from our identity resolve (no `ReportClicked` on hover).
5. Disable/unload with a flyout open and while hovering: chrome gone, no explorer crash, Styler plates return.

### Dead-code sweep (this pass)

Besides item 3: `BarLengthForSide` unused args, broadcast WndProc cases, `kScoreSameFileDifferentPath` if we drop 900, any `HasVisualState` leftover name.
