"""Exercise production thumbnail normalization and recency PID checks."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
def function(signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]
hosted = source[source.index('struct HostedWindowLink {'):source.index('// Diagnostic snapshots only:')]
helpers = function('HWND NormalizeFocusHwnd(') + '\n' + hosted + '\n' + function('HWND HwndFromMappingEntry(') + '\n' + function('bool IsWindowRecentForPreviewLocked(')
cpp = r'''
#include <cassert>
#include <map>
#include <algorithm>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <string>
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
struct ThumbnailTaskItemMapping { HWND hwnd; DWORD pid; void* taskGroup=nullptr; void* taskItem=nullptr; };
template<class... T> void Wh_Log(const wchar_t*, T...) {}
void LogPreviewWindowIdentity(const wchar_t*, HWND) {}
struct Recent { ULONGLONG lastConfirmedTick; DWORD pid; };
struct DesktopRecencyState { std::map<HWND, Recent> windowFocusMap; };
struct Settings { int previewDecayMinutes = 15; } settings;
Settings* SettingsSnap() { return &settings; }
ULONGLONG clockTick = 1000;
ULONGLONG GetTickCount64() { return clockTick; }
ULONGLONG DecayMsFromMinutes(int n) { return n * 60000ULL; }
bool IsTickDecayed(ULONGLONG t, ULONGLONG d, ULONGLONG now) { return now-t >= d; }
constexpr int kHostedWindowTimerId = 5;
bool timerArmed = false;
int previewRequests = 0;
void ArmHookTimer(int, ULONGLONG) { timerArmed = true; }
void DisarmHookTimer(int) { timerArmed = false; }
void RequestApplyPreviewVisuals() { ++previewRequests; }
long GetWindowLongW(HWND h, int n) { return GetWindowLong(h,n); }
std::wstring GetWindowClassName(HWND h) {
    if (!IsWindow(h)) return {};
    return windows.at(h).pid == 10 || windows.at(h).pid == 11
        ? L"ApplicationFrameWindow" : L"Windows.UI.Core.CoreWindow";
}
HWND FindWindowExW(HWND frame, HWND after, const wchar_t*, const wchar_t*) {
    for (const auto& [h,w] : windows) {
        if (h > after && (w.style & WS_CHILD) && w.root == frame) return h;
    }
    return nullptr;
}
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
    windows[a].pid = 10;
    // Capture both links while attached; minimize by detaching the content.
    assert(RememberHostedWindow(childA,a));
    assert(RememberHostedWindow(childB,b));
    windows[childA].style = 0; windows[childA].root = childA;
    windows[childB].style = 0; windows[childB].root = childB;
    assert(HwndFromMappingEntry({childA,20}) == a);
    assert(HwndFromMappingEntry({childB,30}) == b);
    windows[a].pid = 11;
    assert(HwndFromMappingEntry({childA,20}) == childA);
    windows[a].pid = 10;
    // A detached child with no observation is never guessed by class/process.
    assert(HwndFromMappingEntry({childA,20}) == childA);
    // Delayed attachment captured by bounded focus-thread polling.
    ObserveHostedFrame(a);
    assert(timerArmed && g_pendingHostedFrames.size() == 1);
    windows[childA].style = WS_CHILD; windows[childA].root = a;
    PollHostedFrames();
    assert(!timerArmed && g_pendingHostedFrames.empty() && previewRequests == 1);
    windows[childA].style = 0; windows[childA].root = childA;
    assert(HwndFromMappingEntry({childA,20}) == a);
    // New direct parent supersedes the old association.
    windows[childA].style = WS_CHILD; windows[childA].root = b;
    assert(HwndFromMappingEntry({childA,20}) == b);
    windows[childA].style = 0; windows[childA].root = childA;
    assert(HwndFromMappingEntry({childA,20}) == b);
    ObserveHostedFrame(a);
    clockTick += kHostedCaptureDeadlineMs;
    PollHostedFrames();
    assert(g_pendingHostedFrames.empty() && !timerArmed);
    // Recycled child cannot reuse the remembered frame.
    windows[childA].pid = 21;
    assert(ResolveHostedPreviewWindow(childA) == childA);
    windows.erase(childA);
    assert(!HwndFromMappingEntry({childA,20}));
    assert(!HwndFromMappingEntry({nullptr,0}));
}
'''.replace('// HELPERS', helpers)
with tempfile.TemporaryDirectory(prefix='windhawk-preview-') as folder:
    cpp_path = Path(folder) / 'test.cpp'
    exe = Path(folder) / 'test.exe'
    cpp_path.write_text(cpp, encoding='utf-8')
    subprocess.run(['g++', '-std=c++20', str(cpp_path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('PASS: hosted child/frame identity, distinct top-level windows, child and root PID guards')
