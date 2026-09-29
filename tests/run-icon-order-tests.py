"""Exercise the production host placement planner without Explorer injection."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
s=(root/'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
a=s.index('struct GlowOrderChild {')
b=s.index('// Preserve the established style layering',a)
helpers=s[a:b]
a=s.index('void EnsureGlowHostZOrder(')
b=s.index('bool ButtonHasOurChrome(',a)
body=s[a:b]
assert 'children.RemoveAt(current)' in body and 'children.InsertAt(target, host)' in body
assert 'if (target == current) return;' in body
creation=s[s.index('Controls::Grid EnsureGlowHost('):s.index('void HideAllGlowLayers(')]
assert 'children.InsertAt(children.Size(), host)' in creation
assert '.Append(' not in creation and '.Append(' not in body
assert 'SetZIndex(' not in s and 'ZIndexProperty()' not in s
assert 'RestoreGlowZOrder' not in s and 'WhRecentFocusZOrder' not in s
clear=s[s.index('void ClearButtonHighlight('):s.index('Controls::Grid EnsureGlowHost(')]
assert 'Visibility::Collapsed' not in clear
with tempfile.TemporaryDirectory(prefix='windhawk-glow-order-') as folder:
    cpp=Path(folder)/'test.cpp';exe=Path(folder)/'test.exe'
    cpp.write_text((root/'tests/icon-child-order.cpp').read_text().replace('// PRODUCTION_HELPERS',helpers))
    subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
print('PASS: explicit insertion, no native ZIndex writes or retained-host clear')
