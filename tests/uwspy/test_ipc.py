import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import struct
import subprocess
import time
import unittest
from uwspy_client import Client, K, endpoints, parse_dump
from harness import badge_state, Inconclusive

class DumpTests(unittest.TestCase):
    def test_real_badge_regression(self):
        root=Path(__file__).parent/'fixtures'
        for state in ('front','absent','behind'):
            with self.subTest(state=state):
                nodes=parse_dump((root/f'badge-{state}.txt').read_text(encoding='utf-8'))
                self.assertEqual(badge_state(nodes),state)
    def test_local_visibility_wins(self):
        nodes=parse_dump('Path: Panel > Icon\nName: Icon\nChild index: 1\nOther Properties:\n- Canvas.ZIndex: 0\n\nPath: Panel > Badge\nName: OverlayIcon\nChild index: 2\nLocal properties:\n- Visibility: 1\nOther Properties:\n- Visibility: 0\n- Canvas.ZIndex: 0\n')
        self.assertEqual(badge_state(nodes),'absent')

class TransportTests(unittest.TestCase):
    def setUp(self):
        self.fixture=subprocess.Popen([str(Path(__file__).parent/'bin'/'IpcFixture.exe')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
        line=self.fixture.stdout.readline().strip()
        if not line:raise RuntimeError(self.fixture.stderr.read())
        self.pid,self.h1,self.h2=map(int,line.split())
        endpoint=next(p for p in endpoints() if p.startswith(f'\\\\.\\pipe\\UWPSpy-{self.pid}-'))
        self.client=Client(endpoint)
    def tearDown(self):
        if self.fixture.poll() is None:
            self.fixture.stdin.write('\n');self.fixture.stdin.flush()
            try:self.fixture.wait(timeout=20)
            except subprocess.TimeoutExpired:self.fixture.kill();self.fixture.wait();raise
        self.fixture.stdin.close();self.fixture.stdout.close();self.fixture.stderr.close()
    def open_raw(self):
        deadline=time.monotonic()+3
        while True:
            h=K.CreateFileW(self.client.endpoint,0xC0000000,0,None,3,0,None)
            if h!=W.HANDLE(-1).value:return h
            if time.monotonic()>deadline:raise C.WinError(C.get_last_error())
            time.sleep(.01)
    def test_multi_ui_routing_unicode_and_events(self):
        trees=self.client.call('trees')['trees'];self.assertEqual(len(trees),2)
        self.assertNotEqual(trees[0]['tid'],trees[1]['tid'])
        for tree in trees:
            reply=self.client.call('echo',tree=tree['tree'],value='žluťoučký 🐦')
            self.assertEqual(reply['tid'],tree['tid']);self.assertEqual(reply['echo'],'žluťoučký 🐦')
        self.client.call('publish',tree=trees[0]['tree'])
        events=self.client.call('events',after='0')
        self.assertTrue(events['gap']);self.assertEqual(len(events['events']),256)
        self.assertEqual(events['cursor'],'300')
        self.assertEqual(self.client.call('events',after='300')['events'],[])
        with self.assertRaisesRegex(RuntimeError,'unknown tree'):self.client.call('get',tree='nope')
    def test_fragmented_and_malformed_requests(self):
        for data in (b'{bad json',json.dumps({'op':'trees','request_id':'fragment'}).encode()):
            h=self.open_raw()
            try:
                packet=struct.pack('<I',len(data))+data
                for byte in packet:
                    n=W.DWORD();b=C.create_string_buffer(bytes([byte]));self.assertTrue(K.WriteFile(h,b,1,C.byref(n),None))
                def read(size):
                    b=C.create_string_buffer(size);n=W.DWORD();self.assertTrue(K.ReadFile(h,b,size,C.byref(n),None));self.assertEqual(n.value,size);return b.raw
                size,=struct.unpack('<I',read(4));reply=json.loads(read(size))
                if data==b'{bad json':self.assertIn('error',reply)
                else:self.assertEqual(reply['request_id'],'fragment')
                n=W.DWORD();K.WriteFile(h,C.create_string_buffer(b'!'),1,C.byref(n),None)
            finally:K.CloseHandle(h)
        self.assertEqual(len(self.client.call('trees')['trees']),2)
    def test_idle_client_does_not_block_shutdown(self):
        h=self.open_raw()
        try:
            start=time.monotonic();self.fixture.stdin.write('\n');self.fixture.stdin.flush()
            self.fixture.wait(timeout=3);self.assertLess(time.monotonic()-start,3)
        finally:K.CloseHandle(h)
    def test_abandoned_partial_request_recovers(self):
        h=self.open_raw();n=W.DWORD();K.WriteFile(h,C.create_string_buffer(b'\x04'),1,C.byref(n),None);K.CloseHandle(h)
        self.assertEqual(len(self.client.call('trees')['trees']),2)
    def test_ui_timeout_is_reported_and_recovers(self):
        tree=self.client.call('trees')['trees'][0]
        with self.assertRaisesRegex(RuntimeError,'timed out'):
            self.client.call('delay',tree=tree['tree'])
        reply=self.client.call('echo',tree=tree['tree'],value='after timeout')
        self.assertEqual(reply['echo'],'after timeout')

if __name__=='__main__':unittest.main(verbosity=2)
