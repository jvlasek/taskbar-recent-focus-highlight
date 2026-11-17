#!/usr/bin/env python3
"""The committed .wh.cpp must equal src/*.cpp joined."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
r = subprocess.run([sys.executable, str(root / "tools" / "build_mod.py"), "--check"])
if r.returncode:
    sys.exit(r.returncode)
print("PASS: generated .wh.cpp matches src/")
