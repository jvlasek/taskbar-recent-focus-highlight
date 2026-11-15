"""App keys: one taskbar button = one app (window AUMID first, else path)."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
def function(signature):
    # First occurrence at line start whose body opens before any ';'
    # (skips forward declarations).
    start = 0
    while True:
        start = source.index('\n' + signature, start) + 1
        brace, semi = source.find('{', start), source.find(';', start)
        if brace != -1 and (semi == -1 or brace < semi):
            return source[start:source.index('\n}', start)+2]
helpers = '\n'.join(function(x) for x in (
    'std::wstring StripAppIdPrefix(', 'std::wstring CanonicalAppId(',
    'bool IsAppIdKey(', 'std::wstring MakeAppKey(', 'std::wstring PathFromAppKey(',
    'std::wstring AppIdFromAppKey('))
cpp = r'''
#include <cassert>
#include <cwchar>
#include <string>
#include <algorithm>
#include <cwctype>
int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wint_t x = towupper(a[i]), y = towupper(b[i]);
        if (x != y || !x) return x < y ? -1 : (x > y);
    }
    return 0;
}
std::wstring ToUpper(std::wstring s) { for (auto& c : s) c = towupper(c); return s; }
// HELPERS
int main() {
    const std::wstring tc = L"C:\\PROGRAM FILES\\TOTALCMD\\TOTALCMD64.EXE";
    // TC main window has no window AUMID: grouped by exe, keyed by path.
    assert(MakeAppKey(tc, L"") == tc);
    // Lister sets its own AUMID: separate taskbar button, separate app.
    const std::wstring lister = MakeAppKey(tc, L"LISTER{CECFE544-EF6E-499D-8F87-56B61FA2EC44}");
    assert(lister == L"APPID:LISTER{CECFE544-EF6E-499D-8F87-56B61FA2EC44}");
    assert(lister != MakeAppKey(tc, L""));
    assert(IsAppIdKey(lister) && PathFromAppKey(lister).empty());
    assert(AppIdFromAppKey(lister) == L"LISTER{CECFE544-EF6E-499D-8F87-56B61FA2EC44}");
    // UWP host unchanged.
    assert(MakeAppKey(L"C:\\WINDOWS\\SYSTEM32\\APPLICATIONFRAMEHOST.EXE",
                      L"MICROSOFT.WINDOWSCALCULATOR_8WEKYB3D8BBWE!APP") ==
           L"APPID:MICROSOFT.WINDOWSCALCULATOR_8WEKYB3D8BBWE!APP");
    // AutomationId-style prefix is canonicalized like the button side.
    assert(MakeAppKey(tc, L"Appid: com.squirrel.Discord.Discord") ==
           L"APPID:COM.SQUIRREL.DISCORD.DISCORD");
}
'''.replace('// HELPERS', helpers)
with tempfile.TemporaryDirectory(prefix='windhawk-appkey-') as folder:
    cpp_path = Path(folder)/'test.cpp'; exe = Path(folder)/'test.exe'
    cpp_path.write_text(cpp, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', str(cpp_path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('app key tests passed')
