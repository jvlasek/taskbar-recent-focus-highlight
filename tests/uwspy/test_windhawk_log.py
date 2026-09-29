import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from windhawk_log import WindhawkLog


class CollectorTests(unittest.TestCase):
    def test_capture_labels_and_cleanup(self):
        with tempfile.TemporaryDirectory() as folder:
            label=['setup']
            collector=WindhawkLog(folder,lambda:label[0])
            original=subprocess.Popen
            def launch(*args,**kwargs):
                code="import time; print("+repr(collector.marker)+",flush=True); time.sleep(.3); print('[WH] example',flush=True); time.sleep(30)"
                return original([sys.executable,'-u','-c',code],**kwargs)
            with patch('windhawk_log.subprocess.Popen',side_effect=launch):
                try:
                    collector.start('unused')
                    label[0]='badge-recreated'
                    deadline=time.monotonic()+3
                    while '[WH] example' not in Path(folder,'windhawk.log').read_text():
                        collector.check()
                        self.assertLess(time.monotonic(),deadline)
                        time.sleep(.02)
                finally:collector.close()
            records=[json.loads(line) for line in Path(folder,'windhawk.jsonl').read_text().splitlines()]
            self.assertTrue(records[0]['probe'])
            self.assertEqual(records[-1]['label'],'badge-recreated')
            self.assertIsNotNone(collector.process.poll())
            self.assertFalse(collector.thread.is_alive())

    def test_conflicting_viewer_is_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            collector=WindhawkLog(folder,lambda:'setup')
            original=subprocess.Popen
            def launch(*args,**kwargs):
                return original([sys.executable,'-u','-c',"print('Local capture error: DBWIN_BUFFER_READY (183)',flush=True)"],**kwargs)
            with patch('windhawk_log.subprocess.Popen',side_effect=launch):
                try:
                    with self.assertRaises(RuntimeError):collector.start('unused')
                finally:
                    try:collector.close()
                    except RuntimeError:pass
            self.assertIn('capture error',Path(folder,'windhawk.log').read_text())

if __name__=='__main__':unittest.main()
