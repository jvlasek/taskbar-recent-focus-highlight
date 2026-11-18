"""AppId identity (0.11.33): button identity is Explorer's AppId, read
synchronously; app keys are APPID:<AppId>; the exclude list still matches
process names / paths / AUMIDs. Runs the production functions with stubs."""
import sys as _sys
from pathlib import Path as _Path

_sys.path.insert(0, str(_Path(__file__).resolve().parent))
from mod_source import source_text  # src/*.cpp joined
from pathlib import Path
import subprocess
import tempfile

source = source_text()


def function(signature):
    start = 0
    while True:
        start = source.index('\n' + signature, start) + 1
        brace, semi = source.find('{', start), source.find(';', start)
        if brace != -1 and (semi == -1 or brace < semi):
            return source[start:source.index('\n}', start) + 2]


def block(start_marker, end_marker):
    start = source.index(start_marker)
    return source[start:source.index(end_marker, start) + len(end_marker)]


types = '\n'.join([
    block('struct AppFocusInfo {', '\n};\n'),
    block('struct CapturedWindow {', '\n};\n'),
    block('// One synchronous snapshot of a taskbar button', '\n};\n'),
    block('struct ButtonState {', '\n};\n'),
    block('struct ButtonIdentity {', '\n};\n'),
    block('struct WindowAppIdEntry {', '\n};\n'),
    block('struct WindowAppFocus {', '\n};\n'),
])
helpers = '\n'.join([
    function('ButtonState* FindButtonStateLocked('),
    function('std::wstring ProvisionalAppKey('),
    function('bool IsTickDecayed('),
    function('bool IsShellHostFileName('),
    function('int RankIntensity('),
    function('std::wstring AppIdFromAppKey('),
    function('std::wstring StripAppIdPrefix('),
    function('std::wstring CanonicalAppId('),
    function('bool IsExcludedKey('),
    function('bool IdentityMatchesRank('),
    function('bool AppAppearsOnTaskbar('),
    function('DWORD WindowPid('),
    function('void PublishWindowAppIds('),
    function('void ForgetWindowAppId('),
    function('std::wstring LookupWindowAppId('),
    function('std::wstring EnsureButtonIdentity('),
    function('void DeriveAppsFromWindowsLocked('),
])

cpp = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using DWORD = unsigned long;
using ULONGLONG = unsigned long long;
using HWND = void*;
template<class... T> void Wh_Log(const wchar_t*, T...) {}
#include <cstdio>
#define swprintf_s(buf, ...) swprintf(buf, sizeof(buf) / sizeof(*(buf)), __VA_ARGS__)
int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wint_t x = towupper(a[i]), y = towupper(b[i]);
        if (x != y || !x) return x < y ? -1 : (x > y);
    }
    return 0;
}
std::wstring ToUpper(std::wstring s) { for (auto& c : s) c = towupper(c); return s; }
std::wstring FileNameFromPath(const std::wstring& p) {
    auto i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? p : p.substr(i + 1);
}
struct HwndHash { size_t operator()(HWND h) const { return std::hash<void*>{}(h); } };
bool IsWindow(HWND h) { return h != nullptr; }
bool HwndMatchesStoredPid(HWND h, DWORD p) { return h && p == 42; }
DWORD g_recycledPid = 0;  // nonzero: every window now belongs to this PID
void GetWindowThreadProcessId(HWND h, DWORD* pid) { *pid = h ? (g_recycledPid ? g_recycledPid : 42) : 0; }
enum class TaskbarEdge { Bottom };
namespace Media { struct ScaleTransform {}; }
namespace winrt {
template<class T> struct weak_ref { std::shared_ptr<int> p; weak_ref() = default; weak_ref(std::shared_ptr<int> x) : p(x) {} explicit operator bool() const { return (bool)p; } };
namespace Windows::Foundation { struct Point { float X, Y; }; }
}
struct Button { int id; };
using FrameworkElement = std::shared_ptr<int>;
namespace winrt { inline weak_ref<FrameworkElement> make_weak(FrameworkElement b) { return {b}; } }
struct Settings { std::unordered_set<std::wstring> excludedPrograms; };
Settings g_settings;
const Settings* SettingsSnap() { return &g_settings; }
constexpr ULONGLONG kIsRunningGraceMs = 400;
ULONGLONG tick = 1000;
ULONGLONG GetTickCount64() { return tick; }
std::atomic<bool> g_unloading{false}, g_taskbandResolveReady{true};
std::mutex g_buttonStateMutex, g_windowAppIdMutex;
// HELPERS_DECL
struct DesktopRecencyState {
    std::unordered_map<HWND, WindowAppFocus, HwndHash> windowAppFocus;
    std::unordered_map<std::wstring, AppFocusInfo> appFocusMap;
};
#define LOG_DEFERRED(...) ((void)0)
std::unordered_map<HWND, WindowAppIdEntry, HwndHash> g_windowAppId;
std::unordered_map<void*, ButtonState> g_buttonStates;
ButtonCapture g_nextCapture;
void* InspectableIdentity(FrameworkElement b) { return b.get(); }
template<class T> bool WeakIsSameElement(const winrt::weak_ref<T>& w, FrameworkElement b) { return w.p == b; }
ButtonCapture CaptureButton(FrameworkElement) { return g_nextCapture; }
// HELPERS
int main() {
    HWND tcWin = (HWND)1, listerWin = (HWND)2, newWin = (HWND)3;
    const std::wstring tc = L"{6D809377-6AF0-444B-8957-A3773F02200E}\\TOTALCMD\\TOTALCMD64.EXE";
    const std::wstring lister = L"LISTER{CECFE544-EF6E-499D-8F87-56B61FA2EC44}";
    // Window -> AppId map from button captures.
    PublishWindowAppIds({tcWin}, tc);
    PublishWindowAppIds({listerWin}, lister);
    assert(LookupWindowAppId(tcWin) == tc);
    assert(LookupWindowAppId(listerWin) == lister);
    assert(LookupWindowAppId(newWin).empty());
    // A recycled handle (another process now) does not inherit the AppId.
    g_recycledPid = 7;
    assert(LookupWindowAppId(listerWin).empty());
    g_recycledPid = 0;
    PublishWindowAppIds({listerWin}, lister);
    ForgetWindowAppId(listerWin);
    assert(LookupWindowAppId(listerWin).empty());
    PublishWindowAppIds({listerWin}, lister);

    // Synchronous button identity; a change invalidates the paint rank.
    auto b1 = std::make_shared<int>(1);
    g_nextCapture = {};
    g_nextCapture.buttonId = b1.get(); g_nextCapture.hwnd = tcWin; g_nextCapture.pid = 42;
    g_nextCapture.running = true; g_nextCapture.windows = {{tcWin, 42}};
    g_nextCapture.explorerAppId = tc;
    assert(EnsureButtonIdentity(b1) == tc);
    auto& e = g_buttonStates[b1.get()];
    e.lastPaintRank = 1; e.observedRunning = true;
    assert(EnsureButtonIdentity(b1) == tc && e.lastPaintRank == 1);  // unchanged
    g_nextCapture.windows = {{tcWin, 42}, {newWin, 42}};
    EnsureButtonIdentity(b1);
    assert(e.lastPaintRank == -1 && e.groupWindows.size() == 2);    // window set
    e.lastPaintRank = 1;
    g_nextCapture.explorerAppId = lister;                            // reused button
    assert(EnsureButtonIdentity(b1) == lister && e.lastPaintRank == -1);
    g_nextCapture.explorerAppId = tc; EnsureButtonIdentity(b1);

    // Matching: AppId equality; TC and Lister are different apps.
    ButtonIdentity ident; ident.appIdUpper = tc; ident.sampleHwnd = tcWin;
    ident.samplePid = 42; ident.groupWindows = {{tcWin, 42}};
    AppFocusInfo tcRank; tcRank.key = L"APPID:" + tc;
    AppFocusInfo listerRank; listerRank.key = L"APPID:" + lister;
    assert(IdentityMatchesRank(ident, tcRank));
    assert(!IdentityMatchesRank(ident, listerRank));
    ButtonIdentity empty;
    assert(!IdentityMatchesRank(empty, tcRank));
    // Per-window slot (Never Combine): only the button listing that window.
    AppFocusInfo slot = tcRank; slot.perWindow = true; slot.lastHwnd = newWin; slot.lastPid = 42;
    assert(!IdentityMatchesRank(ident, slot));
    ident.groupWindows.push_back({newWin, 42});
    assert(IdentityMatchesRank(ident, slot));

    // Uncombined button + app-level rank: only the app's last window.
    ButtonIdentity w1; w1.appIdUpper = tc; w1.uncombined = true;
    w1.sampleHwnd = tcWin; w1.samplePid = 42; w1.groupWindows = {{tcWin, 42}};
    ButtonIdentity w2 = w1; w2.sampleHwnd = newWin; w2.groupWindows = {{newWin, 42}};
    AppFocusInfo appRank = tcRank; appRank.lastHwnd = tcWin; appRank.lastPid = 42;
    assert(IdentityMatchesRank(w1, appRank));
    assert(!IdentityMatchesRank(w2, appRank));
    // On the taskbar: a running button with that AppId.
    assert(AppAppearsOnTaskbar(L"APPID:" + tc));
    assert(!AppAppearsOnTaskbar(L"APPID:" + lister));
    e.observedRunning = false; e.lastRunningTick = tick;             // closing grace
    assert(AppAppearsOnTaskbar(L"APPID:" + tc));
    tick += kIsRunningGraceMs;
    assert(!AppAppearsOnTaskbar(L"APPID:" + tc));

    // Exclude list: names, paths and AUMIDs still work with AppId keys.
    const std::wstring key = L"APPID:" + tc;
    const std::wstring path = L"C:\\PROGRAM FILES\\TOTALCMD\\TOTALCMD64.EXE";
    assert(!IsExcludedKey(key, L"TOTALCMD64.EXE", L"", path));
    g_settings.excludedPrograms = {L"TOTALCMD64.EXE"};
    assert(IsExcludedKey(key, L"OTHER", L"", path));                 // file name of path
    g_settings.excludedPrograms = {path};
    assert(IsExcludedKey(key, L"OTHER", L"", path));                 // full path
    g_settings.excludedPrograms = {tc};
    assert(IsExcludedKey(key, L"OTHER", L"", L""));                  // Explorer AppId
    g_settings.excludedPrograms = {L"MICROSOFT.WINDOWSCALCULATOR_8WEKYB3D8BBWE!APP"};
    assert(IsExcludedKey(L"APPID:MICROSOFT.WINDOWSCALCULATOR_8WEKYB3D8BBWE!APP",
                         L"CALCULATOR", L"", L"C:\\WINDOWS\\SYSTEM32\\APPLICATIONFRAMEHOST.EXE"));
    // Pending-focus key of a window without a button: never an AppId.
    const std::wstring prov = ProvisionalAppKey(newWin);
    assert(AppIdFromAppKey(prov).empty() && !AppAppearsOnTaskbar(prov));
    // Recency is per window; the app is derived from Explorer's AppId at
    // rank time (Fable 4.3). No AppId yet: recorded, not ranked.
    {
        DesktopRecencyState desk;
        HWND w5 = (HWND)5, w6 = (HWND)6;
        const std::wstring calc = L"MICROSOFT.WINDOWSCALCULATOR_8WEKYB3D8BBWE!APP";
        desk.windowAppFocus[w5] = {L"", L"Calculator", L"Calc", L"", L"", 42, 100};
        DeriveAppsFromWindowsLocked(desk, 0, 200);
        assert(desk.appFocusMap.empty() && desk.windowAppFocus[w5].key.empty());
        PublishWindowAppIds({w5, w6}, calc);
        DeriveAppsFromWindowsLocked(desk, 0, 200);
        assert(desk.windowAppFocus[w5].key == L"APPID:" + calc);
        auto& app = desk.appFocusMap[L"APPID:" + calc];
        assert(app.lastHwnd == w5 && app.lastConfirmedFocusTick == 100);
        // An older window of the same app does not overwrite a newer one.
        desk.windowAppFocus[w6] = {L"", L"Calculator", L"Calc 2", L"", L"", 42, 50};
        DeriveAppsFromWindowsLocked(desk, 0, 200);
        assert(app.lastHwnd == w5);
        // Decayed windows go; the app record keeps its history until its own
        // decay in the recompute.
        DeriveAppsFromWindowsLocked(desk, 1000, 2000);
        assert(desk.windowAppFocus.empty() && desk.appFocusMap.size() == 1);
    }
    // Shell UI is transient, not an app (jump lists, Start menu).
    assert(IsShellHostFileName(L"SHELLEXPERIENCEHOST.EXE"));
    assert(IsShellHostFileName(L"STARTMENUEXPERIENCEHOST.EXE"));
    assert(!IsShellHostFileName(L"TOTALCMD64.EXE"));
    // Ranks 4+ fade from rank 3 instead of repeating it.
    const int per[3] = {100, 70, 45};
    assert(RankIntensity(per, 0) == 100 && RankIntensity(per, 2) == 45);
    assert(RankIntensity(per, 3) == 34 && RankIntensity(per, 4) == 25);
    assert(RankIntensity(per, 15) == 10);
    const int low[3] = {100, 70, 5};
    assert(RankIntensity(low, 9) == 5);
}
'''
cpp = cpp.replace('// HELPERS_DECL', types).replace('// HELPERS', helpers)
with tempfile.TemporaryDirectory(prefix='appid-identity-') as d:
    c = Path(d) / 't.cpp'
    exe = Path(d) / 't'
    c.write_text(cpp, encoding='utf-8')
    subprocess.run(['g++', '-std=c++20', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
print('PASS: AppId identity, matching, taskbar presence and exclusion')
