#include <windows.h>
#include <winrt/base.h>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <future>
#include "ipc.h"
#pragma comment(lib,"windowsapp.lib")
struct Context { std::shared_ptr<ipc::Server> server; std::wstring tree; };
LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto context=reinterpret_cast<Context*>(GetWindowLongPtr(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){context=static_cast<Context*>(reinterpret_cast<CREATESTRUCT*>(l)->lpCreateParams);SetWindowLongPtr(hwnd,GWLP_USERDATA,(LONG_PTR)context);}
    if(context && msg==ipc::Server::Message){
        auto r=context->server->Take(context->tree);if(!r)return 0;
        try {
            auto input=ipc::json::JsonObject::Parse(r->input);
            auto op=input.GetNamedString(L"op");
            ipc::json::JsonObject result;
            if(op==L"publish"){
                for(int i=0;i<300;++i){ipc::json::JsonObject e;ipc::String(e,L"kind",L"snapshot");context->server->Publish(context->tree,e);}
            }else if(op==L"delay") Sleep(16000);
            result.Insert(L"tid",ipc::json::JsonValue::CreateNumberValue(GetCurrentThreadId()));
            ipc::String(result,L"echo",input.GetNamedString(L"value",L""));r->output=result.Stringify();
        }catch(...){r->output=L"{\"error\":\"fixture command failed\"}";}
        SetEvent(r->done.get());return 0;
    }
    if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
    if(msg==WM_DESTROY){context->server->Unregister(context->tree);PostQuitMessage(0);return 0;}
    return DefWindowProc(hwnd,msg,w,l);
}
void Pump(std::shared_ptr<ipc::Server> server,std::promise<HWND>& ready) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    Context context{server,{}};
    HWND hwnd=CreateWindow(L"UWPSpyIpcFixture",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandle(nullptr),&context);
    context.tree=server->Register(hwnd,GetCurrentThreadId());ready.set_value(hwnd);
    MSG msg;while(GetMessage(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}
    winrt::uninit_apartment();
}
int main(){
    winrt::init_apartment();
    WNDCLASS cls{};cls.lpfnWndProc=Proc;cls.lpszClassName=L"UWPSpyIpcFixture";cls.hInstance=GetModuleHandle(nullptr);RegisterClass(&cls);
    auto server=ipc::Server::Acquire();
    std::promise<HWND> p1,p2;auto f1=p1.get_future(),f2=p2.get_future();
    std::thread t1(Pump,server,std::ref(p1)),t2(Pump,server,std::ref(p2));
    auto h1=f1.get(),h2=f2.get();std::cout<<GetCurrentProcessId()<<" "<<(uintptr_t)h1<<" "<<(uintptr_t)h2<<std::endl;
    std::string line;std::getline(std::cin,line);
    PostMessage(h1,WM_CLOSE,0,0);PostMessage(h2,WM_CLOSE,0,0);t1.join();t2.join();server.reset();
    winrt::uninit_apartment();return 0;
}
