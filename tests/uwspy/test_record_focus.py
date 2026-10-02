"""Launcher lifecycle checks without touching desktop focus or debug capture."""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import record_focus


class LauncherTests(unittest.TestCase):
    def run_launcher(self, *, interrupt=False, start_failure=False, close_failure=False):
        observer, windhawk = Mock(), Mock()
        if start_failure:
            observer.start.side_effect = RuntimeError('observer failed to start')
        if close_failure:
            observer.close.side_effect = RuntimeError('observer failed to stop')
        with tempfile.TemporaryDirectory() as temp, \
                patch.object(record_focus, 'ForegroundLog', return_value=observer), \
                patch.object(record_focus, 'WindhawkLog', return_value=windhawk), \
                patch.object(record_focus, 'build_observer', return_value=Path('observer.exe')), \
                patch('sys.argv', ['record_focus.py', '--output', temp, '--seconds', '.001']), \
                patch.object(record_focus.time, 'sleep', side_effect=KeyboardInterrupt if interrupt else None), \
                contextlib.redirect_stdout(io.StringIO()):
            result = record_focus.main()
        observer.close.assert_called_once()
        windhawk.close.assert_called_once()
        return result

    def test_ctrl_c_closes_both(self):
        self.assertEqual(self.run_launcher(interrupt=True), 0)

    def test_start_failure_closes_both(self):
        self.assertEqual(self.run_launcher(start_failure=True), 1)

    def test_cleanup_failure_does_not_skip_other_collector(self):
        self.assertEqual(self.run_launcher(interrupt=True, close_failure=True), 1)


if __name__ == '__main__':
    unittest.main()
