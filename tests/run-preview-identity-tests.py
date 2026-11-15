"""Preview recency guard: a recorded window counts only while its PID holds.

Until 0.11.19 this also exercised GetWindowForThumbnailTaskItem (native
click / ctor HWND getter); that code was removed in 0.11.20 along with every
taskbar.dll hook.
"""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
def function(signature):
    start = 0
    while True:
        start = source.index('\n' + signature, start) + 1
        brace, semi = source.find('{', start), source.find(';', start)
        if brace != -1 and (semi == -1 or brace < semi):
            return source[start:source.index('\n}', start)+2]
helpers = function('bool IsWindowRecentForPreviewLocked(')
cpp = r'''
#include <cassert>
#include <map>
using HWND = void*;
using DWORD = unsigned long;
using ULONGLONG = unsigned long long;
std::map<HWND,DWORD> windows;
bool IsWindow(HWND h) { return windows.count(h); }
DWORD PidFromHwnd(HWND h) { return IsWindow(h) ? windows.at(h) : 0; }
bool HwndMatchesStoredPid(HWND h, DWORD p) { return p && PidFromHwnd(h)==p; }
struct Recent { ULONGLONG lastConfirmedTick; DWORD pid; };
struct DesktopRecencyState { std::map<HWND,Recent> windowFocusMap; };
struct Settings { int previewDecayMinutes=15; } settings;
Settings* SettingsSnap() { return &settings; }
ULONGLONG GetTickCount64() { return 1000; }
ULONGLONG DecayMsFromMinutes(int n) { return n*60000ULL; }
bool IsTickDecayed(ULONGLONG t,ULONGLONG d,ULONGLONG now) { return now-t>=d; }
// HELPERS
int main() {
    HWND a=reinterpret_cast<HWND>(1), b=reinterpret_cast<HWND>(2);
    windows[a]=10; windows[b]=10;
    DesktopRecencyState desk;
    desk.windowFocusMap[a]={900,10}; desk.windowFocusMap[b]={950,10};
    ULONGLONG tick=0;
    assert(IsWindowRecentForPreviewLocked(desk,b,&tick) && tick==950);
    windows[b]=11;  // HWND recycled by another process
    assert(!IsWindowRecentForPreviewLocked(desk,b));
    windows.erase(a);
    assert(!IsWindowRecentForPreviewLocked(desk,a));
}
'''.replace('// HELPERS',helpers)
with tempfile.TemporaryDirectory(prefix='windhawk-preview-') as folder:
    cpp_path=Path(folder)/'test.cpp'; exe=Path(folder)/'test.exe'
    cpp_path.write_text(cpp,encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(cpp_path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
assert 'kHostedWindowTimerId' not in source and 'g_hostedWindowLinks' not in source
assert 'AddThumbnailTaskItemMapping' not in source and 'g_TaskGroup_Thumbnails' not in source
assert 'HandleClick' not in source and 'QueryViaVtable' not in source
print('PASS: preview recency PID guard; no ctor map, Thumbnails capture or taskbar.dll click path')
