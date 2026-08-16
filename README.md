# Taskbar Recent Focus Highlight

A Windhawk mod that visually highlights the most recently used **apps** on the
Windows 11 taskbar, and (optionally) ranks **windows** inside multi-instance
thumbnail previews with the same kind of intensity ladder.

**Mod file:** `taskbar-recent-focus-highlight.wh.cpp`  
**Author:** Jakub Vlášek / Grok Build
**Status:** v0.8.17 — app ranks + per-flyout thumbnail ranks + hybrid plate/title

For deep design notes aimed at contributors / coding agents, see **[AGENTS.md](./AGENTS.md)**.

## Problem

With many open applications (20–30+ icons), it becomes hard to quickly locate
the programs you were most recently working with. With several windows of the
same app (VS Code, Terminal, Calibre, …), hover previews look nearly identical
too — so “which one did I just use?” is unclear.

## Solution

1. **App ranks** — track focus and highlight the top N recent **running** taskbar
   icons (left bar, frame, full plate, or bottom running indicator).
2. **Preview ranks** — separately track focus per **window** (`HWND`). When a
   multi-window thumbnail flyout opens, that flyout gets its own recency list
   and marks the top N windows with rank intensities (title bar, soft title
   tint, whole plate, hybrid plate+title, or ring).

## Features

- Only affects **running apps** (pinned-only icons ignored)
- Highlights the **top N** recent apps (default 3; configurable)
- Highlights the **top N** windows **per thumbnail flyout** (default 3; own intensities)
- Icon styles: **left bar** (default), frame, full plate, bottom running bar
- Optional subtle **icon size scaling** per rank
- **Minimum focus time** — filters Alt+Tab noise (default 8s for apps)
- **Decay** — apps drop out after idle (default 30 min)
- **Exclude list** — paths / exe names / app IDs
- **Tray-only filter** — ignore focus targets with no taskbar button (default on)
- **Thumbnail previews** — multi-window flyouts only; own top-N ranks, intensities,
  min-focus, decay, and style
- Fully configurable through Windhawk settings

## Settings

Windhawk’s settings schema does **not** support section headers (`$group` is
rejected). Options are ordered and named with prefixes so groups stay clear in
the flat list:

### General
| Setting | Description | Default |
|---------|-------------|---------|
| Enabled | Master toggle (icons + previews) | On |
| Number of highlighted apps | How many recent apps to boost (1–6 recommended) | 3 |
| Minimum focus time (seconds) | App must stay focused this long to enter ranks | 8 |
| When to skip min-focus | Immediate if in map / only if already top-N / always wait | Immediate if in map |
| Decay time (minutes) | Drop app from ranks after idle | 30 |
| Only apps on the taskbar | Skip tray-only / popup focus targets | On |
| Exclude list | Never highlight (exe, path, or AppId) | (empty) |

### Taskbar icons
| Setting | Description | Default |
|---------|-------------|---------|
| Icon highlight style | Left bar / Frame / Full / Bottom bar | **Left bar** |
| Glow color | Accent / fixed / custom hex (also used for previews) | Accent |
| Intensity rank 1/2/3 | Strength per rank (0–100) | 100 / 70 / 45 |
| Thickness / roundness / size / layers | Geometry of icon chrome | 3 / 28 / 92 / 2 |
| Fill opacity | Full plate / left & bottom bar strength | 40 |
| Size boost rank 1/2/3 (%) | Icon scale (0 = off) | 10 / 6 / 3 |

### Thumbnail previews
| Setting | Description | Default |
|---------|-------------|---------|
| Highlight recent windows in previews | Multi-window flyout ranks | On |
| Number of highlighted windows | Top N in **that** flyout (1–6 recommended) | 3 |
| Preview highlight style | Title bar / Title bg / Plate / Hybrid / Ring | **Title bar** |
| Intensity rank 1/2/3 | Strength per window rank (0–100) | 100 / 70 / 45 |
| Preview tint opacity | Plate + title-background wash (0–100) | 40 |
| Preview minimum focus (seconds) | Window→preview recency (separate from apps) | 1 |
| Preview decay (minutes) | Drop window from preview recency | 15 |

### Advanced
| Setting | Description | Default |
|---------|-------------|---------|
| Debug log (verbose) | Path binds + preview resolve details | Off |

### Preview styles (multi-window flyout only)

| Style | Look | Notes |
|-------|------|--------|
| **Title bar** (default) | Thin accent line under the window title | Good default; sits just below the text |
| **Title background** | Soft wash behind the title | Kept light so text stays readable; intensity scales linearly |
| **Whole preview plate** | Tints the card chrome (`BackgroundBorder`) | Strong, clear signal |
| **Hybrid (plate + title)** | Plate on rank 1, title wash on ranks 2+ | Rank 1 pops; 2+ stay light |
| **Ring** | Hollow frame around the card | Simple placeholder |

Single-window flyouts are **never** marked (nothing to disambiguate). Ranking
is **per flyout**: Chrome’s last-used window is rank 1 in the Chrome flyout even
if you then used Notepad. Set the window count to **1** to restore the old
“most recent only” look.

## Share / install (for testers)

1. Install [Windhawk](https://windhawk.net/).
2. Create a **local mod** (or update yours) and paste / load
   `taskbar-recent-focus-highlight.wh.cpp`.
3. Compile, enable (injects into `explorer.exe` only).
4. Optional: enable **Debug logging** on the mod while testing.
5. After updating the `.cpp`, recompile in Windhawk; a full **explorer restart**
   is the cleanest way to pick up new hooks.

## How to test

1. Set app min-focus to `1`–`2`s (and preview min-focus to `0`–`1`) for faster trials.
2. Focus several apps long enough → icon ranks 1 > 2 > 3 (left bar by default).
3. Open **three+** windows of one app, focus them in turn, hover the combined
   icon → previews show ranks 1 > 2 > 3 (strongest on the last focused window
   of that app). Unranked cards stay unmarked.
4. Same-title windows (e.g. two Calibre views of the same file) should still
   track separately when TaskItem maps resolve (`how=group-order` / `taskitem`
   in debug log).
5. Useful log lines: `Confirmed focus:`, `Preview focus confirmed:`,
   `Preview resolve:`, `sibling[`, `ApplyAllHighlights`.
6. Disable the mod or toggle **Enabled** off → all chrome clears.

---

## Architecture (why things look the way they do)

### Two different identities (apps)

| Side | What we get | Example |
|------|-------------|---------|
| **Focus tracking** | `HWND` → process image path | `C:\…\WindowsTerminal.exe` |
| **Taskbar UI** | XAML `TaskListButton` | Automation name `"Windows Terminal"` |

Buttons are not HWNDs. The mod:

1. Keeps a **recency list** keyed by process path.
2. **Matches** buttons via taskband path cache (primary), then name scores.
3. **Paints** only matched running buttons.

### Window identity (previews)

| Side | What we get | Example |
|------|-------------|---------|
| **Focus** | `HWND` + title | Calibre window A vs B |
| **Flyout UI** | `TaskItemThumbnailView` | Two nearly identical cards |

Preview matching prefers **TaskItem → HWND** maps from optional
`TaskItemThumbnail` ctor hooks, then **group construction order** when
DataContext does not line up with the map, then unique title assignment.
Identical titles cannot be disambiguated by name alone.

```
  Focus (HWND / path)
         │
         ├─► App recency (path)  ──match──►  TaskListButton glow
         │
         └─► Window recency (HWND) ──match──►  Thumbnail ranks (2+ cards)
```

### Other important decisions

| Topic | Decision | Why |
|-------|----------|-----|
| App min focus | Default 8s | Alt+Tab should not reshuffle ranks |
| Preview min focus | Default 1s (separate) | Snappier for multi-window |
| Rank key | Full process path (UPPER) | Distinct installs of same exe name |
| Icon default style | Left vertical bar | Clear without heavy chrome |
| Preview default | Title bar under label | Light; plate available for stronger mark |
| Focus hook | Dedicated WinEvent thread | Reliable timers + pump |
| UI updates | XAML dispatcher only | Unsafe to touch tree off UI thread |
| Glow chrome | Own named overlays | Avoid fighting hover/active storyboards |
| Shell / UWP host | Skipped | Don’t rank explorer / AFH as the app |

### High-level runtime flow

1. User focuses an app long enough → process enters / refreshes app map → top N ranks.
2. Matching binds ranks to `TaskListButton`s → icon style applied.
3. Same focus path confirms **HWND** after preview min-focus → window map.
4. Multi-window flyout opens → resolve each thumbnail to HWND → rank by recency
   inside that flyout → paint top N at preview intensities.
5. Unload / disable → clear icon and preview chrome.

---

## Technical constraints

- Injects into `explorer.exe` only (`@include explorer.exe`)
- Windows 11 XAML taskbar (`Taskbar.View.dll` + `taskbar.dll` identity hooks)
- Should coexist with **Windows 11 Taskbar Styler** where possible (own named
  elements; not guaranteed conflict-free)
- Must stay lightweight — taskbar is critical UI
- Thumbnail symbol hooks are **optional**; missing symbols leave app ranks working

## Implementation progress

| Step | Status | What |
|------|--------|------|
| 1 | **Done** | Focus hook, min-focus, ranking, decay, exclude, settings |
| 2 | **Done** | App match → `TaskListButton`; styles + size boost |
| 3 | **Done** | Multi-layer / bars / bottom indicator; path cache (option C) |
| 4 | **Done** | Thumbnail recency, per-flyout ranks + intensities, styles, HWND maps |
| 5 | Later | Composition shadow, reliable UWP AppId, less fuzzy app matching |

## Potential future enhancements

- Per-app custom boost strength
- Temporary manual boost
- Composition / true GPU outer glow
- Stronger AppId / package identity for UWP
- Classic (non-XAML) thumbnail path if still needed on some builds

## Target users

Power users with many simultaneously open applications who frequently switch
between a small set of active programs — and often multiple windows per app.

## Repo layout

```
whawk-lru/
  README.md                              ← product + architecture (this file)
  AGENTS.md                              ← design decisions for contributors/agents
  taskbar-recent-focus-highlight.wh.cpp  ← the Windhawk mod (single file)
  example/                               ← reference mods (read-only)
```
