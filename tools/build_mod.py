#!/usr/bin/env python3
"""Join src/*.cpp (in name order) into taskbar-recent-focus-highlight.wh.cpp.

The development source is split into parts; Windhawk and windhawk-mods take
one file. Edit the parts, then run this script and commit both.

  python tools/build_mod.py           write the .wh.cpp
  python tools/build_mod.py --check   fail if the .wh.cpp is out of date
  python tools/build_mod.py --split   one-off: cut the .wh.cpp into parts
                                      (refuses when src/ has parts)

Parts are joined byte for byte (no separators), so the output is exactly the
concatenation and line numbers in Windhawk logs map to one part each.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
OUT = ROOT / "taskbar-recent-focus-highlight.wh.cpp"

# (part name, first line of the part: an exact line of the joined file).
# Used only by --split; afterwards src/ is the source of truth.
SPLIT = [
    ("00-metadata.cpp", None),
    ("01-includes-settings.cpp", "#include <windhawk_utils.h>"),
    ("02-state.cpp", "// Focus / recency state"),
    ("03-virtual-desktops.cpp", "// Virtual desktops (public COM"),
    ("04-helpers.cpp", "// Helpers"),
    ("05-ranking.cpp", "// Ranking"),
    ("06-window-recency.cpp", "// Window-level recency (thumbnail previews)"),
    ("07-colors.cpp", "// Color / style helpers (UI thread)"),
    ("08-identity-matching.cpp", "// Button identity matching"),
    ("09-glow-host.cpp", "// Apply / clear visual highlight on one button"),
    ("10-taskbar-edge-layout.cpp", "PCWSTR GlowStyleName(GlowStyle s) {"),
    ("11-button-paint.cpp", "void ApplyButtonHighlight(FrameworkElement button, int rankOneBased) {"),
    ("12-button-identity.cpp", "// Button → task item / task group"),
    ("13-previews.cpp", "// Thumbnail preview glow (multi-window flyouts)"),
    ("14-preview-overlay.cpp", "// Card overlay (Plate / Outline preview styles"),
    ("15-preview-paint.cpp", "void ApplyThumbnailHighlight(FrameworkElement thumbView, int rankOneBased) {"),
    ("16-ui-dispatch.cpp", "bool DispatcherOpWasQueued("),
    ("17-taskbar-view-hooks.cpp", "// Taskbar.View.dll hooks"),
    ("18-focus-promotion.cpp", "// Focus promotion (min focus time)"),
    ("19-focus-thread.cpp", "// WinEvent hook thread"),
    ("20-settings-load.cpp", "// Settings load"),
    ("21-entry-points.cpp", "// Windhawk entry points"),
]
BANNER = b"// " + b"-" * 75


def parts():
    return sorted(SRC.glob("*.cpp"))


def joined():
    files = parts()
    if not files:
        raise SystemExit("no parts in src/")
    return b"".join(p.read_bytes() for p in files)


def split():
    if parts():
        raise SystemExit("src/ already has parts; --split would overwrite them")
    data = OUT.read_bytes()
    lines = data.splitlines(keepends=True)
    starts = []
    for name, marker in SPLIT:
        if marker is None:
            starts.append(0)
            continue
        m = marker.encode("utf-8")
        hits = [i for i, l in enumerate(lines) if l.rstrip(b"\r\n").startswith(m)]
        if len(hits) > 1 and not m.startswith(b"//"):
            hits = hits[:1]  # e.g. an overload: the first definition
        if len(hits) != 1:
            raise SystemExit(f"{name}: marker {marker!r} matches {len(hits)} lines")
        i = hits[0]
        # A section starts at its banner line, when it has one; a function
        # at the comment block (and template line) above it.
        if i > 0 and lines[i - 1].rstrip(b"\r\n") == BANNER:
            i -= 1
        elif not m.startswith(b"//"):
            while i > 0 and lines[i - 1].lstrip().startswith((b"//", b"template")):
                i -= 1
        starts.append(i)
    if starts != sorted(starts):
        raise SystemExit("markers out of order")
    SRC.mkdir(exist_ok=True)
    for old in parts():
        old.unlink()
    for k, (name, _) in enumerate(SPLIT):
        end = starts[k + 1] if k + 1 < len(starts) else len(lines)
        (SRC / name).write_bytes(b"".join(lines[starts[k]:end]))
    assert joined() == data
    print(f"{len(SPLIT)} parts -> {SRC}")


def main():
    args = sys.argv[1:]
    if args == ["--split"]:
        split()
        return
    data = joined()
    if args == ["--check"]:
        if not OUT.exists() or OUT.read_bytes() != data:
            raise SystemExit(f"{OUT.name} is out of date: run tools/build_mod.py")
        print(f"{OUT.name} matches src/ ({len(parts())} parts)")
        return
    if args:
        raise SystemExit(__doc__)
    if OUT.exists() and OUT.read_bytes() == data:
        print(f"{OUT.name} unchanged")
        return
    OUT.write_bytes(data)
    print(f"wrote {OUT.name} from {len(parts())} parts")


if __name__ == "__main__":
    main()
