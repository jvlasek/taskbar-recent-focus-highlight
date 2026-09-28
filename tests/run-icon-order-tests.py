"""Test production drawing-order planner and reject collection repositioning."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
s=(root/'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
start=s.index('struct IconZOrderInput {')
end=s.index('using IconZOrderBag',start)
helpers=s[start:end]
start=s.index('void EnsureGlowHostZOrder(')
end=s.index('bool ButtonHasOurChrome(',start)
positioning=s[start:end]
for mutation in ('children.RemoveAt(', 'children.InsertAt(', 'children.Append('):
    assert mutation not in positioning, f'IconPanel collection mutation reintroduced: {mutation}'
assert 'RestoreGlowZOrder(child)' in s
harness=(root/'tests/icon-child-order.cpp').read_text(encoding='utf-8-sig')
with tempfile.TemporaryDirectory(prefix='windhawk-icon-order-') as directory:
    cpp=Path(directory)/'order.cpp';exe=Path(directory)/'order.exe'
    cpp.write_text(harness.replace('// PRODUCTION_HELPERS',helpers),encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)

start=s.index('bool StillOwnIconZIndex(')
end=s.index('void RestoreGlowZOrder(FrameworkElement host) {',start)
owner_helpers=s[start:end]
with tempfile.TemporaryDirectory(prefix='windhawk-icon-owner-') as directory:
    cpp=Path(directory)/'owner.cpp';exe=Path(directory)/'owner.exe'
    harness=(root/'tests/icon-z-owner.cpp').read_text(encoding='utf-8-sig')
    cpp.write_text(harness.replace('// PRODUCTION_HELPERS',owner_helpers),encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
