// Windows harness. run-dispatcher-tests.py inserts the production helpers.
#include <windows.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>

namespace winrt::Windows::Foundation {
enum class AsyncStatus { Started, Completed, Error, Canceled };
struct State {
    AsyncStatus status = AsyncStatus::Completed;
    bool result = true;
    bool subscriptionFails = false;
    bool statusFails = false;
};
template <typename T> struct IAsyncOperation {
    std::shared_ptr<State> state;
    IAsyncOperation(std::nullptr_t = nullptr) {}
    explicit IAsyncOperation(std::shared_ptr<State> s) : state(s) {}
    explicit operator bool() const { return !!state; }
    AsyncStatus Status() const {
        if (state->statusFails) throw std::runtime_error("disconnected status");
        return state->status;
    }
    bool GetResults() const { return state->result; }
    template <typename F> void Completed(F fn) const {
        if (state->subscriptionFails) throw std::runtime_error("subscription");
        if (state->status != AsyncStatus::Started) fn(*this, state->status);
    }
};
}
namespace winrt::Windows::UI::Core {
enum class CoreDispatcherPriority { High, Low };
enum class CoreProcessEventsOption { ProcessOneIfPresent };
using DispatchedHandler = std::function<void()>;
}
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Core;
struct Handle { HANDLE value; HANDLE get() const { return value; } };
struct FakeDispatcher {
    mutable int high = 0, low = 0, pumps = 0;
    std::function<IAsyncOperation<bool>(CoreDispatcherPriority)> post;
    std::function<void()> pump;
    IAsyncOperation<bool> TryRunAsync(CoreDispatcherPriority p,
                                     const DispatchedHandler& fn) const {
        (p == CoreDispatcherPriority::High ? high : low)++;
        auto op = post(p);
        if (op && op.state->status == AsyncStatus::Completed && op.state->result) fn();
        return op;
    }
    void ProcessEvents(CoreProcessEventsOption) const {
        ++pumps;
        if (pump) pump();
    }
};
struct UiDispatcher { FakeDispatcher dispatcher; Handle thread; DWORD threadId; };
template <typename... T> void Wh_Log(const wchar_t*, T...) {}

// PRODUCTION_HELPERS

struct Owner {
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0, [](void* p)->DWORD {
        WaitForSingleObject(static_cast<Owner*>(p)->stop, INFINITE);
        return 0;
    }, this, 0, &id);
    ~Owner() { SetEvent(stop); WaitForSingleObject(thread, INFINITE);
               CloseHandle(thread); CloseHandle(stop); }
};
IAsyncOperation<bool> result(bool ran = true) {
    auto s = std::make_shared<State>(); s->result = ran;
    return IAsyncOperation<bool>{s};
}
int main() {
    Owner owner;
    UiDispatcher ui{{}, {owner.thread}, owner.id};
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    int cleanups = 0;
    ui.dispatcher.post = [](auto) { return result(); };
    assert(DrainOneUiDispatcher(ui, [&]{ ++cleanups; }, done) == DrainProof::SentinelRan);
    assert(cleanups == 1 && ui.dispatcher.high == 1 && ui.dispatcher.low == 1);

    // Rejection must not repeat a completed High cleanup or count as a drain.
    int lowAttempts = 0;
    ui.dispatcher.post = [&](auto p) {
        return result(p == CoreDispatcherPriority::High || ++lowAttempts == 3);
    };
    assert(DrainOneUiDispatcher(ui, [&]{ ++cleanups; }, done) == DrainProof::SentinelRan);
    assert(lowAttempts == 3 && cleanups == 2);

    // A disconnected/failed post is retried, never treated as thread exit.
    int posts = 0;
    ui.dispatcher.post = [&](auto) {
        if (++posts == 1) throw std::runtime_error("closed dispatcher");
        return result();
    };
    assert(DrainOneUiDispatcher(ui, [&]{ ++cleanups; }, done) == DrainProof::SentinelRan);
    assert(posts == 3 && cleanups == 3);

    // Event/subscription fallback and canceled/error observations.
    auto op = result(); op.state->subscriptionFails = true;
    assert(ObserveDispatcherOp(ui, op, done) == DispatcherOpEnd::Ran);
    assert(ObserveDispatcherOp(ui, op, nullptr) == DispatcherOpEnd::Ran);
    op.state->status = AsyncStatus::Canceled;
    assert(ObserveDispatcherOp(ui, op, nullptr) == DispatcherOpEnd::NotRun);
    op.state->status = AsyncStatus::Error;
    assert(ObserveDispatcherOp(ui, op, nullptr) == DispatcherOpEnd::NotRun);

    // Persistent rejection while the owner is live MUST keep the image loaded.
    ui.dispatcher.post = [](auto) { return result(false); };
    auto rejected = std::async(std::launch::async, [&] {
        return DrainOneUiDispatcher(ui, []{}, done);
    });
    assert(rejected.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
    SetEvent(owner.stop);
    assert(rejected.get() == DrainProof::ThreadExited);

    // A subscribed operation that never signals completion still wakes on exit.
    {
        Owner pendingOwner;
        UiDispatcher pendingUi{{}, {pendingOwner.thread}, pendingOwner.id};
        auto pending = result(); pending.state->status = AsyncStatus::Started;
        auto wait = std::async(std::launch::async, [&] {
            return ObserveDispatcherOp(pendingUi, pending, done);
        });
        assert(wait.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
        SetEvent(pendingOwner.stop);
        assert(wait.get() == DispatcherOpEnd::ThreadExited);
    }
    // Exceptions reading a still-owned operation cannot authorize early exit.
    {
        Owner failingOwner;
        UiDispatcher failingUi{{}, {failingOwner.thread}, failingOwner.id};
        auto failing = result(); failing.state->statusFails = true;
        auto wait = std::async(std::launch::async, [&] {
            return ObserveDispatcherOp(failingUi, failing, nullptr);
        });
        assert(wait.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
        SetEvent(failingOwner.stop);
        assert(wait.get() == DispatcherOpEnd::ThreadExited);
    }
    // On-owner polling pumps rather than blocking its own dispatcher.
    HANDLE current = nullptr;
    assert(DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                           &current, SYNCHRONIZE, FALSE, 0));
    UiDispatcher same{{}, {current}, GetCurrentThreadId()};
    auto pending = result(); pending.state->status = AsyncStatus::Started;
    same.dispatcher.pump = [&] { pending.state->status = AsyncStatus::Completed; };
    assert(ObserveDispatcherOp(same, pending, nullptr) == DispatcherOpEnd::Ran);
    assert(same.dispatcher.pumps == 1);
    CloseHandle(current);
    CloseHandle(done);
    std::puts("Dispatcher drain scenarios passed");
}
