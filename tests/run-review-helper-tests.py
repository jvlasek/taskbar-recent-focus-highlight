"""Exercise the production hook-attempt guard and nested path-cache scope."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent.parent
source = (root / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
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
std::atomic<int> attempts{0}, applied{0};
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
        attempts = 0; applied = 0; g_taskbarViewHookAttempted = false;
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
print('PASS: concurrent hook attempts, failed-attempt suppression, nested cache release')
