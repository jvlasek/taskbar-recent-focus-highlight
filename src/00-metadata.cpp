// ==WindhawkMod==
// @id              taskbar-recent-focus-highlight
// @name            Taskbar Recent Focus Highlight
// @description     Visually highlight the most recently focused running apps on the taskbar
// @version         0.12.1
// @author          Jakub Vlášek
// @github          https://github.com/jvlasek
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luuid -lshell32 -ladvapi32
// ==/WindhawkMod==
// -loleaut32 is required: WinRT hresult_error calls SysFreeString / SysStringLen.

// Source code is published under The GNU General Public License v3.0.
// Development repository, tests and investigation notes:
// https://github.com/jvlasek/taskbar-recent-focus-highlight
// (developed as parts in src/, joined into this file by tools/build_mod.py)

// ==WindhawkModReadme==
/*
# Taskbar Recent Focus Highlight

Highlights the taskbar icons of the apps you actually stayed on, and ranks
the recently focused windows inside a multi-window thumbnail flyout.
Previews have their own on/off setting.

![A crowded taskbar with the three most recently used apps marked, and the open flyout ranking that app's windows](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/hero.png)

## What you see

After an app stays focused for the **minimum focus time** (default 8 seconds),
it joins a per-virtual-desktop recency list. The top N **running** taskbar
buttons on the current desktop get a highlight:

- **Side bar** (default) — a bar beside the icon (left on a bottom/top
  taskbar, under the icon on a left/right taskbar)

  ![Side bar](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/icons-side-bar.png)
- **Plate** — the button's own rounded plate, tinted in the highlight color

  ![Plate](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/icons-plate.png)
- **Plate with accent** — the same plate at half the tint, with an accent
  border

  ![Plate with accent](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/icons-plate-with-accent.png)

On a button with several windows the plates also tint Windows' stacked
edge. An app that asks for attention keeps Windows' red marks on top.

Rank 1 is strongest; ranks 2 and 3 use the intensities you set, and ranks
4+ fade further from rank 3.
Pinned icons that are not running on this desktop are never highlighted.
A new app is added only after the minimum focus time. Re-focus of an app
already in the list follows “When to skip min-focus” (default: it becomes
rank 1 immediately). A short Alt+Tab that never settles does not add a new
app. Each virtual desktop keeps its own list.

An app drops out of the list a set time (**Decay**, default 30 minutes)
after you last *used* it: keyboard or mouse input while it was in front
counts, so the app you are working in keeps its highlight however long you
stay in it. Away from the keyboard everything ages out from your last input.

When you hover a combined icon and the flyout shows **two or more** windows,
that flyout gets its own recency ladder (independent of icon ranks). The
last focused window of that app is rank 1 in the flyout even if you used
other apps more recently. Single-window flyouts are left alone unless
**Also mark single-window previews** is on; then the card is marked as rank 1
while its window is recent, so it reads as "used within the preview decay".

Preview styles: rank 1 and ranks 2+ are set separately (default: whole-card
plate for rank 1, title plate for ranks 2+); either can also have an accent
border, or be a thin bar under the title; ranks 2+ can be left unmarked.

![Default preview styles: whole plate for rank 1, title plates for ranks 2 and 3](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/flyout-plate-title.png)

![The "with accent" preview styles](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/flyout-plate-accent-title-accent.png)

If you only want the active window highlighted in previews,
[taskbar-active-window-highlight](https://windhawk.net/mods/taskbar-active-window-highlight)
is a much lighter mod for exactly that.

## Settings (short)

- **General** — how many apps, min-focus wait, when to skip the wait on
  re-focus, decay, exclude list
- **Icons** — style, color (including system accent), per-rank intensity,
  side bar thickness / length, fill opacity, accent thickness
- **Previews** — on/off, how many windows, single-window previews, styles,
  intensities, separate min-focus and decay

All of those are in the Windhawk settings panel. Disable the mod in Windhawk
to clear highlights.

## Limitations

- **SOLIDWORKS** and other apps that show their open documents as separate
  preview cards (one card per document, one real window) manage those
  previews themselves: Explorer links the cards to no window, and switching
  documents is not a window focus change, so their previews are not marked.
  Their taskbar icon is ranked normally.
- When the mod is enabled while Explorer is already running, the ranks show
  up after the first taskbar button changes (hover or a window switch).

## Notes

- Works with Windows 11 Taskbar Styler: the mod draws its own elements,
  changes no property of the native ones and takes no layout space, so the
  highlights stay visible on hover and follow themes that resize the
  buttons.
- An icon is matched by the application ID Explorer itself groups taskbar
  buttons by, not by its label or process name. A window that has no taskbar
  button is never ranked.
- Preview cards match through Explorer's own card view model (the window the
  card shows). If that is unavailable, the card is left unmarked rather than
  guessed from its title.
- File Explorer folder windows (`CabinetWClass`) are ranked like other apps.
  The taskbar, desktop, Start, and IME stay ignored.
- Multi-monitor: the same rank is applied on every taskbar that shows that
  app.
- All mod log lines are plain `Wh_Log` output: they appear only when the
  mod's logging (Advanced tab) is set to **Mod logs** or **Detailed debug
  logs**, and nothing is logged with **None**. There is no extra in-mod
  debug toggle.
- Button → task group capture is adapted from
  [taskbar-volume-control-per-app](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-volume-control-per-app.wh.cpp);
  button windows and preview cards use the public WindowsUdk task
  interfaces, as in
  [taskbar-active-window-highlight](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-active-window-highlight.wh.cpp),
  whose look (a tinted plate with an accent border) is also where the
  "with accent" styles come from;
  the flyout `ItemsRepeater` handling follows
  [taskbar-thumbnail-reorder](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-thumbnail-reorder.wh.cpp).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- highlightCount: 3
  $name: Number of highlighted apps
  $description: How many recent apps to highlight on the taskbar (1–6 recommended)
- minFocusSeconds: 8
  $name: Minimum focus time (seconds)
  $description: >-
    Only count an app as recent if it stays focused at least this long.
    Filters Alt+Tab noise. (Preview windows use a separate timer under Previews.)
    See “When to skip min-focus” for re-focus of apps already in the list.
- promoteMode: immediateTracked
  $name: When to skip min-focus
  $description: >-
    Controls instant promotion when you re-focus an app (confirmed apps still
    become rank 1 once promoted — this only skips the wait timer).

    Immediate if in recency map (default): an app whose last confirm is still
    inside the decay window promotes immediately, even if it is not currently
    highlighted. A confirm past that window waits again.

    Immediate only if highlighted: instant only when the app is already in the
    top-N glow set; rank 4+ and new apps wait the full min-focus time.

    Always wait: every app focus (including re-focus) waits min-focus seconds.
  $options:
  - immediateTracked: Immediate if confirm is still inside decay (default)
  - immediateTopN: Immediate only if already highlighted (top N)
  - alwaysWait: Always wait min-focus time
- decayMinutes: 30
  $name: Decay time (minutes)
  $description: >-
    Time since you last used an app (keyboard or mouse input while it was
    in front) before it drops out of the taskbar highlight list. The app
    you are working in stays highlighted. (Preview windows use a separate
    decay under Previews.)
- excludedPrograms: [""]
  $name: Exclude list
  $description: >-
    Apps that should never be highlighted (icons or previews). Entries can be
    process names, paths or application IDs (Explorer's AppId of a taskbar
    button is shown in the mod log as "Button identity: ... appId=", which
    also tells apart two buttons of one exe), for example:

    mspaint.exe

    C:\Windows\System32\notepad.exe

    Microsoft.WindowsCalculator_8wekyb3d8bbwe!App
- icons:
    - glowStyle: leftBar
      $name: Highlight style
      $description: >-
        How ranked apps look on the taskbar. Bars rotate with the taskbar edge
        (bottom / left / top / right). Side bar = beside the icon (left on a
        bottom or top taskbar, under the icon on a left or right taskbar).
        Plate = the button's own plate tinted (Fill opacity). Plate with
        accent = the same at half the tint with an accent border (Accent
        thickness).
      $options:
      - leftBar: Side bar
      - full: Plate
      - frame: Plate with accent
    - glowColor: accent
      $name: Glow color
      $description: Base color for icon highlights (and previews)
      $options:
      - accent: System accent color
      - green: Green
      - blue: Blue
      - orange: Orange
      - white: White
      - custom: Custom (see custom glow color)
    - customGlowColor: "#00C853"
      $name: Custom glow color
      $description: Used when glow color is Custom (hex, e.g. #00C853)
    - glowIntensityRank1: 100
      $name: Intensity rank 1
      $description: Overlay opacity for the most recent app (0–100). Linear — 60 is 60% of rank 1, not 36%.
    - glowIntensityRank2: 70
      $name: Intensity rank 2
      $description: Overlay opacity for the 2nd most recent app (0–100)
    - glowIntensityRank3: 45
      $name: Intensity rank 3
      $description: >-
        Overlay opacity for the 3rd most recent app (0–100). Ranks 4+ fade
        further from this value.
    - glowThickness: 3
      $name: Side bar thickness (px)
      $description: Side bar thickness (1–16).
    - glowSize: 92
      $name: Side bar length (%)
      $description: Side bar length (40–100).
    - glowFillOpacity: 40
      $name: Fill opacity
      $description: >-
        0–100. Side bar and Plate strength; half of it for Plate with accent.
        (Thumbnail tints use Previews → Tint opacity.)
    - glowAccentThickness: 2
      $name: Accent thickness (px)
      $description: Border width for Plate with accent (1–8).
  $name: Taskbar icons
- previews:
    - highlightEnabled: true
      $name: Highlight recent windows
      $description: >-
        When hovering a multi-window taskbar icon, rank that flyout’s thumbnails
        by window recency and mark the top N. Single-window flyouts: see the
        next option.
    - highlightCount: 3
      $name: Number of highlighted windows
      $description: >-
        How many recent windows to mark in a multi-window flyout (1–6 recommended).
        Ranking is local to that flyout: the last focused window of this app is
        rank 1 even if other apps were used more recently. Set to 1 to mark only
        the latest window.
    - singleWindow: false
      $name: Also mark single-window previews
      $description: >-
        Mark the preview of an app with only one window too, as rank 1 while
        that window is recent (Preview minimum focus / Decay below), the same
        way the latest window of a multi-window flyout is marked. For a single
        window the mark therefore means "used within the preview decay";
        lower the decay to tighten it.
        For an active-window-only preview highlight, the much smaller
        taskbar-active-window-highlight mod does just that.
    - topStyle: plate
      $name: Rank 1 style
      $description: >-
        How to mark the most recent window. Whole plate = tint the whole
        card. Whole plate with accent = tinted card with an accent border (the
        look of Taskbar Active Window Highlight). Title = a tinted plate over
        the title row. Title with accent = the same with an accent border.
        Bar under title = thin line under the title (thickness follows Icons →
        Side bar thickness). The plates follow the card's rounded corners and stay
        visible on hover.
      $options:
      - plate: Whole plate
      - outline: Whole plate with accent
      - titleBg: Title
      - titleOutline: Title with accent
      - titleBar: Bar under title
    - restStyle: titleBg
      $name: Ranks 2+ style
      $description: >-
        How to mark the other ranked windows (2nd, 3rd …). Not marked = only
        rank 1 is shown.
      $options:
      - titleBg: Title
      - titleOutline: Title with accent
      - titleBar: Bar under title
      - plate: Whole plate
      - outline: Whole plate with accent
      - none: Not marked
    - outlineThickness: 2
      $name: Accent thickness
      $description: >-
        Border width in pixels for the "with accent" styles (1–8).
    - intensityRank1: 100
      $name: Intensity rank 1
      $description: Strength for the most recent window in the flyout (0–100)
    - intensityRank2: 70
      $name: Intensity rank 2
      $description: Strength for the 2nd most recent window in the flyout (0–100)
    - intensityRank3: 45
      $name: Intensity rank 3
      $description: >-
        Strength for the 3rd most recent window in the flyout (0–100). Ranks
        4+ fade further from this value.
    - fillOpacity: 40
      $name: Tint opacity
      $description: >-
        0–100. Strength of the Whole plate and Title tint (half of it for the
        "with accent" styles, and for ranks 2+ when rank 1 uses one, so no
        lower rank is tinted more than rank 1). Bar under title uses the full
        accent and ignores this. Independent of Icons → Fill opacity. Per-rank intensity
        still scales the result.
    - minFocusSeconds: 1
      $name: Minimum focus (seconds)
      $description: >-
        How long a window must stay focused before it enters that flyout’s
        recency list. Separate from app ranking min focus (0 = immediate).
        Clicking a thumbnail counts like any other focus.
    - decayMinutes: 15
      $name: Decay (minutes)
      $description: >-
        Drop a window from preview recency this long after you last used it
        (0 = never). Separate from app ranking decay.
  $name: Thumbnail previews
*/
// ==/WindhawkModSettings==

