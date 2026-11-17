"""Run extracted production drain helpers against controlled dispatcher outcomes.

Requires Windows and g++ on PATH. Does not inject into Explorer or touch Windhawk.
"""
import sys as _sys
from pathlib import Path as _Path

_sys.path.insert(0, str(_Path(__file__).resolve().parent))
from mod_source import source_text  # src/*.cpp joined
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = source_text()
start = source.index("// Unload handshake: Completed signals")
end = source.index("std::vector<std::shared_ptr<UiDispatcher>> CollectUiDispatchers", start)
helpers = source[start:end]
start = source.index("enum class DrainProof", end)
end = source.index("// Returns only after each Low sentinel", start)
helpers += source[start:end]
harness = (root / "tests/dispatcher-drain.cpp").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="windhawk-drain-tests-") as directory:
    cpp = Path(directory) / "drain.cpp"
    exe = Path(directory) / "drain.exe"
    cpp.write_text(harness.replace("// PRODUCTION_HELPERS", helpers), encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-pthread", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
