"""Controlled four-app recency/badge recreation scenario; no automatic Explorer injection."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import re
from pathlib import Path
import subprocess
import threading
import time
from datetime import datetime, timezone
from uwspy_client import Client

U = C.WinDLL('user32', use_last_error=True)
U.GetForegroundWindow.restype = W.HWND
U.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
U.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
U.SendMessageTimeoutW.argtypes = [W.HWND,W.UINT,W.WPARAM,W.LPARAM,W.UINT,W.UINT,C.POINTER(C.c_size_t)]
U.SendMessageTimeoutW.restype = C.c_ssize_t
U.IsWindow.argtypes = [W.HWND]
ENUM = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
U.EnumWindows.argtypes = [ENUM, W.LPARAM]
U.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
WIN_EVENT = C.WINFUNCTYPE(None, W.HANDLE,W.DWORD,W.HWND,C.c_long,C.c_long,W.DWORD,W.DWORD)
U.SetWinEventHook.argtypes = [W.DWORD,W.DWORD,W.HMODULE,WIN_EVENT,W.DWORD,W.DWORD,W.DWORD]
U.SetWinEventHook.restype = W.HANDLE
U.UnhookWinEvent.argtypes = [W.HANDLE]
U.PeekMessageW.argtypes = [C.POINTER(W.MSG),W.HWND,W.UINT,W.UINT,W.UINT]
U.TranslateMessage.argtypes = [C.POINTER(W.MSG)]
U.DispatchMessageW.argtypes = [C.POINTER(W.MSG)]

class Disrupted(Exception): pass
class Inconclusive(Exception): pass
class Failure(Exception): pass


def utc(): return datetime.now(timezone.utc).isoformat()

def send(hwnd, message, w=0, l=0):
    result=C.c_size_t()
    if not U.SendMessageTimeoutW(hwnd,message,w,l,2,3000,C.byref(result)):
        raise Disrupted('child did not respond')
    return result.value


def property_value(node, key, default=None):
    values=node['local'].get(key) or node['other'].get(key)
    return values[0] if values else default


def badge_state(nodes):
    icons=[n for n in nodes if n.get('name')=='Icon']
    badges=[n for n in nodes if n.get('name')=='OverlayIcon']
    if not badges: return 'absent'
    if len(icons)!=1 or len(badges)!=1: raise Inconclusive('ambiguous Icon/OverlayIcon nodes')
    icon,badge=icons[0],badges[0]
    if property_value(badge,'Visibility') != '0': return 'absent'
    if badge['path'].rsplit(' > ',1)[0] != icon['path'].rsplit(' > ',1)[0]:
        raise Inconclusive('badge and icon are not siblings')
    try:
        iz=int(property_value(icon,'Canvas.ZIndex','0')); bz=int(property_value(badge,'Canvas.ZIndex','0'))
        return 'front' if (bz,int(badge['child_index'])) > (iz,int(icon['child_index'])) else 'behind'
    except (ValueError,KeyError) as error: raise Inconclusive('missing ordering data') from error


def test_app_identity(nodes):
    if not nodes: return None
    identity=property_value(nodes[0], 'AutomationProperties.AutomationId', '')
    for name in 'ABCD':
        if identity.casefold()==f'Appid: UWPSpy.TestHarness.{name}'.casefold(): return name
    return None


def button_layout_eligible(nodes):
    """Exported rectangles are relative to the XAML root, not desktop coordinates."""
    if not nodes: return False
    node=nodes[0]
    if property_value(node,'Visibility','0')!='0': return False
    rect=node.get('rectangle','')
    match=re.match(r'\((-?\d+),(-?\d+)\) - \((-?\d+),(-?\d+)\)',rect)
    if not match: return False
    left,top,right,bottom=map(int,match.groups())
    # Recycled repeater elements can retain their old AppId at (-10000,-10000).
    # Negative desktop monitor coordinates are irrelevant: these are root-relative.
    return right>left and bottom>top and right>0 and bottom>0


def discover_test_buttons(client, tree=None):
    matches={name:[] for name in 'ABCD'}
    inventory=[]
    for candidate in client.find():
        if tree and candidate['tree']!=tree: continue
        entry=dict(candidate)
        try:
            result=client.call('get', **{k:candidate[k] for k in ('tree','handle','generation')})
            nodes=result['nodes']
            entry['automation_id']=property_value(nodes[0], 'AutomationProperties.AutomationId', '') if nodes else ''
            name=test_app_identity(nodes)
            entry['rectangle']=nodes[0].get('rectangle') if nodes else None
            entry['layout_eligible']=button_layout_eligible(nodes)
            if name and entry['layout_eligible']: matches[name].append(candidate)
        except (RuntimeError, OSError) as error:
            entry['inspection_error']=str(error)
        inventory.append(entry)
    return matches,inventory


class ForegroundMonitor:
    def __init__(self):
        self.events=[];self.lock=threading.Lock();self.stop=threading.Event();self.ready=threading.Event();self.ok=False
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
        self.ready.wait(3)
        if not self.ok: raise Inconclusive('foreground event hook failed')
    def run(self):
        @WIN_EVENT
        def callback(hook,event,hwnd,obj,child,tid,tick):
            with self.lock:self.events.append({'utc':utc(),'monotonic':time.monotonic(),'hwnd':int(hwnd or 0),'event_tick':tick})
        hook=U.SetWinEventHook(3,3,None,callback,0,0,0)
        self.ok=bool(hook);self.ready.set()
        if not hook:return
        msg=W.MSG()
        try:
            while not self.stop.wait(.005):
                while U.PeekMessageW(C.byref(msg),None,0,0,1):
                    U.TranslateMessage(C.byref(msg));U.DispatchMessageW(C.byref(msg))
        finally:U.UnhookWinEvent(hook)
    def since(self, start):
        with self.lock:return [e.copy() for e in self.events if e['monotonic']>=start]
    def close(self):self.stop.set();self.thread.join(3)


class Run:
    def __init__(self,args):
        self.args=args;self.client=Client(args.endpoint);self.children={};self.windows={};self.elements={}
        self.monitor=None;self.watch_tree=None;self.cursor='0';self.recency=[];self.label='setup';self.expected_hwnd=None;self.focus_mark=0
        self.output=args.output.resolve()/datetime.now().strftime('%Y%m%d-%H%M%S-%f');self.output.mkdir(parents=True)
        self.logfile=(self.output/'run.jsonl').open('w',encoding='utf-8')
    def log(self,kind,**fields):
        record={'utc':utc(),'kind':kind,'label':self.label,**fields}
        self.logfile.write(json.dumps(record,ensure_ascii=False)+'\n');self.logfile.flush()
    def check(self):
        if any(p.poll() is not None for p in self.children.values()):raise Disrupted('a test child exited')
        if self.expected_hwnd:
            for e in self.monitor.since(self.focus_mark):
                if e['hwnd']!=self.expected_hwnd:raise Disrupted(f"unexpected foreground transition: {e}")
            if U.GetForegroundWindow()!=self.expected_hwnd:raise Disrupted('foreground no longer belongs to expected child')
    def wait(self,seconds):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:self.check();time.sleep(min(.05,max(0,deadline-time.monotonic())))
        self.events()
    def events(self):
        reply=self.client.call('events',after=self.cursor);self.cursor=reply['cursor']
        self.log('watch_events',**reply)
        if reply['gap']:raise Inconclusive('watch event history overflowed')
        if any(e['kind']=='stopped' and e['tree']==self.watch_tree for e in reply['events']):
            raise Inconclusive('watch stopped during scenario')
    def step(self,label):
        self.check();self.label=label;self.log('step')
        print(f'Step: {label}',flush=True)
        for tree in {e['tree'] for e in self.elements.values()}:
            self.client.call('label',tree=tree,label=label)
    def get(self,name):
        self.check();result=self.client.call('get',**self.reference(name));self.check()
        self.log('get',app=name,result=result)
        return result['nodes']
    def reference(self,name):return {k:self.elements[name][k] for k in ('tree','handle','generation')}
    def capture(self,name):
        self.check();result=self.client.call('capture',**self.reference(name),output=str(self.output),screenshots=self.args.screenshots,label=self.label)
        self.check();self.log('capture',app=name,result=result)
    def focus(self,name):
        target=self.windows[name];old=U.GetForegroundWindow()
        self.check();begin=time.monotonic()
        if old not in self.windows.values():raise Disrupted('focus left test apps before switch')
        self.log('focus_request',app=name,old=old,target=target)
        send(old,0x8000+11,l=target)
        deadline=time.monotonic()+3
        while U.GetForegroundWindow()!=target:
            if time.monotonic()>deadline:raise Disrupted('Windows rejected focus switch')
            time.sleep(.02)
        # Drain out-of-context events before evaluating the transition window.
        time.sleep(.05)
        transition=self.monitor.since(begin)
        if any(e['hwnd'] not in (old,target) for e in transition):raise Disrupted('third-party focus during switch')
        self.log('focus_confirmed',app=name,events=transition)
        self.expected_hwnd=target;self.focus_mark=time.monotonic()
        self.wait(self.args.focus_seconds)
        self.recency=[name]+[n for n in self.recency if n!=name]
        observed=set()
        for app in 'ABCD':
            nodes=self.get(app)
            if any(n.get('name')=='WhRecentFocusGlow' for n in nodes):observed.add(app)
            if app!='A' and badge_state(nodes)!='absent':raise Failure(f'unexpected badge on {app}')
        expected=set(self.recency[:self.args.top]) if self.args.mode=='enabled' else set()
        self.log('highlight_membership',expected=sorted(expected),observed=sorted(observed),history=self.recency.copy())
        if not expected.issubset(observed):raise Failure('expected recent app is not highlighted')
        if (self.args.mode=='disabled' or len(self.recency)==4) and observed!=expected:
            raise Failure('highlighted test-app set differs from expected top N')
    def badge(self,number,expected):
        self.log('badge_request',app='A',number=number)
        hr=send(self.windows['A'],0x8000+10,w=number)&0xffffffff
        self.log('badge_ack',hresult=hex(hr))
        if hr&0x80000000:raise Inconclusive('SetOverlayIcon failed')
        deadline=time.monotonic()+5
        while True:
            nodes=self.get('A');state=badge_state(nodes)
            if expected=='present' and state!='absent':break
            if expected=='absent' and state=='absent':break
            if time.monotonic()>deadline:raise Failure(f'badge did not become {expected}')
            self.wait(.25)
        self.wait(.35)
        state=badge_state(self.get('A'));self.capture('A')
        self.log('badge_assertion',state=state)
        if expected=='present' and state!='front':raise Failure(f'badge is {state}')
        if expected=='absent' and state!='absent':raise Failure('badge reappeared unexpectedly')
    def setup(self):
        self.log('configuration',arguments={k:str(v) for k,v in vars(self.args).items()},endpoint=self.args.endpoint)
        for name in 'ABCD':self.children[name]=subprocess.Popen([str(Path(__file__).parent/'bin'/f'{name}.exe')])
        deadline=time.monotonic()+15
        while len(self.windows)!=4:
            @ENUM
            def collect(hwnd,unused):
                pid=W.DWORD();U.GetWindowThreadProcessId(hwnd,C.byref(pid));cls=C.create_unicode_buffer(256);U.GetClassNameW(hwnd,cls,256)
                for name,p in self.children.items():
                    if pid.value==p.pid and cls.value=='UWPSpyTestChild':self.windows[name]=hwnd
                return True
            U.EnumWindows(collect,0)
            if time.monotonic()>deadline:raise Inconclusive('test windows did not start')
            time.sleep(.1)
        for hwnd in self.windows.values():
            while not send(hwnd,0x8000+12):
                if time.monotonic()>deadline:raise Inconclusive('taskbar did not register a test window')
                time.sleep(.1)
        print('Keep the IPC UWPSpy inspectors open. Press Enter here when ready to find the four buttons.')
        input()
        deadline=time.monotonic()+5
        while True:
            discovered,inventory=discover_test_buttons(self.client,self.args.tree)
            self.log('button_discovery',buttons=inventory)
            if all(discovered.values()) or time.monotonic()>=deadline: break
            time.sleep(.25)
        for name in 'ABCD':
            matches=discovered[name]
            if not matches:
                raise Inconclusive(f'{name}: no laid-out button with Appid: UWPSpy.TestHarness.{name}; keep the taskbar visible; see button_discovery in run.jsonl')
            if len(matches)!=1:
                raise Inconclusive(f'{name}: found {len(matches)} matching buttons; close older test apps or use --tree for separate taskbar trees; see button_discovery in run.jsonl')
            self.elements[name]=matches[0]
        self.log('elements',elements=self.elements,windows=self.windows)
        self.watch_tree=self.elements['A']['tree']
        result=self.client.call('watch',**self.reference('A'),output=str(self.output),screenshots=self.args.screenshots,label='setup')
        self.log('watch_started',result=result)
        self.cursor=self.client.call('events',after='0')['cursor']
        self.monitor=ForegroundMonitor()
        print('Click the A test window now. The run starts after it becomes foreground. Do not use mouse/keyboard during the run.',flush=True)
        deadline=time.monotonic()+120
        while U.GetForegroundWindow()!=self.windows['A']:
            if time.monotonic()>deadline:raise Disrupted('A was not activated')
            time.sleep(.05)
        time.sleep(.1)
        print(f'Starting run... A has focus. Waiting {self.args.focus_seconds:g} seconds for the focus threshold; no further input needed.',flush=True)
        self.expected_hwnd=self.windows['A'];self.focus_mark=time.monotonic();self.wait(self.args.focus_seconds)
    def execute(self):
        outcome='inconclusive';reason='not started'
        try:
            self.setup()
            for cycle in range(self.args.cycles):
                self.step(f'{cycle:02}-A-focused');self.focus('A')
                self.step(f'{cycle:02}-A-badge-initial');self.badge(1,'present')
                self.step(f'{cycle:02}-A-badge-cleared');self.badge(0,'absent')
                for name in 'BCD':self.step(f'{cycle:02}-focus-{name}');self.focus(name)
                self.step(f'{cycle:02}-A-out-of-topN')
                if any(n.get('name')=='WhRecentFocusGlow' for n in self.get('A')):
                    raise Failure('A remains highlighted after three other apps; check highlightCount')
                for name in 'ABCD':self.capture(name)
                self.step(f'{cycle:02}-A-badge-recreated');self.badge(2,'present')
            outcome='pass';reason='badge ordering, highlight membership, and focus preconditions held'
        except Disrupted as e:outcome='disrupted';reason=str(e)
        except Failure as e:outcome='fail';reason=str(e)
        except KeyboardInterrupt:outcome='disrupted';reason='user interrupted'
        except Exception as e:outcome='inconclusive';reason=repr(e)
        finally:
            if self.monitor:self.log('foreground_history',events=self.monitor.since(0));self.monitor.close()
            self.log('result',outcome=outcome,reason=reason)
            (self.output/'result.json').write_text(json.dumps({'outcome':outcome,'reason':reason},indent=2),encoding='utf-8')
            if self.watch_tree:
                try:self.client.call('stop',tree=self.watch_tree)
                except Exception as e:self.log('cleanup_error',error=str(e))
            for name,hwnd in self.windows.items():
                pid=W.DWORD();U.GetWindowThreadProcessId(hwnd,C.byref(pid))
                if self.children[name].poll() is None and pid.value==self.children[name].pid:
                    U.PostMessageW(hwnd,0x10,0,0)
            self.logfile.close()
        print(f'{outcome.upper()}: {reason}\nEvidence: {self.output}')
        return 0 if outcome=='pass' else 1


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--endpoint',required=True)
    p.add_argument('--output',type=Path,default=Path('test-runs'))
    p.add_argument('--tree',help='restrict matching when secondary taskbars produce multiple matches')
    p.add_argument('--mode',choices=['enabled','disabled'],required=True,help='record actual mod state; no mod settings are changed')
    p.add_argument('--focus-seconds',type=float,default=10,help='must exceed configured app focus minimum')
    p.add_argument('--cycles',type=int,default=3)
    p.add_argument('--top',type=int,choices=[1,2,3],default=3,help='must match the mod highlightCount setting')
    p.add_argument('--screenshots',action='store_true')
    a=p.parse_args()
    if a.focus_seconds<=0 or a.cycles<1:p.error('focus-seconds and cycles must be positive')
    raise SystemExit(Run(a).execute())
if __name__=='__main__':main()
