# Focus signals: static hook audit, 2026-10-04

Scope: mod 0.10.13, installed taskbar binaries, matching cached public-symbol
data from the earlier investigations, and three current upstream mods. No
runtime hook was added, no debugger attached, and no production source changed.

## Main finding: our native click hook does not cover the XAML thumbnail route

The mod hooks `CTaskListWnd::HandleClick`. Installed code shows this distinct
path for XAML thumbnail activation:

```text
TaskItemThumbnailView::OnPointerReleased
  or TaskItemThumbnailView::Invoke (another invocation entry)
    → consume ITaskItemThumbnail2::ReportClicked
    → produce<TaskItemThumbnail, ITaskItemThumbnail2>::ReportClicked
    → ITaskListUI virtual slot +0x1B8
    → CTaskListWnd::HandleExtendedUIClick
    → normal activation branch: CTaskListWnd::SwitchToItem
```

This is not the `HandleClick` entry that currently posts our explicit preview
confirmation. Static calls and vtable entries establish the distinction;
the absence of click diagnostics in the earlier failed recording is consistent
with it. A fresh trace is still needed to correlate these native entries with
the exact failing activation.

### Checkable binary evidence

All addresses below are RVAs, for investigation only; never hardcode them in
the mod. Scratch disassembly and downloaded sources: `tmp/focus-analysis/`.

| Module | RVA | Observation |
|---|---|---|
| Taskbar.View.dll | `0x2AB2A4` | `OnPointerReleased` calls the Thumbnail2 consume wrapper at `0x26B380` |
| Taskbar.View.dll | `0x6C44F4` | `Invoke` also calls that wrapper |
| taskbar.dll | `0xADC00` | Thumbnail2 ABI `ReportClicked` invokes virtual slot `+0x1B8`, supplying group, item and launcher options |
| taskbar.dll | `0x1C2B10` | CTaskListWnd table: `+0x1B0` → HandleClick (`0xA0900`); `+0x1B8` → HandleExtendedUIClick (`0x1500F0`) |
| taskbar.dll | `0x1500F0` | Extended click decodes options; normal activation calls SwitchToItem (`0x39B40`) at `0x15023F` |
| taskbar.dll | `0xA0900` | HandleClick instead enters `_HandleClick` (`0xA130C`) |

The older `TaskItemThumbnail::ReportClicked` symbol at `0x1277D4` throws
`hresult_not_implemented`. Picking that name without distinguishing interface
versions would be another misleading shortcut. The Thumbnail2 ABI entry is
implemented. Likewise, the `CTaskListWndMulti::HandleExtendedUIClick` body on
this build fail-fasts; a symbol name alone is not evidence of a usable callback.

**Important:** ExtendedUIClick also has close/context-menu branches. Hooking
its successful return and unconditionally stamping recency would be wrong.
`SwitchToItem` is a narrower activation-request candidate. Neither an activation
request nor S_OK establishes that the window actually became foreground.

## Existing mods corroborate the route

Sources retrieved 2026-10-04; current main branches can change:

- [Taskbar Thumbnail Reorder](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-thumbnail-reorder.wh.cpp)
  hooks `HandleExtendedUIClick` specifically to suppress a click after a XAML
  thumbnail drag. Its other hooks cover model construction, collection lookup,
  pointer movement, flyout positioning and legacy thumbnail messages. It does
  not present a general foreground-notification solution.
- [Taskbar minimize/restore on scroll](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-button-scroll.wh.cpp)
  hooks both `_HandleClick` and `HandleExtendedUIClick`, and resolves the
  `ITaskItemThumbnail2::ReportClicked` ABI entry for XAML thumbnail actions.
  It also uses pointer-wheel and classic message hooks. This is a practical
  precedent for distinguishing thumbnail actions from taskbar-button actions.
- [Taskbar Thumbnail Size](https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-thumbnail-size.wh.cpp)
  changes scaled thumbnail sizing and applies templates. Its hooks concern
  geometry, not completed window activation.

The installed binaries and these mods support investigating the missing native
route before treating timed foreground reconciliation as the final design.

## A second lead: Explorer has separate activation notification paths

The installed taskbar.dll contains:

```text
_OnWindowActivated(HWND) [0x178F8]
  → _HandleActivate [0x1777C]
    → _HandleTaskActivated(group, item) [0x26970]

_OnImmersiveAppActivated(IImmersiveApplication*) [0x15170]
  → resolve/match app and task item
    → _HandleTaskActivated(group, item) [0x26970]
```

The common handler calls the task-list `ActivateTask` interface and updates
interactive-use bookkeeping. These are stronger candidates for observing the
shell's activation state than pointer hover or thumbnail creation. However,
this pass has not established their delivery timing, all callers, whether they
fire in our missing-WinEvent case, or whether they can also occur without real
foreground use. Do not promote solely from the function name.

The requested switch also differs for immersive applications: `_SwitchToWindow`
at `0x9A018` has an immersive branch using an async coroutine or an MTA task,
depending on a feature check. The ordinary branch contains `SwitchToThisWindow`
and `ShowWindowAsync`. This demonstrates different machinery, not the cause of
the lost notification; the active feature state was not measured.

## What the WinEvent audit does and does not rule out

Both the mod and standalone observer register all-process/all-thread,
out-of-context foreground hooks, with no skip-own-process/thread flags. Both
have message pumps. The mod filters null HWND / non-OBJID_WINDOW; the observer
logs all callbacks without that filter. A callback merely rejected by that mod
filter should therefore still appear in the independent log. HWND normalization
also cannot erase a callback from the observer's raw event log.

`EVENT_SYSTEM_DESKTOPSWITCH` is the other mod event; it is for desktop changes,
not a second activation signal. The observer does not yet subscribe to
`EVENT_OBJECT_FOCUS`. That event is about keyboard focus, which can change
inside an already-foreground app; it is a candidate trigger for checking actual
foreground, not a direct replacement recency event. See Microsoft's
[event definitions](https://learn.microsoft.com/en-us/windows/win32/winauto/event-constants).

Shell-hook activation messages (`HSHELL_WINDOWACTIVATED` and
`HSHELL_RUDEAPPACTIVATED`) are another observation channel. Their documented
desktop scope and compatibility caveat matter; they must be measured alongside
the failing transition, not assumed more reliable. See
[RegisterShellHookWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registershellhookwindow).

## Recommended next experiment, before changing behavior

1. Temporarily trace the existing HandleClick, ExtendedUIClick/SwitchToItem and
   common `_HandleTaskActivated` routes with entry/exit timestamps, TIDs and
   exact live task-item HWND/PID. Mark these probes explicitly temporary.
   Capture no raw task-item pointers for later dereferencing and do no slow
   identity lookup inside the UI hooks.
2. Extend the independent observer with keyboard-focus and shell-activation
   notifications. Keep its actual foreground sampling as the comparison, not
   as a proposed production solution.
3. Reproduce first/second activation, long flyout dwell, keyboard invocation,
   Alt-Tab, hover-only, right-click and close, with Win32 and Calculator.
   Identify which signal arrives after actual activation, including the failed
   WinEvent cases, and which signals only indicate intent or bookkeeping.
4. Prefer the smallest validated event path feeding normal focus handling.
   Reassess whether bounded recovery can be removed or narrowed only after
   tests cover both app and preview recency. Fixing explicit thumbnail clicks
   alone does not cover every focus source.

This pass found a concrete omission in our native click coverage. It did not
prove why Windows omitted/did not deliver the foreground WinEvent, nor that a
universal event-driven replacement has already been found. The previous review
reply remains accurate about the current code's limits; this discovery should
inform the next change, not be advertised as an implemented fix.

Binary SHA-256:

- taskbar.dll: `5DCEEA036939ACB4E43B801A7A63530F328EDAD164741E6529D3D32E9B1C5866`
- Taskbar.View.dll: `22ABD1F0BB23C89679676499473B1504F53A7786F2824461C57BA7348495550B`
