#!/usr/bin/env python3
"""Line-level code breakdown of the mod by feature.

Every line of src/NN-*.cpp is assigned to a *domain* (which half of the mod
it serves) and a *feature area*:

  apps     - Recent apps: taskbar icon ranks and glow
  windows  - Recent app windows: thumbnail flyout ranks and card paint
  shared   - used by both (focus tracking, desktops, threading, lifecycle...)

Classification is per top-level item (function, struct, global, macro),
found with tree-sitter. Comment and blank lines belong to the item that
follows them. A few items mix both halves line by line (README, settings
YAML, the Settings struct, LoadSettings, the Taskbar.View hook table); those
use explicit line ranges (OVERRIDES).

Usage:
  python tools/code_breakdown.py [--src src] [--out doc/code] [--json]
Writes <out>/code-breakdown.md (mermaid pie
and sankey charts plus tables). Regenerate after changing src/; the numbers
are line counts of the parts, so they move with every edit. --json also
writes the raw counts (per domain, area, part and item) to
<out>/code-breakdown.json.
Needs: pip install tree-sitter tree-sitter-cpp
"""
import argparse
import collections
import glob
import json
import os
import re

import tree_sitter_cpp as tscpp
from tree_sitter import Language, Parser

APPS, WINDOWS, SHARED = "apps", "windows", "shared"
DOMAIN_LABEL = {
    APPS: "Recent apps (taskbar)",
    SHARED: "Shared",
    WINDOWS: "Recent windows (previews)",
}

# Feature areas (sankey middle column).
A_META = "Metadata, README, settings UI"
A_SETTINGS = "Settings model & load"
A_FOCUS = "Focus sources & candidate"
A_CONFIRM = "Min-focus confirmation"
A_RECENCY = "Recency, ranking & decay"
A_DESKTOP = "Virtual desktops"
A_IDENTITY = "App identity & exclusion"
A_BUTTON = "Button identity & rank binding"
A_CARD = "Card -> window & flyout ranking"
A_ICONPAINT = "Icon glow painting & layout"
A_CARDPAINT = "Card overlay painting"
A_COLORS = "Colors & accent"
A_HOOKS = "Taskbar.View hooks"
A_DISPATCH = "UI dispatch & repaint scheduling"
A_WORKER = "Focus worker thread"
A_LIFECYCLE = "Init, settings change, unload"
A_INFRA = "Core types, state, logging, XAML helpers"

# Default (domain, area) per source part.
FILE_DEFAULT = {
    "00": (SHARED, A_META),
    "01": (SHARED, A_SETTINGS),
    "02": (SHARED, A_INFRA),
    "03": (SHARED, A_DESKTOP),
    "04": (SHARED, A_IDENTITY),
    "05": (APPS, A_RECENCY),
    "06": (WINDOWS, A_RECENCY),
    "07": (SHARED, A_COLORS),
    "08": (APPS, A_BUTTON),
    "09": (APPS, A_ICONPAINT),
    "10": (APPS, A_ICONPAINT),
    "11": (APPS, A_ICONPAINT),
    "12": (APPS, A_BUTTON),
    "13": (WINDOWS, A_CARD),
    "14": (WINDOWS, A_CARDPAINT),
    "15": (WINDOWS, A_CARDPAINT),
    "16": (SHARED, A_DISPATCH),
    "17": (SHARED, A_HOOKS),
    "18": (SHARED, A_CONFIRM),
    "19": (SHARED, A_WORKER),
    "20": (SHARED, A_SETTINGS),
    "21": (SHARED, A_LIFECYCLE),
}

# Per item: (part, name) -> (domain, area). Unlisted items use FILE_DEFAULT.
ITEM = {}


def items(part, domain, area, names):
    for n in names.split():
        ITEM[(part, n)] = (domain, area)


# 01 includes + settings
items("01", SHARED, A_INFRA, "GetCurrentTime MIDL_INTERFACE WH_WINRT_WINUI2 Xaml kClsidVirtualDesktopManager EnumName EnumToName NameToEnum MinFocusConfirmMode")
items("01", APPS, A_SETTINGS, "GlowStyle TaskbarEdge BarSide PromoteMode kGlowStyleNames kPromoteModeNames kTaskbarEdgeNames kBarSideNames")
items("01", WINDOWS, A_SETTINGS, "PreviewStyle kPreviewStyleNames")
items("01", SHARED, A_COLORS, "GlowColorMode kGlowColorNames kDefaultAccentPacked g_cachedAccent PackColor UnpackColor")
items("01", SHARED, A_CONFIRM, "MinFocusConfirmMode")
# 02 state
items("02", SHARED, A_RECENCY, "PendingFocus HwndHash GuidHash GuidEqual DesktopRecencyState g_stateMutex g_desktopMaps g_pendingFocus g_decayTimerArmed kDecayCheckIntervalMs RankIntensity")
items("02", APPS, A_RECENCY, "AppFocusInfo WindowAppFocus g_uncombinedMutex g_uncombinedWindowsByThread")
items("02", WINDOWS, A_RECENCY, "WindowFocusInfo g_windowConfirmSeq")
items("02", SHARED, A_IDENTITY, "WindowAppIdEntry g_windowAppIdMutex g_windowAppId WindowPid PublishWindowAppIds LookupWindowAppId ForgetWindowAppId")
items("02", SHARED, A_DESKTOP, "g_currentDesktopId g_haveCurrentDesktop g_vdm g_vdmMutex")
items("02", APPS, A_BUTTON, "CapturedWindow ButtonCapture ButtonState g_buttonStateMutex g_buttonStates g_taskbandResolveReady FindButtonStateLocked g_buttonsMutex g_trackedButtons kIsRunningGraceMs g_buttonSeedGeneration g_reseedOnDebouncedPass")
items("02", WINDOWS, A_CARD, "g_thumbViewsMutex g_trackedThumbViews")
items("02", APPS, A_DISPATCH, "g_pendingOverlaySweep g_fullRebindTimerArmed kFullRebindTimerId kFullRebindDebounceMs WM_APP_REQUEST_APPLY_DEBOUNCED")
items("02", SHARED, A_WORKER, "g_winEventHookThreadMutex g_winEventHookThread g_hookThreadHwnd g_hookThreadId g_hookThreadReadyEvent SignalHookThreadReady HookThreadWindow PostToHookThread OnHookThread WM_APP_FOREGROUND_CHANGED WM_APP_DESKTOP_SWITCHED WM_APP_SHUTDOWN WM_APP_REFRESH_ACCENT WM_APP_SETTINGS_CHANGED WM_APP_TASKBAR_READY g_shellHookMessage")
items("02", SHARED, A_RECENCY, "kDecayTimerId")
items("02", SHARED, A_DISPATCH, "g_dispatchersMutex UiDispatcher RememberUiDispatcher DispatcherTryRun")
items("02", SHARED, A_LIFECYCLE, "g_unloading g_taskbarViewHookAttempted g_taskbarViewHooked")
items("02", APPS, A_CONFIRM, "kMinFocusTimerId")
items("02", WINDOWS, A_CONFIRM, "kPreviewMinFocusTimerId")
items("02", SHARED, A_CONFIRM, "kTransientRetryPollMs")
items("02", APPS, A_ICONPAINT, "kGlowElementName kGlowBarName kGlowContourName kGlowStackName kBackgroundElementName")
items("02", WINDOWS, A_CARDPAINT, "kThumbGlowElementName kThumbGlowLayerNames kThumbTitleBgName kThumbTitleBarName")
# 03 virtual desktops (also hosts a few icon-layout globals)
items("03", APPS, A_ICONPAINT, "IconPanelLayoutWatch g_layoutWatchMutex g_layoutWatches g_iconPanelRelayoutDepth")
items("03", APPS, A_DISPATCH, "g_lastFullRefreshTick")
items("03", APPS, A_BUTTON, "ClearButtonRunningGrace")
# 04 helpers
items("04", SHARED, A_CONFIRM, "RemainingDeadlineMs TransientGraceMs TransientRetryDelayMs")
items("04", SHARED, A_WORKER, "ClampWinTimerMs ArmHookTimer DisarmHookTimer")
items("04", SHARED, A_RECENCY, "DecayMsFromMinutes IsTickDecayed RecencyMapsEmptyLocked EnsureDecayTimerArmed StopDecayTimerIfIdle")
items("04", SHARED, A_FOCUS, "NormalizeFocusHwnd GetWindowClassName ShouldIgnoreHwnd IsShellHostFileName IsExplorerFolderWindowClass IsTransientForeground GetWindowTitle")
items("04", SHARED, A_INFRA, "ToUpper FindChildByName FindDescendantByName")
# 05/06 ranking: AppAppearsOnTaskbar reads button state
items("05", APPS, A_RECENCY, "AppAppearsOnTaskbar DeriveAppsFromWindowsLocked RecomputeRanksForDesktopLocked")
# 07 colors: forward declarations of later parts
items("07", APPS, A_BUTTON, "ButtonIdentity PaintCacheState")
# 09 glow host: two helpers are reused by the card overlay
items("09", SHARED, A_INFRA, "SpanHostOverPanel ClearOurHostClip")
# 10 edge/layout: name helpers + promote-mode rule
items("10", APPS, A_CONFIRM, "PromoteModeName ShouldSkipAppMinFocus")
items("10", WINDOWS, A_SETTINGS, "PreviewStyleName")
# 12 button identity: generic live-element walker
items("12", SHARED, A_INFRA, "ForEachLiveElementOnThisDispatcher")
items("12", APPS, A_BUTTON, "ApplyAllHighlights_UIThread ClearAllHighlights_UIThread")
# 11 button tracking
items("11", APPS, A_BUTTON, "PruneTrackedButtons_UIThread PruneDeadButtonStates_UIThread TrackButton_UIThread")
# 13 card resolve
items("13", SHARED, A_INFRA, "FindAncestorByClassName")
# 14/15 preview scheduling
items("14", WINDOWS, A_DISPATCH, "g_previewFlyoutRefreshPending g_previewFlyoutRefreshNeeded g_thumbRelayoutDepth g_previewFlyoutRefreshFollowUps SchedulePreviewFlyoutRefresh PickLiveFlyoutThumbOnThisDispatcher")
items("15", WINDOWS, A_CARD, "RefreshThumbnailFlyout_UIThread ClearAllThumbnailHighlights_UIThread")
items("15", WINDOWS, A_DISPATCH, "RequestApplyPreviewVisuals")
items("15", SHARED, A_DISPATCH, "g_loggedNoDispatcherAnchor")
# 16 dispatch: the full pass is the icon pass
items("16", APPS, A_DISPATCH, "g_fullPassQueuedMutex g_fullPassQueuedThreads ClearFullPassQueued QueueFullPass RequestApplyVisuals RequestApplyVisualsDebounced")
# 17 hooks
items("17", APPS, A_HOOKS, "TaskListButton_UpdateVisualStates_t TaskListButton_UpdateVisualStates_Original TaskListButtonElementFromThis TaskListButton_UpdateVisualStates_Hook")
items("17", APPS, A_BUTTON, "MarkButtonUnranked RefreshButtonHighlight")
items("17", APPS, A_DISPATCH, "ScheduleRefreshAllHighlights")
items("17", WINDOWS, A_HOOKS, "TaskItemThumbnailView_OnApplyTemplate_t TaskItemThumbnailView_OnApplyTemplate_Original TaskItemThumbnailView_OnApplyTemplate_Hook TaskItemThumbnailView_put_ThumbnailSource_t TaskItemThumbnailView_put_ThumbnailSource_Original TaskItemThumbnailView_put_ThumbnailSource_Hook HoverFlyoutModel_TargetItemKey_t HoverFlyoutModel_TargetItemKey_Original HoverFlyoutModel_TargetItemKey_Hook")
# 18 min-focus: one state machine, two tracks
items("18", APPS, A_CONFIRM, "CancelMinFocusTimer AppTrack OnMinFocusTimerElapsed EnsurePendingAppTimer")
items("18", WINDOWS, A_CONFIRM, "CancelPreviewMinFocusTimer PreviewTrack OnPreviewMinFocusTimerElapsed EnsurePendingPreviewTimer SchedulePreviewConfirm")
items("18", SHARED, A_FOCUS, "StillPendingForeground HandleForegroundChanged")
items("18", SHARED, A_RECENCY, "OnDecayTimer HandleWindowDestroyed")
# 19 worker
items("19", SHARED, A_FOCUS, "HandleForegroundNotification HandleShellActivation WinEventProc")
items("19", SHARED, A_LIFECYCLE, "DropExcludedRecords")
# 20 settings
items("20", WINDOWS, A_SETTINGS, "kLegacyPreviewStyleState LegacyPreviewStyle ApplyLegacyPreviewStyle")

# Line ranges inside mixed items: part -> [(first, last, domain, area)].
OVERRIDES = {
    "00": [
        (21, 28, SHARED, A_META),       # README intro + hero screenshot
        (29, 56, APPS, A_META),         # Styles, How apps get ranked
        (57, 71, WINDOWS, A_META),      # Previews
        (126, 213, APPS, A_META),       # YAML: icons.* (count, decay, min focus, look)
        (214, 300, WINDOWS, A_META),    # YAML: previews.*
    ],
    "01": [
        (177, 187, APPS, A_SETTINGS),   # Settings: icon fields
        (189, 189, APPS, A_SETTINGS),   # decayMinutes
        (188, 188, WINDOWS, A_SETTINGS),
        (190, 200, WINDOWS, A_SETTINGS),
    ],
    "20": [
        (80, 105, APPS, A_SETTINGS),    # LoadSettings: general + icons
        (107, 127, WINDOWS, A_SETTINGS),  # LoadSettings: previews
    ],
    "17": [
        (231, 260, APPS, A_HOOKS),      # hook table: button identity + paint
        (261, 295, WINDOWS, A_HOOKS),   # hook table: optional thumbnail hooks
        (306, 319, WINDOWS, A_HOOKS),   # preview resolve availability logs
    ],
    "21": [
        (127, 219, SHARED, A_LIFECYCLE),
    ],
}


def is_code_line(text):
    s = text.strip()
    return bool(s) and not s.startswith("//") and not s.startswith("/*") \
        and not s.startswith("*")


def top_items(parser, src):
    tree = parser.parse(src)
    out = []

    def name_of(node, kind):
        if kind == "function_definition":
            d = node.child_by_field_name("declarator")
            while d is not None and d.type != "function_declarator":
                nxt = d.child_by_field_name("declarator")
                if nxt is None:  # reference_declarator has no field name
                    nxt = next((x for x in d.children
                                if x.type.endswith("declarator")), None)
                d = nxt
            if d is not None:
                nm = d.child_by_field_name("declarator")
                return src[nm.start_byte:nm.end_byte].decode()
            return "?"
        if kind in ("struct_specifier", "class_specifier", "enum_specifier"):
            nm = node.child_by_field_name("name")
            return src[nm.start_byte:nm.end_byte].decode() if nm else "<anon>"
        if kind in ("preproc_def", "preproc_function_def"):
            nm = node.child_by_field_name("name")
            return src[nm.start_byte:nm.end_byte].decode() if nm else "?"
        txt = src[node.start_byte:node.end_byte].decode(errors="replace")
        head = re.split(r"[={(;]", txt, maxsplit=1)[0]
        m = re.findall(r"\b(g_\w+|t_\w+|k\w+|WM_APP_\w+|[A-Z]\w+)\b", head)
        if "(" in txt.split("=")[0] and not m:
            m = re.findall(r"\b(\w+)\s*\(", txt)
        return m[-1] if m else head.strip()[:40]

    def walk(n):
        for c in n.children:
            if c.type == "namespace_definition":
                body = c.child_by_field_name("body")
                if body:
                    walk(body)
                continue
            if c.type == "comment" or not c.is_named:
                continue
            node, kind = c, c.type
            if c.type == "template_declaration":
                for cc in c.children:
                    if cc.type in ("function_definition", "struct_specifier",
                                   "class_specifier", "declaration",
                                   "alias_declaration"):
                        node, kind = cc, cc.type
            if kind == "declaration":
                # A forward declaration of a function: name the function.
                fd = None
                stack = [node]
                while stack:
                    x = stack.pop()
                    if x.type == "function_declarator":
                        fd = x
                        break
                    stack.extend(x.children)
                if fd is not None:
                    nm = fd.child_by_field_name("declarator")
                    name = src[nm.start_byte:nm.end_byte].decode()
                else:
                    name = name_of(node, kind)
            else:
                name = name_of(node, kind)
            if c.type in ("preproc_include", "using_declaration",
                          "namespace_alias_definition"):
                kind = "infra"
            out.append((c.start_point[0] + 1, c.end_point[0] + 1, kind, name))

    walk(tree.root_node)
    return out


def classify(src_dir):
    lang = Language(tscpp.language())
    parser = Parser(lang)
    lines_out = []  # (part, file, lineno, domain, area, item, is_code)
    unused = set(ITEM)
    paths = sorted(glob.glob(os.path.join(src_dir, "[0-9][0-9]-*.cpp")))
    # Pass 1: where each function is defined, so a forward declaration in
    # an earlier part takes the classification of its definition.
    defined = {}
    for path in paths:
        part = os.path.basename(path)[:2]
        raw = open(path, "rb").read().replace(b"\r\n", b"\n")
        for (_s, _e, kind, name) in top_items(parser, raw):
            if kind == "function_definition" and name not in defined:
                key = (part, name)
                defined[name] = ITEM.get(key, FILE_DEFAULT[part])
                unused.discard(key)
    for path in paths:
        fname = os.path.basename(path)
        part = fname[:2]
        raw = open(path, "rb").read().replace(b"\r\n", b"\n")
        text = raw.decode("utf-8").split("\n")
        if text and text[-1] == "":
            text.pop()
        n = len(text)
        owner = [None] * (n + 2)  # 1-based: item index
        its = top_items(parser, raw)
        for idx, (s, e, kind, name) in enumerate(its):
            for ln in range(s, e + 1):
                owner[ln] = idx
        # Comments / blanks go to the next item; trailing ones to the last.
        nxt = None
        for ln in range(n, 0, -1):
            if owner[ln] is None:
                owner[ln] = nxt
            else:
                nxt = owner[ln]
        last = None
        for ln in range(1, n + 1):
            if owner[ln] is None:
                owner[ln] = last
            last = owner[ln]
        for ln in range(1, n + 1):
            idx = owner[ln]
            if idx is None:
                dom, area, name = (*FILE_DEFAULT[part], "(file)")
            else:
                _s, _e, kind, name = its[idx]
                key = (part, name)
                if key in ITEM:
                    dom, area = ITEM[key]
                    unused.discard(key)
                elif kind == "declaration" and name in defined:
                    dom, area = defined[name]  # forward declaration
                elif kind == "infra":
                    dom, area = SHARED, A_INFRA
                else:
                    dom, area = FILE_DEFAULT[part]
            for (a, b, d2, a2) in OVERRIDES.get(part, []):
                if a <= ln <= b:
                    dom, area = d2, a2
            # Part 00 is Windhawk metadata, README and settings YAML inside
            # comments: counted as lines, never as code.
            lines_out.append(dict(part=part, file=fname, line=ln, domain=dom,
                                  area=area, item=name,
                                  code=part != "00" and
                                  is_code_line(text[ln - 1])))
    return lines_out, sorted(unused)


def summarize(lines):
    tot = collections.Counter()
    code = collections.Counter()
    for l in lines:
        for key in [("domain", l["domain"]),
                    ("area", l["domain"], l["area"]),
                    ("file", l["file"], l["domain"]),
                    ("filearea", l["file"], l["area"], l["domain"]),
                    ("item", l["file"], l["item"], l["domain"], l["area"])]:
            tot[key] += 1
            if l["code"]:
                code[key] += 1
    return tot, code


PIE_COLORS = {
    # Colorblind-safe trio: apps blue, shared grey, windows orange.
    APPS: "#2F6FB3", SHARED: "#8C8C8C", WINDOWS: "#D9822B",
}
# One fixed color per feature area, so an area looks the same in every pie.
AREA_COLOR = {
    A_BUTTON: "#2F6FB3", A_ICONPAINT: "#7FA7D9",
    A_CARD: "#D9822B", A_CARDPAINT: "#F0B67A",
    A_FOCUS: "#4E9A6A", A_CONFIRM: "#9CCB9F", A_RECENCY: "#B5495B",
    A_WORKER: "#7A5BA6", A_IDENTITY: "#C9A227", A_INFRA: "#8C8C8C",
    A_DISPATCH: "#3C9BB0", A_LIFECYCLE: "#9C6B3E", A_DESKTOP: "#5E8C31",
    A_COLORS: "#C46FA0", A_META: "#BDBDBD", A_SETTINGS: "#6B7A8F",
    A_HOOKS: "#E07B7B",
}


def pct(n, d):
    return f"{100.0 * n / d:.1f} %" if d else "-"


def mermaid_pie(title, pairs, colors=None, show_data=True):
    init = ""
    if colors:
        tv = ", ".join(f'"pie{i + 1}": "{c}"' for i, c in enumerate(colors))
        init = ("%%{init: {\"themeVariables\": {" + tv +
                ", \"pieOuterStrokeWidth\": \"1px\"}}}%%\n")
    body = "\n".join(f'    "{k}" : {v}' for k, v in pairs)
    return (f"```mermaid\n{init}pie {'showData ' if show_data else ''}"
            f"title {title}\n{body}\n```\n")


def short_part(fname):
    return fname[:-4]  # "05-ranking"


def write_markdown(data, out_md):
    T = data["total_lines"]
    dom = {r["key"][0]: r for r in data["domains"]}
    order = [APPS, SHARED, WINDOWS]
    L = []
    w = L.append
    w("# Code breakdown: recent apps vs recent windows\n")
    w("<!-- Generated by tools/code_breakdown.py from src/. Do not edit by "
      "hand; rerun the script. -->\n")
    w(f"The mod is {T} lines in 22 parts under `src/` "
      f"({data['total_code']} code lines: not blank, not comment, metadata "
      "excluded). Every line is assigned to one of three domains and one "
      "feature area by `tools/code_breakdown.py` (classification per "
      "function, struct and global; comments go with the item below them).\n")
    w("| Domain | What it is | Lines | Share | Code lines |")
    w("|---|---|---:|---:|---:|")
    what = {
        APPS: "Taskbar icon ranks: app recency, button identity, glow painting",
        SHARED: "Focus sources, min-focus machine, desktops, identity, "
                "threads, lifecycle",
        WINDOWS: "Flyout card ranks: window recency, card -> window, card "
                 "painting",
    }
    for d in order:
        r = dom[d]
        w(f"| **{DOMAIN_LABEL[d]}** | {what[d]} | {r['lines']} | "
          f"{pct(r['lines'], T)} | {r['code']} |")
    w("")
    w("## Domain split\n")
    w(mermaid_pie("Lines by domain",
                  [(DOMAIN_LABEL[d], dom[d]["lines"]) for d in order],
                  [PIE_COLORS[d] for d in order]))
    w("Without the shared part, the taskbar half is "
      f"{pct(dom[APPS]['lines'], dom[APPS]['lines'] + dom[WINDOWS]['lines'])}"
      " and the preview half "
      f"{pct(dom[WINDOWS]['lines'], dom[APPS]['lines'] + dom[WINDOWS]['lines'])}"
      " of the feature-specific code.\n")

    # Sankey: part -> domain -> area
    w("## Where the lines go\n")
    w("Left: source parts. Middle: domain. Right: feature area. An area fed "
      "by more than one domain has a shared core plus feature-specific "
      "branches (for example the min-focus machine: one shared state "
      "machine, an app track and a preview track).\n")
    w("```mermaid")
    w("---")
    w("config:")
    w("  sankey:")
    w("    showValues: true")
    w("    linkColor: source")
    w("    nodeAlignment: justify")
    w("    width: 1100")
    w("    height: 900")
    w("---")
    w("sankey-beta")
    for r in data["files"]:
        fname, d = r["key"]
        w(f'{short_part(fname)},"{DOMAIN_LABEL[d]}",{r["lines"]}')
    area_rows = sorted(data["areas"], key=lambda r: (order.index(r["key"][0]),
                                                     -r["lines"]))
    for r in area_rows:
        d, area = r["key"]
        w(f'"{DOMAIN_LABEL[d]}","{area}",{r["lines"]}')
    w("```\n")

    # Per-domain pies
    w("## Feature areas inside each domain\n")
    for d in order:
        rows = sorted([r for r in data["areas"] if r["key"][0] == d],
                      key=lambda r: -r["lines"])
        w(f"### {DOMAIN_LABEL[d]} ({dom[d]['lines']} lines)\n")
        pairs = [(r["key"][1], r["lines"]) for r in rows]
        colors = [AREA_COLOR[a] for a, _ in pairs]
        if len(pairs) > 12:  # mermaid pie has 12 theme colors
            rest = pairs[11:]
            pairs = pairs[:11] + [(
                "Other: " + ", ".join(a for a, _ in rest),
                sum(n for _, n in rest))]
            colors = colors[:11] + ["#E3E3E3"]
        w(mermaid_pie(DOMAIN_LABEL[d], pairs, colors))

    # Area x domain table
    w("## Feature area x domain\n")
    w("Lines (code lines in brackets). A feature with numbers in more than "
      "one column is partly shared.\n")
    areas = sorted({r["key"][1] for r in data["areas"]})
    cell = {(r["key"][0], r["key"][1]): r for r in data["areas"]}
    w("| Feature area | Recent apps | Shared | Recent windows | Total |")
    w("|---|---:|---:|---:|---:|")
    for a in sorted(areas, key=lambda a: -sum(
            cell[(d, a)]["lines"] for d in order if (d, a) in cell)):
        vals = []
        tot = 0
        for d in order:
            c = cell.get((d, a))
            if c:
                vals.append(f"{c['lines']} ({c['code']})")
                tot += c["lines"]
            else:
                vals.append("")
        w(f"| {a} | " + " | ".join(vals) + f" | {tot} |")
    w(f"| **Total** | " + " | ".join(
        f"**{dom[d]['lines']}**" for d in order) + f" | **{T}** |")
    w("")

    # Parts table
    w("## Source parts\n")
    w("How pure each part is. Mixed parts are where the two halves meet.\n")
    w("| Part | Lines | Recent apps | Shared | Recent windows | Mostly |")
    w("|---|---:|---:|---:|---:|---|")
    byfile = collections.defaultdict(dict)
    for r in data["files"]:
        byfile[r["key"][0]][r["key"][1]] = r["lines"]
    for f in sorted(byfile):
        tot = sum(byfile[f].values())
        top = max(byfile[f], key=byfile[f].get)
        purity = byfile[f][top] / tot
        mostly = DOMAIN_LABEL[top] + ("" if purity > 0.98 else
                                      f" ({purity * 100:.0f} %)")
        w(f"| `{f}` | {tot} | " + " | ".join(
            str(byfile[f].get(d, "")) for d in order) + f" | {mostly} |")
    w("")

    # Largest items
    w("## Largest items\n")
    w("Functions and types over 60 lines (with their leading comments).\n")
    w("| Item | Part | Domain | Area | Lines |")
    w("|---|---|---|---|---:|")
    merged = collections.Counter()
    meta = {}
    for r in data["items"]:
        f, item, d, a = r["key"]
        if f.startswith("00") or item.startswith("#include"):
            continue
        merged[(f, item)] += r["lines"]
        meta[(f, item)] = (d, a) if (f, item) not in meta else ("mixed", "")
    for (f, item), n in merged.most_common():
        if n < 60:
            break
        d, a = meta[(f, item)]
        dl = DOMAIN_LABEL.get(d, "mixed")
        w(f"| `{item}` | {short_part(f)} | {dl} | {a} | {n} |")
    w("")
    w("## How the classification works\n")
    w("- Domain = which half of the mod needs the item. *Shared* means "
      "both halves call it, or it is infrastructure neither works without "
      "(focus worker, settings snapshot, UI dispatch, unload).")
    w("- Items are classified one by one in `ITEM` in "
      "`tools/code_breakdown.py`; unlisted items take their part's default "
      "(`FILE_DEFAULT`). A forward declaration takes the class of the "
      "definition it declares.")
    w("- Items that mix both halves line by line use line ranges "
      "(`OVERRIDES`): the README and settings YAML in `00-metadata`, the "
      "`Settings` struct, `LoadSettings`, and the Taskbar.View hook table.")
    w("- `HandleForegroundChanged`, `OnDecayTimer`, `HandleWindowDestroyed` "
      "and `Wh_ModSettingsChanged` serve both halves in one body and count "
      "as shared.")
    w("- After moving or renaming functions, rerun the script; it warns "
      "about classification entries that no longer match an item.\n")
    with open(out_md, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="src")
    ap.add_argument("--out", default=os.path.join("doc", "code"))
    ap.add_argument("--json", action="store_true",
                    help="also write code-breakdown.json")
    args = ap.parse_args()
    lines, unused = classify(args.src)
    tot, code = summarize(lines)
    if unused:
        print("WARNING: classification entries that matched no item:")
        for u in unused:
            print("  ", u)
    os.makedirs(args.out, exist_ok=True)

    def rows(kind):
        return [dict(key=list(k[1:]), lines=v, code=code[k])
                for k, v in sorted(tot.items()) if k[0] == kind]

    data = dict(total_lines=len(lines),
                total_code=sum(1 for l in lines if l["code"]),
                domains=rows("domain"), areas=rows("area"),
                files=rows("file"), items=rows("item"))
    if args.json:
        with open(os.path.join(args.out, "code-breakdown.json"), "w",
                  encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=1)
    write_markdown(data, os.path.join(args.out, "code-breakdown.md"))
    for r in data["domains"]:
        print(f"{DOMAIN_LABEL[r['key'][0]]:28} {r['lines']:5} lines "
              f"{r['code']:5} code")


if __name__ == "__main__":
    main()
