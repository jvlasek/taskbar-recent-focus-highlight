# Review response — 0.9.46

The notification-badge regression was reproduced with a four-app harness:
repeated highlight removal/recreation placed OverlayIcon behind Icon on the
second cycle, and retaining the highlight only delayed the failure until the
second disable/re-enable lifetime.

Static inspection of matching Taskbar.View/Windows.UI.Xaml binaries found
that overlay badges use deferred XAML loading. The collection Append path
omits the deferred-index insertion notification while RemoveAt sends the
removal notification. Explicit InsertAt at the end takes the notification
path. This explains how a visibly restored tree can retain a shifted future
insertion position.

The isolated change from Append to InsertAt(end), retaining removal on
unhighlight, passed five badge cycles and three enabled/disabled pairs for
each badge-present and badge-absent unload variant. No native badge repair,
retained overlay, private-offset manipulation or additional hooks are needed.

0.9.45 keeps native-contour Frame/Full and removes the Edge style,
with guarded geometry reads. It uses style-specific placement by moving only the owned glow host,
using explicit InsertAt everywhere in that path. Native children keep their
relative order and properties. Cleanup restores owned icon scaling and removes
the glow; existing worker shutdown, handler revocation and dispatcher drain
remain unchanged.

Automated validation: 30,240 extracted-planner placement cases, invalid-geometry/radius tests,
and a Windhawk 1.7.3 compiler build. Existing IPC/collector/lifecycle tests
passed during the previous pass; the harness has not changed.

On .45, five enabled badge cycles passed (capture
`20260930-022800-951939`). Manual switching between Side, Frame and Full,
hover, initial positioning and a multi-window flyout showed no issues.
There was an initial settings-switch hiccup around the removed Edge style;
its cause was not established. The same Explorer process has been in use
since .42 without a reported badge recurrence. The badge-present/absent
lifecycle harness results above belong to .41; those variants have not been
rerun on .45. Broader theme and multimonitor coverage is not claimed.

Version .46 extends explicit end insertion to the three remaining thumbnail
panel mutations: overlay creation, bring-to-front and plate ownership marker
creation. This is preventive consistency; no deferred-child failure was
reproduced in these flyout panels. Build and automated checks passed; live
preview validation of .46 is still outstanding.
