# Thumbnail identity: installed taskbar.dll investigation

Date: 2026-10-02. Static analysis only: installed taskbar.dll, matching cached
public PDB, DbgHelp symbol enumeration, and GNU objdump. No debugger attached,
no hooks installed, and no mod source changed. Mod remains 0.10.9.

## Confirmed in this binary

- CImmersiveTaskItem::GetAppWindow at RVA 0x135df0 adjusts its interface this
  pointer by -0x100 and calls _GetWindow(false).
- CImmersiveTaskItem::GetWindow and GetThumbnailWindow share RVA 0x703d0,
  adjust their ITaskItem this pointer by -0x10, and call _GetWindow(true).
- _GetWindow at RVA 0x19720 obtains an interface from the object at +0x110,
  passing a mode of 0 or 4 respectively and IID
  c6636ec2-eba1-4e6d-a995-8fa14b8b2891. On successful acquisition it invokes
  that interface's slot +0x20 with an HWND output pointer. Failures can leave
  that output null. A separate flag-controlled fallback exists. The meaning
  of mode 4 and the interface's full contract are not established here.
- Thus our GetAppWindow lookup and Explorer's thumbnail-window getter are
  distinct queries. It is not established that the null recorded by our mod
  means every Explorer lookup would also return null.
- SetWindow at RVA 0x1371b0 simply returns 0x8007139f. It is not a useful
  setter/update notification on this build. Do not infer behavior from its name.
- TaskItemThumbnail::UpdateThumbnailFactory at RVA 0x160df0 invokes the stored
  task item's vtable slot +0x78. The immersive ITaskItem table at RVA 0x1bbe90
  has GetThumbnailWindow/GetWindow at that slot. Explorer therefore uses the
  true/mode-4 route in this path, unlike our current GetAppWindow route.
- CTaskBand::_HandlePresentedWindowChanged exists at RVA 0x153b4. It calls
  _MatchApp, receives two reference-counted objects, notifies through another
  interface with those objects, then releases them. This is a real candidate
  update path, but its precise interface contracts and runtime timing have not
  been verified. A hook after it returns cannot use released local objects.

## Implications

First compare GetAppWindow with GetThumbnailWindow on a task item whose lifetime
is guaranteed by an active Explorer call (for example the existing constructor
hook). Log which path failed, including the vtable conversion, rather than
assuming a licensing delay. A direct symbol-resolved immersive getter must
receive the matching ITaskItem interface pointer; the GetAppWindow pointer is
not interchangeable with it.

If the thumbnail getter supplies the frame/usable handle during the failing
sequence, it could avoid both the initial-null snapshot and some parent-chain
workarounds. That is a hypothesis for live validation, not a demonstrated fix.
If both getters return null, investigate the presented-window change path or
observe subsequent native getter calls, using only live references and exact
object identity. Do not dereference saved raw pointers on a timer, introduce
name/PID-only matching, or hardcode any of these RVAs/offsets in the mod.

## Evidence

Local scratch scripts and annotated disassembly are in tmp/thumbnail-analysis/.
Public symbols can have folded aliases; conclusions above use instructions and
vtable entries together. Leaf-function dumps include padding/neighboring bytes;
interpret only instructions up to their return/tail jump.

The manual-20261002-025423-141833 recording shows four successful hosted links
but four thumbnail constructor snapshots with rawHwnd=0. Later flyout GetAt
successfully found those same models and their cached zeros. This static pass
explains why another getter is worth comparing; it does not prove the runtime
reason for those zeros.

Installed taskbar.dll SHA-256: 5DCEEA036939ACB4E43B801A7A63530F328EDAD164741E6529D3D32E9B1C5866.

## Live comparison and implementation follow-up

The 0.10.10 recordings manual-20261002-095646-169397 and
manual-20261002-095741-875862 show GetAppWindow returning null during model
construction while GetThumbnailWindow returns a live ApplicationFrameWindow.
In the latter run the returned handles 0860100, 09220AA and 14A0198 exactly
match recorded Calculator focus confirmations. Later flyout lookup fails
because the diagnostic build still caches the app getter's null result.

0.10.11 uses GetThumbnailWindow only for immersive thumbnail capture and removes
the 0.10.7–0.10.10 normalization/cache/polling workarounds and temporary flyout
diagnostics. App identity remains on GetAppWindow. Controlled getter/PID tests
and the Windhawk 1.7.3 build pass; live validation of 0.10.11 remains outstanding.
