"""Exercise production notification filtering and duplicate-preview deadlines."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]

helpers = '\n'.join(function(s) for s in (
    'void HandleForegroundNotification(', 'void HandleShellActivation(',
    'void EnsurePendingPreviewTimer() {'))
start = source.index('    if (sameAppPending) {', source.index('void HandleForegroundChanged('))
end = source.index('\n    CancelMinFocusTimer();', start)
duplicate = source[start:end]
fixture = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <mutex>
using HWND = int;
using WPARAM = unsigned long long;
using ULONGLONG = unsigned long long;
constexpr WPARAM HSHELL_WINDOWACTIVATED=4, HSHELL_RUDEAPPACTIVATED=32772;
constexpr int kPreviewMinFocusTimerId=3;
std::atomic<bool> g_unloading=false;
HWND foreground=1;
int handled=0, scheduled=0, appEnsures=0, confirmed=0, canceled=0;
ULONGLONG now=100, armed=0;
struct PendingFocus {
    bool valid=true, previewConfirmed=false, appConfirmed=false;
    HWND hwnd=1;
    ULONGLONG previewStartTick=100, focusStartTick=100;
} g_pendingFocus;
struct Settings {bool previewHighlightEnabled=true; int previewMinFocusSeconds=1;} settings;
std::mutex g_stateMutex;
enum class MinFocusConfirmMode {Immediate, FromTimer};
Settings* SettingsSnap() {return &settings;}
ULONGLONG GetTickCount64() {return now;}
ULONGLONG RemainingDeadlineMs(ULONGLONG start,int seconds,ULONGLONG tick) {
    auto end=start+seconds*1000; return end>tick?end-tick:0;
}
void CancelPreviewMinFocusTimer() {++canceled; armed=0;}
void OnPreviewMinFocusTimerElapsed(MinFocusConfirmMode mode) {
    assert(mode==MinFocusConfirmMode::FromTimer || settings.previewMinFocusSeconds==0);
    ++confirmed;
}
void ArmHookTimer(int id,ULONGLONG duration) {assert(id==3); armed=now+duration;}
HWND GetForegroundWindow() {return foreground;}
HWND NormalizeFocusHwnd(HWND hwnd) {return hwnd==11?1:hwnd;}
void HandleForegroundChanged(HWND hwnd) {assert(hwnd==NormalizeFocusHwnd(foreground)); ++handled;}
void SchedulePreviewConfirm(bool) {++scheduled;}
void EnsurePendingAppTimer() {++appEnsures;}
using UINT = unsigned;
void Wh_Log(const wchar_t*, ...) {}
// HELPERS
void Duplicate(bool hwndChanged,bool windowAlreadyTracked) {
    bool sameAppPending=true;
    // DUPLICATE
}
int main() {
    HandleForegroundNotification(2); assert(handled==0); // background or stale event
    HandleShellActivation(1,1); HandleShellActivation(32774,1); // create / flash
    HandleShellActivation(4,0); HandleShellActivation(4,2); assert(handled==0);
    HandleShellActivation(4,1); assert(handled==1); // missing WinEvent case
    HandleShellActivation(32772,1); assert(handled==2);
    HandleForegroundNotification(11); assert(handled==3); // normalized child
    foreground=0; HandleShellActivation(4,1); assert(handled==3);
    foreground=1; g_unloading=true;
    HandleShellActivation(4,1); HandleForegroundNotification(1); assert(handled==3);
    g_unloading=false;
    now=300; Duplicate(false,false); assert(armed==1100 && scheduled==0 && appEnsures==1);
    now=700; Duplicate(false,false); assert(armed==1100 && scheduled==0 && appEnsures==2);
    now=1100; Duplicate(false,false); assert(confirmed==1); // deadline not delayed
    g_pendingFocus.previewConfirmed=true; armed=0;
    Duplicate(false,true); assert(armed==0 && confirmed==1);
    Duplicate(true,false); assert(scheduled==1); // distinct sibling starts its own wait
    g_pendingFocus.previewConfirmed=false; settings.previewHighlightEnabled=false;
    Duplicate(false,false); assert(g_pendingFocus.previewConfirmed && canceled==1);
}
'''.replace('// HELPERS', helpers).replace('// DUPLICATE', duplicate)

assert 'TEMP_FOCUS_TRACE' not in source
assert 'TraceExtendedClick' not in source
assert 'ForegroundRecheck' not in source
assert 'g_foregroundRecheckDeadline' not in source
worker = function('DWORD WINAPI WinEventHookThread(')
assert 'WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE' in worker and 'HWND_MESSAGE' not in worker
assert worker.index('RegisterShellHookWindow(hwnd)') < worker.index('g_hookThreadHwnd.store(hwnd')
assert worker.count('DeregisterShellHookWindow(hwnd)') == 2  # WinEvent failure + normal exit
assert 'HandleForegroundNotification(reinterpret_cast<HWND>(wParam))' in function('LRESULT CALLBACK HookThreadWndProc(')

with tempfile.TemporaryDirectory(prefix='windhawk-shell-') as folder:
    cpp = Path(folder) / 'test.cpp'
    exe = Path(folder) / 'test.exe'
    cpp.write_text(fixture, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('PASS: activation filtering, stale/background/null targets, unload, normalization, duplicate deadlines, sibling transitions, registration structure, no recovery timer')
