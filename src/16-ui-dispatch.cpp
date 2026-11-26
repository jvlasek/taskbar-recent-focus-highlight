// TryRunAsync returns IAsyncOperation<bool> (GetResults = queued). Do not
// static_cast the operation object to bool (always true).
bool DispatcherOpWasQueued(
    winrt::Windows::Foundation::IAsyncOperation<bool> const& op) {
    if (!op) {
        return false;
    }
    if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Completed) {
        try {
            return op.GetResults();
        } catch (...) {
            return false;
        }
    }
    if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Error ||
        op.Status() == winrt::Windows::Foundation::AsyncStatus::Canceled) {
        return false;
    }
    return true;
}

bool DispatcherTryRun(
    winrt::Windows::UI::Core::CoreDispatcher const& dispatcher,
    winrt::Windows::UI::Core::CoreDispatcherPriority prio,
    winrt::Windows::UI::Core::DispatchedHandler const& handler) {
    try {
        return DispatcherOpWasQueued(dispatcher.TryRunAsync(prio, handler));
    } catch (...) {
        return false;
    }
}

// Unload handshake: Completed signals the waiter. The Low callback must not.
// Completed can be set only once; a failure here does not mean the op is idle.
template <typename Op>
bool SubscribeDrainCompleted(Op const& op, HANDLE done) {
    if (!op || !done) {
        return false;
    }
    try {
        op.Completed([done](auto&&, auto&&) { SetEvent(done); });
        return true;
    } catch (...) {
        return false;
    }
}

// Completion and thread termination are different safe boundaries. A failed
// API call, rejected post, or elapsed interval is neither one.
enum class DispatcherOpEnd { Ran, NotRun, ThreadExited };

bool UiThreadExited(const UiDispatcher& ui) {
    return WaitForSingleObject(ui.thread.get(), 0) == WAIT_OBJECT_0;
}

void PauseDispatcherAttempt(const UiDispatcher& ui) {
    if (GetCurrentThreadId() == ui.threadId) {
        try {
            ui.dispatcher.ProcessEvents(
                winrt::Windows::UI::Core::CoreProcessEventsOption::
                    ProcessOneIfPresent);
        } catch (...) {
            // A disconnected object is not proof its thread has terminated.
        }
    }
    // Also wakes immediately if the original owning thread exits.
    if (WaitForSingleObject(ui.thread.get(), 20) == WAIT_FAILED) {
        Sleep(20);  // Wait failure must not authorize unload or busy-spin.
    }
}

// Keep the operation until its result is known or its owning thread exits.
// Completed is the barrier, not a signal from inside the dispatched callback.
// If event creation/subscription fails, poll the same operation instead.
template <typename Op>
DispatcherOpEnd ObserveDispatcherOp(const UiDispatcher& ui, Op const& op,
                                   HANDLE done) {
    if (!op) {
        return DispatcherOpEnd::NotRun;
    }
    if (done) {
        ResetEvent(done);
    }
    const bool subscribed = done && SubscribeDrainCompleted(op, done);
    if (subscribed) {
        HANDLE waits[] = {done, ui.thread.get()};
        for (;;) {
            const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0 + 1) {
                return DispatcherOpEnd::ThreadExited;
            }
            if (result == WAIT_OBJECT_0) {
                break;
            }
            // Do not close the event while its completion delegate can run.
            PauseDispatcherAttempt(ui);
        }
    }
    for (;;) {
        if (UiThreadExited(ui)) {
            return DispatcherOpEnd::ThreadExited;
        }
        try {
            const auto status = op.Status();
            if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
                return op.GetResults() ? DispatcherOpEnd::Ran
                                       : DispatcherOpEnd::NotRun;
            }
            if (status == winrt::Windows::Foundation::AsyncStatus::Error ||
                status == winrt::Windows::Foundation::AsyncStatus::Canceled) {
                return DispatcherOpEnd::NotRun;
            }
        } catch (...) {
            // Retain this operation: inability to observe it is not completion.
        }
        PauseDispatcherAttempt(ui);
    }
}

std::vector<std::shared_ptr<UiDispatcher>> CollectUiDispatchers() {
    std::lock_guard<std::mutex> lock(g_dispatchersMutex);
    if (!g_uiDispatchers) {
        return {};
    }
    return *g_uiDispatchers;
}

enum class DrainProof { SentinelRan, ThreadExited };

BOOL CALLBACK TakeFirstThreadWindow(HWND hwnd, LPARAM out) {
    *reinterpret_cast<HWND*>(out) = hwnd;
    return FALSE;
}

// The sentinel ran, but its thread is still on its way out of mod code: the
// Completed delegate calls SetEvent (which wakes the waiter) before it
// returns, and the delegate and the sentinel are then released, all code in
// this module. A message sent to a window of that thread is handled only
// back in its message loop, after all of that. Unbounded like the rest of
// the drain, but ends if the thread exits.
void WaitUntilUiThreadLeftModCode(const UiDispatcher& ui) {
    if (GetCurrentThreadId() == ui.threadId) {
        return;  // drained on this thread: nothing of it is still running
    }
    bool loggedNoWindow = false;
    for (;;) {
        if (UiThreadExited(ui)) {
            return;
        }
        HWND hwnd = nullptr;
        EnumThreadWindows(ui.threadId, TakeFirstThreadWindow,
                          reinterpret_cast<LPARAM>(&hwnd));
        if (!hwnd) {
            // A XAML UI thread owns windows. If none is found, there is no
            // barrier to send; the brief wait below is all that is left.
            if (!loggedNoWindow) {
                Wh_Log(L"UI drain: thread %u has no window for the return "
                       L"barrier", ui.threadId);
                loggedNoWindow = true;
            }
            PauseDispatcherAttempt(ui);
            return;
        }
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_NORMAL, 100,
                                &result)) {
            return;
        }
        // Timed out (thread busy) or the window just went away: try again.
    }
}

DrainProof DrainOneUiDispatcher(
    const UiDispatcher& ui,
    const winrt::Windows::UI::Core::DispatchedHandler& handler,
    HANDLE done) {
    using AsyncOp = winrt::Windows::Foundation::IAsyncOperation<bool>;
    using Pri = winrt::Windows::UI::Core::CoreDispatcherPriority;
    // Never wait for a dispatcher callback on its own thread without pumping.
    if (GetCurrentThreadId() == ui.threadId) {
        done = nullptr;
    }
    bool highRan = false;
    bool loggedRejection = false;
    for (;;) {
        if (UiThreadExited(ui)) {
            return DrainProof::ThreadExited;
        }
        AsyncOp op{nullptr};
        try {
            op = highRan ? ui.dispatcher.TryRunAsync(Pri::Low, []() {})
                         : ui.dispatcher.TryRunAsync(Pri::High, handler);
        } catch (...) {
            // Includes disconnected HRESULTs: wait for actual thread exit.
        }
        const auto end = ObserveDispatcherOp(ui, op, done);
        if (end == DispatcherOpEnd::ThreadExited) {
            return DrainProof::ThreadExited;
        }
        if (end == DispatcherOpEnd::Ran) {
            if (highRan) {
                WaitUntilUiThreadLeftModCode(ui);
                return DrainProof::SentinelRan;
            }
            highRan = true;
            continue;
        }
        if (!loggedRejection) {
            Wh_Log(L"UI drain: post rejected or failed; waiting for a sentinel "
                   L"or thread %u exit (no unsafe timeout)", ui.threadId);
            loggedRejection = true;
        }
        // A closed dispatcher on a still-live thread may remain here. Keeping
        // the mod loaded is intentional; rejection alone cannot prove safety.
        PauseDispatcherAttempt(ui);
    }
}

// Returns only after each Low sentinel completed or its owning thread exited.
bool RunOnEachUiDispatcherAndWait(
    const winrt::Windows::UI::Core::DispatchedHandler& handler) {
    auto dispatchers = CollectUiDispatchers();
    bool allOk = true;
    for (const auto& ui : dispatchers) {
        winrt::handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (DrainOneUiDispatcher(*ui, handler, done.get()) ==
            DrainProof::ThreadExited) {
            Wh_Log(L"UI drain: original thread %u exited", ui->threadId);
            allOk = false;  // No claim that native visual cleanup ran.
        }
    }
    return allOk;
}

bool RunOnUiThread(const winrt::Windows::UI::Core::DispatchedHandler& handler) {
    if (g_unloading.load()) {
        return false;
    }
    auto dispatchers = CollectUiDispatchers();

    if (dispatchers.empty()) {
        // App + preview apply both hit this before the first button hook.
        if (!g_loggedNoDispatcherAnchor.exchange(true)) {
            Wh_Log(L"RunOnUiThread: no dispatcher anchor yet (no buttons seen) "
                   L"— will apply on first TaskListButton");
        }
        return false;
    }

    bool any = false;
    for (const auto& ui : dispatchers) {
        if (UiThreadExited(*ui)) {
            continue;
        }
        const auto& dispatcher = ui->dispatcher;
        try {
            if (dispatcher.HasThreadAccess()) {
                handler();
                any = true;
                continue;
            }
            if (DispatcherTryRun(
                    dispatcher,
                    winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                    handler)) {
                any = true;
            }
        } catch (...) {
            HRESULT hr = winrt::to_hresult();
            Wh_Log(L"RunOnUiThread error %08X", hr);
        }
    }
    return any;
}


// One queued full pass per UI thread: worker requests (RequestApplyVisuals)
// and UI requests (ScheduleRefreshAllHighlights) coalesce.
// The flag is cleared when the pass starts, so a request made during a pass
// queues the next one.
std::mutex g_fullPassQueuedMutex;
std::unordered_set<DWORD> g_fullPassQueuedThreads;

void ClearFullPassQueued(DWORD threadId) {
    std::lock_guard<std::mutex> lock(g_fullPassQueuedMutex);
    g_fullPassQueuedThreads.erase(threadId);
}

// True if a pass is queued (now or already) on that thread.
bool QueueFullPass(winrt::Windows::UI::Core::CoreDispatcher const& dispatcher,
                   DWORD threadId,
                   winrt::Windows::UI::Core::CoreDispatcherPriority prio) {
    {
        std::lock_guard<std::mutex> lock(g_fullPassQueuedMutex);
        if (!g_fullPassQueuedThreads.insert(threadId).second) {
            return true;
        }
    }
    bool ok = false;
    try {
        ok = DispatcherTryRun(dispatcher, prio, []() {
            ClearFullPassQueued(GetCurrentThreadId());
            try {
                if (!g_unloading.load()) {
                    ApplyAllHighlights_UIThread();
                }
            } catch (...) {
            }
        });
    } catch (...) {
    }
    if (!ok) {
        ClearFullPassQueued(threadId);
    }
    return ok;
}

void RequestApplyVisuals() {
    if (g_unloading.load()) {
        return;
    }
    bool any = false;
    for (const auto& ui : CollectUiDispatchers()) {
        if (UiThreadExited(*ui)) {
            continue;
        }
        try {
            if (ui->dispatcher.HasThreadAccess()) {
                ApplyAllHighlights_UIThread();
                any = true;
            } else if (QueueFullPass(
                           ui->dispatcher, ui->threadId,
                           winrt::Windows::UI::Core::CoreDispatcherPriority::
                               Normal)) {
                any = true;
            }
        } catch (...) {
            HRESULT hr = winrt::to_hresult();
            Wh_Log(L"RequestApplyVisuals error %08X", hr);
        }
    }
    if (!any) {
        // Buttons not tracked yet — will apply on next UpdateVisualStates.
        std::lock_guard<std::mutex> lock(g_stateMutex);
        const auto& ranks = CurrentDeskLocked().rankedApps;
        Wh_Log(L"Ranks ready (%zu) — waiting for TaskListButton hooks",
               ranks.size());
        for (size_t i = 0; i < ranks.size(); i++) {
            Wh_Log(L"  Rank %zu: %s", i + 1, ranks[i].displayName.c_str());
        }
    }
}

void RequestApplyVisualsDebounced() {
    if (g_unloading.load()) {
        return;
    }
    PostToHookThread(WM_APP_REQUEST_APPLY_DEBOUNCED);
}

