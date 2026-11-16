# AppId identity rewrite — design (branch `appid-identity`)

Status: step 2 implemented (0.11.33), live test pending; 2026-11-16. Baseline: 0.11.31 (`main`), perf baseline
`tests/perf/results/0.11.31-quiet-baseline.txt`. Target: part of the single
0.12.0 windhawk-mods submission.

## Goal

"One taskbar button = one app" by Explorer's own definition: the
`AppId` Explorer groups buttons by (`ITaskGroup::get_AppId` /
`ITaskItem::get_AppId`, public WindowsUdk, slot 0 — already declared in our
ABI structs). Today we rebuild that identity ourselves and then try to match
it back to buttons.

## Today (0.11.31)

- **Focus side (worker):** `ResolveAppIdentity(hwnd)` → process image path +
  window AUMID + class → key `APPID:<aumid>` or `<EXE PATH>`.
- **Button side:** async resolve — UI capture (`CaptureButtonResolveData`) →
  worker (`ResolveOneButtonOnFocusThread`: process path, window AUMID, class)
  → UI consume (`EnsureButtonPathCached`) with serials, deadlines,
  supersession, retry/recheck throttles, `CanRetainGroupIdentity`,
  UWP-host and same-exe/different-class special cases.
- **Matching:** `IdentityMatchesRank` (path / AUMID / HWND / class rules).
- Known gaps: apps whose AUMID is process-level only stay path-keyed;
  several buttons of one exe (Chrome/Edge profiles, PWAs) can share a key.

## Proposal

1. **Button identity = AppId, captured synchronously on the UI thread.**
   `CaptureButtonResolveData` already holds the button's `ITaskItem` /
   `ITaskGroup`; add `get_AppId` (one call, microseconds). No worker round
   trip, no serials/deadlines. The cache keeps per button: AppId, window set
   (as today), running state.
2. **Window → AppId map, published by the UI thread.** From every captured
   button: each window in its window set maps to the button's AppId
   (uncombined/Never Combine buttons: their own window). Published under a
   small mutex; the worker only reads it.
3. **Focus side keeps window-level tracking.** On a confirmed focus the
   worker still normalizes the HWND (owner climb, UWP frame) and checks the
   exclusion list (needs the exe path / AUMID, as today — the exclusion
   setting is unchanged). The **app key becomes the AppId** looked up in the
   map for that HWND.
   - Not in the map yet (window brand new, button not captured yet): keep
     the confirmation as a pending window record and attribute it when the
     map gains that HWND (next capture), instead of guessing.
   - No button at all (tray-only window): not ranked — same as today with
     `requireTaskbarButton` on (the default).
4. **Matching = string equality** of AppIds (canonical case), plus the
   existing per-window rule for uncombined buttons (HWND in the button's own
   window set). `IdentityMatchesRank` shrinks to that.
5. **Preview ranks unchanged** (already per HWND via the card view model).

## What should go away

Most of the async identity machinery: `ResolveOneButtonOnFocusThread`,
`QueueButtonResolve` + queue/serial/deadline/supersession,
`CanRetainGroupIdentity`, `kUnresolvedRetryMs` / `kResolvedRecheckMs`
re-resolves, `MakeAppKey` / `APPID:` vs path keys, the UWP-host and
class-split special cases in matching, and the identity-repaint coalescing
that exists only for async results. Estimate: several hundred lines, plus
their tests (rewritten, not dropped).

## Must keep working (live checks on the branch)

- Total Commander vs Lister: two buttons, two AppIds (`…TOTALCMD64.EXE` vs
  `LISTER{…}`) — expected to work natively.
- Owned dialogs credit their owner (NormalizeFocusHwnd stays).
- UWP / hosted apps (Calculator, Settings): frame window in the button's
  window set.
- Never Combine / combine-when-full: per-window ranks.
- Exclusion list (names, paths, AUMIDs) — unchanged semantics; also accept
  an Explorer AppId spelling (e.g. `{6D809377-…}\TOTALCMD\TOTALCMD64.EXE`).
- Recycled taskbar buttons (0.11.26) and button reuse for another app: a
  changed AppId on capture simply changes the button's identity.
- Multi-monitor: same AppId on several taskbars → same rank everywhere.
- Virtual desktops, decay, min-focus, previews: unchanged logic.

## Compatibility

- **Settings:** none renamed. `excludedPrograms` keeps its meaning.
- **Stored state:** recency lives in memory only (mod storage holds just the
  0.11 → 0.12 preview-style transition flag), so there is nothing to migrate;
  the first run after update starts with empty ranks, as after any restart.
- **`requireTaskbarButton` — decided 2026-11-16 (Jakub): remove the
  setting.** Windows without a button have no AppId and are never ranked.
  The setting was added because a tray-only Home Assistant widget window
  stole recency; with AppId identity that case is the only behavior. A
  stored value is simply ignored (no transition needed: default "on" was the
  kept behavior). Mention in the changelog.

## Plan

1. Add `get_AppId` capture + window→AppId map alongside today's identity;
   log both (shadow, no behavior change) — one live run to confirm keys
   and map coverage (any focus confirmation that finds no AppId is logged).
   **Done in 0.11.32:** log lines `AppId shadow: <app> key=… -> appId=…`
   (once per new pair, `[SPLIT]` / `[MERGE]` marks), `AppId shadow: MISS …`,
   and `group AppId … != item AppId …` (should never appear).
2. Switch ranking/matching to AppId; remove the async machinery.
   **Done in 0.11.33** (net about -200 lines in the mod after the shadow
   code; also removed: the OnPointerPressed hook, the requireTaskbarButton
   setting, dead helpers). Live test pending.
3. Rewrite the identity tests; perf build + quiet run vs the 0.11.31
   baseline; live checks above.
4. Merge into `main`, then 0.12.0.
