# Recency engine: focus sources, min-focus timers, ranks, decay

Read before changing focus tracking, the pending-focus state machine, timers,
promotion rules, decay or virtual-desktop handling. App keys and matching are
in `identity.md`.

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| App rank key | `APPID:<Explorer AppId>` of the button that lists the window, derived at each recompute from the per-window records (`DeriveAppsFromWindowsLocked`); a window without a known AppId is recorded but not ranked | One taskbar button = one app, by Explorer's own grouping (see `identity.md`) |
| Recency scope | Per virtual desktop GUID | Workspaces don’t share top-N |
| Current desktop | Registry `CurrentVirtualDesktop`, VDM fallback. Switches come from a `RegNotifyChangeKeyValue` watch on the `VirtualDesktops` key (focus thread, 0.11.49); without it, every foreground change reads the value (before the transient early return). The decay tick reads it too. Any reader that sees a change runs `OnVirtualDesktopSwitched` | Public APIs only; the watch fires when Explorer writes the value, which can be after the foreground change of the switch |
| Window key | `HWND` + PID (per desktop map) | Multi-instance previews; PID rejects a recycled handle |
| App last HWND | `lastHwnd` + `lastPid` | Same recycle guard as preview; HWND-only would bind the rank to whichever button now owns the handle |
| Display name | Win32: file name; UWP host: window title (else AUMID stem) | Logs / exclude UX |
| App min focus | Default 8s | Alt+Tab noise |
| Promote mode | immediateTracked / immediateTopN / alwaysWait | When re-focus skips app min-focus. A confirm past `decayMinutes` does not qualify, even if the 30 s prune has not removed the map entry yet |
| Preview min focus | Default 1s; independent of app min-focus | Snappier window mark. App confirm does not stamp the current HWND. |
| Same-PID during app timer | Still the same candidate only if the new foreground window maps to the same Explorer AppId | New window of the app; UWP hosts and TC / Lister share a process |
| App decay | Default 30 min, **from last use** (0.12.1) | List stays “recent” |
| Preview decay | Default 15 min, from last use | Separate |
| Last use (0.12.1) | Decay tick: the confirmed window in front gets `GetLastInputInfo` time; on a foreground change the window that left gets it (`NoteForegroundForLastUse`, `RefreshInFrontLastUse`). Only records confirmed in the current stretch in front (`tick >= g_inFrontSince`) move. Transient windows (`IsTransientForeground`) do not start a stretch (0.12.2); a destroyed window in front is stamped and folded into its app at once | "Recently used", not "recently switched to": an app used without switching for longer than the decay lost its highlight while in use (Windows Sandbox, capture manual-20261120-071227). A visit shorter than min focus refreshes nothing, so it cannot jump ranks. Away from the keyboard the input time stops, so everything still ages out; lock screen = shell, refreshes nothing; before 0.12.2 Alt+Tab / Start moved the stretch to the switcher and, with no second foreground event on landing back, the app in use decayed (Neon review 1.1.1). Watching without input ages out too. Checked on capture manual-20261120-154409-086197 (26 h): 59 decays, none earlier than 30 min after the last confirm, 19 extended by use (up to 53 min after the last switch) |
| Exclude list | Path / file / AppId, case-insensitive | Standard Windhawk UX |
| Shell hosts ignored | SearchHost, StartMenuExperienceHost, ShellExperienceHost, ShellHost, TextInputHost; explorer **except** `CabinetWClass` / `ExploreWClass` | Don’t rank the shell. Folder windows are a real app (same process as the taskbar). AFH/WWAHost **are** ranked via `APPID:` |
| Highlight count | 0–16 (UI suggests 1–6) | Settings-capped |
| Preview highlight count | 0–16 (UI suggests 1–6) | Per-flyout cap |
| No taskbar button | Provisional key: never ranked, decays | Tray widgets and popups. Replaced the `requireTaskbarButton` setting (removed in 0.11.33) |

Promotion (apps):

1. Foreground → `g_pendingFocus` (tagged with current desktop GUID). At
   confirm, `ConfirmDesktopId` re-reads the registry; if it changed and the
   window is on the current desktop (`IsWindowOnCurrentVirtualDesktop`), the
   confirm goes to the current desktop (registry lag right after a switch,
   0.11.42)
2. After `minFocusSeconds`, or immediately per `promoteMode` → that desktop’s app map tick
3. Sort → top `highlightCount` → that desktop’s `rankedApps`
4. UI apply snapshots `IsRunning` into `observedRunning`, then recomputes
   eligibility (even if the rank list was empty). Grace is only for a
   just-observed not-running flicker, not a 400 ms heartbeat.
5. Desktop switch → `EVENT_SYSTEM_DESKTOPSWITCH` / registry GUID change →
   clear observed running, load that desktop’s history, UI snapshot + recompute,
   sweep overlays

Shared confirm helpers (do not fork another copy):

- `StillPendingForeground` — same PID still focused (new top-level HWND OK)
  and the new window maps to the same Explorer AppId
- `StampWindowRecencyLocked` — preview HWND tick + confirmSeq + PID + prune
- `DecayMsFromMinutes` / `IsTickDecayed` / `RemainingDeadlineMs` — app map, window map, min-focus timers
- `SettingsSnap` / `PublishSettings` — immutable settings; never mutate in place

App and preview confirmation are one state machine since 0.11.45:
`MinFocusTrack` (timer id, seconds, start tick, retry field; `AppTrack` /
`PreviewTrack`), `TakePendingForConfirm` (copy + exclude check),
`MinFocusDeadlineReached` (deadline, still-foreground, Alt-Tab grace;
the app track drops the candidate when focus left, the preview track does
not), `ArmOrConfirmTrack`. Only the confirm steps after it differ.

`KillTimer` does not flush a `WM_TIMER` already queued. App and preview
min-focus handlers (`FromTimer`) re-check the *current* candidate’s start tick
(`focusStartTick` / `previewStartTick`) and re-arm for the remainder instead
of confirming a newer pending focus early. `Immediate` (min=0 / promoteMode /
already-tracked window) skips that wait. A positive minimum
whose deadline has already elapsed resumes through `FromTimer`, so Alt-Tab
grace still applies. Settings changes cancel timers only for a newly excluded
candidate; an allowed pending episode is re-armed with `EnsurePendingAppTimer`
/ `EnsurePendingPreviewTimer`. The 30 s decay timer is **not**
started with the focus thread; first confirmed app or window recency arms it,
and `OnDecayTimer` stops it when every desktop map is empty and there is no
pending focus.

Alt-Tab UI, taskbar, desktop, and IME (`IsTransientForeground`) are **not**
a leave: do not clear `g_pendingFocus` or cancel min-focus timers. The landed
app often does not get a second `EVENT_SYSTEM_FOREGROUND`. After the original
deadline, re-arm at 200 ms only for `TransientGraceMs` (same as min-focus,
at least 2 s), then drop the candidate — File Explorer used to poll forever
because it is this `explorer.exe`. Folder windows (`CabinetWClass`) are a
real leave and can be ranked. Same-app foreground events call
`EnsurePendingAppTimer` so a stale `WM_TIMER` that `KillTimer`’d the live
one-shot cannot leave a candidate with no clock. Transient and “ranks already
exist” repaints use `RequestApplyVisualsDebounced` (300 ms, skip `SetTimer`
if already armed) so Alt+Tab is one full bind, not two. Confirm, decay, and
desktop switch stay immediate.

Promotion (windows): `previewMinFocusSeconds` → `StampWindowRecencyLocked` on
that desktop’s window map. On flyout open, siblings are sorted by **this
desktop’s** map (tick, then confirmSeq) and the top `previewHighlightCount`
get ranks 1…N. Card → HWND resolve: see `previews.md`.

## Focus sources (WinEvent + shell activation, since 0.11.0)

Foreground WinEvents and shell activation notifications feed
one foreground validation path on the focus worker. Only WINDOWACTIVATED and
RUDEAPPACTIVATED are accepted; null or stale/background targets are ignored.
Creation and attention notifications do not confer recency. Identity tracking
remains separate and unchanged. Both app and preview minimum-focus deadlines
survive duplicate notifications for the same pending window.

The worker owns a hidden, never-shown WS_POPUP window with TOOLWINDOW and
NOACTIVATE extended styles, replacing its message-only window. It registers
SHELLHOOK before reporting startup. If registration fails it logs the degraded
mode and keeps foreground WinEvents; no polling fallback is added. Successful
registrations are removed before destruction, including WinEvent setup failure. WM_CLOSE cannot destroy this worker-owned notification window.
Being top-level, it also receives the accent broadcasts
(`WM_SETTINGCHANGE` "ImmersiveColorSet", `WM_DWMCOLORIZATIONCOLORCHANGED`);
once tracking runs they arm a 250 ms timer that re-reads the accent
(0.12.2, replaces `UISettings::ColorValuesChanged`).

The 0.10.13 two-second foreground polling recovery and its timer are removed;
there is no polling fallback in this implementation. Existing minimum-focus timers
and transient-focus grace remain. Temporary native traces and their three private hooks are removed. The standalone observer's sampling is diagnostic.

Run `python tests/run-shell-activation-tests.py`. Live Calculator first-click and positive-delay checks passed on 0.10.15.
Fresh Win32, desktop-switch and unload checks of 0.11.0 remain unverified;
see doc/focus-shell-calculator-results.md for the precise coverage. The reason for
missing Calculator foreground WinEvents remains unknown.

## Blocking calls on the worker

- Window titles: `GetWindowTitle` calls `InternalGetWindowText` and returns
  empty on failure. Never `GetWindowTextW` / `GetWindowTextLengthW`: they send
  messages to same-process (Explorer) windows and can stall the worker and the
  unload join. Titles are for logs only, never identity.
- `SHGetPropertyStoreForWindow`, `OpenProcess` / `QueryFullProcessImageName`
  and the registry read run on the worker (a settings change too, since
  0.12.2: `DropExcludedRecords` on `WM_APP_SETTINGS_CHANGED`), never on the
  taskbar UI thread and never under `g_stateMutex`.
- `IVirtualDesktopManager` is created, used and released only on the worker
  (registry first, COM only if that fails). The UI reads `g_currentDesktopId`.
