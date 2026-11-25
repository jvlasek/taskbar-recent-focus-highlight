# Claude submission review (round 8) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5617947403

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.9.

---

## Copied review (required + optionals)

**1.** Title-similarity preview fallback (passes 3/4) still guesses; English `NormalizeAutomationName`; if keeping it, leave a PR note.

**2.** Pass 1 indexes a global `g_TaskGroup_Thumbnails` that may be from another flyout; Pass 2 DataContext is exact per card. Prefer DataContext first; distrust GetAt if it disagrees.

**3.** IconPanel order snapshot skips unnamed children, so Styler-added unnamed elements are compacted to the back on restore.

Optionals: dead `ScoreButtonForRank`; skip init when nothing to paint; hoist debounce out of `ScheduleRefreshAllHighlights` + `g_unloading`; `RunAsync` vs `TryRunAsync`; double `GetProcessImagePath` per FG; clear `g_TaskGroup_Thumbnails` on uninit.

Functionality: demote-forever; snap-group second icon; paint-cache key vs `GlowContentBoxSize`; `ButtonHasOurChrome` descendant walk.

---

## Draft PR comment

Thanks — two leftovers from the last restore/resolve work, and the title fallback stays with a note.

**1. Unique-title preview fallback — keeping it, with the note you asked for.**

Icon matching is already fail-closed (path / AUMID / HWND). The leftover name code is **preview-only**, and only when the exact paths miss.

Pass 1 (`Thumbnails.GetAt`) and pass 2 (`DataContext` ↔ ctor map) are optional symbols. On builds where they are missing, unique-title is how a multi-window flyout still gets a ladder. Identical titles stay unmatched — we do not steal. The English `" running"` / `" pinned"` strip is a no-op on other languages, which means *fewer* unique-title binds, not a wrong bind.

Pass 4 is not a bind: it copies recency onto an HWND that pass 1/2 already assigned (Lister / tab-proxy HWND vs `EVENT_SYSTEM_FOREGROUND` HWND). No string matching.

Wrong glow on an **icon** is worse than none. A blank multi-window flyout when GetAt is unavailable is worse than a unique-title hit. `how=repeater|taskitem|title` is in the log if a human review wants that fallback dropped from evidence.

**2. Stale global `Thumbnails` collection — agreed, fixed.**

Confirmed: `g_TaskGroup_Thumbnails` is only captured inside `TargetItemKey`, and the flyout refresh also runs from `OnApplyTemplate`, title remeasure, decay, and click confirm. Same window *count* as the previous app is enough for the size guard to pass, then every card gets the previous app’s HWNDs.

DataContext → ctor map is per-card and cannot go stale. We now run that **first**. Repeater `GetAt` only fills holes, and only if it agrees with at least one DataContext HWND on the same card (mismatch ⇒ skip the index path, log it). Order still comes from the repeater.

**3. Unnamed snapshot children — agreed, fixed.**

The snapshot now stores `weak_ref<UIElement>` in visual order, including unnamed children, skipping only our glow host. Restore is by object identity, so a Styler element without `x:Name` keeps its place, and two children with the same name cannot swap.

### Optional

- **`ScoreButtonForRank`.** Agreed — unused wrapper, deleted.
- **Init when nothing to paint.** Agreed — `highlightCount == 0` and previews off → `Wh_ModInit` returns `FALSE`; settings change reloads.
- **Debounce hoist + `g_unloading`.** Agreed.
- **`TryRunAsync` for flyout refresh.** Agreed.
- **`GetProcessImagePath` twice per FG.** Agreed — same-thread cache for the duration of `HandleForegroundChanged`.
- **`g_TaskGroup_Thumbnails` on uninit.** Agreed — reset with the other maps.

### Functionality notes

- **Demote-forever.** Skipping. Fail-closed on identity is the catalog rule.
- **Snap-group second icon.** Agreed — if `IconsRepeater` has 2+ children *and* the card still resolves a window HWND, it is not treated as a snap group.
- **Paint-cache key vs host size.** Agreed — the key now uses the same `GlowContentBoxSize` fallback (existing host, else `BackgroundElement`, else panel).
- **`ButtonHasOurChrome` descendant walk.** Agreed — `IconPanel` direct child only; the nested-host loop in `ClearButtonHighlight` already covers the odd case.

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| 1 unique-title | Keep + PR note | Pass 4 is tick copy, not a bind |
| 2 stale Thumbnails | Take | TaskItem first; GetAt only if it agrees |
| 3 unnamed snapshot | Take | `weak_ref<UIElement>` vector |
| Opt dead wrapper | Take | Delete `ScoreButtonForRank` |
| Opt init both-off | Take | `return FALSE` |
| Opt debounce hoist | Take | Check tick before `TryRunAsync` |
| Opt TryRunAsync | Take | Flyout Low pass |
| Opt path 2× | Take | Scope cache in `HandleForegroundChanged` |
| Opt uninit thumbs | Take | Reset weak ref |
| Fn demote N | Skip | Fail-closed |
| Fn snap-group | Take | HWND cross-check |
| Fn paint key | Take | `GlowContentBoxSize` for the key |
| Fn chrome walk | Take | Direct child only |
