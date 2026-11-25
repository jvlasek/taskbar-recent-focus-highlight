# Claude submission review (round 7) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5602533501

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.8.

---

## Copied review (abridged in the draft; full text on GitHub)

**1.** Mod is very large; thumbnail pass 3/4 is string-similarity guesswork; `NormalizeAutomationName` is English-only.

**2.** `ApplyButtonHighlight` paint cache is `{rank, settingsGen, accent}` only — taskbar edge and icon-panel size are not in the key, so SizeChanged / edge move leaves the old bar geometry.

**3.** Native `IconPanel` children are reordered; restore assumes stock order (`BackgroundElement` first, `OverlayIcon` above `Icon`) instead of the pre-mod order. Taskbar Styler themes that reordered those children lose that order on clear/disable.

Optionals: kernelbase-before-taskbar.dll; `Wh_GetStringSetting` never NULL; `SettingsSnap` null fallback; unbounded empty-resolve retry; hoist `ScoreButtonForRank` per-button work; `g_pendingOverlaySweep` is process-global.

Functionality: click bypasses preview min-focus; `ReportClicked` vs `HandleExtendedUIClick`; registry desktop id + double `GetProcessImagePath` per FG; `requireTaskbarButton` demotion; thumbnail XAML tear-down every paint.

---

## Draft PR comment

Thanks — three findings, then the optionals.

**1. Line count / title heuristics — keeping the preview fallback, not cutting it.**

Icon matching is already exact (path / AUMID / HWND). The leftover name code is **preview-only**, and only when the exact paths miss: repeater `GetAt` + ctor map (pass 1) and DataContext (pass 2) are optional symbols; on builds where they are missing, unique-title is how a multi-window flyout still gets a ladder. Identical titles stay unmatched (we do not steal). The English `"N running"` / `" pinned"` strip is documented: on other languages it is a no-op, which means *fewer* unique-title binds, not a wrong bind.

Wrong glow on an icon is worse than none; a missed preview mark when GetAt is unavailable is worse than a unique-title hit. We will not apply the icon fail-closed rule to that last preview fallback. `how=repeater|taskitem|title` is already in the log if we need to drop it later from evidence.

The IconPanel z-order / Styler-plate / OverlayIcon work is not “scaffolding on scaffolding” — those are coexistence bugs we hit with Taskbar Styler and Discord badges. Item 3 below is the restore side of that, not a deletion of it.

**2. Paint cache ignores edge and size — agreed, fixed.**

Confirmed: SizeChanged updates `lastEdge` and calls `RefreshButtonHighlight` → `ApplyButtonHighlight`, which returns if `{rank, generation, accent}` match. The bar side and length never update.

The cache now includes the resolved taskbar edge and the icon-panel size (rounded). A relayout or edge change misses the early-out and repaints. `ScheduleRefreshAllHighlights` then sees the same new key on the other buttons.

**3. Restore assumed stock z-order — agreed, we now snapshot the pre-mod order.**

Yes: that is exactly what it wanted, and what we should have been restoring. We still have to *move* native children while the glow is up (side bar above a Styler hover plate, OverlayIcon above the glyph, host under a native thin pill). A single `Canvas.ZIndex` on our host cannot express those three relative constraints, so a purely additive overlay is not a drop-in.

What we *can* do is remember the child names in `IconPanel` **before** the first move, and put that order back on clear/unload instead of inventing `BackgroundElement` at 0. Stock taskbar: snapshot == native template, restore is a no-op. Styler that reordered: snapshot is Styler’s order, disable gives it back. Buttons we never painted are still untouched.

### Optional

- **kernelbase before `taskbar.dll`.** Agreed — lookup first so a failure cannot leak a `LoadLibraryEx` ref.
- **`Wh_GetStringSetting` never NULL.** Agreed — empty-string checks only.
- **`SettingsSnap` fallback.** Agreed — pointer is never null; drop the branch.
- **Unbounded empty resolve.** Agreed — cap empty path+AUMID retries (`kMaxEmptyResolveAttempts`). Pinned→running still forces one more try.
- **Hoist per-button identity.** Agreed — `ButtonCountsAsRunning` + `GetCachedButtonIdentity` once per button in `ApplyAllHighlights`.
- **Sweep flag vs multi-dispatcher.** Skipping. You noted all taskbars share one Explorer UI thread today.

### Functionality notes

- **Click vs `previews.minFocusSeconds`.** Intentional (explicit “this window”). Setting text now says a thumbnail / grouped-icon click confirms immediately.
- **`ReportClicked` vs `HandleExtendedUIClick`.** Written down. Volume-per-app only uses `HandleClick`; we already short-circuit the sentinel there and only probe on full bind / press / pinned→running. Not adding a second click hook without a late-24H2 miss in the log.
- **Desktop GUID + double `OpenProcess` on FG.** Skipping this pass. Desktop id already refreshes on `EVENT_SYSTEM_DESKTOPSWITCH` and decay; the FG registry read is a lag fallback. Path cache per PID across a recycle is easy to get wrong.
- **Demote-forever / packaged AUMID mismatch.** Skipping. Fail-closed on identity is the catalog rule; a demote counter would hide a real miss.
- **Reuse thumbnail overlay host.** Skipping this pass (layout risk; host is ≤6 cards).

---

## Internal notes (not for the PR)

| # | Verdict | Work |
|---|---------|------|
| 1 size / title guess | Keep unique-title | Explain; icon path stays fail-closed |
| 2 geometry cache | Take | `PaintCacheState` + entry: edge, boxW, boxH |
| 3 z-order restore | Snapshot, not Canvas.ZIndex | Save native child names before first move; restore that list |
| Opt kernelbase | Take | Lookup before `HookTaskbarDllSymbols` |
| Opt StringSetting NULL | Take | `!*s.get()` |
| Opt SettingsSnap | Take | Return `g_settingsPtr` |
| Opt empty resolve cap | Take | 8 attempts while path and AUMID empty |
| Opt hoist score | Take | Identity + running once per button |
| Opt sweep global | Skip | One Explorer UI thread |
| Fn click min-focus | Document | YAML + README |
| Fn ExtendedUIClick | Write down | No extra hook |
| Fn FG registry/OpenProcess | Skip | Switch event is primary |
| Fn demote N | Skip | Fail-closed |
| Fn thumb XAML reuse | Skip | Later |

### Item 3 why not Canvas.ZIndex only

Need simultaneously: glow under native thin pill, glow *above* Styler full-cell hover plate (`RunningIndicator` restyled), OverlayIcon above Icon. Those are pairwise constraints among *native* siblings, not one ZIndex on our host. Snapshot+restore is the coexistence we can actually undo.
