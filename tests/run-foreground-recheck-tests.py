"""Run production foreground recovery helpers against a controlled event clock."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start)+2]
helpers = '\n'.join(function(s) for s in (
    'void StopForegroundRecheck()', 'void ObserveForegroundForRecheck(',
    'void OnForegroundRecheckTimer()'))
cpp = r'''
#include <atomic>
#include <cassert>
#include <string>
using HWND = int;
using DWORD = unsigned long;
using ULONGLONG = unsigned long long;
constexpr int kForegroundRecheckTimerId=5, kForegroundRecheckIntervalMs=100;
constexpr ULONGLONG kForegroundRecheckWindowMs=2000;
ULONGLONG g_foregroundRecheckDeadline=0, now=100;
std::atomic<bool> g_unloading=false;
HWND foreground=1, owner=99;
bool timer=false, failTimer=false;
int starts=0, recovered=0;
std::wstring shellClass=L"TaskListThumbnailWnd";
DWORD PidFromHwnd(HWND h) { return h==1 ? 42 : 43; }
bool IsOwnExplorerProcess(DWORD p) { return p==42; }
std::wstring GetWindowClassName(HWND) { return shellClass; }
bool IsTransientForeground(HWND h) { return h==1 || h==3; }
HWND GetForegroundWindow() { return foreground; }
HWND NormalizeFocusHwnd(HWND h) { return h; }
HWND HookThreadWindow() { return owner; }
ULONGLONG GetTickCount64() { return now; }
int SetTimer(HWND, int id, int ms, void*) {
    assert(id==5 && ms==100); ++starts;
    timer=!failTimer; return timer;
}
void DisarmHookTimer(int id) { assert(id==5); timer=false; }
void Wh_Log(const wchar_t*, ...) {}
void HandleForegroundChanged(HWND h) {
    assert(!timer && !g_foregroundRecheckDeadline && h==foreground);
    ++recovered;
}
// HELPERS
int main() {
    for (auto cls : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                    L"XamlExplorerHostIslandWindow", L"TaskListThumbnailWnd"}) {
        shellClass=cls; ObserveForegroundForRecheck(1);
        assert(timer && g_foregroundRecheckDeadline==2100);
        StopForegroundRecheck();
    }
    ObserveForegroundForRecheck(2); // same class in a different process
    assert(!timer);
    shellClass=L"CabinetWClass"; ObserveForegroundForRecheck(1);
    assert(!timer);
    shellClass=L"TaskListThumbnailWnd";
    ObserveForegroundForRecheck(1); now=500;
    ObserveForegroundForRecheck(1); assert(g_foregroundRecheckDeadline==2100);
    foreground=0; OnForegroundRecheckTimer(); assert(timer && !recovered);
    foreground=3; OnForegroundRecheckTimer(); assert(timer && !recovered);
    foreground=2; OnForegroundRecheckTimer(); assert(recovered==1 && !timer);
    OnForegroundRecheckTimer(); assert(recovered==1); // stale queued message
    ObserveForegroundForRecheck(1);
    ObserveForegroundForRecheck(4); assert(timer); // stale app event
    ObserveForegroundForRecheck(2); assert(!timer); // normal actual app event
    OnForegroundRecheckTimer(); assert(recovered==1);
    ObserveForegroundForRecheck(1); now+=2000;
    OnForegroundRecheckTimer(); assert(!timer && recovered==1); // expired
    ObserveForegroundForRecheck(1); g_unloading=true;
    OnForegroundRecheckTimer(); assert(!timer && recovered==1);
    ObserveForegroundForRecheck(1); assert(!timer);
    g_unloading=false; failTimer=true;
    ObserveForegroundForRecheck(1); assert(!g_foregroundRecheckDeadline);
    failTimer=false; owner=0;
    ObserveForegroundForRecheck(1); assert(!g_foregroundRecheckDeadline);
    owner=99; ObserveForegroundForRecheck(1); assert(timer);
    OnForegroundRecheckTimer(); assert(recovered==2); // retry after failure
}
'''.replace('// HELPERS', helpers)
with tempfile.TemporaryDirectory(prefix='windhawk-recheck-') as folder:
    cpp_path=Path(folder)/'test.cpp'; exe=Path(folder)/'test.exe'
    cpp_path.write_text(cpp, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', str(cpp_path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('PASS: bounded foreground recovery, normal event cancellation, timeout, unload, stale messages and timer failure')
