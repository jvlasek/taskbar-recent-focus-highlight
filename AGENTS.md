# Agent / contributor guide

Developer context for `taskbar-recent-focus-highlight.wh.cpp` (v0.11.42).

This file is the index. It holds the rules that always apply and says which
topic file to read for the task at hand. Read the topic file before changing
that area; you do not need the others.

## What this project is

A **Windhawk mod** injected into `explorer.exe` that:

1. Watches which window the user is actually using (foreground changes).
2. Keeps **app-level** recency per virtual desktop (top N apps) → taskbar
   icon highlight. An app is one taskbar button, identified by Explorer's own
   AppId.
3. Keeps **window-level** recency (`HWND`) → per-flyout thumbnail ranks.
4. Paints own-named XAML overlays; it does not restyle native elements.

Reference mods are in `example/` (read-only).

## Rules that always apply

- **Edit `src/`, not the `.wh.cpp`.** The mod is developed as parts
  `src/NN-*.cpp`, joined in name order by `python tools/build_mod.py` into
  `taskbar-recent-focus-highlight.wh.cpp` (byte for byte, no separators).
  Commit both; `tests/run-build-tests.py` fails when they differ. Tests read
  the joined parts (`tests/mod_source.py`). The Windhawk metadata, README and
  settings YAML are in `src/00-metadata.cpp`. One translation unit: a part
  may use anything declared in an earlier part.

- **Version checkpoint.** After each version increase, commit the corresponding source, documentation,
  and checks to local Git before handing the version to the user. Run the relevant
  checks first and record any unverified behavior. Do not push unless requested.
  Do not include unrelated user changes in the version commit.
- **Version sync.** With a version bump keep in sync: the `@version` line
  (`src/00-metadata.cpp`, then rebuild),
  the version in this file's first line, the README version line,
  `doc/investigation-notes.md` and `doc/runtime-flow.svg` (regenerate with
  `python doc/generate-runtime-flow.py`; edit that source, not only the SVG).
- **Fail closed.** Missing identity means no highlight. Never guess from
  labels, titles, process names or element order. A wrong highlight is worse
  than none.
- **Unload is total.** When `Wh_ModUninit` returns, no thread, window
  procedure, event handler or queued callback of the mod may remain. Worker
  first, then the UI drain, no timeouts. → `doc/agents/threading-unload.md`
- **The taskbar UI thread stays cheap.** `UpdateVisualStates` is hover-hot:
  paint the cached rank only; no cross-process, registry or COM work there.
  Measure with the perf build before a release. → `doc/agents/testing.md`
- **Own elements only.** Own-named overlays, `InsertAt` (never `Append`), no
  native reordering, no writes to native properties.
- **Settings compatibility.** Once released, a renamed, split or re-meant
  setting needs a transition. → `doc/agents/settings.md`
- **Keep these docs true.** Change the topic file in the same commit as the
  behaviour. Move text about a removed mechanism to `doc/agents/history.md`
  instead of leaving it in place or annotating it as "historical".

## Where to read

| Task | File |
|------|------|
| Button capture, app keys, rank matching, Never Combine, exclude list, private dependencies | `doc/agents/identity.md` |
| Focus sources, pending focus, min-focus timers, promotion, decay, virtual desktops | `doc/agents/recency.md` |
| Thumbnail flyout: card → window, preview ranks, card painting | `doc/agents/previews.md` |
| Icon painting: glow host, z-order, contours, bars | `doc/agents/visuals.md` |
| Threads, locks, shared state, dispatcher posts, init and unload | `doc/agents/threading-unload.md` |
| Settings keys, code mapping, release transitions | `doc/agents/settings.md` |
| Tests, perf build, log lines, live checklist, real-input harness | `doc/agents/testing.md` |
| windhawk-mods PR and review rounds; incident log | `doc/agents/review-lessons.md` |
| Why something was removed (async identity, taskbar.dll hooks, fallbacks) | `doc/agents/history.md` |

Other documents:

- `doc/investigation-notes.md`, `doc/runtime-flow.svg`: runtime overview and
  investigation evidence.
- `doc/appid-identity-design.md`: the AppId identity design (0.11.33).
- `TODO.md`: backlog. `reviews/fable.md`: overall review of 0.11.37.
- `tests/uwspy/README.md`, `tests/perf/README.md`: harness and perf guides.

## File layout

```
whawk-lru/
  taskbar-recent-focus-highlight.wh.cpp  # the mod (single file for Windhawk)
  AGENTS.md                              # this index
  doc/agents/                            # topic files listed above
  doc/                                   # investigation notes, design notes, diagram
  README.md                              # product + high-level architecture
  TODO.md                                # backlog
  tests/                                 # checks, perf build, real-input harness
  reviews/                               # review rounds and responses
  example/                               # reference mods (read-only)
```
