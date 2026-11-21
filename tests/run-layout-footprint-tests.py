"""Overlay chrome must not size native panels (0.12.1).

A plate with an explicit Width/Height and balanced margins asked its icon
panel for the old BackgroundElement size, and held its button big after a
Taskbar Styler theme (BottomDensy) shrank the others. The overlays now use
margins whose far side takes back offset and size: desired size 0, same
placement. This simulates XAML's measure / arrange for one axis.
"""
import sys as _sys
from pathlib import Path as _Path

_sys.path.insert(0, str(_Path(__file__).resolve().parent))
from mod_source import source_text  # src/*.cpp joined
import re

source = source_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


def desired(size, near, far):
    return max(0.0, size + near + far)


def place(slot, size, near, far, align):
    # FrameworkElement arrange: deflate the slot by the margins, then align
    # the element (explicit size) inside what is left. Returns (start, clip).
    inner = slot - near - far
    if align == 'start':
        off = 0.0
    elif align == 'end':
        off = inner - size
    else:
        off = (inner - size) / 2
    return near + off, size > inner + 1e-9


def zero_desired(w, h, dx, dy):
    return (dx - w * 0.5, dy - h * 0.5, -dx - w * 0.5, -dy - h * 0.5)


# The formula in the source is the one simulated here.
zdm = function('Thickness ZeroDesiredMargin(')
assert 'return Thickness{dx - w * 0.5, dy - h * 0.5, -dx - w * 0.5, -dy - h * 0.5};' in zdm
assert 'b.Margin(ZeroDesiredMargin(w, h, dx, dy));' in function('void PaintNativeContour(')
assert not re.search(r'b\.Margin\(\{dx, dy, -dx, -dy\}\)', source)

# Plates: centred, moved by dx; old and new place it identically.
for slot in (32.0, 40.0, 44.0, 48.0):
    for w in (24.0, 32.0, 40.0, 48.0, 60.0):
        for dx in (-4.0, 0.0, 2.5):
            l, t, r, b = zero_desired(w, w, dx, dx)
            assert desired(w, l, r) == 0.0
            old = place(slot, w, dx, -dx, 'center')
            new = place(slot, w, l, r, 'center')
            assert abs(old[0] - new[0]) < 1e-9, (slot, w, dx, old, new)
            assert not new[1]  # no layout clip, even when w > slot

# Side bar: near pad, far side takes back thickness + pad; centred length.
bar = function('void StyleGlowBarOnSide(')
assert 'const double back = -(t + edgePad);' in bar
for m in ('Thickness{edgePad, -len * 0.5, back, -len * 0.5}',
          'Thickness{back, -len * 0.5, edgePad, -len * 0.5}',
          'Thickness{-len * 0.5, edgePad, -len * 0.5, back}',
          'Thickness{-len * 0.5, back, -len * 0.5, edgePad}'):
    assert m in bar, m
for slot in (32.0, 44.0):
    for t in (2.0, 4.0):
        for pad in (1.0, 2.0):
            back = -(t + pad)
            assert desired(t, pad, back) == 0.0
            for align, near, far in (('start', pad, back), ('end', back, pad)):
                o = place(slot, t, *((pad, 0.0) if align == 'start' else (0.0, pad)), align)
                n = place(slot, t, near, far, align)
                assert abs(o[0] - n[0]) < 1e-9 and not n[1]
            ln = slot * 0.8
            assert desired(ln, -ln / 2, -ln / 2) == 0.0
            assert abs(place(slot, ln, 0, 0, 'center')[0] -
                       place(slot, ln, -ln / 2, -ln / 2, 'center')[0]) < 1e-9

# Preview overlay children: Left/Top at (x, y) with an explicit size.
overlay = function('void PlaceOverlayChild(')
assert 'el.Margin(Thickness{x, y, width > 0 ? -(x + width) : 0,' in overlay
for slot in (120.0, 200.0):
    for x, w in ((0.0, 120.0), (4.0, 100.0), (6.0, 200.0)):
        assert desired(w, x, -(x + w)) == 0.0
        o = place(slot, w, x, 0.0, 'start')
        n = place(slot, w, x, -(x + w), 'start')
        assert o[0] == n[0] and not n[1]

print('PASS: plates, side bar and preview overlays have zero desired size and unchanged placement')
