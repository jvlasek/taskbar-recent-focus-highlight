// Read-only test discovery. Separate process so a shell property query can be
// bounded by the Python controller without blocking the harness indefinitely.
#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <cstdio>
#include <cwchar>

static bool first = true;
static BOOL CALLBACK Visit(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    bool match = false;
    if (wcscmp(cls, L"ApplicationFrameWindow") == 0) {
        IPropertyStore* store = nullptr;
        if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) {
            PROPVARIANT value{};
            if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &value)) &&
                value.vt == VT_LPWSTR && value.pwszVal) {
                match = CompareStringOrdinal(value.pwszVal, -1,
                    L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App", -1, TRUE) == CSTR_EQUAL;
            }
            PropVariantClear(&value);
            store->Release();
        }
    } else if (wcscmp(cls, L"Windows.UI.Core.CoreWindow") != 0 && !GetWindow(hwnd, GW_OWNER)) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            wchar_t path[32768]; DWORD size = ARRAYSIZE(path);
            if (QueryFullProcessImageNameW(process, 0, path, &size)) {
                auto name = wcsrchr(path, L'\\');
                match = name && _wcsicmp(name + 1, L"CalculatorApp.exe") == 0;
            }
            CloseHandle(process);
        }
    }
    if (match) {
        std::printf("%s{\"hwnd\":%llu,\"pid\":%lu}", first ? "" : ",",
                    static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(hwnd)), pid);
        first = false;
    }
    return TRUE;
}
int main() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;
    std::printf("[");
    BOOL ok = EnumWindows(Visit, 0);
    std::printf("]\n");
    CoUninitialize();
    return ok ? 0 : 1;
}
