"""Exercise shadow AppId partition bookkeeping (split / merge / cap)."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start)+2]
def block(start_marker, end_marker):
    start = source.index(start_marker)
    return source[start:source.index(end_marker, start)+len(end_marker)]
helpers = '\n'.join([
    'constexpr size_t kShadowMaxKeys = 4;',
    block('struct ShadowIdentityMaps {', '\n};\n'),
    block('enum ShadowPairResult : unsigned {', '\n};\n'),
    function('unsigned ShadowRecordPair('),
    function('std::wstring ShadowJoin('),
])
cpp = r'''
#include <cassert>
#include <cstddef>
#include <map>
#include <set>
#include <string>
// HELPERS
int main() {
    ShadowIdentityMaps m;
    const std::wstring tc=L"C:\\TOTALCMD\\TOTALCMD64.EXE";
    // First sighting is a new pair; repeat is silent.
    assert(ShadowRecordPair(m,tc,L"TC.MAIN")==kShadowNewPair);
    assert(ShadowRecordPair(m,tc,L"TC.MAIN")==kShadowSeen);
    // TC + Lister: one mod key, two Explorer AppIds -> SPLIT.
    assert(ShadowRecordPair(m,tc,L"TC.LISTER")==(kShadowNewPair|kShadowSplit));
    assert(ShadowJoin(m.keyToAppIds[tc])==L"TC.LISTER | TC.MAIN");
    // Two mod keys behind one AppId -> MERGE (and the second key is new).
    assert(ShadowRecordPair(m,L"C:\\A.EXE",L"SHARED")==kShadowNewPair);
    assert(ShadowRecordPair(m,L"C:\\B.EXE",L"SHARED")==(kShadowNewPair|kShadowMerge));
    // Split and merge can both fire for one pair.
    assert(ShadowRecordPair(m,L"C:\\A.EXE",L"TC.MAIN")==
           (kShadowNewPair|kShadowSplit|kShadowMerge));
    // Empty inputs never record.
    assert(ShadowRecordPair(m,L"",L"X")==kShadowSeen);
    assert(ShadowRecordPair(m,L"C:\\A.EXE",L"")==kShadowSeen);
    // Cap: 3 keys / 4 AppIds so far; a 4th key fits, a 5th does not.
    assert(m.keyToAppIds.size()==3 && m.appIdToKeys.size()==3);
    assert(ShadowRecordPair(m,L"C:\\D.EXE",L"D")==kShadowNewPair);
    assert(ShadowRecordPair(m,L"C:\\E.EXE",L"E")==kShadowFull);
    // Known key + known AppId still records at the cap.
    assert(ShadowRecordPair(m,L"C:\\D.EXE",L"SHARED")==
           (kShadowNewPair|kShadowSplit|kShadowMerge));
    assert(!m.keyToAppIds.count(L"C:\\E.EXE"));
}
'''.replace('// HELPERS',helpers)
with tempfile.TemporaryDirectory(prefix='windhawk-shadow-') as folder:
    cpp_path=Path(folder)/'test.cpp'; exe=Path(folder)/'test.exe'
    cpp_path.write_text(cpp,encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(cpp_path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
print('shadow AppId tests passed')
