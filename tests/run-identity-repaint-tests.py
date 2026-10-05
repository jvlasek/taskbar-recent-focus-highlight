"""Exercise actual per-dispatcher completion scheduling with controlled queues."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / 'taskbar-recent-focus-highlight.wh.cpp').read_text(encoding='utf-8')
start = source.index('bool QueueIdentityCompletionApply(')
end = source.index('void RequestApplyVisuals() {', start)
code = source[start:end]
fixture = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
namespace winrt::Windows::UI::Core {enum class CoreDispatcherPriority {Normal};}
struct Dispatcher {
    bool accept=true, throws=false;
    std::vector<std::function<void()>> queued;
};
struct UiDispatcher {
    Dispatcher dispatcher;
    bool exited=false;
    std::atomic<bool> identityApplyQueued{false};
};
std::atomic<bool> g_unloading=false;
std::vector<std::shared_ptr<UiDispatcher>> all;
auto CollectUiDispatchers() {return all;}
bool UiThreadExited(const UiDispatcher& ui) {return ui.exited;}
template<class F> bool DispatcherTryRun(Dispatcher& d,winrt::Windows::UI::Core::CoreDispatcherPriority,F f) {
    if(d.throws) throw std::runtime_error("rejected");
    if(!d.accept) return false;
    d.queued.emplace_back(f); return true;
}
int paints=0;
std::function<void()> duringPaint;
void ApplyAllHighlights_UIThread(bool refresh) {
    assert(!refresh); ++paints; if(duringPaint) duringPaint();
}
void Wh_Log(const wchar_t*,...) {}
// PRODUCTION
void run(std::shared_ptr<UiDispatcher> ui) {
    auto f=std::move(ui->dispatcher.queued.front());
    ui->dispatcher.queued.erase(ui->dispatcher.queued.begin()); f();
}
int main() {
    auto a=std::make_shared<UiDispatcher>(), b=std::make_shared<UiDispatcher>();
    all={a,b};
    for(int i=0;i<20;++i) assert(RequestIdentityCompletionApply());
    assert(a->dispatcher.queued.size()==1 && b->dispatcher.queued.size()==1);
    run(a); assert(paints==1);
    RequestIdentityCompletionApply();
    assert(a->dispatcher.queued.size()==1 && b->dispatcher.queued.size()==1);
    run(b); run(a); assert(paints==3);
    a->dispatcher.accept=false; b->exited=true;
    assert(!RequestIdentityCompletionApply() && !a->identityApplyQueued);
    a->dispatcher.accept=true; a->dispatcher.throws=true;
    assert(!RequestIdentityCompletionApply() && !a->identityApplyQueued);
    a->dispatcher.throws=false;
    assert(RequestIdentityCompletionApply());
    duringPaint=[] {assert(RequestIdentityCompletionApply());};
    run(a); duringPaint={};
    assert(a->dispatcher.queued.size()==1); // new result during paint gets a pass
    run(a);
    RequestIdentityCompletionApply(); g_unloading=true;
    int before=paints; run(a); assert(paints==before && !a->identityApplyQueued);
    assert(!RequestIdentityCompletionApply());
}
'''.replace('// PRODUCTION', code)
with tempfile.TemporaryDirectory(prefix='windhawk-repaint-') as folder:
    cpp=Path(folder)/'test.cpp'; exe=Path(folder)/'test.exe'
    cpp.write_text(fixture,encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
print('PASS: 20 completions coalesce per dispatcher, independent queues, failure retry, publication during paint, unload')
