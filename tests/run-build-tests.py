#!/usr/bin/env python3
"""The committed .wh.cpp must equal src/*.cpp joined."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
r = subprocess.run([sys.executable, str(root / "tools" / "build_mod.py"), "--check"])
if r.returncode:
    sys.exit(r.returncode)
# A lone CR (not part of CRLF) makes git treat the file as binary and shows
# up in the windhawk-mods diff.
for f in sorted((root / "src").glob("*.cpp")) + [root / "taskbar-recent-focus-highlight.wh.cpp"]:
    b = f.read_bytes()
    assert b.count(b"\r") == b.count(b"\r\n"), f"lone CR in {f.name}"
print("PASS: generated .wh.cpp matches src/, no lone CR")
