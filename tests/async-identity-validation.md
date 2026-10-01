# 0.10.3 identity lookup validation

Automated checks passed: extracted production queue/worker/result logic,
coalescing and supersession, stale deadlines, changed groups, recycled button
keys, PID rejection, bounded queue, rejected wakeups and shutdown. Existing
placement/geometry, late-hook/cache, and dispatcher-drain tests also passed.
The Windhawk 1.7.3 compiler build passed. Fixtures do not emulate XAML or prove
OS-call latency. No live Explorer validation has been performed for this version.

Manual checks after recompiling the local source:

1. Run the existing enabled harness for five cycles. Look at screenshots as
   well as its assertions; unknown identities may acquire their highlight
   shortly after resolution completes.
2. Launch a pinned app, close it, and launch it again. Confirm identity resolves
   without requiring a second click. Click repeatedly during launch.
3. Exercise two same-named executables in different folders and a pinned app
   whose executable was moved/replaced. No stale result may highlight the wrong
   button. Changing group membership while resolving should discard old results.
4. Click among a multi-window flyout's cards; verify per-window ranks. Close a
   window while switching focus. Flyout rendering/resolution is unchanged, but
   icon metadata now shares its existing focus worker.
5. Disable/re-enable during activity and run the same-button lifecycle variants.
   A deadline drops late results; it does not cancel a blocking API call or
   authorize unloading while the worker is still executing.

The remaining GetWindowTextLengthW tool-window filter now uses the shared
non-messaging title helper. Missing stored text continues to mean no title.
