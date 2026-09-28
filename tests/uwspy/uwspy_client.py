"""Load the canonical UWPSpy client from a configurable external checkout."""
import importlib.util
import os
from pathlib import Path

ROOT = Path(os.environ.get('UWPSPY_ROOT',
    Path(__file__).resolve().parents[3] / 'UWPSpy' / 'watcher')).resolve()
CLIENT = ROOT / 'tools' / 'uwspy_cli.py'
if not CLIENT.is_file():
    raise ImportError(f'UWPSpy client not found: {CLIENT}. Set UWPSPY_ROOT to your UWPSpy checkout.')
spec = importlib.util.spec_from_file_location('_uwspy_external_cli', CLIENT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Client = module.Client
endpoints = module.endpoints
parse_dump = module.parse_dump
K = module.K  # low-level Win32 transport used by integration tests
