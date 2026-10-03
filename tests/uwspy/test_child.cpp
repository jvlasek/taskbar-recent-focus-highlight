#include <windows.h>
#include <shobjidl.h>
#include <string>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
static ITaskbarList3* taskbar;
static bool ready;
static UINT taskbarCreated;
static HICON badge;
static wchar_t identity = L'A';
static std::wstring instanceLabel;
constexpr UINT Badge = WM_APP + 10, Focus = WM_APP + 11, Ready = WM_APP + 12;
HICON MakeBadge(int number) {
    HDC screen = GetDC(nullptr), colorDC = CreateCompatibleDC(screen), maskDC = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, 16, 16), mask = CreateBitmap(16, 16, 1, 1, nullptr);
    auto oldColor = SelectObject(colorDC, color), oldMask = SelectObject(maskDC, mask);
    RECT rect{0,0,16,16};
    FillRect(colorDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    FillRect(maskDC, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HBRUSH red = CreateSolidBrush(RGB(220,0,40));
    auto oldBrush = SelectObject(colorDC, red);
    auto oldMaskBrush = SelectObject(maskDC, GetStockObject(BLACK_BRUSH));
    Ellipse(colorDC,0,0,16,16); Ellipse(maskDC,0,0,16,16);
    SetBkMode(colorDC, TRANSPARENT); SetTextColor(colorDC, RGB(255,255,255));
    HFONT font = CreateFont(-13,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto oldFont = SelectObject(colorDC,font);
    auto text = std::to_wstring(number);
    DrawText(colorDC,text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(colorDC,oldFont); DeleteObject(font);
    SelectObject(colorDC,oldBrush); SelectObject(maskDC,oldMaskBrush); DeleteObject(red);
    SelectObject(colorDC,oldColor); SelectObject(maskDC,oldMask);
    ICONINFO info{TRUE,0,0,mask,color}; HICON result = CreateIconIndirect(&info);
    DeleteObject(color); DeleteObject(mask); DeleteDC(colorDC); DeleteDC(maskDC); ReleaseDC(nullptr,screen);
    return result;
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == taskbarCreated) { ready = true; return 0; }
    switch(message) {
    case Ready: return ready && taskbar;
    case Focus:
        if (!IsWindow((HWND)l)) return FALSE;
        return SetForegroundWindow((HWND)l);
    case Badge: {
        if (!ready || !taskbar || w > 9) return E_FAIL;
        HICON next = w ? MakeBadge((int)w) : nullptr;
        if (w && !next) return E_FAIL;
        auto description = w ? std::to_wstring(w) + L" test notifications" : L"";
        HRESULT hr = taskbar->SetOverlayIcon(hwnd,next,description.c_str());
        if (badge) DestroyIcon(badge); badge = next;
        InvalidateRect(hwnd,nullptr,TRUE);
        return hr;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(hwnd,&paint); RECT r; GetClientRect(hwnd,&r);
        auto text=std::wstring(L"UWPSpy Test ")+identity+L"\nClick A when the harness asks.\nThen leave mouse and keyboard alone.\nThe controller logs verified foreground changes.";
        DrawText(dc,text.c_str(),-1,&r,DT_CENTER|DT_WORDBREAK);EndPaint(hwnd,&paint);return 0;
    }
    case WM_DESTROY: PostQuitMessage(0);return 0;
    }
    return DefWindowProc(hwnd,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    wchar_t path[MAX_PATH];GetModuleFileName(nullptr,path,MAX_PATH);
    auto filename = wcsrchr(path,L'\\'); if(filename && filename[1])identity=filename[1];
    auto appid=std::wstring(L"UWPSpy.TestHarness.")+identity;
    SetCurrentProcessExplicitAppUserModelID(appid.c_str());
    CoCreateInstance(CLSID_TaskbarList,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&taskbar));
    if(taskbar)taskbar->HrInit();
    taskbarCreated=RegisterWindowMessage(L"TaskbarButtonCreated");
    WNDCLASS cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.lpszClassName=L"UWPSpyTestChild";
    cls.hIcon=LoadIcon(nullptr,IDI_APPLICATION);cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClass(&cls);
    auto title=std::wstring(L"UWPSpy Test ")+identity;
    // Optional unique label; all instances still share the executable's AppId.
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    bool minimized=false, attention=false, background=false;
    for(int i=1;i<argc;++i) {
        if(wcscmp(argv[i],L"--minimized")==0)minimized=true;
        else if(wcscmp(argv[i],L"--attention")==0)attention=true;
        else if(wcscmp(argv[i],L"--background")==0)background=true;
        else if(wcscmp(argv[i],L"--label")==0 && i+1<argc)instanceLabel=argv[++i];
    }
    if(argv)LocalFree(argv);
    if(!instanceLabel.empty())title+=L" "+instanceLabel;
    HWND hwnd=CreateWindow(cls.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,
        100+(identity-L'A')*55,150+(identity-L'A')*40,480,220,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 1;
    ShowWindow(hwnd,minimized ? SW_SHOWMINNOACTIVE : background ? SW_SHOWNOACTIVATE : show);
    if(attention) {
        FLASHWINFO flash{sizeof(flash),hwnd,FLASHW_TRAY|FLASHW_TIMERNOFG,0,0};
        FlashWindowEx(&flash);
    }
    MSG msg;while(GetMessage(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}
    if(badge)DestroyIcon(badge);if(taskbar)taskbar->Release();CoUninitialize();return 0;
}
