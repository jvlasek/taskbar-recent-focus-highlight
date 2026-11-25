"""Settings transition 0.11 -> 0.12, part 2 (0.12.2): the app-ranking keys
moved into the Taskbar icons group. Old values apply until settings are
saved, only on a 0.11 install, only while the new keys are unset/default."""
import sys as _sys
from pathlib import Path as _Path

_sys.path.insert(0, str(_Path(__file__).resolve().parent))
from mod_source import source_text  # src/*.cpp joined
from pathlib import Path
import re
import subprocess
import tempfile

source = source_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


def between(start, end):
    a = source.index(start)
    return source[a:source.index(end, a)]


# The YAML: only the exclude list is top level; the moved keys are in icons.
yaml = between('// ==WindhawkModSettings==', '// ==/WindhawkModSettings==')
top = re.findall(r'\n- (\w[\w.]*):', yaml)
assert top == ['excludedPrograms', 'icons', 'previews'], top
icons = yaml[yaml.index('\n- icons:'):yaml.index('\n- previews:')]
for key in ('highlightCount', 'decayMinutes', 'minFocusSeconds', 'promoteMode'):
    assert f'\n    - {key}:' in icons, key
load = function('void LoadSettings(bool userSavedSettings) {')
for key in ('highlightCount', 'minFocusSeconds', 'promoteMode', 'decayMinutes'):
    assert f'L"icons.{key}"' in load, key
    assert f'L"{key}"' not in load, key
assert 'ApplyLegacyAppSettings(s, userSavedSettings);' in load

helpers = '\n'.join([
    between('template <typename E>\nstruct EnumName {', 'constexpr EnumName<GlowColorMode>'),
    between('constexpr EnumName<PromoteMode> kPromoteModeNames[]', 'constexpr EnumName<PreviewStyle>'),
    function('PCWSTR EnumToName('),
    function('bool NameToEnum('),
    function('PCWSTR PromoteModeName(PromoteMode m) {'),
    function('int ClampedIntSetting(PCWSTR name, int lo, int hi) {'),
    between('template <typename E, size_t N>\nE EnumSetting(', '\nvoid LoadSettings') .split('\n// Settings transition')[0],
    between('constexpr wchar_t kLegacyAppSettingsState[]', '\nvoid LoadSettings'),
])
# EnumToName / NameToEnum carry their own template line before the function.
helpers = helpers.replace('PCWSTR EnumToName(', 'template <typename E, size_t N>\nPCWSTR EnumToName(', 1)
helpers = helpers.replace('bool NameToEnum(', 'template <typename E, size_t N>\nbool NameToEnum(', 1)

fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
using PCWSTR = const wchar_t*;
enum class PromoteMode { ImmediateTracked, ImmediateTopN, AlwaysWait };
struct Settings {
    int highlightCount = 3, minFocusSeconds = 8, decayMinutes = 30;
    PromoteMode promoteMode = PromoteMode::ImmediateTracked;
};
std::map<std::wstring, int> ints;            // stored int settings
std::map<std::wstring, std::wstring> strs;   // stored string settings
std::map<std::wstring, int> storage;         // mod storage
int logs = 0;
std::wstring fmt(PCWSTR f, va_list a) {
    // MSVC-style %s is wide; glibc wants %ls.
    std::wstring g = f;
    for (size_t i = 0; (i = g.find(L"%s", i)) != std::wstring::npos; i += 3)
        g.replace(i, 2, L"%ls");
    wchar_t b[256]; vswprintf(b, 256, g.c_str(), a); return b;
}
int Wh_GetIntSetting(PCWSTR f, ...) {
    va_list a; va_start(a, f); auto k = fmt(f, a); va_end(a);
    auto it = ints.find(k); return it == ints.end() ? 0 : it->second;
}
int Wh_GetIntValue(PCWSTR k, int d) {
    auto it = storage.find(k); return it == storage.end() ? d : it->second;
}
void Wh_SetIntValue(PCWSTR k, int v) { storage[k] = v; }
void Wh_Log(PCWSTR, ...) { ++logs; }
namespace WindhawkUtils {
struct StringSetting {
    std::wstring v;
    static StringSetting make(PCWSTR f, ...) {
        va_list a; va_start(a, f); auto k = fmt(f, a); va_end(a);
        auto it = strs.find(k);
        return {it == strs.end() ? L"" : it->second};
    }
    const wchar_t* get() const { return v.c_str(); }
};
}
''' + helpers + r'''
Settings Load(bool saved) {
    Settings s;
    s.highlightCount = ClampedIntSetting(L"icons.highlightCount", 0, 16);
    s.minFocusSeconds = ClampedIntSetting(L"icons.minFocusSeconds", 0, 1 << 30);
    s.promoteMode = EnumSetting(L"icons.promoteMode", kPromoteModeNames,
                                PromoteMode::ImmediateTracked);
    s.decayMinutes = ClampedIntSetting(L"icons.decayMinutes", 0, 1 << 30);
    ApplyLegacyAppSettings(s, saved);
    return s;
}
void Reset() { ints.clear(); strs.clear(); storage.clear(); }
void Old011(int count, int minFocus, PCWSTR promote, int decay) {
    ints[L"highlightCount"] = count; ints[L"minFocusSeconds"] = minFocus;
    strs[L"promoteMode"] = promote; ints[L"decayMinutes"] = decay;
    strs[L"previews.style"] = L"plateTitle";
}
void New012Defaults() {
    ints[L"icons.highlightCount"] = 3; ints[L"icons.minFocusSeconds"] = 8;
    strs[L"icons.promoteMode"] = L"immediateTracked";
    ints[L"icons.decayMinutes"] = 30;
}
int main() {
    // Fresh 0.12.2 install, nothing stored: defaults (unset reads 0 here,
    // Windhawk itself returns the YAML default).
    Reset(); New012Defaults();
    Settings s = Load(false);
    assert(s.highlightCount == 3 && storage[L"legacyAppSettingsState"] == 2);

    // 0.11 install with custom values, new keys unset: old values used.
    Reset(); Old011(5, 3, L"alwaysWait", 60);
    s = Load(false);
    assert(s.highlightCount == 5 && s.minFocusSeconds == 3 &&
           s.promoteMode == PromoteMode::AlwaysWait && s.decayMinutes == 60);
    assert(storage[L"legacyAppSettingsState"] == 1);
    // Same with the new keys stored at their defaults.
    Reset(); Old011(0, 0, L"immediateTopN", 0); New012Defaults();
    s = Load(false);
    assert(s.highlightCount == 0 && s.minFocusSeconds == 0 &&
           s.promoteMode == PromoteMode::ImmediateTopN && s.decayMinutes == 0);
    // Still applied on the next load (Explorer restart) ...
    s = Load(false);
    assert(s.highlightCount == 0);
    // ... until the user saves: then the new keys win, for good.
    ints[L"icons.highlightCount"] = 4;
    s = Load(true);
    assert(s.highlightCount == 4 && storage[L"legacyAppSettingsState"] == 2);
    s = Load(false);
    assert(s.highlightCount == 4 && s.promoteMode == PromoteMode::ImmediateTracked);

    // A new key already changed (dev build): no transition.
    Reset(); Old011(5, 3, L"alwaysWait", 60); New012Defaults();
    ints[L"icons.decayMinutes"] = 45;
    s = Load(false);
    assert(s.highlightCount == 3 && s.decayMinutes == 45);
    assert(storage[L"legacyAppSettingsState"] == 2);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='windhawk-settings-') as folder:
    cpp = Path(folder) / 'test.cpp'
    exe = Path(folder) / 'test.exe'
    cpp.write_text(fixture, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-w', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('PASS: settings layout (exclude list top level, app ranking in icons), '
      '0.11 app settings transition (fresh, legacy, saved, dev)')
