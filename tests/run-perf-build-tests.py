"""Check the perf-build generator against the current mod and run its timing
header (with Windows stubs) through a small scenario."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("mpb", root / "tests/perf/make_perf_build.py")
mpb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mpb)

source = (root / "taskbar-recent-focus-highlight.wh.cpp").read_bytes().decode("utf-8")
out = mpb.build(source).replace("\r\n", "\n")
for slot, _, _ in mpb.SLOTS:
    assert out.count(f"PERF_SCOPE({slot});") == 1, slot
assert out.count("perf::Report(/*sinceLoad=*/true);") == 1
assert "std::lock_guard<std::mutex> lock(g_stateMutex);" not in out
assert out.count("perf::TimedLock lock(g_stateMutex);") == source.count("std::lock_guard<std::mutex> lock(g_stateMutex);")
# Hook scopes start after Explorer's original call, inside the same function.
for slot, kind, _ in mpb.SLOTS:
    if kind != "UI-hook":
        continue
    at = out.index(f"PERF_SCOPE({slot});")
    fn_start = out.rindex("WINAPI ", 0, at)
    assert "_Original(" in out[fn_start:at], slot
assert "-perf\n" in out.split("// ==/WindhawkMod==")[0]
quiet = mpb.build(source, quiet=True).replace("\r\n", "\n")
q_head_end = quiet.index("// ======================= end of PERF BUILD header")
assert "Wh_Log(" not in quiet[q_head_end:], "quiet build left a mod Wh_Log"
assert "-perf-quiet\n" in quiet.split("// ==/WindhawkMod==")[0]
assert quiet.count("PERF_QUIET_LOG(") == source.count("Wh_Log(") + 1  # + the #define

start = out.index("namespace perf {")
end = out.index("#define PERF_SCOPE")
header = out[start:out.index("\n", end) + 1]
cpp = r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>
#include <time.h>
union LARGE_INTEGER { long long QuadPart; };
static long long g_fake = 0;  // nanoseconds, advanced by the test
inline void QueryPerformanceCounter(LARGE_INTEGER* v) { v->QuadPart = g_fake; }
inline void QueryPerformanceFrequency(LARGE_INTEGER* f) { f->QuadPart = 1000000000LL; }
static std::wstring g_log;
// Windows wide printf takes %s as wide; glibc needs %ls.
inline void Wh_Log(const wchar_t* fmt, ...) {
    std::wstring f(fmt);
    for (size_t p = 0; (p = f.find(L"%s", p)) != std::wstring::npos; p += 3) f.replace(p, 2, L"%ls");
    wchar_t b[512];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(b, 512, f.c_str(), ap);
    va_end(ap);
    g_log += b;
    g_log += L"\n";
}
// HEADER
int main() {
    using namespace perf;
    {  // 3 us UpdateVisualStates with a nested 1 us RefreshButtonHighlight
        PERF_SCOPE(UpdateVisualStates);
        g_fake += 2000;
        { PERF_SCOPE(RefreshButtonHighlight); g_fake += 1000; }
    }
    { PERF_SCOPE(HandleForeground); g_fake += 500; }
    State& s = S();
    assert(s.total[UpdateVisualStates].count == 1);
    assert(s.total[UpdateVisualStates].totalNs == 3000);
    assert(s.busyTotalNs[0] == 3000);   // nested scope not double-counted
    assert(s.busyTotalNs[1] == 500);
    assert(s.total[UpdateVisualStates].PercentileUs(50) == 4);  // 3 us < 4 us
    g_fake += 61000000000LL;            // next scope end crosses 60 s
    { PERF_SCOPE(ApplyAllHighlights); g_fake += 250000; }
    assert(g_log.find(L"PERF interval: span=61s") != std::wstring::npos);
    assert(g_log.find(L"PERF interval UpdateVisualStates: n=1") != std::wstring::npos);
    assert(s.interval[UpdateVisualStates].count == 0);
    Report(true);
    assert(g_log.find(L"PERF since-load ApplyAllHighlights: n=1 total=0.2ms avg=250.0us") != std::wstring::npos);
    {  // g_stateMutex timing on the worker: 2 us wait-free hold
        std::mutex m;
        { PERF_SCOPE(HandleForeground); perf::TimedLock lock(m); g_fake += 2000; }
        assert(s.total[WorkerStateLockHold].count == 1);
        assert(s.total[WorkerStateLockHold].totalNs == 2000);
        assert(s.total[UIStateLockWait].count == 0);
    }
    fputws(g_log.c_str(), stdout);
}
'''.replace("// HEADER", header)
with tempfile.TemporaryDirectory(prefix="perf-build-tests-") as d:
    c = Path(d) / "perf.cpp"
    exe = Path(d) / "perf"
    c.write_text(cpp, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wno-attributes", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
print("Perf build checks passed")
