"""Guard the 0.9.39 host-only diagnostic against native ZIndex writes/moves.
The older C++ planner/owner fixtures describe 0.9.37 and are retained as history.
This structural check is not a live XAML regression test.
"""
from pathlib import Path
root=Path(__file__).resolve().parent.parent
s=(root/'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
a=s.index('void EnsureGlowHostZOrder(')
b=s.index('bool ButtonHasOurChrome(',a)
body=s[a:b]
assert 'Children(' not in body
assert 'SetZIndex(host, hostZ)' in body
import re
value=int(re.search(r'constexpr int hostZ = (\d+);',body).group(1))
assert 0 < value <= 1000000, 'ZIndex exceeds the XAML API limit'
assert s.count('SetZIndex(')==1
assert 'ZIndexProperty()' not in s
assert 'RestoreGlowZOrder' not in s
assert 'PlanIconZOrder' not in s
assert 'WhRecentFocusZOrder' not in s
print('PASS: host-only ZIndex; no native ZIndex ownership or collection access in positioning helper')
