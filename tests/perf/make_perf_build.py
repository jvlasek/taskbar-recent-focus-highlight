"""Make a timing-instrumented copy of the mod (perf build).

Usage: python tests/perf/make_perf_build.py [--quiet] [source] [output]
Default: taskbar-recent-focus-highlight.wh.cpp -> tmp/perf/<name>.perf.wh.cpp
(--quiet: <name>.perf-quiet.wh.cpp)

--quiet turns every Wh_Log of the mod itself into a no-op (arguments are not
evaluated), so only the PERF lines remain and the numbers exclude the mod's
own logging. Use it for release baselines; the normal perf build shows the
cost with Mod logs on.

The copy keeps the mod id (settings stay shared) and gets "-perf" appended to
its version. Paste it into Windhawk's editor, use the taskbar normally, and
collect the "PERF ..." log lines (60 s intervals + a since-load summary at
unload). Never commit or submit the generated file. If an anchor below no
longer matches, the script fails loudly: update the anchor, keep the slot
name so results stay comparable between releases.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

# (slot, kind, anchor). The anchor is a unique substring of the line that
# opens the function or lambda body (or of the first signature line; the
# body starts at the next line ending in "{"). Hooks (kind "UI-hook") are
# timed only after Explorer's original function returns: the scope goes
# before the first top-level statement after the "_Original(" call, so the
# numbers are the mod's own cost, not Explorer's (fixed 2026-11-16: the
# first 0.11.31 runs included Explorer's work in the five hook slots).
SLOTS = [
    # Taskbar UI thread: Explorer calls into these (hooks, events).
    ("UpdateVisualStates", "UI-hook", "void WINAPI TaskListButton_UpdateVisualStates_Hook(void* pThis) {"),
    ("ThumbOnApplyTemplate", "UI-hook", "void WINAPI TaskItemThumbnailView_OnApplyTemplate_Hook(void* pThis) {"),
    ("ThumbPutSource", "UI-hook", "int32_t WINAPI TaskItemThumbnailView_put_ThumbnailSource_Hook(void* pThis,"),
    ("FlyoutTargetItemKey", "UI-hook", "void WINAPI HoverFlyoutModel_TargetItemKey_Hook(void* pThis, void* param1) {"),
    ("IconPanelSizeChanged", "UI", "watch.sizeChanged = iconPanel.SizeChanged("),
    ("RepeaterElementClearing", "UI", "token = repeater.ElementClearing("),
    # Taskbar UI thread: our own dispatcher work and its main pieces.
    ("ApplyAllHighlights", "UI", "void ApplyAllHighlights_UIThread() {"),
    ("RefreshButtonHighlight", "UI", "void RefreshButtonHighlight(FrameworkElement button) {"),
    ("ApplyButtonHighlight", "UI", "void ApplyButtonHighlight(FrameworkElement button, int rankOneBased) {"),
    ("EnsureButtonIdentity", "UI", "std::wstring EnsureButtonIdentity(FrameworkElement button) {"),
    ("CaptureButtonResolve", "UI", "ButtonCapture CaptureButton(FrameworkElement button) {"),
    ("RefreshFlyout", "UI", "void RefreshThumbnailFlyout_UIThread(FrameworkElement anyThumb) {"),
    ("ApplyThumbHighlight", "UI", "void ApplyThumbnailHighlight(FrameworkElement thumbView, int rankOneBased) {"),
    # Focus worker thread (never blocks the taskbar).
    ("WorkerWndProc", "Worker", "LRESULT CALLBACK HookThreadWndProc(HWND hWnd,"),
    ("WinEventProc", "Worker", "void CALLBACK WinEventProc(HWINEVENTHOOK /*hWinEventHook*/,"),
    ("HandleForeground", "Worker", "void HandleForegroundChanged(HWND hWnd) {"),
    ("AppMinFocusConfirm", "Worker", "void OnMinFocusTimerElapsed(MinFocusConfirmMode mode) {"),
    ("PreviewMinFocusConfirm", "Worker", "void OnPreviewMinFocusTimerElapsed(MinFocusConfirmMode mode) {"),
    ("DecayTimer", "Worker", "void OnDecayTimer() {"),
    # Either thread, nested in the above (ranking runs under g_stateMutex on
    # the worker and in the UI full pass). Added 2026-11-17 for 11-19 ms
    # worker lock holds in DecayTimer / AppMinFocusConfirm.
    ("RecomputeRanks", "Inner", "void RecomputeRanksForDesktopLocked(DesktopRecencyState& desk) {"),
    ("AttributeProvisional", "Inner", "void AttributeProvisionalAppsLocked(DesktopRecencyState& desk) {"),
    ("AppAppearsOnTaskbar", "Inner", "bool AppAppearsOnTaskbar(const std::wstring& key) {"),
    ("PruneWindowFocusMap", "Inner", "void PruneWindowFocusMapLocked(DesktopRecencyState& desk) {"),
]


# Slots without an anchor: g_stateMutex wait / hold (TimedLock in the header).
LOCK_SLOTS = [
    ("UIStateLockWait", "UI"),
    ("UIStateLockHold", "UI"),
    ("WorkerStateLockWait", "Worker"),
    ("WorkerStateLockHold", "Worker"),
    ("UIButtonLockWait", "UI"),
    ("UIButtonLockHold", "UI"),
    ("WorkerButtonLockWait", "Worker"),
    ("WorkerButtonLockHold", "Worker"),
]
STATE_LOCK = "std::lock_guard<std::mutex> lock(g_stateMutex);"
BUTTON_LOCK = "std::lock_guard<std::mutex> lock(g_buttonStateMutex);"


def build(text, quiet=False):
    crlf = "\r\n" in text
    body = text.replace("\r\n", "\n")
    if quiet:
        # Only calls; the "#define Wh_Log" of other code is not in this file.
        body = re.sub(r"\bWh_Log\(", "PERF_QUIET_LOG(", body)
    if STATE_LOCK not in body:
        raise SystemExit("no g_stateMutex lock_guard found")
    body = body.replace(STATE_LOCK, "perf::TimedLock lock(g_stateMutex);")
    body = body.replace(BUTTON_LOCK,
                        "perf::TimedLock lock(g_buttonStateMutex, true);")
    lines = body.split("\n")

    def find_unique(sub):
        hits = [i for i, l in enumerate(lines) if sub in l]
        if len(hits) != 1:
            raise SystemExit(f"anchor {sub!r}: {len(hits)} matches, need 1")
        return hits[0]

    inserts = []
    for slot, kind, anchor in SLOTS:
        i = find_unique(anchor)
        j = i
        while not lines[j].rstrip().endswith("{"):
            j += 1
            if j - i > 8:
                raise SystemExit(f"anchor {anchor!r}: no body brace")
        k = j + 1
        while not lines[k].strip():
            k += 1
        indent = re.match(r"\s*", lines[k]).group(0)
        at = j + 1
        if kind == "UI-hook":
            c = next((n for n in range(j + 1, j + 12) if "_Original(" in lines[n]), None)
            if c is None:
                raise SystemExit(f"{slot}: no _Original( call near the hook start")
            at = c + 1
            while (not lines[at].strip() or
                   not lines[at].startswith(indent) or
                   lines[at][len(indent)] in " }?:"):
                at += 1
                if at - c > 10:
                    raise SystemExit(f"{slot}: no statement after _Original(")
        inserts.append((at, f"{indent}PERF_SCOPE({slot});"))
    for at, line in sorted(inserts, reverse=True):
        lines.insert(at, line)

    i = find_unique("void Wh_ModUninit() {")
    lines.insert(i + 1, "    perf::Report(/*sinceLoad=*/true);")

    all_slots = [(s, k) for s, k, _ in SLOTS] + LOCK_SLOTS
    slots = "\n".join(
        [f"enum Slot {{ {', '.join(s for s, _ in all_slots)}, kSlotCount }};",
         "constexpr SlotDef kSlots[kSlotCount] = {"]
        + [f'    {{L"{s}", Kind::{k if k in ("Worker", "Inner") else "UI"}}},'
           for s, k in all_slots] + ["};"])
    header = (HERE / "perf_header.inc").read_text(encoding="utf-8")
    header = header.replace("// PERF_SLOTS", slots)
    i = find_unique("using namespace winrt::Windows::UI::Xaml;")
    quiet_macro = ["#define PERF_QUIET_LOG(...) ((void)0)"] if quiet else []
    lines[i + 1:i + 1] = (["", "#include <mutex>"] + quiet_macro
                          + header.rstrip("\n").split("\n"))

    i = next(n for n, l in enumerate(lines) if l.startswith("// @version"))
    lines[i] = lines[i].rstrip() + ("-perf-quiet" if quiet else "-perf")
    i = next(n for n, l in enumerate(lines) if l.startswith("// @name"))
    lines[i] = lines[i].rstrip() + " (perf build)"

    out = "\n".join(lines)
    return out.replace("\n", "\r\n") if crlf else out


def main():
    args = sys.argv[1:]
    quiet = "--quiet" in args
    args = [a for a in args if a != "--quiet"]
    src = Path(args[0]) if args else ROOT / "taskbar-recent-focus-highlight.wh.cpp"
    suffix = ".perf-quiet.wh.cpp" if quiet else ".perf.wh.cpp"
    dst = Path(args[1]) if len(args) > 1 else ROOT / "tmp" / "perf" / (src.name.replace(".wh.cpp", "") + suffix)
    text = src.read_bytes().decode("utf-8")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(build(text, quiet).encode("utf-8"))
    print(f"{len(SLOTS)} scopes -> {dst}")


if __name__ == "__main__":
    main()
