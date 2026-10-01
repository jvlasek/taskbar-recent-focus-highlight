"""Exercise production thumbnail normalization and recency PID checks."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
def function(signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]
helpers = '\n'.join(function(x) for x in (
    'HWND NormalizeFocusHwnd(', 'HWND HwndFromMappingEntry(',
    'bool IsWindowRecentForPreviewLocked('))
cpp = r'''
#include <cassert>
#include <map>
using HWND = void*;
using DWORD = unsigned long;
using ULONGLONG = unsigned long long;
constexpr int GWL_STYLE = -16, GA_ROOT = 2;
constexpr unsigned long WS_CHILD = 0x40000000;
struct Window { DWORD pid; unsigned long style; HWND root; };
std::map<HWND, Window> windows;
bool IsWindow(HWND h) { return windows.count(h); }
DWORD PidFromHwnd(HWND h) { return IsWindow(h) ? windows.at(h).pid : 0; }
long GetWindowLong(HWND h, int) { return windows.at(h).style; }
HWND GetAncestor(HWND h, int flag) { assert(flag == GA_ROOT); return windows.at(h).root; }
bool HwndMatchesStoredPid(HWND h, DWORD p) { return p && PidFromHwnd(h) == p; }
struct ThumbnailTaskItemMapping { HWND hwnd; DWORD pid; };
struct Recent { ULONGLONG lastConfirmedTick; DWORD pid; };
struct DesktopRecencyState { std::map<HWND, Recent> windowFocusMap; };
struct Settings { int previewDecayMinutes = 15; } settings;
Settings* SettingsSnap() { return &settings; }
ULONGLONG GetTickCount64() { return 1000; }
ULONGLONG DecayMsFromMinutes(int n) { return n * 60000ULL; }
bool IsTickDecayed(ULONGLONG t, ULONGLONG d, ULONGLONG now) { return now-t >= d; }
// HELPERS
int main() {
    HWND a = reinterpret_cast<HWND>(1), b = reinterpret_cast<HWND>(2);
    HWND childA = reinterpret_cast<HWND>(3), childB = reinterpret_cast<HWND>(4);
    HWND owned = reinterpret_cast<HWND>(5);
    windows[a] = {10, 0, a}; windows[b] = {10, 0, b};
    windows[childA] = {20, WS_CHILD, a}; windows[childB] = {30, WS_CHILD, b};
    windows[owned] = {10, 0, owned};
    DesktopRecencyState desk;
    desk.windowFocusMap[a] = {900,10}; desk.windowFocusMap[b] = {950,10};
    auto first = HwndFromMappingEntry({childA,20});
    auto second = HwndFromMappingEntry({childB,30});
    assert(first == a && second == b && first != second);
    ULONGLONG tick = 0;
    assert(IsWindowRecentForPreviewLocked(desk,first,&tick) && tick == 900);
    assert(IsWindowRecentForPreviewLocked(desk,second,&tick) && tick == 950);
    // Top-level GIMP-like windows and owned dialogs do not merge.
    assert(HwndFromMappingEntry({a,10}) == a);
    assert(HwndFromMappingEntry({b,10}) == b);
    assert(HwndFromMappingEntry({owned,10}) == owned);
    // Recycled child is rejected before walking to an otherwise recent root.
    windows[childA].pid = 21;
    assert(!HwndFromMappingEntry({childA,20}));
    windows[childA].pid = 20;
    // Root's PID must match recency independently of the valid child PID.
    windows[a].pid = 11;
    assert(!IsWindowRecentForPreviewLocked(desk,HwndFromMappingEntry({childA,20})));
    windows.erase(childA);
    assert(!HwndFromMappingEntry({childA,20}));
    assert(!HwndFromMappingEntry({nullptr,0}));
}
'''.replace('// HELPERS', helpers)
with tempfile.TemporaryDirectory(prefix='windhawk-preview-') as folder:
    cpp_path = Path(folder) / 'test.cpp'
    exe = Path(folder) / 'test.exe'
    cpp_path.write_text(cpp, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', str(cpp_path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('PASS: hosted child/frame identity, distinct top-level windows, child and root PID guards')
