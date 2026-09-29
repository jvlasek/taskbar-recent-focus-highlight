# Taskbar badge insertion: static analysis

Date: 2026-09-29. Scope: installed x64 binaries, matching public PDBs,
Microsoft's public XAML source, and existing harness evidence. No debugger
attached, no Explorer changes, no mod changes, and no VM provisioned.

## Finding

There is a concrete append/remove bookkeeping asymmetry in deferred XAML
children. This is a strong candidate explanation for the repeatable one-slot
badge drift. It is more specific than a generic z-order or animation race.
The relevant bookkeeping belongs to Windows.UI.Xaml, not an index that we
have identified in TaskListButton itself.

The installed binary confirms the asymmetry. We have NOT observed the live
badge proxy's saved index or proven that this is the sole cause of the harness
failure. A proposed insertion change remains an experiment, not a verified fix.

## Exact binaries

| Binary | File version | PDB signature (GUID + age) |
|---|---|---|
| Taskbar.View.dll | 2607.28001.200.0 | 1A545A37EB1E419AB835E94A8F18F0771 |
| Windows.UI.Xaml.dll | 10.0.26100.8972 | 2A7B381FC8C25B7402BAD032E0603FB31 |

Taskbar path: `C:\Windows\SystemApps\MicrosoftWindows.Client.Core_cw5n1h2txyewy\Taskbar.View.dll`.
XAML path: `C:\Windows\System32\Windows.UI.Xaml.dll`.

SHA-256:

- Taskbar.View: `22ABD1F0BB23C89679676499473B1504F53A7786F2824461C57BA7348495550B`
- Windows.UI.Xaml: `16302BFE92CD6D1CDAE9666767E53323FA11E10AB1C751D8142E6F95F3A82899`

DbgHelp enumerated the matching symbols without invading a process. GNU
objdump disassembled selected functions; x64 exception-directory entries
supplied function extents where public symbols had no size. This is annotated
assembly analysis, not recovery of original Explorer source. Optimizer-folded
functions have multiple symbol aliases: annotations alone are not proof of the
semantic name of a particular call. Specific conclusions below also use the
surrounding instructions and/or public source.

## Taskbar side: create lazily, unload through XAML

`TaskListButton::EnsureOverlayIconLoaded` (RVA `0x3f150`) looks up the literal
`OverlayIcon` through `GetTemplateChildT<Image>` and stores the resulting image
reference at implementation-object offset `+0x240`. The UTF-16 literal was
verified at RVA `0x824fa8`. The function also checks other stored references
and applies the source/size. It does not itself calculate a Children index.

`TaskListButton::OverlayIcon` (RVA `0x1f3254`) has feature-dependent branches,
composition animation batches and completion callbacks. At least two overlay
cleanup callback bodies call `XamlMarkupHelper::UnloadObject`:

- callback at RVA `0x6c9a00`, call at `0x6c9ace`;
- dispatched callback at RVA `0x6cbd88`, call at `0x6cbe40`.

These inspect the current image reference and a captured image; the dispatched
path additionally checks IsLoaded. This supports delayed cleanup after
animation, not an assumption that clearing the badge synchronously removes it.
`HideBadge` also uses UnloadObject, but that separate badge path should not be
confused with the Win32 OverlayIcon path above.

## XAML side: hidden deferred-element state

A deferred element has a proxy even when its actual image is absent. Public
source describes saved template/runtime context, a weak realized-element
reference, an owning storage reference, and a saved index. Its storage owns an
ordered proxy list, parent/property identity, and a suspension flag.

The saved index counts deferred slots as well as live children. On realization,
XAML derives the physical insertion position by subtracting preceding proxies
that are not currently realized. Therefore an identical exported Children list
need not imply identical future insertion behavior.

On this installed build, assembly confirms:

| Function | RVA | Observed behavior |
|---|---|---|
| CDeferredElementStorage::ElementInserted | `0x6323e0` | Unless suspended, increments affected saved indices when insertion index <= saved index |
| CDeferredElementStorage::ElementRemoved | `0x6829e0` | Unless suspended, decrements affected saved indices when removal index < saved index |
| CalculateRealizedElementInsertionIndex | `0x5fc86c` | Reads saved index, subtracts preceding unrealized proxies |
| CDeferredElement::InsertRealizedElement | `0x35812c` | Uses deferred realization machinery; public source suspends index notifications during its own modifications |

In the first three functions, the saved proxy index is accessed at `+0xa8`;
the storage suspension flag is at `+0x32`. These are analysis landmarks for
this exact DLL only, NOT offsets to ship in a mod.

## The append/remove asymmetry

Installed `CUIElementCollection::Append` at RVA `0x24c2d0` calls the base append
path and handles layout/render invalidation. It does not perform the deferred
storage's insertion callback seen in `Insert`.

`CUIElementCollection::Insert` at RVA `0x358810` accesses the collection-change
callback and invokes its insertion slot at `0x358928` with the insertion index.
Public source names this callback `ElementInserted`.

`RemoveAt` at RVA `0x19fd40` routes through `Remove` at `0xb808`. That function
obtains the collection-change callback and invokes its removal slot. The
optimized path has a direct call to `CDeferredElementStorage::ElementRemoved`
at `0xb8ef` (with an indirect fallback).

Thus adding our host with Append and removing it with RemoveAt need not be
neutral to deferred proxy indices. A physical removal near the end can still
compare below a saved index which includes absent deferred slots. XAML's own
realize/defer operations suspend this adjustment; our ordinary child edits do
not use that special scope.

## How this fits the recorded failure

Capture `20260929-123416-338747` retained the same four button references over
mod lifetimes. Before each unload the absent-badge A list included Icon at 4,
DefaultIcon at 5, and collapsed glow at 6. After each unload the glow was gone.
The first subsequent badge insertion was at 5; after the second lifetime it
was at 4, moving Icon to 5. Native ZIndex values were untouched.

Illustrative arithmetic, NOT measured live proxy values:

- Saved badge index 8, with two preceding unrealized proxies: insertion at 6.
- An unbalanced external removal at physical index 6 decrements saved 8 to 7:
  subsequent insertion becomes 5.
- Repeating the removal decrements 7 to 6: insertion becomes 4.

This reproduces the observed direction and one-slot-per-lifetime pattern.
Actual proxy counts/indices must be traced or validated through a minimal
reproducer before claiming a fully demonstrated root cause. Retention avoids
rank-time collection churn but leaves the append/remove imbalance at unload,
which explains why a single unload test passed while repeated lifetimes failed.

## Narrow next experiment

On a fresh Explorer session, replace ONLY creation-time
`Children().Append(host)` with `Children().InsertAt(Children().Size(), host)`.
Keep the .40 host reuse, host-only ZIndex and cleanup behavior unchanged.
This places the host at the same physical end position, without moving native
children, but should exercise the insertion notification path. No private hooks
or offsets are required for the experiment.

Run the repeated lifecycle harness with badge-absent boundaries first, then
badge-present boundaries. Start fresh so old proxy drift cannot contaminate the
result. If this passes, separately test ordinary repeated removal/recreation
in a diagnostic build to distinguish the insertion change from retention.
Do not restore production layering or change multiple mechanisms in that run.
An explicit insertion notification is not a general mathematical guarantee for
every possible deferred collection; timing and proxy layout still matter.

Before a VM, a tiny standalone Windows.UI.Xaml island reproducer could test a
Grid with deferred template children plus external append/remove cycles. This
would isolate framework bookkeeping from the taskbar and other mods.

If results remain ambiguous, use a disposable VM matching these DLL versions.
Trace the relevant collection's Append/Insert/Remove callbacks and the badge
proxy's saved index/realization state across two lifetimes. Filter by owning
collection/proxy to avoid overwhelming unrelated XAML traffic. Breakpoints
should log and continue where possible; pauses can alter animation timing.

## Public source references

Compared with microsoft/microsoft-ui-xaml revision
`38097125da83c6f47e8150b6420ee74ca55938c5` (WinUI source, not a claim that this
revision built the installed Windows.UI.Xaml DLL):

- [DeferredElement.cpp](https://github.com/microsoft/microsoft-ui-xaml/blob/38097125da83c6f47e8150b6420ee74ca55938c5/dxaml/xcp/core/core/elements/DeferredElement.cpp): realization, suspended updates, saved-index adjustments.
- [DeferredElement.h](https://github.com/microsoft/microsoft-ui-xaml/blob/38097125da83c6f47e8150b6420ee74ca55938c5/dxaml/xcp/core/inc/DeferredElement.h): proxy and storage fields.
- [UIElementCollection.cpp](https://github.com/microsoft/microsoft-ui-xaml/blob/38097125da83c6f47e8150b6420ee74ca55938c5/dxaml/xcp/components/Collection/UIElementCollection.cpp): distinct Append, Insert and removal paths.
- [DOCollection.cpp](https://github.com/microsoft/microsoft-ui-xaml/blob/38097125da83c6f47e8150b6420ee74ca55938c5/dxaml/xcp/components/Collection/DOCollection.cpp): base append path.

Working evidence is under ignored `tmp/badge-analysis/`: symbol enumeration
scripts/JSON, selected annotated disassemblies, downloaded matching PDBs,
public source copies, and `index-model.py`. The model only illustrates the
arithmetic; it is not an end-to-end regression test. No Windows binaries or
PDBs should be committed or bundled with the mod.
