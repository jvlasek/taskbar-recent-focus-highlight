"""Generate the source-controlled runtime overview SVG; no third-party packages."""
from html import escape
from pathlib import Path

parts = ['''<svg xmlns="http://www.w3.org/2000/svg" width="1480" height="1440" viewBox="0 0 1480 1440" role="img" aria-labelledby="title description">
<title id="title">Taskbar Recent Focus Highlight — runtime flow</title>
<desc id="description">Shared foreground tracking feeds independent app and window recency. Taskbar identity uses an asynchronous metadata cache. Flyout identity captures the presented HWND using GetThumbnailWindow for immersive apps. All XAML painting stays on its UI dispatcher.</desc>
<defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0L8 4L0 8Z" fill="#64748b"/></marker></defs>
<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#172b43} .heading{font-size:23px;font-weight:700}.body{font-size:18px}.small{font-size:16px;fill:#475569}.tag{font-size:15px;font-weight:700;letter-spacing:1px}.wire{fill:none;stroke:#64748b;stroke-width:2;marker-end:url(#arrow)}</style>
<rect width="1480" height="1440" fill="#f6f8fc"/>
''']

def text(x, y, value, cls='body'):
    parts.append(f'<text x="{x}" y="{y}" class="{cls}">{escape(value)}</text>')

def box(x, y, w, h, heading, lines, fill='#fff', border='#cbd5e1'):
    parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{fill}" stroke="{border}"/>')
    text(x+20,y+32,heading,'heading')
    for i,line in enumerate(lines):
        text(x+20,y+61+25*i,line)

def wire(points):
    parts.append(f'<path class="wire" d="{points}"/>')

text(40,46,'Taskbar Recent Focus Highlight','heading')
text(40,76,'Two recency paths · experimental mod 0.10.15 · architecture checked 2026-10-04','small')
box(40,102,680,110,'Foreground event',[
    'EVENT_SYSTEM_FOREGROUND → focus thread',
    'Reject stale targets; validate actual foreground.'])
box(760,102,680,110,'Shell activation notification',[
    'WINDOWACTIVATED / RUDEAPPACTIVATED.',
    'Same foreground validation; no recovery polling.'])
wire('M380 212 V234 H650 V252')
wire('M1100 212 V234 H830 V252')
box(300,252,880,110,'Shared focus worker · HandleForegroundChanged',[
    'Normalize HWND → ResolveAppIdentity → exclusions + current desktop.',
    'Separate pending app and preview timers; transient shell focus gets bounded grace.'], '#eef2ff','#a5b4fc')

parts.append('<rect x="24" y="407" width="704" height="804" rx="18" fill="#edf7ff"/>')
parts.append('<rect x="752" y="407" width="704" height="804" rx="18" fill="#edf9f4"/>')
text(44,440,'TASKBAR ICONS · WHICH APPS?','tag')
text(772,440,'FLYOUT PREVIEWS · WHICH WINDOWS?','tag')
wire('M650 362 V390 H380 V458')
wire('M830 362 V390 H1100 V458')
box(44,458,660,130,'App recency · focus worker',[
    'App minimum: default 8 s; promotion mode can skip waiting.',
    'Key = full path, or APPID:AUMID for supported UWP hosts.',
    'Per-desktop history → eligible app top N + decay.'])
box(776,458,656,130,'Window recency · focus worker',[
    'Preview minimum: default 1 s; tracked/click paths may confirm now.',
    'StampWindowRecencyLocked → HWND + PID + tick / sequence.',
    'Per-desktop history; independent of taskbar top N.'])

text(44,622,'IDENTITY BRIDGE · UI DATA + WORKER METADATA','tag')
box(44,642,660,135,'TaskListButton → native task item / group',[
    'UI captures window handles, PIDs and button identity.',
    'GetWindowFromTaskItem: immersive GetAppWindow for metadata.',
    'Clicks and full binds request asynchronous identity resolution.'])
wire('M374 777 V802')
box(44,802,660,135,'QueueButtonResolve → focus worker → UI cache',[
    'ResolveOneButtonOnFocusThread: process path / AUMID / class.',
    'Validate request serial, result age and live HWND/PID.',
    'Exact HWND / AUMID / path match; missing identity → no glow.'])

text(776,622,'IDENTITY BRIDGE · LIVE NATIVE TASK ITEM','tag')
box(776,642,656,135,'Thumbnail constructor → capture presented HWND',[
    'AddThumbnailTaskItemMapping → GetWindowForThumbnailTaskItem',
    'Immersive: GetThumbnailWindow(ITaskItem*) → frame HWND',
    'Ordinary: existing window getter. Store model + HWND + PID.'], '#dcfce7','#22a06b')
wire('M1104 777 V802')
box(776,802,656,135,'Flyout opens / template or target changes',[
    'RefreshThumbnailFlyout_UIThread matches DataContext first;',
    'current repeater + model collection fill unresolved holes.',
    'Validate HWND/PID; missing getter or match → leave unmarked.'])

wire('M374 937 V995')
wire('M1104 937 V995')
wire('M704 524 H716 V971 H650 V995')
wire('M1432 524 H1444 V971 H1375 V995')
text(62,978,'App ranks + exact button match','small')
text(794,978,'Window history + resolved sibling HWNDs','small')
box(44,995,660,188,'ApplyAllHighlights_UIThread',[
    'Running buttons only; pinned-only entries do not glow.',
    'Own WhRecentFocusGlow: Side / Frame / Full.',
    'UpdateVisualStates repaints cached rank; full binds refresh identity.',
    'InsertAt even at end; never reorder native badges or rewrite ZIndex.',
    'Remove own overlays when unranked; preserve other owners.'])
box(776,995,656,188,'Rank within this flyout → paint on UI dispatcher',[
    'Require >1 window card; skip unresolved and snap-group-only cards.',
    'Sort recent siblings → preview top N; no app-rank requirement.',
    'Title bar / title tint / plate / plate + title tint.',
    'Overlays span the card grid; plate restores only our owned brush.',
    'Hover alone does not promote window recency.'])

box(40,1230,1400,90,'Additional input: explicit native click',[
    'HandleClick succeeds → same GetWindowForThumbnailTaskItem helper → post HWND/PID → WM_APP_PREVIEW_CLICK → ConfirmPreviewFocusNow.'], '#fff7ed','#fdba74')
text(40,1352,'THREAD RULE  Native hooks capture live data. The worker handles metadata / recency. XAML changes use the owning UI dispatcher.','small')
text(40,1381,'LIFETIME RULE  Stop and join worker → clean and drain UI dispatchers → unload. Result deadlines do not cancel an OS call.','small')
text(40,1410,'Read investigation-notes.md for evidence and limits. This diagram omits guard branches; private Explorer hooks are build-dependent.','small')
parts.append('</svg>')
Path(__file__).with_name('runtime-flow.svg').write_text('\n'.join(parts), encoding='utf-8')
