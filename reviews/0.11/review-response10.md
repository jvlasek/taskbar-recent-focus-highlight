# Claude submission review (round 10) — PR #5331

Source: https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5692400899

Verified against `taskbar-recent-focus-highlight.wh.cpp` v0.9.20.

---

## Draft PR comment

Thanks — two leftovers, then the optionals.

**1. Ring preview style — removed.**

Confirmed: `min(cardW, cardH)` made a square on the left of a wide card (`data/bigring.png`). Hybrid / plate / title wash already look right, so rather than ship a second-rate frame we dropped `ring`. A saved `previews.style=ring` loads as hybrid (`plateTitle`).

**2. Packaged-app unique-title scope — agreed, that was a real miss.**

App ranks already use `APPID:<aumid>` for AFH/WWAHost. Window recency stamped `PathFromAppKey` and then the host exe, so Calculator and Settings shared `ApplicationFrameHost.exe` in pass 3. We now stamp the same rank key we already built (`pending.key` / `key`) — path for Win32, `APPID:` for hosted UWP. The README AUMID line is now what the code does. (We thought this was already in; it was only on the app map.)

### Optional

- **Stale-identity `OpenProcess` every rebind.** Agreed — the `haveIdentity` branch skipped the `kUnresolvedRetryMs` throttle. Stale check is now throttled the same way, and a negative result bumps `lastResolveTick` so we don’t retry every 300 ms.
- **`g_currentDesktopId` log after unlock.** Agreed — capture the GUID while we hold `g_stateMutex`.
- **`#ifndef WH_MOD_VERSION`.** Agreed — deleted. Windhawk always defines it.
- **`ClearThumbnailHighlight` walks.** Agreed — return early when the card has neither our host nor the plate marker.
- **Idle decay vs leftover `g_pendingFocus`.** Agreed — preview confirm clears pending when the app side is already in the map, then `StopDecayTimerIfIdle`.

### Functionality notes

- **Shared grace tick (defaults 8 s app / 1 s preview).** That *is* the default config. Preview timer fires at 1 s, sets `transientRetryStartTick`, and at t=8 s the app timer sees ~7 s already elapsed against an 8 s grace, so a new app you Alt-Tab away from only gets ~1 s of extra wait instead of 8. Separate `previewTransientRetryStartTick` now, so each timer has its own grace.
- **Same-app unique-title (`GitHub` vs `GitHub - Pull requests`).** Yes, score 91 substring can still bind that when the card’s own window was never confirmed. Same-process only, holes only, documented. Keep/drop still a human call.
- **OverlayIcon after snapshot (Thunderbird badge).** Yes, related. `RememberNativeIconPanelOrder` runs once at first glow. A late `OverlayIcon` (mail count) isn’t in the list; snapshot restore on clear compacted it behind the glyph — and that path returned before `EnsureOverlayIconAboveGlyph`. Restore now raises OverlayIcon after compaction, and a cached-rank UVS paint does the same so a badge that appears while ranked isn’t left behind the icon.
- **Ring vs `glowLayers`.** Moot — ring is gone.

Saved `previews.style=ring` becomes hybrid until the user saves settings again (or Reset to default).
