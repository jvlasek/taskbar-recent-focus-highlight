# Draft — complete 0.9.44 live validation before posting

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

0.9.44 adds native-contour Frame/Full and a surround behind the running pill,
with guarded geometry reads. It uses style-specific placement by moving only the owned glow host,
using explicit InsertAt everywhere in that path. Native children keep their
relative order and properties. Cleanup restores owned icon scaling and removes
the glow; existing worker shutdown, handler revocation and dispatcher drain
remain unchanged.

Automated validation: 40,320 extracted-planner placement cases, invalid-geometry/radius tests,
and a Windhawk 1.7.3 compiler build. Existing IPC/collector/lifecycle tests
passed during the previous pass; the harness has not changed. These do not prove
live rendering. FINAL .44 VISUAL AND LIFECYCLE RESULTS: PENDING — fill in after
`tests/badge-final-validation.md` is completed. .41 results are not .44 results.
