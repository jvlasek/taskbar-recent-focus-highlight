"""Last use (0.12.1, fixes 0.12.2): LastInputTick64, the stretch in front,
transient foregrounds, a destroyed window in front, same-app switches."""
import sys as _sys
from pathlib import Path as _Path

_sys.path.insert(0, str(_Path(__file__).resolve().parent))
from mod_source import source_text  # src/*.cpp joined
from pathlib import Path
import subprocess
import tempfile

source = source_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


# Structure: transient windows do not start a stretch (Neon review 1.1.1).
fg = function('void HandleForegroundChanged(HWND hWnd) {')
assert 'const bool transient = IsTransientForeground(hWnd);' in fg
assert fg.index('const bool transient') < fg.index('NoteForegroundForLastUse(hWnd)')
assert 'if (!transient) {\n        NoteForegroundForLastUse(hWnd);' in fg
assert fg.count('IsTransientForeground(') == 1
# Same-app window switch after the app confirm re-confirms (1.1.3).
same = fg[fg.index('if (hwndChanged) {'):]
same = same[:same.index('}')]
assert 'g_pendingFocus.appConfirmed = false;' in same
# Destroyed window in front (1.1.2 / 1.1.4).
destroyed = function('void HandleWindowDestroyed(HWND hwnd) {')
assert 'g_inFrontHwnd = nullptr;' in destroyed
assert 'tick > app->second.lastConfirmedFocusTick' in destroyed
assert destroyed.index('lastConfirmedFocusTick = tick') < destroyed.index('desk.windowAppFocus.erase(w)')

helpers = '\n'.join(function(s) for s in (
    'ULONGLONG LastInputTick64(ULONGLONG now) {',
    'unsigned RefreshLastUseLocked(HWND hwnd, ULONGLONG since, ULONGLONG at) {',
    'void NoteForegroundForLastUse(HWND hwnd) {',
    'void RefreshInFrontLastUse() {',
))

fixture = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
using HWND = int;
using DWORD = uint32_t;
using ULONGLONG = unsigned long long;
using BOOL = int;
using PCWSTR = const wchar_t*;
struct LASTINPUTINFO { unsigned cbSize; DWORD dwTime; };
ULONGLONG tick = 0;
ULONGLONG inputTick = 0;   // GetTickCount64 time of the last input
bool inputKnown = true;
HWND foreground = 0;
ULONGLONG GetTickCount64() { return tick; }
BOOL GetLastInputInfo(LASTINPUTINFO* lii) {
    lii->dwTime = static_cast<DWORD>(inputTick);
    return inputKnown;
}
HWND GetForegroundWindow() { return foreground; }
HWND NormalizeFocusHwnd(HWND h) { return h; }
bool HwndMatchesStoredPid(HWND, DWORD pid) { return pid != 0; }
struct DeferredLogScope {};
#define LOG_DEFERRED(...) ((void)0)
#define L
std::mutex g_stateMutex;
struct WindowAppFocus { ULONGLONG tick = 0; DWORD pid = 7; };
struct WindowFocusInfo { ULONGLONG lastConfirmedTick = 0; DWORD pid = 7; };
struct Desk {
    std::map<HWND, WindowAppFocus> windowAppFocus;
    std::map<HWND, WindowFocusInfo> windowFocusMap;
};
std::map<int, Desk> g_desktopMaps;
HWND g_inFrontHwnd = 0;
ULONGLONG g_inFrontSince = 0;
constexpr unsigned kLastUseMovedApp = 1, kLastUseMovedPreview = 2;
''' + helpers.replace('L"', '"').replace('%s', '%ls') + r'''
constexpr ULONGLONG MIN = 60000;
int main() {
    // LastInputTick64: plain, wrap, race (input after `now`), unknown.
    tick = 100 * MIN; inputTick = tick - 5000;
    assert(LastInputTick64(tick) == tick - 5000);
    tick = (1ULL << 32) + 10 * MIN; inputTick = tick - 3 * MIN;  // > 49.7 d
    assert(LastInputTick64(tick) == tick - 3 * MIN);
    inputTick = tick + 15;  // keystroke between GetTickCount64 and the read
    assert(LastInputTick64(tick) == tick);
    tick = 10 * MIN; inputTick = tick + 15;
    assert(LastInputTick64(tick) == tick);
    inputKnown = false;
    assert(LastInputTick64(tick) == 0);
    inputKnown = true;

    // App A (hwnd 1) confirmed at t=10 min, used without switching.
    Desk& d = g_desktopMaps[0];
    tick = 10 * MIN; foreground = 1;
    NoteForegroundForLastUse(1);
    d.windowAppFocus[1].tick = tick;
    // Alt+Tab at 20 min: HandleForegroundChanged does not call
    // NoteForegroundForLastUse for the switcher (structure above). Back on A
    // without a new foreground event; typing until 50 min.
    tick = 20 * MIN; foreground = 99;
    RefreshInFrontLastUse();  // switcher in front: nothing moves
    assert(d.windowAppFocus[1].tick == 10 * MIN);
    tick = 50 * MIN; inputTick = tick - 1000; foreground = 1;
    RefreshInFrontLastUse();
    assert(d.windowAppFocus[1].tick == tick - 1000);

    // Switch to B (hwnd 2): A is stamped with the last input.
    tick = 51 * MIN; inputTick = tick - 200; foreground = 2;
    NoteForegroundForLastUse(2);
    assert(d.windowAppFocus[1].tick == tick - 200);
    assert(g_inFrontHwnd == 2 && g_inFrontSince == tick);
    // B not confirmed in this stretch: nothing moves for B.
    d.windowAppFocus[2].tick = 5 * MIN;
    tick = 52 * MIN; inputTick = tick;
    RefreshInFrontLastUse();
    assert(d.windowAppFocus[2].tick == 5 * MIN);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='windhawk-lastuse-') as folder:
    cpp = Path(folder) / 'test.cpp'
    exe = Path(folder) / 'test.exe'
    cpp.write_text(fixture, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-w', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('PASS: last input (wrap, race), transient stretch, leave stamp, '
      'destroyed window in front, same-app re-confirm')
