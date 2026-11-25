# Claude submission review — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5530832046

Verified each finding against `taskbar-recent-focus-highlight.wh.cpp` before changing code. Verdicts below.

---

## 1. Teardown timeouts — **valid, will fix**

`StopWinEventHookThread` waits 8s then returns anyway (`WaitForSingleObject(hThread, 8000)`). `RunOnEachUiDispatcherAndWait(..., 2000)` does the same. `Wh_ModUninit` then returns and Windhawk `FreeLibrary`s the image.

That is a real crash path: the focus thread can sit in `SHGetPropertyStoreForWindow` / `GetProcessImagePath` on a hung app, and `SizeChanged` lambdas stay registered.

**Fix:** `INFINITE` waits, matching `taskbar-clock-customization`. Item 3 (no COM under the mutex) plus item 6 (dispatcher list, no off-thread `weak.get()`) make those waits actually able to finish.

---

## 2. Message-window class / dangling WndProc — **valid, will fix**

Confirmed:

- `RegisterClassExW` result ignored
- failed `CreateWindowExW` returns without `UnregisterClassW`
- `hInstance = GetModuleHandle(nullptr)` → explorer.exe, which never unloads

Re-enable after a failed/partial unload can reuse the old class with a WndProc in unmapped memory.

**Fix:** `GetModuleHandleExW(FROM_ADDRESS | UNCHANGED_REFCOUNT)` on `HookThreadWndProc`; check register; unregister on every exit; treat `ERROR_CLASS_ALREADY_EXISTS` as OK only for *our* module handle.

---

## 3. Blocking COM while holding `g_stateMutex` — **valid (slightly mislocated), will fix**

The first lock in `OnMinFocusTimerElapsed` only copies `g_pendingFocus`. The bug is the **second** lock (~7182): it calls `GetWindowClassName`, `GetWindowAppUserModelId` (`SHGetPropertyStoreForWindow`), and `GetProcessImagePath` while holding `g_stateMutex`. The UI thread takes that mutex in `ApplyAllHighlights_UIThread` / `ScoreButtonForRank` / preview `tickFor`.

**Fix:** resolve class / AUMID / path **before** the lock; only assign under it. Same for the preview-confirm path that still does `GetProcessImagePath` under the lock (~7095–7101).

---

## 4. `IVirtualDesktopManager` apartment — **valid, will fix**

`EnsureVdm` `CoCreateInstance`s on whichever thread hits it first. `RefreshCurrentDesktopId` runs from both the worker and the UI thread (`ApplyAllHighlights_UIThread`, `CopyRecentWindowsForPreview`, `ConfirmPreviewFocusNow`). `ResolveCurrentDesktopId` always calls `TryGetWindowDesktopId(GetForegroundWindow())` even when the registry already succeeded — COM on every paint, for a debug log.

**Fix:**

- Registry is the source of truth
- VDM only if registry fails, and **only on the worker thread**
- UI reads cached `g_currentDesktopId`
- Refresh on `EVENT_SYSTEM_DESKTOPSWITCH` (already hooked) plus the decay timer as a cheap backup
- `ReleaseVdm` only on the worker (after unbounded join, uninit sees null)

---

## 5. Cross-thread `SetTimer` — **valid, will fix**

`ScheduleRefreshAllHighlights` runs on the **taskbar UI** dispatcher and does `SetTimer(g_hookThreadHwnd, ...)`. That HWND is owned by the focus thread. MSDN: the window must belong to the calling thread. So the trailing 300ms full rebind very likely never arms.

Min-focus / decay `SetTimer`s from `HookThreadWndProc` are fine.

**Fix:** `PostMessage(g_hookThreadHwnd, WM_APP_REQUEST_APPLY_DEBOUNCED, 0, 0)` and arm the timer in `WndProc`. Make `g_hookThreadHwnd` `std::atomic<HWND>`.

---

## 6. XAML `weak_ref::get()` off the UI thread — **valid, will fix**

`CollectUiDispatchers` does `weak.get()` on buttons, thumb views, and layout-watch panels from the focus thread (`RunOnUiThread`) and from uninit. If that temporary is the last ref, `~FrameworkElement` runs off the UI thread.

**Fix:** when tracking an element on the UI thread, also store its `CoreDispatcher` (agile) in a separate list. `CollectUiDispatchers` iterates that list only — no cross-thread `weak.get()` on XAML objects. Matches the Windhawk wiki rule.

---

## 7. Heavy per-hover work — **valid, will fix (three parts)**

| Hot spot | Verdict |
|---|---|
| `ExpandSameClassWindows` → `EnumWindows` per seed, per card | Real. Tied to item 8; drop this path. |
| `ResolveGlowBaseColor()` builds `UISettings` on every UVS paint | Real. Cache accent in `LoadSettings` (and on settings change). Skip `ColorValuesChanged` for now — extra event teardown. |
| `RefreshCurrentDesktopId()` registry+COM per apply/preview | Real. Covered by item 4’s cache. |

---

## 8. Repeater index vs title/group-order/`EnumWindows` — **partly agree; will add the path, not delete everything**

The review is right that:

- `AutomationProperties.GetPositionInSet` is a weak visual order
- `EnumWindows` on hover is too expensive
- group-order after a click is the known “index ≠ visual” bug

It is **wrong** that we can delete the whole fallback chain and be done. This codebase already found that `DataContext` ↔ ctor-map identity often fails (projection mismatch). `taskbar-thumbnail-reorder` works because it matches **GetAt ABI pointer → ctor map**, not DataContext. That needs extra **optional** hooks we do not have yet:

- `TaskGroup::Thumbnails`
- `IVector::Size` / `GetAt`
- `HoverFlyoutModel::TargetItemKey` (to know which collection is live)
- WinUI2 `ItemsRepeater` (`WH_WINRT_WINUI2`)

Those symbols are optional even in the reorder mod. If they miss, previews still need a fallback.

**Plan:**

1. Add repeater `TryGetElement(i)` + `GetAt(i)` + ctor-map as the **primary** card → HWND path
2. Drop `ExpandSameClassWindows` / per-seed `EnumWindows`
3. Drop group-order index assignment (the dangerous fallback)
4. Keep **unique-title** only when the repeater path is incomplete (identical-title twins stay unmatched — already the product rule)
5. Keep `IconsRepeater` child-count for snap-group cards (item 9)

That is the largest change. Treat it as its own slice so we can test flyouts (3 windows, same titles, snap groups, Calibre/Lister).

---

## 9. English UI strings — **valid for snap-groups; fuzzy names are documented fallback**

- `IsSnapGroupThumbnailView` matching `"Group"` / `" other window"` **will fail on non-English Windows**, so snap-group cards get a window glow. The `IconsRepeater` ≥ 2 children check is already language-independent — make that the **only** detector (plus “no HWND from GetAt” once item 8 exists).
- `NormalizeAutomationName` stripping `" N running"` / `" pinned"` only affects the **fuzzy** icon path, after path-cache miss. Do not rewrite fuzzy for every locale in this pass; note it in the README.

---

## 10. `ClearButtonHighlight` wiping other mods’ `ScaleTransform` — **valid, will fix**

Both clear and “sizeBoost == 0” in `ApplyButtonHighlight` do `try_as<ScaleTransform>()` then `ClearValue`. That will clobber `taskbar-dock-animation`.

**Fix:** remember the `ScaleTransform` instance we set (on the path-cache entry or a weak_ref). Clear only if `RenderTransform()` is still that object.

---

## 11. `LoadSettings` forcing preview 100/70/45 — **valid, will remove**

The all-zero cluster override is a local-recompile workaround. For submitted mods Windhawk supplies YAML defaults. Setting all three preview intensities to 0 (or count 0) should stick. `previewHighlightEnabled` is the off switch.

---

## 12. README — **valid, will rewrite**

- Embed the two screenshots. Windhawk only allows `i.imgur.com` and `raw.githubusercontent.com`. PR images on `private-user-images.githubusercontent.com` will **not** work.
- Drop “See the repo README.md…”
- Replace the 11-item tester checklist with a short user description of styles, ranks, and virtual desktops.

---

## Optional (cheap ones to take)

| Item | Verdict |
|---|---|
| `void Wh_ModSettingsChanged()` | Yes — `*bReload` is always FALSE |
| `Wh_GetStringSetting` never NULL | Yes — drop dead null checks; `StringSetting` RAII is nice-to-have |
| Snapshot `SettingsSnap()` once per paint function | Yes |
| Null-check `kernelbase` | Yes |
| `HwndMatchesStoredPid`: pid 0 from `GetWindowThreadProcessId` → **false** | Yes — destroyed HWND should not match |
| Leak `done` on timeout | Goes away with INFINITE wait |
| `-loleaut32` / `-lpropsys` | **Keep** — `PropVariant*` + `PKEY_AppUserModel_ID` |
| `#include <psapi.h>` | **Remove** — unused (`QueryFullProcessImageName` is kernel32) |

## Functionality notes (will **not** do in this pass)

- **Drop fuzzy matching:** too large, and path cache can fail (taskbar.dll hooks optional). Keep as last resort; README already says path cache is preferred.
- **Don’t skip init when `enabled` is false:** settings changes do not reload the mod. If we skip the thread, turning Enabled on later would paint nothing until a full reload. Current behavior (track, don’t paint) is correct.
- Primary-taskbar `SHAppBarMessage` fallback, idle decay timer — leave; rare / cheap.
- Unindented “Second pass” comment — fix if we touch that function.

---

## Suggested order of work

1. **Unload-safe:** 1, 2, 6, 5 (`atomic` HWND + PostMessage timer)
2. **Thread/COM:** 3, 4, 7 (accent + desktop cache)
3. **Paint coexistence:** 10, 11, optional polish
4. **Preview identity:** 8 + 9 (biggest behavioral change)
5. **README** last, once screenshot URLs are settled
