"""The mod source as the tests see it: src/*.cpp joined (tools/build_mod.py).

Tests read the parts, not the generated .wh.cpp, so they cover what is
edited even before the .wh.cpp is regenerated. run-build-tests.py checks
that the generated file matches.
"""
import importlib.util
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _builder():
    spec = importlib.util.spec_from_file_location(
        "build_mod", ROOT / "tools" / "build_mod.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def source_bytes():
    return _builder().joined()


def source_text():
    """Like Path.read_text: universal newlines."""
    return source_bytes().decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")


def source_raw_text():
    """Like read_bytes().decode: line endings kept."""
    return source_bytes().decode("utf-8")
