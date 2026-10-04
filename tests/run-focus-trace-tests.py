"""Exercise temporary production hook wrappers without injecting Explorer."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
block = source.split('// TEMP_FOCUS_TRACE_BEGIN:', 1)[1]
block = block[block.index('struct FocusTraceSnapshot'):block.index('// TEMP_FOCUS_TRACE_END')]
fixture = r'''
#include <atomic>
#include <cassert>
#include <stdexcept>
using HWND=void*; using DWORD=unsigned int; using ULONGLONG=unsigned long long;
using BOOL=int; using HRESULT=int; using PCWSTR=const wchar_t*;
#define WINAPI
struct GUITHREADINFO { unsigned long cbSize; HWND hwndActive=nullptr,hwndFocus=nullptr; };
std::atomic<bool> g_unloading{false};
int getterCalls=0, originalCalls=0; bool dead=false, failGetter=false;
HWND GetForegroundWindow(){return reinterpret_cast<HWND>(2);}
DWORD GetWindowThreadProcessId(HWND,DWORD* pid){*pid=10;return 20;}
BOOL GetGUIThreadInfo(DWORD,GUITHREADINFO*){return 1;}
DWORD GetCurrentThreadId(){return 30;}
ULONGLONG GetTickCount64(){return 100;}
bool HwndMatchesStoredPid(HWND h,DWORD p){return h && p==10;}
DWORD PidFromHwnd(HWND h){return h?10:0;}
template<class... Args> void Wh_Log(PCWSTR,Args...){}
HWND GetWindowForThumbnailTaskItem(void* item){
 ++getterCalls; assert(!dead);
 if(failGetter) throw std::runtime_error("getter unavailable");
 return item;
}
// PRODUCTION
void* target=reinterpret_cast<void*>(1);
HRESULT extended(void* self,void* group,void* item,void* options){
 assert(self==target && group==target && item==target && options==target);
 ++originalCalls; dead=true; return -123;
}
void switchItem(void* self,void* item){assert(self==target && item==target);++originalCalls;dead=true;}
void activated(void* self,void* group,void* item){assert(self==target && group==target && item==target);++originalCalls;dead=true;}
int main(){
 TraceExtendedClick_Original=extended;
 TraceSwitchToItem_Original=switchItem;
 TraceTaskActivated_Original=activated;
 assert(TraceExtendedClick_Hook(target,target,target,target)==-123);
 assert(getterCalls==1 && originalCalls==1); // no post-call item dereference
 dead=false; TraceSwitchToItem_Hook(target,target);
 assert(getterCalls==2 && originalCalls==2);
 dead=false; TraceTaskActivated_Hook(target,target,target);
 assert(getterCalls==3 && originalCalls==3);
 dead=false; failGetter=true;
 assert(TraceExtendedClick_Hook(target,target,target,target)==-123);
 assert(getterCalls==4 && originalCalls==4); // diagnostic failure doesn't skip original
 g_unloading=true;
 TraceSwitchToItem_Hook(target,target);
 assert(getterCalls==4 && originalCalls==5);
}
'''.replace('// PRODUCTION', block)
for forbidden in ('PostToHookThread(', 'StampWindowRecencyLocked(', 'SetTimer(', 'ConfirmPreviewFocusNow('):
    assert forbidden not in block, forbidden
with tempfile.TemporaryDirectory(prefix='windhawk-focus-trace-') as directory:
    cpp=Path(directory)/'trace.cpp';exe=Path(directory)/'trace.exe'
    cpp.write_text(fixture,encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
print('PASS: original calls/arguments/results preserved, capture only before original, getter exception, unloading, no recency/timer mutations')
