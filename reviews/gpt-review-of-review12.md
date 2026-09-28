My opinion: \*\*this is a useful, mostly fair review—but its suggested unload fix is incomplete.\*\* I agree with the main diagnosis more than with the proposed remedy. \[Claude’s review](https://github.com/ramensoftware/windhawk-mods/pull/5331#issuecomment-5781605078)



\*\*1. The unload finding is real.\*\*  

I checked the current source: if cleanup is queued successfully but creating or subscribing to the Low sentinel fails, the code signals completion and proceeds with unloading. Logging the failure doesn’t make the pending callbacks safe. This makes concrete the failure-path concern carried forward in our previous reviews.



\*\*2. Don’t copy Claude’s fallback as written.\*\*  

Keeping the High operation is useful, but waiting for it proves only that the cleanup callback finished. \*\*It does not prove previously queued Normal/Low callbacks have drained\*\*—High work can execute ahead of them. Those callbacks still need loaded mod code even to check `g\_unloading` and return. That follows directly from Microsoft’s \[dispatcher priority semantics](https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.coredispatcherpriority?view=winrt-26100).



There’s also a logical error in the proposed fallback: \*\*failure to subscribe to both operations does not mean neither operation exists or has pending work.\*\* Subscription failure is precisely one of the cases being handled. Its final immediate signal therefore retains an unsafe escape path.



The requirement should be: establish that cleanup and all outstanding mod callbacks are finished—not merely find another operation capable of signaling an event.



\*\*3. The size criticism is reasonable as a maintenance concern, not a demand to remove features.\*\*  

Eight thousand lines inside Explorer deserve scrutiny. But line count alone doesn’t distinguish necessary compatibility work from redundant machinery. App highlighting and preview highlighting are both core requirements here. Simplification should preserve both.



A single-dispatcher design might reduce bookkeeping, but \*\*it would not automatically solve the unload problem\*\*. One dispatcher can still contain outstanding callbacks. I agree with leaving that architectural decision to the human reviewer rather than undertaking another speculative rewrite now.



\*\*4. Most optional comments are sensible.\*\*  

The initialization reference leak is worth fixing despite being uncommon. Removing the discarded result, redundant synchronization, and repository-specific stash reference is straightforward cleanup. The DIP label correction is accurate.



The proposed separation of UI identity discovery from process/property lookups is also technically sensible. The earlier response that this necessarily requires off-thread XAML access was too broad: obtaining HWNDs on the UI thread and resolving their metadata elsewhere is possible, provided stale results are rejected.



\*\*5. I wouldn’t endorse “the remaining callback tail cannot be fixed inside a mod” as an established fact.\*\*  

That’s a strong architectural claim without a demonstrated lifetime argument. It shouldn’t become permission to ignore known outstanding callbacks, nor a reason to restart endless speculative repairs.



I’d address the concrete unload failure with a complete lifetime argument and targeted failure tests, take the inexpensive cleanup items, and then seek human review. This round is constructive progress; its proposed patch still needs independent review.

