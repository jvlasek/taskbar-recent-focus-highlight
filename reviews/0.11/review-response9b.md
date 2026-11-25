# Claude submission review (round 9) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5655860034

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.19 (process-scoped unique-title without hover COM, bounded transient min-focus, File Explorer folders, title-wash `CornerRadius{4,4,4,4}`).

---

## Draft PR comment

Thanks — three leftovers in preview resolve / min-focus, then the cheap optionals.

**1. Unique-title not scoped to the flyout — agreed, fixed.**

Confirmed: pass 3 scored every confirmed HWND on the desktop, and `ScoreTitleToAutomationName` 91/93 is substring with a 4-character floor. `usedHwnds` only prevents stealing a card already bound in *this* flyout, so a foreign-process HWND was free to take the glow.

Pass 3 now:

- Anchors on any sibling that resolved by TaskItem or repeater **and** is already in this desktop’s window recency map.
- Filters `recent` to that `processKey` (taken from the map — no `OpenProcess` / `SHGetPropertyStoreForWindow` on the flyout UI thread).
- Skips entirely when nothing resolved exactly (`Preview resolve: skip unique-title`).

Hosted UWP still share ApplicationFrameHost’s image path in that map; unique-title only fills holes after an exact sibling HWND, so Calculator vs Settings that already bound by TaskItem/repeater stay exact. Chrome `GitHub` cannot inherit Edge `GitHub - Profile`.

Wrong glow on a card is worse than a blank flyout when the exact paths missed. Unique-title stays as the last hole-filler for the *same* process, with the note you asked for last round.

**2. Vacuous index-bind cross-check — agreed, fixed.**

Confirmed: `indexBindTrusted` only flipped on disagreement, so “no DataContext HWND” left GetAt trusted. `g_TaskGroup_Thumbnails` is only captured inside `TargetItemKey`, but the refresh also runs from `OnApplyTemplate` / decay / click.

We now `g_TaskGroup_Thumbnails = {}` on the way **in** to `TargetItemKey`, so a non-null collection is from *this* retarget. DataContext-first + disagreement skip stay as a second check. We did not add `anyTaskItemResolved` — that would disable GetAt on the builds where it is the only exact path.

**3. 200 ms transient poll — agreed, fixed.**

Confirmed: after the deadline, both timers re-armed 200 ms with no cap while `IsTransientForeground` was true, and File Explorer is this `explorer.exe`.

Post-deadline wait is now `TransientGraceMs` (same as that timer’s min-focus, at least 2 s) of 200 ms polls, then the app timer drops the candidate (`gave up waiting through transient`). Preview give-up only stops re-arming (the app timer still owns `g_pendingFocus`). A real same-app FOREGROUND clears the grace counter.

### Optional

- **Snap-group vector.** Agreed — `IsSnapGroupThumbnailView` once per flyout, reused for the sibling filter, size check, and GetAt compact.
- **Keep overlay host.** Agreed — ranked repaint restores the plate brush and hides children; `XamlReader::Load` only when the host is missing. Unload / unranked still full-clear. Overlay children use Margin, not RenderTransform (layout clip squared the right end-cap).
- **Debounce restart.** Agreed — `WM_APP_REQUEST_APPLY_DEBOUNCED` skips `SetTimer` if the 300 ms full-rebind timer is already armed.
- **Init log version.** Agreed — `Wh_Log(L"> init " WH_MOD_VERSION)`.
- **`weak.get()` then `HasThreadAccess`.** Skipping. Primary and secondary taskbars share one Explorer UI thread, so the dispatcher filter never fires. Changing it without a second UI thread to drain would be speculative.

### Functionality notes

- **File Explorer.** Agreed — `CabinetWClass` / `ExploreWClass` are a real app (ranked, and a leave from another candidate). Tray / desktop / Start / IME stay ignored via `ShouldIgnoreHwnd`. Documented in the README.
- **Unique-title keep/drop.** Still keeping it, now process-scoped (finding 1). Human call if it should go entirely.
- **Idle decay extra cycle.** Harmless; we now also `StopDecayTimerIfIdle` when the app timer drops a candidate.
