# Claude submission review (round 5 optionals / notes) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5581599719

The two identity-cache bugs from that review are already in (see `review-response5.md`, v0.9.5). This pass is the optional / functionality leftovers against `taskbar-recent-focus-highlight.wh.cpp` v0.9.6.

---

## Copied review (optional + functionality only)

Round 5 items 1–2 (failed resolve cached forever; first UVS stamps rank 0) were agreed and landed. Remaining text from the reviewer:

### Optional improvements

Minor polish — none of this affects users, so it's your call.

* **Dead branch in `ApplyAllHighlights_UIThread`.** `cands` only collects `s >= kScoreExactIdentity` (line 4083), and `kScoreExactIdentity` is now the only non-zero value `ScoreButtonForRank` can return. So `replica` (line 4099) is always true, the `else if (!rankTaken[c.rankIdx])` arm at line 4104 is unreachable, and the `std::sort` by `score` at line 4088 sorts a constant. `Cand::score` and the sort can go with it.

* **`ButtonPathCacheEntry::lastResolveTick`** (line 546) is assigned and never read — either use it as the retry throttle from item 1 or drop the field.

* **`PathAppearsOnTaskbar` shadows its structured binding.** Inside `for (const auto& [id, e] : g_buttonPathCache)` the body declares `std::wstring id = ...` (lines 1456–1458), so `id` means two different things a line apart. Renaming the local to `gotId` reads better.

* **`AppFocusInfo::lastHwnd` has no recycle guard.** `ScoreButtonForRank`'s `hwndOnButton` compares it straight against `ident.sampleHwnd` / `ident.groupHwnds` and returns `kScoreExactIdentity` on a hit, while the preview side stores a `pid` in `WindowFocusInfo` and validates through `HwndMatchesStoredPid`. If a ranked app's window closes and the handle value is recycled, the rank can bind to whichever button now owns it. Carrying the pid next to `lastHwnd` and reusing `HwndMatchesStoredPid` would make the two paths consistent.

### Functionality notes

Non-critical observations and ideas about the feature behavior itself.

* **The hover-switch repaint can be dropped.** `HoverFlyoutModel_TargetItemKey_Hook` (line 6194) clears every tracked card and then calls `ScheduleThumbnailRelayout` on each live one — but `g_thumbRelayoutPending` (line 5071) is a single latch shared with `ApplyThumbnailHighlight`'s deferred remeasure, so only the first call actually schedules, and if a remeasure is already in flight none of them do. The one that does schedule uses the first entry of `g_trackedThumbViews`, i.e. the oldest tracked card, which may no longer be in the repeater — `FindAncestorItemsRepeater` then returns null, the resolve falls back to `CollectSiblingThumbnailViews`, and a single-view result just clears. A dedicated "preview repaint pending" flag, scheduled from a card that is actually in the current flyout, would be more predictable.

* **The flyout is still resolved once per card.** `TaskItemThumbnailView::OnApplyTemplate` fires for every card, and each call runs `RefreshThumbnailFlyout_UIThread` over the whole flyout — so opening a 5-window flyout does 5 full resolves (5 × `CopyRecentWindowsForPreview`, plus `ClearThumbnailHighlight`/`ApplyThumbnailHighlight` on every card each time). Coalescing onto one Low-priority pass, the way `ScheduleThumbnailRelayout` already does, would make it linear.

* **Pass 1 assumes the repeater index and the `Thumbnails` index agree.** `repeaterSourceIndex` keeps the mapping exact on the repeater side now, but `HwndFromThumbnailsGetAt(srcIndex)` indexes `TaskGroup::Thumbnails`. If a snap-group entry is present in one collection and not the other, every card after it binds to a valid-but-wrong HWND, and the `usedHwnds` de-dup won't catch it. Worth testing a flyout that contains both a snap group and individual windows.

* **`ConfirmPreviewFocusNow` does cross-process work inside the click handler.** It runs on the taskbar UI thread from `CTaskListWnd::HandleClick`, doing `OpenProcess` + `SHGetPropertyStoreForWindow` via `ResolveAppIdentity`, and then `RequestApplyPreviewVisuals` → `RunOnUiThread` executes its handler *inline* because the caller already has thread access — so every tracked card is repainted synchronously before the click returns. `PostToHookThread` with the clicked HWND and resolving on the worker thread would keep the click path clean.

* **The decay timer ticks every 30 s for the life of the process**, even when every desktop map is empty. Stopping it when there is nothing tracked and re-arming on the next confirmed focus would be cheap.

* **The `ReportClicked` sentinel probe** now runs at most once per button instead of per paint, which is a big improvement — it's still worth confirming that entering `CTaskListWnd::HandleClick` (even short-circuited by the sentinel) has no MRU / jump-list side effects, since the mod it's adapted from only ever triggers it from real user input.

---

## Draft PR comment

Thanks — identity-cache holes from last round are in. This pass is the optionals and the flyout/click leftovers.

**Optional 1. Dead `replica` / `Cand::score` sort — agreed, pruned.** After dropping filename-900, `ScoreButtonForRank` only returns 0 or 1000, so the 1:1 arm was unreachable and the sort was a constant. Exact-identity still binds the same rank to many buttons (secondary taskbar). Also did a leftover sweep: stale “900 is 1:1” comment gone.

**Optional 2. `lastResolveTick` unused — already used, kept.** Item 1 of the same review turned it into the empty-identity retry throttle (`kUnresolvedRetryMs`; `IsRunning` bypasses it). Not dropping the field.

**Optional 3. `PathAppearsOnTaskbar` shadow — agreed, renamed.** Inner `id` is now `gotId`; the range-for key is `cacheKey`.

**Optional 4. `lastHwnd` recycle — agreed.** `AppFocusInfo` now carries `lastPid` next to `lastHwnd`, set on confirm. `ScoreButtonForRank` only treats that HWND as identity when `HwndMatchesStoredPid` says it still belongs to that process (same helper as preview).

### Functionality notes

**1. Hover-switch latch — taking it, with one correction.** Sharing `g_thumbRelayoutPending` was real, but the failure mode is the *stale card*, not the shared flag: title remeasure and hover-switch both want one Low flyout refresh. There is now a dedicated `g_previewFlyoutRefreshPending` whose callback picks a live `TaskItemThumbnailView` that still has an `ItemsRepeater` ancestor, instead of the weak_ref captured at schedule time (often the oldest tracked card). Title remeasure, hover-switch, and `OnApplyTemplate` all coalesce onto that one pass. One follow-up is allowed when the first pass still sees unmeasured cards (same cap as before).

**2. Per-card flyout resolve — agreed, coalesced.** `OnApplyTemplate` tracks the card and schedules the Low pass; it no longer runs `RefreshThumbnailFlyout_UIThread` inline. Opening a 5-window flyout is one resolve after layout. Same for `RequestApplyPreviewVisuals`.

**3. Repeater index vs `Thumbnails` index — this is why, and a snap-group guard.** Round 3 stored `repeaterSourceIndex` because `TryGetElement` skips unrealized / non-thumbnail children, and using the compacted vector index for `GetAt` was already wrong. That source index is the repeater *ItemsSource* slot. `TaskGroup::Thumbnails` is a different collection: a snap-group card can sit in the repeater and not in `Thumbnails` (or the other way around). Pass 1 now compares `Thumbnails.Size()` to the repeater card count vs the window-card count. Same length → keep source index. Snap extra in the repeater and `Size() == window cards` → compact `GetAt` to window ordinal. Any other mismatch → skip the index bind and leave TaskItem / unique-title. Snap-group cards still never get a glow.

**4. `ConfirmPreviewFocusNow` off the click UI thread — agreed.** `HandleClick` posts `WM_APP_PREVIEW_CLICK` with the HWND + PID; the focus thread does `ResolveAppIdentity` / stamp / preview apply. `RunOnUiThread` is then `TryRunAsync`, not inline. PID is checked so a recycled handle between post and process is dropped.

**5. Decay timer when idle — agreed.** It no longer starts with the focus thread. First confirmed app or window recency arms it; `OnDecayTimer` kills it when every desktop map is empty and there is no pending focus. Re-armed on the next confirm.

**6. `ReportClicked` side effects — written down, no extra code.** We cannot prove `CTaskListWnd::HandleClick` is side-effect-free from the sentinel (volume-per-app only ever drives it from real scroll/click). After taking it off UVS it runs at most once per button (full bind or `OnPointerPressed`). We have not seen flyout-dismiss / jump-list jumps from that path; if a human review does, the probe has to move again. Not adding a second identity path this pass.

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| Opt 1 dead replica/sort | Take | Drop `Cand::score`, sort, 1:1 arm; keep many-buttons-one-rank |
| Opt 2 lastResolveTick | Already used | Keep as empty-identity throttle from 0.9.5 |
| Opt 3 shadow | Take | `gotId` / `cacheKey` |
| Opt 4 lastHwnd pid | Take | `AppFocusInfo::lastPid` + `HwndMatchesStoredPid` |
| Fn 1 hover latch | Take (unify + pick live) | Dedicated pending; callback picks in-repeater card; title/hover/OnApplyTemplate share the *work* |
| Fn 2 per-card resolve | Take | OnApplyTemplate → schedule only |
| Fn 3 snap index | Take + explain | Size compare; compact or skip pass 1 |
| Fn 4 click ConfirmPreview | Take | `WM_APP_PREVIEW_CLICK` + pid |
| Fn 5 decay idle | Take | Arm on confirm, stop when maps empty |
| Fn 6 ReportClicked | Write down | Once-per-button; watch jump lists in human test |
| Dead-code sweep | Take | Stale 900 comment; replica locals |

### Why repeater index ≠ Thumbnails index (plain language)

`CollectRepeaterThumbnailViews` walks `ItemsRepeater.ItemsSourceView` with `TryGetElement(i)` and keeps `i` next to each `TaskItemThumbnailView`. That was the round-3 fix for *compacted vs source* (unrealized slots).

`HwndFromThumbnailsGetAt(i)` is `TaskGroup::Thumbnails.GetAt(i)` + the ctor map. The reorder mod treats those two indices as the same because its ItemsSource *is* that vector. A snap-group card is a `TaskItemThumbnailView` in the repeater with an `IconsRepeater` (≥2 children) and is **not** a window. If that extra card is in the repeater and not in `Thumbnails`, using the repeater source index for `GetAt` shifts every later window onto the wrong HWND — `usedHwnds` still sees unique handles, so it looks like a successful bind.

We never glow snap-group cards (language-independent child count). We now also refuse to *index through them* unless the two collections are the same length.

### Decay timer (why stop, why not earlier)

`SetTimer(..., 30s)` is periodic and cheap, but it also calls `RefreshCurrentDesktopId` (registry) and walks every desktop map for the life of Explorer. With no recency there is nothing to decay. Arming on confirm is the same thread as `SetTimer` (focus thread), so no cross-thread timer bug. Pending min-focus has its own one-shot; it does not need the 30 s tick.

### ReportClicked (why we are not ripping it out)

`EnsureButtonPathCached` still needs button → `ITaskItem` → HWND. The sentinel `ReportClicked` is how volume-per-app (and this mod) obtain the native item when `TryGetItemFromContainer` is empty. Taking that off hover was the real cost/safety win. A full replacement (pure vtable walk, no `HandleClick`) is a larger project and not required to ship the highlight.
