"""Guided, real-input taskbar/flyout recency test on a mixed desktop (Windows)."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import time
import winreg

from harness import (U, ENUM, ForegroundMonitor, Failure, Disrupted, Inconclusive,
                     property_value, button_layout_eligible, has_active_glow, utc)
from uwspy_client import Client, endpoints
from record_focus import ForegroundLog, build_observer
from windhawk_log import WindhawkLog, default_collector

HERE = Path(__file__).resolve().parent
CALC_ID = 'Appid: Microsoft.WindowsCalculator_8wekyb3d8bbwe!App'
U.GetCursorPos.argtypes = [C.POINTER(W.POINT)]
U.GetWindowTextW.argtypes = [W.HWND,W.LPWSTR,C.c_int]
U.GetSystemMetrics.argtypes = [C.c_int]
class MouseInput(C.Structure):
    _fields_=[('dx',W.LONG),('dy',W.LONG),('data',W.DWORD),('flags',W.DWORD),
              ('time',W.DWORD),('extra',C.c_size_t)]
class InputUnion(C.Union):
    _fields_=[('mouse',MouseInput)]
class Input(C.Structure):
    _fields_=[('type',W.DWORD),('payload',InputUnion)]
U.SendInput.argtypes = [W.UINT,C.POINTER(Input),C.c_int]
U.SendInput.restype = W.UINT
U.WindowFromPoint.argtypes = [W.POINT]
U.WindowFromPoint.restype = W.HWND
U.GetAncestor.argtypes = [W.HWND, W.UINT]
U.GetAncestor.restype = W.HWND
U.ShowWindowAsync.argtypes = [W.HWND, C.c_int]
U.EnumChildWindows.argtypes = [W.HWND, ENUM, W.LPARAM]
U.SetThreadDpiAwarenessContext.argtypes = [C.c_void_p]
U.SetThreadDpiAwarenessContext.restype = C.c_void_p
U.mouse_event.argtypes = [W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.c_size_t]
U.keybd_event.argtypes = [W.BYTE, W.BYTE, W.DWORD, C.c_size_t]
U.GetAsyncKeyState.argtypes = [C.c_int]
U.GetAsyncKeyState.restype = C.c_short
U.MonitorFromPoint.argtypes = [W.POINT,W.DWORD]
U.MonitorFromPoint.restype = W.HANDLE
class MonitorInfo(C.Structure):
    _fields_=[('size',W.DWORD),('monitor',W.RECT),('work',W.RECT),('flags',W.DWORD)]
U.GetMonitorInfoW.argtypes = [W.HANDLE,C.POINTER(MonitorInfo)]
K = C.WinDLL('kernel32', use_last_error=True)
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
K.CloseHandle.argtypes = [W.HANDLE]


def pid(hwnd):
    value = W.DWORD()
    U.GetWindowThreadProcessId(hwnd, C.byref(value))
    return value.value


def image_name(hwnd):
    handle = K.OpenProcess(0x1000, False, pid(hwnd))
    if not handle:
        return ''
    try:
        size = W.DWORD(32768); text = C.create_unicode_buffer(size.value)
        return Path(text.value).name.lower() if K.QueryFullProcessImageNameW(handle, 0, text, C.byref(size)) else ''
    finally:
        K.CloseHandle(handle)


def windows(app, processes=()):
    if app=='calculator':
        return calculator_windows()
    if app!='win32':
        raise ValueError(f'unsupported app: {app}')
    result = {}
    @ENUM
    def visit(hwnd, unused):
        cls = C.create_unicode_buffer(256); U.GetClassNameW(hwnd, cls, 256)
        if app == 'win32':
            match = cls.value == 'UWPSpyTestChild' and (image_name(hwnd)=='a.exe' if processes is None else pid(hwnd) in processes)
        if match: result[int(hwnd)] = pid(hwnd)
        return True
    U.EnumWindows(visit, 0)
    return result


def build_calculator_probe(compiler):
    source=HERE/'calculator_windows.cpp'; binary=HERE/'bin'/'CalculatorWindows.exe'
    if not binary.exists() or binary.stat().st_mtime<source.stat().st_mtime:
        binary.parent.mkdir(exist_ok=True)
        print('Building Calculator discovery helper...',flush=True)
        subprocess.run([str(compiler),'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(source),
                        '-lole32','-lshell32','-luuid','-luser32','-o',str(binary)],
                       check=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)


def calculator_windows():
    try:
        result=subprocess.run([str(HERE/'bin'/'CalculatorWindows.exe')],capture_output=True,
                              timeout=5,check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    except subprocess.TimeoutExpired as e:
        raise Inconclusive('Calculator window discovery timed out reading shell properties') from e
    return {int(row['hwnd']):int(row['pid']) for row in json.loads(result.stdout)}


def rect(node):
    match = re.match(r'\((-?\d+),(-?\d+)\) - \((-?\d+),(-?\d+)\)', node.get('rectangle', ''))
    if not match: raise Inconclusive('missing XAML rectangle')
    return tuple(map(int, match.groups()))


def desktop_id():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops') as key:
        value,_=winreg.QueryValueEx(key,'CurrentVirtualDesktop')
        return bytes(value).hex()


def thumbnail_names(group, titles):
    group=group.removesuffix(' pinned')
    names={group}
    if ' - ' in group:
        suffix=group.rsplit(' - ',1)[1]
        names.update(title+' - '+suffix for title in titles if title)
    return names


def bind_unique_test_titles(cards, group, window_titles):
    """Test oracle only: controlled unique captions, never production mod matching."""
    suffix=group.removesuffix(' pinned').rsplit(' - ',1)
    if len(suffix)!=2: raise Inconclusive('missing group suffix for fixture title matching')
    names={}
    for hwnd,title in window_titles.items():
        if not title: raise Inconclusive('test window has an empty title')
        name=title+' - '+suffix[1]
        if name in names: raise Inconclusive('test window titles are not unique')
        names[name]=hwnd
    seen=set()
    for card in cards:
        hwnd=names.get(card['ref']['automation_name'].removesuffix(' pinned'))
        if hwnd is None or hwnd in seen:
            raise Inconclusive('fixture thumbnail titles do not uniquely cover owned windows')
        seen.add(hwnd);card['expected_hwnd']=hwnd


def card_key(card):
    # IPC references remain generation-scoped for calls, but are not persistent
    # app-window identities: Explorer recreates these views on every opening.
    if 'expected_hwnd' in card: return ('window',card['expected_hwnd'])
    return (card['ref']['handle'],card['ref']['generation'])


class FlyoutNotReady(Inconclusive): pass


def absolute_coordinate(value, origin, extent):
    if extent<=0 or not origin<=value<origin+extent:
        raise Disrupted('mouse target outside virtual desktop')
    # Center of the physical pixel in SendInput's 0..65535 virtual-desktop range.
    return min(65535, int(((value-origin)+.5)*65536/extent))


def move_mouse(x,y):
    left,top,width,height=(U.GetSystemMetrics(i) for i in (76,77,78,79))
    event=Input();event.type=0
    event.payload.mouse=MouseInput(absolute_coordinate(x,left,width),
        absolute_coordinate(y,top,height),0,0x8000|0x4000|0x2000|0x0001,0,0)
    if U.SendInput(1,C.byref(event),C.sizeof(event))!=1:
        raise Disrupted('mouse movement input rejected')
    time.sleep(.04)
    actual=W.POINT()
    if not U.GetCursorPos(C.byref(actual)) or abs(actual.x-x)>1 or abs(actual.y-y)>1:
        raise Disrupted('mouse movement did not reach target (or external input intervened)')


def pair_cards(cards, rows):
    """Spatial correspondence, not HWND inference. Click results establish HWNDs."""
    if not cards or len(cards) != len(rows):
        raise Inconclusive('UIA/XAML thumbnail counts disagree')
    if len({r['root'] for r in rows}) != 1:
        raise Inconclusive('multiple visible thumbnail roots')
    cards = sorted(cards, key=lambda c: (rect(c['nodes'][0])[0], rect(c['nodes'][0])[1]))
    rows = sorted(rows, key=lambda r: (r['rect'][0], r['rect'][1]))
    if len({rect(c['nodes'][0]) for c in cards})!=len(cards) or len({tuple(r['rect']) for r in rows})!=len(rows):
        raise Inconclusive('overlapping duplicate target rectangles')
    # Verify one affine scale/offset maps every rectangle, including negative origins.
    a = rect(cards[0]['nodes'][0]); b = rows[0]['rect']
    if a[2] <= a[0] or a[3] <= a[1]: raise Inconclusive('empty card rectangle')
    scale = (b[2]-b[0])/(a[2]-a[0])
    if not .5 <= scale <= 4: raise Inconclusive('unsupported geometry scale')
    dx, dy = b[0]-a[0]*scale, b[1]-a[1]*scale
    for card, row in zip(cards, rows):
        r = rect(card['nodes'][0]); expected = [r[0]*scale+dx,r[1]*scale+dy,r[2]*scale+dx,r[3]*scale+dy]
        if any(abs(x-y)>4 for x,y in zip(expected,row['rect'])):
            raise Inconclusive('UIA/XAML geometry is ambiguous or changed')
    return list(zip(cards, rows))


def highlighted(nodes):
    # Plate marker is attached only while the native background is owned.
    # Overlay hosts can remain when unpainted: inspect visible painted children.
    if any(n.get('name') == 'WhRecentFocusThumbNative' for n in nodes): return True
    return any(n.get('name') in ('WhRecentFocusThumbTitleBg','WhRecentFocusThumbTitleBar') and
               property_value(n,'Visibility','0') == '0' and
               float(property_value(n,'Opacity','1')) > 0 for n in nodes)


def screen_cards(cards):
    pairs=[]; seen=set()
    for card in cards:
        bounds=card.get('screen_rect')
        if not bounds:
            raise Inconclusive('Inspector lacks screen_rect. Load the rebuilt UWPSpy inspector, then attach again; see README.')
        if len(bounds)!=4 or bounds[2]<=bounds[0] or bounds[3]<=bounds[1] or tuple(bounds) in seen:
            raise Inconclusive('invalid or overlapping duplicate thumbnail screen bounds')
        seen.add(tuple(bounds))
        pairs.append((card,dict(rect=bounds,ipc={k:card['ref'][k] for k in ('tree','handle','generation')})))
    return sorted(pairs,key=lambda pair:(pair[1]['rect'][0],pair[1]['rect'][1]))


class Run:
    def __init__(self, args):
        self.a=args; self.client=Client(args.endpoint)
        self.explorer=int(re.search(r'UWPSpy-(\d+)-',args.endpoint).group(1))
        self.output=args.output.resolve()/datetime.now().strftime('highlights-%Y%m%d-%H%M%S-%f')
        self.output.mkdir(parents=True)
        self.stream=(self.output/'run.jsonl').open('w',encoding='utf-8')
        self.label='setup'; self.children=[]; self.owned={}; self.mapping={}; self.history=[]
        self.monitor=None; self.observer=None; self.debug=None

    def log(self, kind, **data):
        self.stream.write(json.dumps(dict(utc=utc(),label=self.label,kind=kind,**data))+'\n'); self.stream.flush()

    def step(self, label):
        self.label=label; self.log('step'); print('Step:',label,flush=True)

    def check(self):
        if U.GetAsyncKeyState(0x1B)<0: raise Disrupted('Escape pressed')
        if self.observer: self.observer.check()
        if self.debug: self.debug.check()
        for hwnd, process in self.owned.items():
            if not U.IsWindow(hwnd) or pid(hwnd)!=process: raise Disrupted('test window closed or recycled')

    def wait(self, seconds, allowed=None):
        start=time.monotonic(); mark=start
        while time.monotonic()-start<seconds:
            self.check()
            fg=int(U.GetForegroundWindow() or 0)
            if allowed is not None and fg not in allowed:
                raise Disrupted(f'unexpected foreground {fg:#x}')
            if allowed is not None and any(e['hwnd'] not in allowed for e in self.monitor.since(mark)):
                raise Disrupted('intervening foreground event during hold')
            time.sleep(.025)
        self.log('foreground_interval',events=self.monitor.since(mark),actual=int(U.GetForegroundWindow() or 0))

    def uia(self, point=None):
        extra=[] if point is None else ['-AtPoint','-X',str(point[0]),'-Y',str(point[1])]
        result=subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(HERE/'highlight_uia.ps1'),
                               '-ExplorerPid',str(self.explorer)]+extra,capture_output=True,timeout=15,
                              creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode: raise Inconclusive(result.stderr.decode(errors='replace'))
        rows=json.loads(result.stdout.decode('utf-8-sig'))
        self.log('uia',rows=rows); return rows

    def elements(self, kind):
        result=[]
        for ref in self.client.find(type=kind):
            if self.a.tree and ref['tree']!=self.a.tree: continue
            data=self.client.call('get',**{k:ref[k] for k in ('tree','handle','generation')})
            if button_layout_eligible(data['nodes']): result.append(dict(ref=ref,nodes=data['nodes'],screen_rect=data.get('screen_rect')))
        return result

    def reference(self, card):
        return {k:card['ref'][k] for k in ('tree','handle','generation')}

    def capture(self, card):
        data=self.client.call('capture',**self.reference(card),output=str(self.output),screenshots=True,label=self.label)
        self.log('capture',result=data)

    def buttons(self):
        appid=CALC_ID if self.a.app=='calculator' else 'Appid: UWPSpy.TestHarness.A'
        cards=[c for c in self.elements('Taskbar.TaskListButton') if
               property_value(c['nodes'][0],'AutomationProperties.AutomationId','').casefold()==appid.casefold()]
        rows=[r for r in self.uia() if r['id'].casefold()==appid.casefold()]
        if self.a.taskbar_hwnd: rows=[r for r in rows if r['root']==self.a.taskbar_hwnd]
        return cards,rows

    def button(self):
        cards,rows=self.buttons()
        if len(cards)!=1 or len(rows)!=1:
            raise Inconclusive('expected one grouped app button; use --tree/--taskbar-hwnd for multiple monitors; enable taskbar combining')
        if cards[0]['ref']['automation_name'] != rows[0]['name']:
            raise Inconclusive('UIA/XAML button names changed')
        return cards[0],rows[0]

    def point(self, row, click=False):
        self.check()
        l,t,r,b=row['rect']; x,y=round((l+r)/2),round((t+b)/2)
        # Real input inside the button as well as crossing its boundary: Explorer
        # did not open previews reliably from SetCursorPos repositioning alone.
        move_mouse(max(round(l)+1,x-3),y)
        move_mouse(x,y)
        time.sleep(.08)
        hit=U.WindowFromPoint(W.POINT(x,y))
        root=int(U.GetAncestor(hit,2) or 0)
        if 'ipc' in row:
            cls=C.create_unicode_buffer(256);U.GetClassNameW(root,cls,len(cls))
            if pid(root)!=self.explorer or cls.value not in ('Shell_TrayWnd','Shell_SecondaryTrayWnd','XamlExplorerHostIslandWindow','TaskListThumbnailWnd','ThumbnailDeviceHelperWnd','Xaml_WindowedPopupClass'):
                raise Disrupted('thumbnail screen target is occluded; refusing input')
        elif root!=row['root']: raise Disrupted('target is occluded or moved; refusing input')
        self.log('input',action='click' if click else 'hover',point=[x,y],root=root)
        if click:
            if 'ipc' in row:
                fresh=self.client.call('get',**row['ipc'])
                if fresh.get('screen_rect')!=row['rect']:
                    raise Disrupted('thumbnail moved before click')
            else:
                hit=self.uia((x,y))
                if not hit or any(hit[k]!=row[k] for k in ('name','id','class','rect')):
                    raise Disrupted('automation target changed before click')
            self.check()
            if int(U.GetAncestor(U.WindowFromPoint(W.POINT(x,y)),2) or 0)!=root:
                raise Disrupted('target became occluded during inspection')
            U.mouse_event(2,0,0,0,0)
            U.mouse_event(4,0,0,0,0)

    def flyout(self):
        if self.a.grouping=='separated':
            cards,rows=self.buttons()
            if len(cards)!=self.a.count: raise Inconclusive('separated taskbar must expose one button per test window')
            pairs=pair_cards(cards,rows)
            for c,_ in pairs: self.capture(c)
            return pairs
        button,row=self.button()
        self.leave_button(row)
        # Moving out may dismiss an old flyout. Rediscover in case layout changed.
        button,row=self.button(); self.point(row)
        self.wait(self.a.flyout_seconds)
        self.group_name=row['name']
        deadline=time.monotonic()+5
        while True:
            try: return self.snapshot()
            except FlyoutNotReady:
                if time.monotonic()>=deadline: raise
                self.wait(.2)

    def leave_button(self, row):
        l,t,r,b=row['rect']
        monitor=U.MonitorFromPoint(W.POINT(round((l+r)/2),round((t+b)/2)),2)
        info=MonitorInfo();info.size=C.sizeof(info)
        if not monitor or not U.GetMonitorInfoW(monitor,C.byref(info)):
            raise Inconclusive('cannot find taskbar monitor work area')
        x=(info.work.left+info.work.right)//2; y=(info.work.top+info.work.bottom)//2
        if l<=x<r and t<=y<b: raise Inconclusive('no safe hover reset point')
        move_mouse(x,y)
        self.log('input',action='hover-reset',point=[x,y])
        self.wait(.35)

    def snapshot(self):
        if self.a.grouping=='separated': return self.flyout()
        inventory=self.elements('Taskbar.TaskItemThumbnailView')
        titles={}
        for hwnd,process in self.owned.items():
            if pid(hwnd)!=process: raise Disrupted('test HWND recycled during discovery')
            title=C.create_unicode_buffer(1024);U.GetWindowTextW(hwnd,title,len(title))
            titles[hwnd]=title.value
        names=thumbnail_names(self.group_name,titles.values())
        cards=[c for c in inventory if c['ref']['automation_name'].removesuffix(' pinned') in names]
        self.log('thumbnail_discovery',expected=self.a.count,group=self.group_name,
                 matched=len(cards),
                 xaml=[dict(ref=c['ref'],rectangle=c['nodes'][0].get('rectangle')) for c in inventory])
        if len(cards)!=self.a.count:
            raise FlyoutNotReady(f'expected {self.a.count} thumbnails; XAML matched {len(cards)} of {len(inventory)}; see thumbnail_discovery')
        if self.a.app=='win32':
            bind_unique_test_titles(cards,self.group_name,titles)
            self.log('fixture_card_identity',bindings=[dict(ref=c['ref'],hwnd=c['expected_hwnd']) for c in cards])
        pairs=screen_cards(cards)
        for c,_ in pairs: self.capture(c)
        return pairs

    def assertions(self, pairs, exact):
        if self.a.grouping=='separated':
            if self.history:
                for card,_ in pairs:
                    if not has_active_glow(card['nodes']): raise Failure('same-app separated button missing app highlight')
            return
        expected=set(self.history[:self.a.top]); seen=set()
        for card,_ in pairs:
            key=card_key(card)
            hwnd=card.get('expected_hwnd',self.mapping.get(key)); actual=highlighted(card['nodes'])
            self.log('highlight',card=key,hwnd=hwnd,expected=hwnd in expected,observed=actual)
            if hwnd: seen.add(hwnd)
            if hwnd in expected and not actual: raise Failure(f'recent window {hwnd:#x} has no preview highlight')
            if exact and actual != (hwnd in expected): raise Failure('unexpected preview highlight membership')
            if hwnd in expected and self.a.preview_style=='plateTitle':
                plate=any(n.get('name')=='WhRecentFocusThumbNative' for n in card['nodes'])
                if plate != (hwnd==self.history[0]):
                    raise Failure('preview rank 1 plate is on the wrong window (or native plate unavailable)')
        if not expected.issubset(seen): raise Inconclusive('thumbnail identities changed; refusing stale correspondence')

    def activate(self, pair, ordinal):
        card,row=pair; key=card_key(card)
        self.point(row,click=True)
        deadline=time.monotonic()+3
        while int(U.GetForegroundWindow() or 0) not in self.owned:
            if time.monotonic()>deadline: raise Disrupted('thumbnail click did not activate a test window')
            time.sleep(.02)
        hwnd=int(U.GetForegroundWindow())
        if 'expected_hwnd' in card and card['expected_hwnd']!=hwnd:
            raise Disrupted('thumbnail click did not activate its independently identified fixture window')
        if key in self.mapping and self.mapping[key]!=hwnd: raise Disrupted('thumbnail activated a different window')
        if hwnd in self.mapping.values() and self.mapping.get(key)!=hwnd: raise Inconclusive('two cards resolved to one window')
        self.mapping[key]=hwnd
        self.log('activation',card=key,hwnd=hwnd)
        if self.a.app=='calculator':
            for vk in [0x1B]+[ord(str((ordinal%9)+1))]*3:
                if U.GetForegroundWindow()!=hwnd or pid(hwnd)!=self.owned[hwnd]: raise Disrupted('focus/identity changed before Calculator input')
                U.keybd_event(vk,0,0,0); U.keybd_event(vk,0,2,0)
            self.log('calculator_label',hwnd=hwnd,digits=str((ordinal%9)+1)*3)
        self.wait(self.a.focus_seconds,{hwnd})
        self.history=[hwnd]+[h for h in self.history if h!=hwnd]
        buttons,_=self.buttons()
        if not buttons: raise Inconclusive('test app button disappeared')
        for button in buttons:
            self.capture(button)
            if not has_active_glow(button['nodes']): raise Failure('focused test app has no taskbar highlight after hold')
        if U.GetForegroundWindow()!=hwnd: raise Disrupted('focus changed during post-activation inspection')
        U.ShowWindowAsync(hwnd,6)
        self.wait(.4)

    def setup(self):
        print(f'Starting {self.a.app} highlight test. Preparing recording and window discovery...',flush=True)
        self.log('configuration',arguments={k:str(v) for k,v in vars(self.a).items()})
        if self.a.app=='calculator': build_calculator_probe(self.a.compiler)
        if self.a.app=='win32' and windows('win32',None):
            raise Inconclusive('Existing A test windows found. Close previous A test windows before rerunning; no new windows were launched.')
        self.monitor=ForegroundMonitor()
        self.observer=ForegroundLog(self.output); self.observer.start(build_observer(self.a.compiler))
        if self.a.windhawk_log:
            self.debug=WindhawkLog(self.output,lambda:self.label); self.debug.start(default_collector())
        if self.a.app=='calculator' and windows('calculator'):
            raise Inconclusive('Existing Calculator windows found. Close them yourself before this isolated Calculator run; other apps can stay open.')
        baseline=int(U.GetForegroundWindow() or 0); launch_start=time.monotonic()
        for n in range(self.a.count):
            print(f'Launching {self.a.app} window {n+1}/{self.a.count}...',flush=True)
            if self.a.app=='win32':
                command=[str(HERE/'bin'/'A.exe'),'--label',f'Highlight {n+1}','--background']
                if self.a.launch in ('minimized','attention'): command.append('--minimized')
                if self.a.launch=='attention': command.append('--attention')
                self.children.append(subprocess.Popen(command))
            else:
                subprocess.Popen(['calc.exe']).wait(timeout=10)
            deadline=time.monotonic()+15
            next_progress=time.monotonic()+2
            while True:
                found=windows(self.a.app,[p.pid for p in self.children])
                if len(found)==n+1:
                    self.owned=found
                    self.log('windows_discovered',windows=found)
                    print(f'Found {len(found)}/{self.a.count} test windows.',flush=True)
                    break
                if time.monotonic()>=next_progress:
                    print(f'Waiting for window {n+1}/{self.a.count}: discovered {len(found)} so far...',flush=True)
                    self.log('window_discovery_wait',expected=n+1,windows=found)
                    next_progress=time.monotonic()+2
                if time.monotonic()>deadline: raise Inconclusive(f'new test window was not discovered (expected {n+1}, found {len(found)}); see window_discovery_wait')
                time.sleep(.15)
            if self.a.app=='calculator':
                # Windows may ignore minimized startup for packaged apps. Do not claim
                # these were never activated: Calculator always has a warm-up phase.
                for hwnd in found: U.ShowWindowAsync(hwnd,6)
            elif int(U.GetForegroundWindow() or 0)!=baseline or any(e['hwnd']!=baseline for e in self.monitor.since(launch_start)):
                raise Disrupted('background fixture unexpectedly took focus')
        print('Keep UWPSpy inspectors open. Press Enter to start; then leave mouse/keyboard alone. Hold Escape to stop.',flush=True)
        input(); self.wait(1)

    def execute(self):
        status='inconclusive'; reason='setup incomplete'
        try:
            self.setup()
            desktops=set()
            for desktop in range(self.a.desktop_rounds):
                if desktop:
                    print('Move ALL test windows to another virtual desktop and switch there. Press Enter when ready.',flush=True)
                    input(); self.mapping={}; self.history=[]
                current=desktop_id()
                if current in desktops: raise Inconclusive('desktop round must use a different virtual desktop')
                desktops.add(current); self.log('desktop',guid=current)
                self.step(f'{desktop}-initial-hover')
                pairs=self.flyout()
                if self.a.app=='win32' and desktop==0: self.assertions(pairs,exact=True)
                # Calibrate card -> actual foreground independently of mod logs.
                for n in range(self.a.count):
                    self.step(f'{desktop}-calibrate-{n+1}')
                    pairs=self.flyout(); self.activate(pairs[n],n)
                    self.assertions(self.flyout(),exact=n+1==self.a.count)
                for cycle in range(self.a.cycles):
                    for n in range(self.a.count):
                        self.step(f'{desktop}-{cycle}-hover-{n+1}')
                        pairs=self.flyout(); self.assertions(pairs,exact=True)
                        before=int(U.GetForegroundWindow() or 0)
                        self.point(pairs[n][1]); self.wait(self.a.hover_seconds,{before})
                        # Refresh after hover; no promotion is expected.
                        pairs=self.snapshot(); self.assertions(pairs,exact=True)
                        self.step(f'{desktop}-{cycle}-activate-{n+1}')
                        self.activate(pairs[n],n)
                        self.assertions(self.flyout(),exact=True)
            status='pass'; reason='foreground, icon presence and per-window preview membership held'
        except Failure as e: status='fail'; reason=str(e)
        except (Disrupted,KeyboardInterrupt) as e: status='disrupted'; reason=str(e) or 'interrupted'
        except Exception as e: status='inconclusive'; reason=repr(e)
        finally:
            for obj in (self.debug,self.observer,self.monitor):
                if obj:
                    try: obj.close()
                    except Exception as e:
                        self.log('cleanup_error',error=repr(e))
                        if status=='pass': status='inconclusive'; reason=repr(e)
            # Keep windows for inspecting a failure. Never close pre-existing apps.
            self.log('result',status=status,reason=reason,windows=self.owned)
            (self.output/'result.json').write_text(json.dumps(dict(status=status,reason=reason),indent=2))
            self.stream.close()
        print(f'{status.upper()}: {reason}\nEvidence: {self.output}\nTest windows left open for inspection.')
        return 0 if status=='pass' else 1


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app',choices=['win32','calculator'],required=True)
    p.add_argument('--grouping',choices=['combined','separated'],default='combined',help='record actual Windows setting; separated mode checks icons/activation, not hidden flyout ranks')
    p.add_argument('--preview-style',choices=['plateTitle','other'],default='plateTitle',help='plateTitle also verifies rank 1 has the native plate; other checks membership only')
    p.add_argument('--launch',choices=['background','minimized','attention'],default='attention',help='Win32 only; Calculator uses normal launch plus minimize and calibration')
    p.add_argument('--endpoint'); p.add_argument('--tree')
    p.add_argument('--taskbar-hwnd',type=lambda s:int(s,0))
    p.add_argument('--count',type=int,default=4); p.add_argument('--top',type=int,default=3,help='must match preview count; use fewer than count to test hover non-promotion')
    p.add_argument('--cycles',type=int,default=2); p.add_argument('--desktop-rounds',type=int,default=1)
    p.add_argument('--focus-seconds',type=float,default=10,help='exceed BOTH app and preview thresholds')
    p.add_argument('--hover-seconds',type=float,default=5)
    p.add_argument('--flyout-seconds',type=float,default=1)
    p.add_argument('--windhawk-log',action='store_true')
    p.add_argument('--output',type=Path,default=HERE/'captures')
    p.add_argument('--compiler',type=Path,default=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Windhawk/Compiler/bin/clang++.exe')
    a=p.parse_args()
    if not 2<=a.count<=9 or not 1<=a.top<a.count or min(a.cycles,a.desktop_rounds,a.focus_seconds,a.hover_seconds,a.flyout_seconds)<=0:
        p.error('require 2..9 windows, 1 <= top < count, and positive timings/counts')
    if not a.endpoint:
        choices=endpoints()
        if len(choices)!=1: p.error('expected one UWPSpy session; supply --endpoint for multiple sessions')
        a.endpoint=choices[0]
    U.SetThreadDpiAwarenessContext(C.c_void_p(-4))
    raise SystemExit(Run(a).execute())


if __name__=='__main__': main()
