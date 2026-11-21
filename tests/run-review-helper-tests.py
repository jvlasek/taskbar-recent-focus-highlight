"""Exercise the production hook-attempt guard and nested path-cache scope."""
import sys as _sys
from pathlib import Path as _Path

_sys.path.insert(0, str(_Path(__file__).resolve().parent))
from mod_source import source_text  # src/*.cpp joined
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
source = source_text()
cache = source[source.index('thread_local DWORD g_imagePathCachePid'):source.index('std::wstring GetProcessImagePath(')]
hook = source[source.index('void HandleLoadedModuleIfTaskbarView('):source.index('using LoadLibraryExW_t')]
cpp = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
using DWORD = unsigned long;
using HMODULE = void*;
using LPCWSTR = const wchar_t*;
std::atomic<bool> g_taskbarViewHookAttempted{false};
std::atomic<int> attempts{0}, applied{0}, readyPosts{0};
std::atomic<bool> g_taskbarViewHooked{false};
using UINT = unsigned;
constexpr UINT WM_APP_TASKBAR_READY = 0x8007;
bool PostToHookThread(UINT msg) { assert(msg == WM_APP_TASKBAR_READY); ++readyPosts; return true; }
bool succeed = true;
HMODULE GetTaskbarViewModuleHandle() { return reinterpret_cast<void*>(1); }
void Wh_Log(const wchar_t*, ...) {}
bool HookTaskbarViewDllSymbols(HMODULE) {
    ++attempts;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return succeed;
}
void Wh_ApplyHookOperations() { ++applied; }
// HELPERS
int main() {
    for (bool result : {true, false}) {
        succeed = result;
        attempts = 0; applied = 0; readyPosts = 0; g_taskbarViewHookAttempted = false; g_taskbarViewHooked = false;
        HandleLoadedModuleIfTaskbarView(reinterpret_cast<void*>(2), L"other");
        assert(!g_taskbarViewHookAttempted);
        std::vector<std::thread> threads;
        for (int i=0; i<32; ++i) threads.emplace_back([] {
            HandleLoadedModuleIfTaskbarView(GetTaskbarViewModuleHandle(), L"view");
        });
        for (auto& t : threads) t.join();
        HandleLoadedModuleIfTaskbarView(GetTaskbarViewModuleHandle(), L"repeat");
        assert(attempts == 1);
        assert(applied == (result ? 1 : 0));
        // Only a successful hook starts focus tracking, once.
        assert(g_taskbarViewHooked == result && readyPosts == (result ? 1 : 0));
    }
    auto checkScope = [] {
        {
            ProcessImagePathCacheScope outer;
            g_imagePathCachePid = 42;
            g_imagePathCache.assign(4096, L'x');
            { ProcessImagePathCacheScope inner; }
            assert(g_imagePathCache.size() == 4096);
            assert(g_imagePathCachePid == 42);
        }
        assert(g_imagePathCacheScope == 0 && g_imagePathCachePid == 0);
        assert(g_imagePathCache.empty());
        assert(g_imagePathCache.capacity() == std::wstring().capacity());
    };
    checkScope();
    std::thread worker(checkScope); worker.join();
}
'''.replace('// HELPERS', cache + hook)
with tempfile.TemporaryDirectory(prefix='windhawk-review-') as folder:
    path = Path(folder) / 'test.cpp'
    exe = Path(folder) / 'test.exe'
    path.write_text(cpp, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-pthread', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)

# Log volume (0.12.1, capture manual-20261120-154409-086197): repeated passes
# do not repeat their detail lines.
def body(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]
apply_all = body('void ApplyAllHighlights_UIThread() {')
assert 'for (size_t i = 0; ladderChanged && i < ranks.size(); ++i)' in apply_all
assert apply_all.index('if (ladderChanged) {\n                const auto& r') < apply_all.index('ApplyButtonHighlight(live[bi], rank)')
assert 'for (size_t i = 0; changed && i < scored.size(); ++i)' in body('void RefreshThumbnailFlyout_UIThread(FrameworkElement anyThumb) {')
deadline = body('bool MinFocusDeadlineReached(')
assert 'still waiting on' not in deadline and 'if (!track.preview && firstWait)' in deadline
assert 'watching flyout repeater' not in source
print('PASS: concurrent hook attempts, failed-attempt suppression, nested cache release, repeated-pass log lines suppressed')
