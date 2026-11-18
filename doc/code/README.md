# Code documentation

How the mod's code is divided and how its state behaves. The diagrams are
Mermaid, so they render on GitHub and in any Markdown preview with Mermaid
support (VS Code needs a Mermaid extension).

| Document | What it shows | Maintained |
|---|---|---|
| [code-breakdown.md](code-breakdown.md) | Lines per feature: recent apps (taskbar icons), recent windows (flyout previews), shared. Pie charts, a sankey from source parts to features, tables. | Generated: `python tools/code_breakdown.py` |
| [state-machines.md](state-machines.md) | 14 statecharts, explicit and implicit: focus candidate, min-focus tracks, app and window records, taskbar button, repaint schedulers, desktops, decay timer, worker and mod lifecycle, unload drain, settings transition, XAML watches. | By hand, with the code |

## The two halves in one paragraph

The focus worker turns foreground changes into one **candidate** and runs
two **min-focus tracks** on it (machines 1-2). The app track feeds per-window
records that fold into **app ranks** for the taskbar (machine 3, *recent
apps*); the preview track feeds **window records** that rank the cards of a
thumbnail flyout (machine 4, *recent windows*). The taskbar UI threads bind
ranks to buttons in a debounced **full pass** (machines 5-6) and rank flyout
cards in a coalesced **flyout refresh** (machine 7). Desktops, decay, the
worker thread and unload are shared (machines 8-12).

## Keeping this true

- Rerun `python tools/code_breakdown.py` after changing `src/` (needs
  `pip install tree-sitter tree-sitter-cpp`). It warns about classification
  entries that no longer match an item; new functions take their part's
  default domain until classified in `ITEM`.
- Update `state-machines.md` in the same commit as a change to a state,
  flag, timer or transition it draws.
