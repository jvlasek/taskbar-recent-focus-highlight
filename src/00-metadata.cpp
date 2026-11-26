// ==WindhawkMod==
// @id              taskbar-recent-focus-highlight
// @name            Taskbar Recent Focus Highlight
// @description     Highlight the taskbar icons and window previews of the apps you used last
// @version         0.12.3
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

Highlights the taskbar icons of the few apps you work in at once, so you
find them again on a crowded taskbar. Inside a multi-window preview flyout,
it ranks that app's windows the same way.

![A crowded taskbar with the three most recently used apps marked, and the open flyout ranking that app's windows](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/hero.png)

## Styles

The most recently used app is marked strongest, the next ones fainter.
Three styles to choose from:

- **Side bar** (default): a bar beside the icon.

  ![Side bar](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/icons-side-bar.png)
- **Plate**: the button's own rounded plate, tinted in the highlight color.

  ![Plate](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/icons-plate.png)
- **Plate with accent**: the same plate, lighter, with an accent border.

  ![Plate with accent](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/icons-plate-with-accent.png)

On a button with several windows the plates also tint its stacked edge. An
app that asks for attention keeps Windows' red marks on top.

## How apps get ranked

- An app counts after **8 s** in front (Minimum focus time); Alt+Tab passing
  through does not.
- It drops out **30 min** after you last *used* it (typing or clicking), so
  the app you work in stays highlighted.
- Only running apps. One list per virtual desktop, the same on every
  monitor.
- Start, Alt+Tab, the taskbar, desktop and lock screen do not count.

## Previews

Flyout window previews are ranked by their last use, for each running app
independently (own timing: 1 s minimum focus, 15 min decay). Rank 1 and the
rest have separate styles: tinted card, tinted title (either with an accent
border), or a bar under the title.

![Default preview styles: whole plate for rank 1, title plates for ranks 2 and 3](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/flyout-plate-title.png)

![The "with accent" preview styles](https://raw.githubusercontent.com/jvlasek/taskbar-recent-focus-highlight/10ce8381849664bb9a73e1530bf889528e38b812/screenshots/0.12.1/flyout-plate-accent-title-accent.png)

Single-window flyouts only with **Also mark single-window previews**. Just
the active window marked? Use the much lighter
[taskbar-active-window-highlight](https://windhawk.net/mods/taskbar-active-window-highlight).

## Settings

- **Taskbar** (first four): how many apps, decay, minimum focus time,
  coming back to an app.
- **Taskbar icons**: style, color (system accent or your own), intensity per
  rank, side bar size, fill, accent border width.
- **Thumbnail previews**: on/off, how many windows, their own decay and
  minimum focus time, single-window previews, styles, intensity per rank.
- **Exclude list** (last): apps never highlighted, on icons or previews.

The exclude list takes process names, paths or app IDs. Two buttons of one
program (Total Commander and its Lister, for example) have different app
IDs; with the mod log on, each button's ID is shown as
`Button identity: ... appId=`.

## Compatibility and limitations

- Works with Windows 11 Taskbar Styler. The mod only adds its own elements:
  it changes nothing Windows draws, takes no space, stays visible on hover
  and follows themes that resize the buttons.
- Icons are matched by the app ID Explorer itself groups buttons by. A
  window without a taskbar button is never ranked.
- **SOLIDWORKS** and other apps that show each open document as its own
  preview card manage those previews themselves, so their previews are not
  marked. Their taskbar icon is ranked normally.
- "Use" is keyboard or mouse input anywhere while the app is in front.
  Watching a long video or reading without touching anything is not use,
  so such an app drops out after the decay time; mouse movement on another
  monitor still counts for the app in front.
- Enabled while Explorer is already running, the ranks appear after the
  first taskbar button changes (hover or a window switch).

## Credits

- [taskbar-active-window-highlight](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-active-window-highlight.wh.cpp):
  reading button windows and preview cards through Explorer's own task
  interfaces, and the "with accent" look.
- [taskbar-volume-control-per-app](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-volume-control-per-app.wh.cpp):
  finding a taskbar button's task group.
- [taskbar-thumbnail-reorder](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-thumbnail-reorder.wh.cpp):
  handling the preview flyout.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- highlightCount: 3
  $name: "Taskbar: number of highlighted apps"
  $description: How many recently used apps to highlight on the taskbar (1–6 recommended)
- decayMinutes: 30
  $name: "Taskbar: decay time (minutes)"
  $description: >-
    An app drops out this long after you last used it (keyboard or mouse
    input while it was in front), so the app you work in stays highlighted.
    Window previews have their own decay under Thumbnail previews.
- minFocusSeconds: 8
  $name: "Taskbar: minimum focus time (seconds)"
  $description: >-
    An app counts as used only after it stays in front this long, so passing
    through it with Alt+Tab does not count. Window previews have their own
    minimum under Thumbnail previews.
- promoteMode: immediateTracked
  $name: "Taskbar: coming back to an app"
  $description: >-
    Whether an app you switch back to becomes rank 1 at once or waits the
    minimum focus time again.

    At once if still in the list (default): at once when you used the app
    within the decay time, even if it is not highlighted right now.

    At once only if highlighted: at once only when the app is highlighted
    right now; others wait.

    Always wait: every switch waits the minimum focus time.
  $options:
  - immediateTracked: At once if still in the list (default)
  - immediateTopN: At once only if highlighted
  - alwaysWait: Always wait
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
      $name: Highlight color
      $description: Base color for icon highlights (and previews)
      $options:
      - accent: System accent color
      - green: Green
      - blue: Blue
      - orange: Orange
      - white: White
      - custom: Custom (see custom highlight color)
    - customGlowColor: "#00C853"
      $name: Custom highlight color
      $description: Used when highlight color is Custom (hex, e.g. #00C853)
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
        (Thumbnail tints use Thumbnail previews → Tint opacity.)
    - glowAccentThickness: 2
      $name: Accent thickness (px)
      $description: Border width for Plate with accent (1–8).
  $name: Taskbar icons
- previews:
    - highlightEnabled: true
      $name: Highlight recent windows
      $description: >-
        When hovering a multi-window taskbar icon, rank that flyout’s thumbnails
        by when you last used them and mark the top N. Single-window flyouts:
        see "Also mark single-window previews".
    - highlightCount: 3
      $name: Number of highlighted windows
      $description: >-
        How many recent windows to mark in a multi-window flyout (1–6 recommended).
        Ranking is local to that flyout: the last focused window of this app is
        rank 1 even if other apps were used more recently. Set to 1 to mark only
        the latest window.
    - decayMinutes: 15
      $name: Decay time (minutes)
      $description: >-
        A window's preview drops out this long after you last used it
        (0 = never). Separate from the taskbar's decay time.
    - minFocusSeconds: 1
      $name: Minimum focus time (seconds)
      $description: >-
        How long a window must stay in front before its preview is ranked
        (0 = at once). Separate from the taskbar's minimum focus time.
        Clicking a thumbnail counts like any other switch.
    - singleWindow: false
      $name: Also mark single-window previews
      $description: >-
        Mark the preview of an app with only one window too, as rank 1 while
        that window is recent (the preview minimum focus and decay above), the same
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
        Bar under title = thin line under the title (thickness follows Taskbar
        icons → Side bar thickness). The plates follow the card's rounded corners and stay
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
      $description: Intensity for the most recent window in the flyout (0–100)
    - intensityRank2: 70
      $name: Intensity rank 2
      $description: Intensity for the 2nd most recent window in the flyout (0–100)
    - intensityRank3: 45
      $name: Intensity rank 3
      $description: >-
        Intensity for the 3rd most recent window in the flyout (0–100). Ranks
        4+ fade further from this value.
    - fillOpacity: 40
      $name: Tint opacity
      $description: >-
        0–100. Strength of the Whole plate and Title tint (half of it for the
        "with accent" styles, and for ranks 2+ when rank 1 uses one, so no
        lower rank is tinted more than rank 1). Bar under title uses the full
        accent and ignores this. Independent of Taskbar icons → Fill opacity. Per-rank intensity
        still scales the result.
  $name: Thumbnail previews
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
*/
// ==/WindhawkModSettings==

