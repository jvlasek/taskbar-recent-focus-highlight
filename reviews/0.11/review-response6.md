# Claude submission review (round 6) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5590777197

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.6 (post-5b: idle decay timer, click confirm on the focus thread, flyout Low coalesce, `lastHwnd`+pid, snap-group GetAt guard, `|CLS:` gone).

---

## Copied review

Good round again. The decay timer now stops when nothing is tracked and re-arms on the next confirm, the thumbnail click confirm is posted to the focus thread with a PID guard instead of running `ResolveAppIdentity` inline on the taskbar UI thread, the whole flyout refresh is coalesced into one Low-priority pass per UI thread (so a 5-card flyout no longer does 5 full resolves), `AppFocusInfo::lastHwnd` got its recycle guard, the repeater/`Thumbnails` sizes are cross-checked before index binding, and the dead `Cand::score` sort plus the `|CLS:` key plumbing are gone. Teardown still reads correct end to end. Two things left, both in the button-identity path.

**1. A pinned button's identity is cached as "resolved" from its AutomationId, so the icon is never highlighted after you launch that app.**

`EnsureButtonPathCached` (line 3818) falls back to the AutomationId when the HWND lookup yields nothing:

```cpp
std::wstring autoIdUpper = GetButtonAutomationAppId(button);
if (appIdUpper.empty()) {
    appIdUpper = autoIdUpper;
}
```

Taskbar buttons carry that AutomationId whether or not the app is running — your own comment at line 1171 documents the `"Appid: com.squirrel.Discord.Discord"` shape. So the first resolve of a **pinned, not-running** button produces `pathUpper = ""`, `sampleHwnd = nullptr`, `groupHwnds = {}` and a non-empty `appIdUpper`, and the new fast path at line 3739 treats that as a finished resolve:

```cpp
const bool haveIdentity = !it->second.pathUpper.empty() ||
                          !it->second.appIdUpper.empty();
if (haveIdentity) {
    return it->second.pathUpper;   // "" forever
}
```

The taskbar reuses the same `TaskListButton` container when the app launches, so `WeakIsSameElement` still matches and the entry is never re-resolved. For a Win32 app the rank key is the image path (`MakeAppKey` only returns `APPID:` for UWP hosts), and `ScoreButtonForRank` (line 2135) then needs `ident.pathUpper == info.key`, or an HWND from `sampleHwnd` / `groupHwnds` — all three are empty, so the score is 0 and the icon stays unhighlighted no matter how long you use the app. The only escape is `force=true` from `OnPointerPressed`, i.e. the user has to click the taskbar button a *second* time (the click that launched the app resolves while it is still not running).

The same round's escape hatch has the opposite problem for the genuinely-empty case:

```cpp
if (unresolvedRecent && !TaskListButton_IsRunning(button)) {
    return {};
}
```

For a *running* button the `kUnresolvedRetryMs` throttle is bypassed entirely, so a running button whose identity never resolves re-runs the full resolve — `ReportClicked` sentinel ×2, the `HDPA` walk, `OpenProcess` + `QueryFullProcessImageName`, `SHGetPropertyStoreForWindow` — on every rebind pass, on the taskbar UI thread.

One change fixes both: re-resolve exactly once on the not-running → running transition, and keep the tick throttle for everything else.

```cpp
// ButtonPathCacheEntry
bool resolvedWhileRunning = false;

// EnsureButtonPathCached
const bool running = TaskListButton_IsRunning(button);
...
} else if (!force && it->second.resolveAttempted &&
           !(running && !it->second.resolvedWhileRunning)) {
    const bool haveIdentity = !it->second.pathUpper.empty() ||
                              !it->second.appIdUpper.empty();
    if (haveIdentity ||
        now - it->second.lastResolveTick < kUnresolvedRetryMs) {
        return it->second.pathUpper;
    }
}
...
e->lastResolveTick = now;
e->resolvedWhileRunning = running;
```

and drop the `unresolvedRecent` local and its early return.

**2. `HwndFromMappingEntry` dereferences an `ITaskItem*` the mod does not own.**

`ThumbnailTaskItemMapping::taskItem` is a raw pointer captured in the `TaskItemThumbnail` constructor hook with no `AddRef` (line 4234), and the fallback at line 4253 calls into it:

```cpp
HWND HwndFromMappingEntry(const ThumbnailTaskItemMapping& item) {
    if (item.hwnd && IsWindow(item.hwnd)) {
        return item.hwnd;
    }
    if (item.taskItem) {
        HWND h = GetWindowFromTaskItem(item.taskItem);   // reads *(void**)taskItem
```

That branch is reached precisely when the window is gone (`IsWindow(item.hwnd)` false) — which is also when the native `CWindowTaskItem` / `CImmersiveTaskItem` behind `taskItem` is most likely to have been released. `GetWindowFromTaskItem` reads the vtable slot, then `QueryViaVtable` scans up to 32 pointers from the object base, then calls `CWindowTaskItem::GetWindow` with the freed pointer as `this`. Entries are only pruned when a new thumbnail with the same identity is constructed or at the 128 cap, so a stale entry can outlive its task item while its `thumbnail` weak ref is still resolvable (close one window of a multi-window group while the flyout is open, then hover it again).

`entry.hwnd` is already resolved at construction time while the item is definitely alive, so the fallback buys nothing:

```cpp
HWND HwndFromMappingEntry(const ThumbnailTaskItemMapping& item) {
    return (item.hwnd && IsWindow(item.hwnd)) ? item.hwnd : nullptr;
}
```

[taskbar-thumbnail-reorder](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-thumbnail-reorder.wh.cpp#L827), which this mapping is adapted from, stores the same raw `taskItem` but only ever *compares* it against pointers in the live `HDPA` — it never dereferences it. If you do need a late lookup, hold a `winrt::com_ptr<IUnknown>` instead of the bare pointer.

### Optional improvements

Minor polish — none of this affects users, so it's your call.

* **`ButtonPathCacheEntry::pid` / `ButtonIdentity::pid` is write-only.** It's assigned at line 3845 and copied at line 3896, and nothing ever reads it — `ScoreButtonForRank` matches on path, AUMID and HWND only. Drop the field.

* **`windowOrdinal` is always equal to `si`.** In the pass-1 loop (lines 5549–5568) both start at 0 and are incremented together on every non-snap view, so `compactSnap ? windowOrdinal : srcIndex` can just be `compactSnap ? static_cast<int>(si) : srcIndex`.

* **The `e.appIdUpper.empty() ? e.autoIdUpper : e.appIdUpper` fallbacks are dead.** Since line 3819 already copies `autoIdUpper` into `appIdUpper` when the latter is empty, `appIdUpper` can only be empty when `autoIdUpper` is too — so the ternaries at lines 1483 and 2127 always take the first branch.

* **`TaskListButton_IsRunning` treats a failed call as "not running"** (line 1888, `SUCCEEDED(hr) && isRunning`), which suppresses the highlight. [taskbar-centered-start-split-icons.wh.cpp#L759](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-centered-start-split-icons.wh.cpp#L759) deliberately defaults to *true* in that case so a failure degrades to "don't skip anything" rather than a silent misclassification. The symbol is a required hook here so it should always resolve, but the safer default costs nothing.

* **README credit.** The code comments name `taskbar-volume-control-per-app` (line 3458) and `taskbar-thumbnail-reorder` (lines 280, 6123) as the sources of the taskband resolve and the thumbnail mapping. Both are GPLv3 like this mod, so the license is fine — a line in the README crediting them would be a nice touch.

### Functionality notes

Non-critical observations and ideas about the feature behavior itself.

* **The overlay sweep still blanks ranked icons until the next full pass.** `RefreshButtonHighlight` (line 5906) starts with `if (g_pendingOverlaySweep.load()) { ClearButtonHighlight(button); return; }`, and the flag is set on every virtual-desktop switch and on every decay pass that changes the rank list. Any `UpdateVisualStates` between the flag going up and `ApplyAllHighlights_UIThread` clearing it wipes a highlight that is about to be repainted, so a desktop switch can show a brief flicker on the icons that keep their rank.

* **`PickLiveFlyoutThumbOnThisDispatcher` can anchor on a card from the previous flyout.** It returns the first live tracked view that still has an `ItemsRepeater` ancestor — and recycled containers keep that ancestor, so a card the repeater has already re-bound to another app can be picked. `RefreshThumbnailFlyout_UIThread` then resolves the flyout from that card. It self-corrects on the next `OnApplyTemplate`, but anchoring on the card that triggered the schedule (when it is still in the tree) would be more predictable than "oldest tracked".

* **Every foreground change drives a full rebind, twice per Alt+Tab.** `HandleForegroundChanged` calls `RequestApplyVisuals()` for the transient foreground (the Alt+Tab frame, line 6827) and again for the app that lands (line 6899). Each one iterates every tracked button × every rank. It's cheap now that identities are cached, but a single debounced pass per settled foreground would halve it.

* **The `ReportClicked` sentinel probe still runs outside real user input.** With the fix in item 1 it will additionally re-enter `CTaskListWnd::HandleClick` (short-circuited by the sentinel) once more per button, when the button flips to running. Worth confirming once that entering that path has no MRU / jump-list side effects, since the mod it's adapted from only triggers it from an actual click.

* **~7.6k lines is still a lot of surface** for an exact resolve (button → `ITaskItem` → HWND → PID → path) plus a recency list. The remaining title-based preview tiers (`ScoreTitleToAutomationName`, `MatchTitleToUnusedRecent`, the pass-4 same-PID tick copy) are the parts most likely to need re-verification on each Windows update; every one you can replace with "no highlight" is one less.

---

## Draft PR comment

Thanks — two more identity-path holes, both agreed.

**1. AutomationId counted as a finished resolve — agreed, will fix.**

Confirmed: pinned buttons have an AutomationId (`Appid: com.squirrel.Discord.Discord` and the same shape for ordinary Win32). We copy that into `appIdUpper` when there is no HWND, then `haveIdentity` is true, so the empty-path retry from last round never runs. Explorer reuses the same `TaskListButton` on launch. Win32 ranks are the image path, so `ScoreButtonForRank` has neither path nor HWND and the icon stays dark. The launch click’s `force` runs while the process still does not exist.

The `IsRunning` bypass of `kUnresolvedRetryMs` is the other side of the same hatch: a running button that never gets a path re-enters `ReportClicked` on every full bind.

Will take the suggested `resolvedWhileRunning` bit: one re-resolve on the not-running → running transition, tick throttle for everything else, drop `unresolvedRecent`.

**2. `HwndFromMappingEntry` UAF on a raw `ITaskItem*` — agreed, will fix.**

Confirmed: we store the ctor’s `taskItem` without `AddRef`, and the fallback calls `GetWindowFromTaskItem` exactly when `IsWindow(hwnd)` is already false — the case where the native item is most likely gone. `QueryViaVtable` then walks freed memory. Thumbnail-reorder keeps the same raw pointer but only compares it. HWND is captured while the item is alive; the fallback buys nothing. Will return that HWND or null. Keep the raw pointer for identity compare in the map (same as reorder); no late dereference.

### Optional

- **`pid` write-only.** Agreed, dropping `ButtonPathCacheEntry::pid` and `ButtonIdentity::pid`.
- **`windowOrdinal == si`.** Agreed, using `si`.
- **`appIdUpper` / `autoIdUpper` ternary.** Agreed — after the cache write, empty `appIdUpper` means empty `autoIdUpper` too. Dropping the ternaries and the stored `autoIdUpper` field (the local AutomationId read still fills `appIdUpper` when the window AUMID is missing).
- **`IsRunning` fail-open.** Not taking. Pinned-not-running must never glow; a failed getter defaulting to true would light pinned icons. Wrong glow is worse than none. The symbol is a required hook, so a failure is already a “don’t guess” path.
- **README credit.** Agreed — a short line for `taskbar-volume-control-per-app` and `taskbar-thumbnail-reorder`.

### Functionality notes

- **Sweep flicker.** Agreed. UVS will not `ClearButtonHighlight` just because `g_pendingOverlaySweep` is set; cached ranks stay painted and the coalesced `ApplyAllHighlights` is the sweep.
- **Flyout pick.** Agreed. The Low callback will prefer the card that scheduled the pass if it is still live and still under an `ItemsRepeater`, then fall back to `PickLiveFlyoutThumbOnThisDispatcher`.
- **Alt+Tab double rebind.** Agreed for the *repaint* path. Transient FG and “ranks already exist” will post the existing 300 ms debounce instead of calling `ApplyAllHighlights` inline. Rank-changing confirms, decay, and desktop switch stay immediate.
- **`ReportClicked` on the running transition.** Written down. Item 1 adds one more sentinel enter per button, when it flips to running. Still no second identity path; watch jump lists in a human test.
- **Title-based preview tiers / line count.** Not cutting this pass. Unique-title stays last-resort when GetAt misses; `how=repeater|taskitem|title` is already in the log.

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| 1 AutomationId = haveIdentity | Valid | `resolvedWhileRunning`; one resolve on pinned→running; keep tick throttle; drop `unresolvedRecent` |
| 2 raw `taskItem` deref | Valid UAF | `HwndFromMappingEntry` = stored HWND or null. Keep pointer for `==` compare only |
| Opt pid | Take | Drop both `pid` fields |
| Opt windowOrdinal | Take | `compactSnap ? (int)si : srcIndex` |
| Opt autoId ternary | Take | Use `appIdUpper` only; drop stored `autoIdUpper` |
| Opt IsRunning default true | **Reject** | Fail-closed: pinned-only must not glow |
| Opt README credit | Take | One line, volume-per-app + thumbnail-reorder |
| Fn sweep flicker | Take | UVS must not clear on sweep flag |
| Fn pick stale card | Take | Prefer schedule-time weak_ref if still in a repeater |
| Fn Alt+Tab 2× rebind | Take (repaint only) | Debounce `HandleForegroundChanged` visuals; keep immediate on confirm/decay/switch |
| Fn ReportClicked | Write down | One extra enter on running transition |
| Fn 7.6k / unique-title | Skip cut | Log already has `how=` |

### Item 1 in plain language

Round 5 said “empty identity is not finished.” We treated “has an AUMID string” as finished. Pinned Discord (and most Win32) already has `AutomationId = Appid: …` while not running. That string is not the rank key for Win32 (`MakeAppKey` is path-only unless the host is AFH/WWAHost), so we locked in a cache entry that can never score. Launch reuses the button; glow never appears unless you click again after it is running.

`resolvedWhileRunning` is the right latch: the interesting transition is pinned → running, exactly once, not “retry forever while running.”

### Item 2 in plain language

Ctor map: `{ thumbnail weak_ref, taskGroup*, taskItem*, hwnd }`. HWND is read while `taskItem` is the live ctor argument. Later, if that window has closed, walking `taskItem` is a use-after-free. Comparing the pointer to another live pointer (reorder mod, our map de-dup) is fine.

### Why not fail-open `IsRunning`

centered-start-split-icons would rather label a pinned icon than skip it. We would rather skip a glow than put one on a pinned-not-running icon (product rule, virtual-desktop lists, catalog review). Required hook miss → no glow.

### Sweep flicker

```
UVS → RefreshButtonHighlight → if sweep: ClearButtonHighlight; return
UVS → ScheduleRefreshAllHighlights (300 ms) → ApplyAllHighlights
```

Ranked icons lose chrome for that debounce. Fix: UVS during sweep paints the cached rank (or no-ops) and still schedules the full bind. `ApplyAllHighlights` clears who actually dropped out.
