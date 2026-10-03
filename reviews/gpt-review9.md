# Local follow-up review — 0.10.13

Reviewed 2026-10-03. Scope: the foreground-recovery patch, removal of temporary
0.10.12 diagnostics, thumbnail getter usage, and verification of the previous
Claude findings against current code. This is not an exhaustive re-audit of
every line or a claim of live Explorer validation.

## Findings and remaining limitations

No additional blocking defect identified in the reviewed change. A timer-start
failure originally left a nonzero recovery deadline without a timer; fixed
before committing and covered by the new production-helper test.

The two-second recovery window is deliberately bounded. A missing activation
event after a longer flyout dwell, or without a preceding shell event, can still
be missed. This is the principal live-validation question. Recovery starts the
normal focus clock when observed, potentially about 100 ms later than activation
under an otherwise responsive worker. It does not retroactively infer focus or
bypass thresholds. Worker delays can increase that latency or exhaust the window.

Normal app events cancel recovery; timeout, desktop switch and shutdown stop it.
No new XAML subscription, COM ownership or independent thread is introduced.
The explicit native click path now uses the same thumbnail HWND getter as model
capture. The observed failing UWP path did not invoke that hook, so the worker
reconciliation is still needed.

## Previous review follow-ups

- Late view-hook installation uses an atomic claim before attempting hooks,
  shared by the loader and AfterInit paths.
- The outermost process-path cache scope releases string capacity.
- Side layers are limited to two; the unused third rectangle is removed.
- The preview roundness reference matches the settings label.
- Title retrieval uses InternalGetWindowText with an empty result on failure;
  the tool-window filter no longer calls GetWindowTextLengthW.
- Button metadata resolution runs on the worker. UI acceptance checks request
  serial and recaptured target data. Deadlines reject stale work/results, but
  do not interrupt an OS call already in progress.
- Unload still waits for worker completion and UI drain. This intentionally
  favors avoiding callbacks into unloaded code over bounded unload latency.

## Checks

Passed: Windhawk 1.7.3 compiler build; foreground recovery scenarios;
preview identity/getter/PID checks; asynchronous identity queue/result checks;
dispatcher drain scenarios; 30,240 host-placement cases and contour safety;
git whitespace check.

Still needed in Explorer: first-click Calculator reproduction, ordinary Win32
multi-window previews, positive preview minimum, long flyout dwell, and unload
while recovery could be armed. Keep the independent recorder for this pass.

The patch is small relative to the earlier hosted-window workarounds and keeps
the simplified GetThumbnailWindow design. Do not expand it into permanent
polling merely to make a single reproduction pass.
