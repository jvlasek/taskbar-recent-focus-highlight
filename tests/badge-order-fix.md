# Badge-order investigation and 0.9.36 candidate fix

The enabled-mod run `20260928-185424-995907` passed cycle 00 and failed cycle 01
when recreating A's badge. Highlight membership was correct throughout. The
user's subsequent control run, with all mods disabled and Explorer restarted,
passed all three cycles.

| Step | Icon index | Badge index | ZIndex (both) |
|---|---:|---:|---:|
| 00-A-badge-recreated | 4 | 5 | 0 |
| 01-A-badge-recreated | 5 | 4 | 0 |

Screenshots confirm the failure. Immediately before recreation, both cycles
had the same native list: divider, unnamed child, background, running indicator,
icon, default icon. The differing insertion is not explained by that visible
pre-insertion order alone.

Source inspection found that the old restore helper physically removed and
reinserted native children to compact its saved sequence past the glow, even
when their relative order was correct. Badge healing also moved OverlayIcon
past DefaultIcon. The first cycle's initial badge dumps show that latter move.

These unnecessary native mutations are the leading suspect for disturbed XAML
realization/insertion state. The precise private bookkeeping mechanism is not
proven by the recordings.

0.9.36 removes native-order snapshots, compaction, and badge healing. Only the
owned WhRecentFocusGlow is moved; native children retain relative order through
paint, cleanup, style changes and relayout. No unranked-button repair was added.
About 200 lines were removed. Existing damaged sessions are not repaired.

Validation: 5,760 cases exercising the extracted production placement helper,
all four styles and pill variants, native order preservation, no native removal,
idempotence and ten dynamic badge cycles per style. The old HEAD fails the same
invariant test. Windhawk 1.7.3 x64 syntax check and full DLL compile/link pass.
These tests do not emulate XAML's internal deferred-element behavior.

Live validation remains: compile/load the updated source in Windhawk, restart
Explorer, reattach UWPSpy and use its new endpoint. Run the enabled harness for
10 cycles, with top/focus settings matching the mod. Also check all highlight
styles and Styler hover-plate coexistence. No modified DLL was injected by the
agent, and the mod/Explorer configuration was left unchanged.


## Follow-up: 0.9.36 failed; 0.9.37 uses ZIndex

Run `20260928-201950-942323` still failed on the second badge recreation.
Its captures confirmed native relative order stayed unchanged during highlight
painting. That rules out native moves as the only necessary trigger; inserting
and removing the glow ahead of native deferred slots remains sufficient in this
reproduction. The exact private XAML mechanism remains unverified.

0.9.37 never repositions the appended glow. Native baseline (ZIndex, child-index)
order is sorted stably, assigned spaced integer values, and the glow is drawn
in the gap appropriate to its style. Save/restore entries live on a collapsed
marker inside the owned glow; its existing scale-transform Tag stays separate.
Cached paints incorporate newly created badges. Detached entries are restored
and released; cleanup restores prior local/unset values before deleting the
owned overlay. A later different native ZIndex is preserved and becomes the
baseline if we take over again. An external same-value write is indistinguishable.

Validation: 14,400 extracted production planner cases, int bounds, stable native
draw order, repeated badge cycles, extracted restore-helper tests (prior/unset/
later owner), full Windhawk 1.7.3 x64 compile and link. No injection performed.
Live confirmation is pending: fresh Explorer, 0.9.37 enabled, ten harness cycles,
then all styles and Styler coexistence. This supersedes the 0.9.36 candidate
above; the historical checks did not certify XAML's internal realization state.
