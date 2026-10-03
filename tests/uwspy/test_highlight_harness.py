"""Offline checks; never launch apps, move the pointer or attach to Explorer."""
import unittest
from unittest.mock import Mock, patch
from types import SimpleNamespace
from highlight_harness import pair_cards, highlighted, Run, Failure, Disrupted, Inconclusive, FlyoutNotReady, absolute_coordinate, move_mouse, Input, thumbnail_names, screen_cards, bind_unique_test_titles, card_key
import ctypes as C


def node(name='', rectangle='(0,0) - (100,150)', **props):
    return dict(name=name,rectangle=rectangle,local={k:[str(v)] for k,v in props.items()},other={})


def card(handle, x=0, nodes=None):
    return dict(ref=dict(handle=str(handle),generation='1',tree='1'),nodes=nodes or [node(rectangle=f'({x},0) - ({x+100},150)')])


class Geometry(unittest.TestCase):
    def test_recreated_and_reordered_fixture_cards_keep_window_identity(self):
        a,b=card(101),card(202)
        a['ref']['automation_name']='UWPSpy Test A Highlight 1 - 2 running windows'
        b['ref']['automation_name']='UWPSpy Test A Highlight 2 - 2 running windows'
        titles={111:'UWPSpy Test A Highlight 1',222:'UWPSpy Test A Highlight 2'}
        bind_unique_test_titles([a,b],'A - 2 running windows',titles)
        key=card_key(a)
        a['ref'].update(handle='303',generation='999')
        bind_unique_test_titles([b,a],'A - 2 running windows',titles)
        self.assertEqual(card_key(a),key)
        self.assertEqual(b['expected_hwnd'],222)
        run=Run.__new__(Run);run.a=SimpleNamespace(top=1,grouping='combined',preview_style='plateTitle')
        run.mapping={key:111};run.history=[111];run.log=Mock()
        a['nodes']=[node('WhRecentFocusThumbNative')]
        run.assertions([(b,None),(a,None)],True)
        b['nodes']=a['nodes'];a['nodes']=[node()]
        with self.assertRaises(Failure):run.assertions([(b,None),(a,None)],True)

    def test_duplicate_test_titles_are_not_guessed(self):
        with self.assertRaises(Inconclusive):
            bind_unique_test_titles([card(1)],'A - 2 running windows',{1:'same',2:'same'})
    @patch('highlight_harness.pid',return_value=123)
    @patch('highlight_harness.move_mouse')
    @patch('highlight_harness.U')
    def test_ipc_geometry_rechecked_before_click(self,user,move,process):
        run=Run.__new__(Run);run.explorer=123;run.check=Mock();run.log=Mock();run.client=Mock()
        user.GetAncestor.return_value=10
        def class_name(hwnd,buffer,size): buffer.value='ThumbnailDeviceHelperWnd'
        user.GetClassNameW.side_effect=class_name
        row=dict(rect=[0,0,100,100],ipc=dict(tree='1',handle='2',generation='3'))
        run.client.call.return_value={'screen_rect':[10,0,110,100]}
        with self.assertRaisesRegex(Disrupted,'moved before click'):run.point(row,True)
        user.mouse_event.assert_not_called()
        run.client.call.return_value={'screen_rect':row['rect']}
        run.point(row,True)
        self.assertEqual(user.mouse_event.call_count,2)

    def test_ipc_screen_coordinates_and_identity(self):
        a,b=card(1),card(2)
        a['screen_rect']=[-300,120,0,350];b['screen_rect']=[0,120,300,350]
        pairs=screen_cards([b,a])
        self.assertEqual(pairs[0][1],dict(rect=a['screen_rect'],ipc=a['ref']))
        self.assertEqual(pairs[1][0],b)
        b['screen_rect']=a['screen_rect']
        with self.assertRaises(Inconclusive):screen_cards([a,b])
        with self.assertRaisesRegex(Inconclusive,'screen_rect'):screen_cards([card(3)])
    def test_card_names_use_owned_window_titles(self):
        names=thumbnail_names('A - 4 running windows',['UWPSpy Test A Highlight 1','UWPSpy Test A Highlight 2'])
        self.assertIn('UWPSpy Test A Highlight 1 - 4 running windows',names)
        self.assertNotIn('Unrelated - 4 running windows',names)
        self.assertIn('Calculator - 4 running windows',thumbnail_names('Calculator - 4 running windows pinned',['Calculator']))
    def test_absolute_input_negative_origin(self):
        self.assertEqual(absolute_coordinate(-1920,-1920,3840),8)
        self.assertTrue(32768<=absolute_coordinate(0,-1920,3840)<32800)
        with self.assertRaises(Disrupted):absolute_coordinate(1920,-1920,3840)
        self.assertEqual(C.sizeof(Input),40 if C.sizeof(C.c_void_p)==8 else 28)

    @patch('highlight_harness.U')
    def test_rejected_input_stops(self,user):
        user.GetSystemMetrics.side_effect=[0,0,1920,1080]
        user.SendInput.return_value=0
        with self.assertRaisesRegex(Disrupted,'input rejected'):move_mouse(50,50)

    def test_flyout_reenters_and_waits_for_realization(self):
        run=Run.__new__(Run);run.a=SimpleNamespace(grouping='combined',flyout_seconds=1)
        row=dict(name='A - 4 running windows')
        run.button=Mock(return_value=({},row)); run.leave_button=Mock();run.point=Mock();run.wait=Mock()
        run.snapshot=Mock(side_effect=[FlyoutNotReady('not yet'),['ready']])
        self.assertEqual(run.flyout(),['ready'])
        run.leave_button.assert_called_once_with(row)
        self.assertEqual(run.button.call_count,2)
        self.assertEqual(run.snapshot.call_count,2)

    def test_flyout_does_not_retry_identity_ambiguity(self):
        run=Run.__new__(Run);run.a=SimpleNamespace(grouping='combined',flyout_seconds=1)
        run.button=Mock(return_value=({},dict(name='A')));run.leave_button=Mock();run.point=Mock();run.wait=Mock()
        run.snapshot=Mock(side_effect=Inconclusive('ambiguous geometry'))
        with self.assertRaises(Inconclusive):run.flyout()
        run.snapshot.assert_called_once()

    @patch('highlight_harness.windows',return_value={123:456})
    @patch('highlight_harness.subprocess.Popen')
    def test_existing_fixture_rejected_before_launch(self, launch, windows):
        run=Run.__new__(Run);run.a=SimpleNamespace(app='win32');run.log=Mock()
        with self.assertRaisesRegex(Inconclusive,'Existing A test windows'):run.setup()
        launch.assert_not_called()

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

    @patch('highlight_harness.move_mouse')
    @patch('highlight_harness.U')
    def test_changed_same_root_target_receives_no_click(self, user, move):
        run=Run.__new__(Run);run.check=Mock();run.log=Mock();run.uia=Mock(return_value=None)
        user.SetCursorPos.return_value=1;user.GetAncestor.return_value=10
        with self.assertRaises(Disrupted):run.point(dict(root=10,rect=[0,0,100,100]),True)
        user.mouse_event.assert_not_called()

    @patch('highlight_harness.move_mouse')
    @patch('highlight_harness.U')
    def test_occluded_target_receives_no_click(self, user, move):
        run=Run.__new__(Run);run.check=Mock();run.log=Mock()
        user.SetCursorPos.return_value=1;user.GetAncestor.return_value=99
        with self.assertRaises(Disrupted):run.point(dict(root=10,rect=[0,0,100,100]),True)
        user.mouse_event.assert_not_called()


if __name__=='__main__':unittest.main()
