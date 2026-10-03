"""Offline checks; never launch apps, move the pointer or attach to Explorer."""
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace
from highlight_harness import pair_cards, highlighted, Run, Failure, Disrupted, Inconclusive


def node(name='', rectangle='(0,0) - (100,150)', **props):
    return dict(name=name,rectangle=rectangle,local={k:[str(v)] for k,v in props.items()},other={})


def card(handle, x=0, nodes=None):
    return dict(ref=dict(handle=str(handle),generation='1',tree='1'),nodes=nodes or [node(rectangle=f'({x},0) - ({x+100},150)')])


class Geometry(unittest.TestCase):
    def test_negative_monitor_and_scaling(self):
        a,b=card(1),card(2,110)
        r1=dict(root=10,rect=[-1200,200,-1050,425]);r2=dict(root=10,rect=[-1035,200,-885,425])
        self.assertEqual(pair_cards([b,a],[r2,r1]),[(a,r1),(b,r2)])

    def test_ambiguity_rejected(self):
        for cards,rows in [([card(1)],[]),([card(1),card(2)],
            [dict(root=1,rect=[0,0,100,150]),dict(root=2,rect=[110,0,210,150])]),
            ([card(1),card(2)], [dict(root=1,rect=[0,0,100,150])]*2),
            ([card(1),card(2,110)],[dict(root=1,rect=[0,0,100,150]),dict(root=1,rect=[500,0,600,150])])]:
            with self.assertRaises(Inconclusive):pair_cards(cards,rows)

    def test_retained_empty_overlay_not_a_highlight(self):
        self.assertFalse(highlighted([node('WhRecentFocusThumbGlow')]))
        self.assertFalse(highlighted([node('WhRecentFocusThumbTitleBg',Visibility=1)]))
        self.assertFalse(highlighted([node('WhRecentFocusThumbTitleBg',Opacity=0)]))
        self.assertTrue(highlighted([node('WhRecentFocusThumbNative')]))
        self.assertTrue(highlighted([node('WhRecentFocusThumbTitleBg',Visibility=0)]))

    def test_expected_membership_and_recycled_card(self):
        run=Run.__new__(Run);run.a=SimpleNamespace(top=1,grouping='combined',preview_style='plateTitle')
        run.history=[123];run.mapping={('1','1'):123};run.log=Mock()
        run.assertions([(card(1,nodes=[node('WhRecentFocusThumbNative')]),None),(card(2),None)],True)
        with self.assertRaises(Failure):run.assertions([(card(1),None)],True)
        with self.assertRaises(Failure):run.assertions([(card(2,nodes=[node('WhRecentFocusThumbNative')]),None)],True)
        with self.assertRaises(Inconclusive):run.assertions([(card(2),None)],True)

    def test_wrong_rank_one(self):
        run=Run.__new__(Run);run.a=SimpleNamespace(top=2,grouping='combined',preview_style='plateTitle')
        run.history=[123,456];run.mapping={('1','1'):123,('2','1'):456};run.log=Mock()
        with self.assertRaises(Failure):
            run.assertions([(card(1,nodes=[node('WhRecentFocusThumbTitleBg')]),None),
                            (card(2,nodes=[node('WhRecentFocusThumbNative')]),None)],True)

    @patch('highlight_harness.U')
    def test_changed_same_root_target_receives_no_click(self, user):
        run=Run.__new__(Run);run.check=Mock();run.log=Mock();run.uia=Mock(return_value=None)
        user.SetCursorPos.return_value=1;user.GetAncestor.return_value=10
        with self.assertRaises(Disrupted):run.point(dict(root=10,rect=[0,0,100,100]),True)
        user.mouse_event.assert_not_called()

    @patch('highlight_harness.U')
    def test_occluded_target_receives_no_click(self, user):
        run=Run.__new__(Run);run.check=Mock();run.log=Mock()
        user.SetCursorPos.return_value=1;user.GetAncestor.return_value=99
        with self.assertRaises(Disrupted):run.point(dict(root=10,rect=[0,0,100,100]),True)
        user.mouse_event.assert_not_called()


if __name__=='__main__':unittest.main()
