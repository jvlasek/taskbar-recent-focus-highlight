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

![Taskbar side-bar highlight](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/main/screenshots/taskbar-highlight-edge.png)

![Thumbnail flyout ranks](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/main/screenshots/flyout-preview-highlight.png)

## What you see

After an app stays focused for the **minimum focus time** (default 8 seconds),
it joins a per-virtual-desktop recency list. The top N **running** taskbar
buttons on the current desktop get a highlight:

- **Side bar** (default) — a bar beside the icon (left on a bottom/top
  taskbar, under the icon on a left/right taskbar)
- **Frame** / **Full** — a rounded rectangle around or behind the icon

Rank 1 is strongest; ranks 2 and 3 use the intensities you set, and ranks
4+ fade further from rank 3.
Pinned icons that are not running on this desktop are never highlighted.
A new app is added only after the minimum focus time. Re-focus of an app
already in the list follows “When to skip min-focus” (default: it becomes
rank 1 immediately). A short Alt+Tab that never settles does not add a new
app. Each virtual desktop keeps its own list.

When you hover a combined icon and the flyout shows **two or more** windows,
that flyout gets its own recency ladder (independent of icon ranks). The
last focused window of that app is rank 1 in the flyout even if you used
other apps more recently. Single-window flyouts are left alone unless
**Also mark single-window previews** is on; then the card is marked as rank 1
while its window is recent, so it reads as "used within the preview decay".

Preview styles: rank 1 and ranks 2+ are set separately (default: whole-card
plate for rank 1, title tint for ranks 2+); also an outline, an outline
around the title (three experimental variants), a thin bar under the title, or none for
ranks 2+.

If you only want the active window highlighted in previews,
[taskbar-active-window-highlight](https://windhawk.net/mods/taskbar-active-window-highlight)
is a much lighter mod for exactly that.

## Settings (short)

- **General** — how many apps, min-focus wait, when to skip the wait on
  re-focus, decay, exclude list
- **Icons** — style, color (including system accent), per-rank intensity,
  thickness / size / fill
- **Previews** — on/off, how many windows, single-window previews, styles,
  intensities, separate min-focus and decay

All of those are in the Windhawk settings panel. Disable the mod in Windhawk
to clear highlights.

## Notes

- Works with Windows 11 Taskbar Styler: the mod draws its own elements and
  changes no property of the native ones, so the side bar stays visible on
  hover.
- An icon is matched by the application ID Explorer itself groups taskbar
  buttons by, not by its label or process name. A window that has no taskbar
  button is never ranked.
- Preview cards match through Explorer's own card view model (the window the
  card shows). If that is unavailable, the card is left unmarked rather than
  guessed from its title.
- Apps that show documents as taskbar tabs (e.g. SOLIDWORKS: one card per
  open document, one real window) are not marked in previews: Explorer links
  those cards to no window, and switching documents is not a window focus
  change. Their taskbar icon is ranked normally.
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
  [taskbar-active-window-highlight](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-active-window-highlight.wh.cpp);
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
    Time after last focus before an app drops out of the taskbar highlight
    list. (Preview windows use a separate decay under Previews.)
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
        Frame/Full follow the native background contour.
      $options:
      - leftBar: Side bar (left on bottom/top, under icon on left/right)
      - frame: Frame (hollow rounded rectangle)
      - full: Full (filled rounded rectangle)
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
      $name: Thickness (px)
      $description: >-
        Frame stroke or side-bar thickness (1–16). Full has no stroke.
    - glowSize: 92
      $name: Size (%)
      $description: >-
        Frame/Full: inset within native background (≤100, minimum 1px inset).
        Side bar: bar length.
    - glowFillOpacity: 40
      $name: Fill opacity
      $description: >-
        0–100. Soft fill strength for Full; solid bar opacity for Side. Frame uses
        stroke only. (Thumbnail tints use Previews → Tint opacity.)
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
        How to mark the most recent window. Plate = tint the whole card.
        Outline = tinted card with an accent border (the look of Taskbar
        Active Window Highlight). Title background = soft wash behind the
        title. Title bar = thin line under the title (thickness follows
        Icons → Thickness). Title outline (experimental) = the title wash
        with an accent border, like Outline but only around the title. Title
        plate outline (experimental) = Outline cut short under the title
        (same card edges and corners). Inset title plate outline
        (experimental) = the same, 4 px in from the card edges. Inset title
        plate tint (experimental) = Title background in that inset shape.
        Plate and Outline keep the card's own rounded corners and stay
        visible on hover.
      $options:
      - plate: Whole preview plate
      - outline: Plate with accent outline
      - titleBg: Title background tint
      - titleInsetBg: Inset title plate tint (experimental)
      - titleOutline: Title with accent outline (experimental)
      - titlePlateOutline: Title plate with accent outline (experimental)
      - titleInsetOutline: Inset title plate with accent outline (experimental)
      - titleBar: Bar under window title
    - restStyle: titleBg
      $name: Ranks 2+ style
      $description: >-
        How to mark the other ranked windows (2nd, 3rd …). Not marked = only
        rank 1 is shown.
      $options:
      - titleBg: Title background tint
      - titleInsetBg: Inset title plate tint (experimental)
      - titleOutline: Title with accent outline (experimental)
      - titlePlateOutline: Title plate with accent outline (experimental)
      - titleInsetOutline: Inset title plate with accent outline (experimental)
      - titleBar: Bar under window title
      - plate: Whole preview plate
      - outline: Plate with accent outline
      - none: Not marked
    - outlineThickness: 2
      $name: Outline thickness
      $description: >-
        Border width in pixels for the Outline and the Title outline styles
        (1–8).
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
        0–100. Strength of title-background wash and whole-preview plate. Title bar
        line uses full accent and ignores this. Independent of Icons → Fill opacity.
        Per-rank intensity still scales the result.
    - minFocusSeconds: 1
      $name: Minimum focus (seconds)
      $description: >-
        How long a window must stay focused before it enters that flyout’s
        recency list. Separate from app ranking min focus (0 = immediate).
        Clicking a thumbnail counts like any other focus.
    - decayMinutes: 15
      $name: Decay (minutes)
      $description: >-
        Drop a window from preview recency after this idle time (0 = never).
        Separate from app ranking decay.
  $name: Thumbnail previews
*/
// ==/WindhawkModSettings==

