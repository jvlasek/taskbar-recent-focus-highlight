"""Control-flow checks only: no live taskbar or mod interaction."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch
from harness import Run

class LifecycleTests(unittest.TestCase):
    def test_three_rounds_keep_phase_order(self):
        run=Run.__new__(Run)
        run.args=SimpleNamespace(test_unload=True,unload_rounds=3)
        calls=[]
        run.run_cycles=lambda prefix='':calls.append(('cycles',prefix))
        run.pause_for_reload=lambda prefix:calls.append(('enable',prefix))
        run.pause_for_unload=lambda prefix:calls.append(('disable',prefix))
        run.run_phases()
        self.assertEqual([c[0] for c in calls],
            ['cycles','disable','cycles','enable','cycles','disable','cycles','enable','cycles','disable','cycles'])
        self.assertEqual(len(set(p for op,p in calls if op=='cycles')),6)

    def test_reload_preserves_references_and_resets_focus_history(self):
        run=Run.__new__(Run)
        run.mode='disabled';run.expected_hwnd=123;run.recency=['D']
        run.windows={'A':123};run.elements={'A':{'handle':'original'}}
        for name in ('step','log','check','get','await_activation'):setattr(run,name,Mock())
        with patch('builtins.input',return_value=''),patch('builtins.print'):
            run.pause_for_reload('round-02-')
        self.assertEqual(run.mode,'enabled')
        self.assertEqual(run.recency,[])
        self.assertIsNone(run.expected_hwnd)
        self.assertEqual(run.elements,{'A':{'handle':'original'}})
        self.assertEqual([c.args[0] for c in run.get.call_args_list],list('ABCD'))
        run.await_activation.assert_called_once()

if __name__=='__main__':unittest.main()
