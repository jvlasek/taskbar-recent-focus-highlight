"""Exercise production getter selection and captured HWND/PID validation."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start)+2]
helpers = '\n'.join(function(x) for x in (
    'HWND GetWindowForThumbnailTaskItem(', 'HWND HwndFromMappingEntry(',
    'bool IsWindowRecentForPreviewLocked('))
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
struct ThumbnailTaskItemMapping { HWND hwnd; DWORD pid; };
struct Recent { ULONGLONG lastConfirmedTick; DWORD pid; };
struct DesktopRecencyState { std::map<HWND,Recent> windowFocusMap; };
struct Settings { int previewDecayMinutes=15; } settings;
Settings* SettingsSnap() { return &settings; }
ULONGLONG GetTickCount64() { return 1000; }
ULONGLONG DecayMsFromMinutes(int n) { return n*60000ULL; }
bool IsTickDecayed(ULONGLONG t,ULONGLONG d,ULONGLONG now) { return now-t>=d; }
void* CImmersiveTaskItem_vftable_ITaskItem=reinterpret_cast<void*>(100);
HWND (*CImmersiveTaskItem_GetThumbnailWindow)(void*)=nullptr;
struct Item { void* vtable; HWND frame; HWND app; };
void* expected=nullptr;
int appQueries=0, thumbnailQueries=0;
HWND Thumbnail(void* p) {
    assert(p==expected); ++thumbnailQueries;
    return static_cast<Item*>(p)->frame;
}
HWND GetWindowFromTaskItem(void* p) { ++appQueries; return static_cast<Item*>(p)->app; }
// HELPERS
int main() {
    HWND a=reinterpret_cast<HWND>(1), b=reinterpret_cast<HWND>(2);
    HWND content=reinterpret_cast<HWND>(3);
    windows[a]=10; windows[b]=10; windows[content]=20;
    Item first{CImmersiveTaskItem_vftable_ITaskItem,a,nullptr};
    Item second{CImmersiveTaskItem_vftable_ITaskItem,b,content};
    CImmersiveTaskItem_GetThumbnailWindow=Thumbnail;
    expected=&first;
    assert(GetWindowForThumbnailTaskItem(&first)==a); // app HWND not created yet
    expected=&second;
    assert(GetWindowForThumbnailTaskItem(&second)==b); // ignore detached content
    assert(appQueries==0 && thumbnailQueries==2);
    DesktopRecencyState desk;
    desk.windowFocusMap[a]={900,10}; desk.windowFocusMap[b]={950,10};
    ThumbnailTaskItemMapping saved{b,10}; ULONGLONG tick=0;
    assert(HwndFromMappingEntry(saved)==b);
    assert(IsWindowRecentForPreviewLocked(desk,b,&tick) && tick==950);
    // Missing getter or missing native HWND fails closed, never app fallback.
    CImmersiveTaskItem_GetThumbnailWindow=nullptr;
    assert(!GetWindowForThumbnailTaskItem(&second) && appQueries==0);
    CImmersiveTaskItem_GetThumbnailWindow=Thumbnail;
    second.frame=nullptr;
    assert(!GetWindowForThumbnailTaskItem(&second) && appQueries==0);
    Item desktop{reinterpret_cast<void*>(200),nullptr,a};
    assert(GetWindowForThumbnailTaskItem(&desktop)==a && appQueries==1);
    desktop.app=b;
    assert(GetWindowForThumbnailTaskItem(&desktop)==b && appQueries==2);
    assert(!GetWindowForThumbnailTaskItem(nullptr));
    windows[b]=11;
    assert(!HwndFromMappingEntry(saved));
    assert(!IsWindowRecentForPreviewLocked(desk,b));
    windows.erase(b);
    assert(!HwndFromMappingEntry(saved));
}
'''.replace('// HELPERS',helpers)
with tempfile.TemporaryDirectory(prefix='windhawk-preview-') as folder:
    cpp_path=Path(folder)/'test.cpp'; exe=Path(folder)/'test.exe'
    cpp_path.write_text(cpp,encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(cpp_path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
assert 'kHostedWindowTimerId' not in source and 'g_hostedWindowLinks' not in source
assert 'GetWindowForThumbnailTaskItem(taskItem)' in function('void AddThumbnailTaskItemMapping(')
print('PASS: thumbnail getter selection, correct interface, early-null app, missing getter, Win32 identity and PID guards')
