"""Run the production async resolver against controlled button/worker fixtures."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
s = (root/'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
data = s[s.index('struct ButtonResolveData {'):s.index('struct ButtonPathCacheEntry {')]
helpers = s[s.index('bool SameButtonResolveTarget('):s.index('ButtonIdentity GetCachedButtonIdentity(', s.index('bool SameButtonResolveTarget('))]
running_helper = s[s.index('bool ButtonCountsAsRunning('):s.index('bool ButtonCountsAsRunning(')+s[s.index('bool ButtonCountsAsRunning('):].index('\n}')]+'\n}'
fixture = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
using DWORD = unsigned long;
using ULONGLONG = unsigned long long;
using HWND = void*;
struct CapturedWindow { HWND hwnd; DWORD pid; };
// DATA
struct Button { ButtonResolveData data; };
using FrameworkElement = std::shared_ptr<Button>;
namespace winrt { auto make_weak(FrameworkElement b) { return b; } }
struct Entry {
    FrameworkElement button;
    std::wstring pathUpper, appIdUpper, classUpper;
    HWND sampleHwnd = nullptr;
    DWORD samplePid = 0;
    std::vector<CapturedWindow> groupWindows;
    bool resolveAttempted = false, resolvedWhileRunning = false, observedRunning = false;
    ULONGLONG lastRunningTick = 0;
    int emptyResolveAttempts = 0, lastPaintRank = -1;
    ULONGLONG lastResolveTick = 0, resolveSerial = 0, resolveQueuedTick = 0;
    std::optional<ButtonResolveData> resolveResult;
};
std::mutex g_buttonPathMutex;
std::unordered_map<void*, Entry> g_buttonPathCache;
std::atomic<bool> g_unloading{false}, g_taskbandResolveReady{true};
constexpr int WM_APP_RESOLVE_BUTTON = 1, kMaxEmptyResolveAttempts = 8;
constexpr ULONGLONG kUnresolvedRetryMs = 2000, kIsRunningGraceMs = 400;
int rechecks = 0;
void ScheduleRefreshAllHighlights(FrameworkElement) {
    assert(g_buttonPathMutex.try_lock());
    g_buttonPathMutex.unlock(); ++rechecks;
}
ULONGLONG tick = 10000;
int queries = 0, paints = 0, posts = 0;
bool postSucceeds = true;
std::wstring image = L"A.EXE";
std::function<void()> duringQuery;
ULONGLONG GetTickCount64() { return tick; }
bool PostToHookThread(int) { ++posts; return postSucceeds; }
bool HwndMatchesStoredPid(HWND h, DWORD p) { return h && p == 42; }
std::wstring ToUpper(std::wstring s) { return s; }
std::wstring GetProcessImagePath(DWORD) {
    ++queries; if (duringQuery) duringQuery(); return image;
}
std::wstring GetWindowClassName(HWND) { return L"CLASS"; }
std::wstring GetWindowAppUserModelId(HWND) { return L"APPID"; }
void ApplyAllHighlights_UIThread(bool refresh) { assert(!refresh); ++paints; }
template<class F> bool RunOnUiThread(F f) { f(); return true; }
void* InspectableIdentity(FrameworkElement b) { return b->data.buttonId; }
bool TaskListButton_IsRunning(FrameworkElement b) { return b->data.running; }
bool WeakIsSameElement(FrameworkElement a, FrameworkElement b) { return a == b; }
ButtonResolveData CaptureButtonResolveData(FrameworkElement b) { return b->data; }
// RUNNING
// HELPERS
FrameworkElement button(int id) {
    auto b = std::make_shared<Button>();
    b->data.buttonId = reinterpret_cast<void*>(static_cast<size_t>(id));
    b->data.hwnd = reinterpret_cast<void*>(10);
    b->data.pid = 42; b->data.running = true;
    b->data.windows = {{b->data.hwnd, 42}};
    return b;
}
void worker() { std::thread t(ResolveOneButtonOnFocusThread); t.join(); }
int main() {
    // Background close: no foreground event or identity worker is required.
    auto closing = button(88);
    auto& e = g_buttonPathCache[closing->data.buttonId];
    e.button = closing; e.sampleHwnd = closing->data.hwnd; e.samplePid = 99;
    e.lastRunningTick = tick; e.observedRunning = true;
    closing->data.running = false;
    assert(!ButtonCountsAsRunning(closing));
    assert(!e.observedRunning && e.lastRunningTick == 0 && rechecks == 0);
    // A live sibling permits brief grace, but always arranges an expiry check.
    e.groupWindows = {{closing->data.hwnd, 42}};
    e.lastRunningTick = tick;
    assert(ButtonCountsAsRunning(closing) && rechecks == 1);
    tick += kIsRunningGraceMs;
    assert(!ButtonCountsAsRunning(closing) && rechecks == 1);
    // Relaunch resets episode bookkeeping.
    closing->data.running = true;
    e.resolvedWhileRunning = true; e.emptyResolveAttempts = 8;
    assert(ButtonCountsAsRunning(closing) && e.observedRunning);
    assert(!e.resolvedWhileRunning && e.emptyResolveAttempts == 0);
    g_buttonPathCache.clear();

    auto b = button(1);
    assert(EnsureButtonPathCached(b, true).empty());
    assert(queries == 0 && g_buttonResolveQueue.size() == 1);
    EnsureButtonPathCached(b, true); EnsureButtonPathCached(b, true);
    assert(g_buttonResolveQueue.size() == 1 && posts == 1);
    worker();
    assert(queries == 1 && paints == 1);
    assert(EnsureButtonPathCached(b, false, false) == L"A.EXE");
    assert(g_buttonResolveQueue.empty());
    tick += 3000;
    EnsureButtonPathCached(b, false, false);
    assert(g_buttonResolveQueue.empty()); // result-driven paints never poll
    image = L"B.EXE";
    EnsureButtonPathCached(b, true); worker();
    assert(EnsureButtonPathCached(b, false, false) == L"B.EXE");
    // A newer click during an old query supersedes that result.
    EnsureButtonPathCached(b, true);
    duringQuery = [&] { EnsureButtonPathCached(b, true); };
    worker(); duringQuery = {};
    assert(!g_buttonPathCache[b->data.buttonId].resolveResult);
    assert(g_buttonResolveQueue.size() == 1);
    worker(); EnsureButtonPathCached(b, false, false);
    // Deadline: slow result cannot replace a known path.
    image = L"LATE.EXE";
    EnsureButtonPathCached(b, true);
    duringQuery = [] { tick += kButtonResolveDeadlineMs+1; };
    worker(); duringQuery = {};
    assert(EnsureButtonPathCached(b, false, false) == L"B.EXE");
    // Changed group after capture: reject and resolve the new target.
    EnsureButtonPathCached(b, true); worker();
    b->data.hwnd = reinterpret_cast<void*>(11);
    b->data.windows = {{b->data.hwnd, 42}};
    assert(EnsureButtonPathCached(b, false, false).empty());
    assert(g_buttonResolveQueue.size() == 1);
    worker(); EnsureButtonPathCached(b, false, false);
    // Same address key but different button: do not consume the old result.
    EnsureButtonPathCached(b, true); worker();
    auto recycled = button(1);
    assert(EnsureButtonPathCached(recycled, false, false).empty());
    assert(g_buttonPathCache.find(b->data.buttonId) == g_buttonPathCache.end());
    // Wrong PID in a group invalidates a result.
    auto a = button(2)->data, c = a;
    c.windows[0].pid = 99;
    assert(!SameButtonResolveTarget(a,c) && !ButtonResolveWindowsLive(c));
    g_buttonResolveQueue.clear(); g_buttonResolvePosted = false;
    for (size_t i=0; i<kMaxButtonResolveQueue; ++i) {
        auto d = button(static_cast<int>(i+100))->data;
        assert(QueueButtonResolve(d));
    }
    assert(!QueueButtonResolve(button(999)->data));
    g_unloading = true; ResolveOneButtonOnFocusThread();
    assert(g_buttonResolveQueue.empty() && !g_buttonResolvePosted);
    assert(!QueueButtonResolve(a));
    g_unloading = false; postSucceeds = false;
    assert(!QueueButtonResolve(a) && g_buttonResolveQueue.empty());
}
'''.replace('// DATA', data).replace('// HELPERS', helpers).replace('// RUNNING', running_helper)
with tempfile.TemporaryDirectory(prefix='windhawk-async-') as folder:
    cpp=Path(folder)/'test.cpp'; exe=Path(folder)/'test.exe'
    cpp.write_text(fixture, encoding='utf-8')
    subprocess.run(['g++','-std=c++17','-pthread',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
assert 'GetWindowTextLengthW(' not in s
ui = s[s.index('ButtonResolveData CaptureButtonResolveData('):s.index('bool SameButtonResolveTarget(')]
ui += s[s.index('std::wstring EnsureButtonPathCached('):s.index('ButtonIdentity GetCachedButtonIdentity(', s.index('std::wstring EnsureButtonPathCached('))]
assert 'GetProcessImagePath(' not in ui and 'GetWindowAppUserModelId(' not in ui
print('PASS: async coalescing, supersession, deadlines, target/PID validation, queue bounds and shutdown')
