/* Standalone diagnostic observer. No injection or input/focus changes.
 * EVENT records are WinEvent notifications; SAMPLE records are 50 ms
 * foreground/keyboard-focus observations, emitted only when state changes.
 * The launcher owns stdin: a byte or EOF requests clean shutdown.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD main_thread;
static UINT shell_message;

typedef struct {
    HWND foreground, active, focus, focus_root;
    DWORD pid, tid;
    BOOL gui_ok;
} State;

static void json_string(const WCHAR* text) {
    putchar('"');
    for (; *text; ++text) {
        if (*text >= 32 && *text < 127 && *text != '"' && *text != '\\')
            putchar((char)*text);
        else
            printf("\\u%04x", (unsigned)*text);
    }
    putchar('"');
}

static void header(const char* kind) {
    SYSTEMTIME now;
    GetSystemTime(&now);
    printf("{\"kind\":\"%s\",\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\","
           "\"tick_ms\":%llu", kind, now.wYear, now.wMonth, now.wDay,
           now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
           (unsigned long long)GetTickCount64());
}

static void window_info(HWND hwnd) {
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    WCHAR name[256] = {0};
    if (hwnd) GetClassNameW(hwnd, name, 256);
    printf("{\"hwnd\":\"%p\",\"pid\":%lu,\"tid\":%lu,\"root\":\"%p\",\"class\":",
           (void*)hwnd, (unsigned long)pid, (unsigned long)tid,
           (void*)(hwnd ? GetAncestor(hwnd, GA_ROOT) : NULL));
    json_string(name);
    putchar('}');
}

static State read_state(void) {
    State state = {0};
    state.foreground = GetForegroundWindow();
    if (state.foreground) {
        GUITHREADINFO gui = {0};
        gui.cbSize = sizeof(gui);
        state.tid = GetWindowThreadProcessId(state.foreground, &state.pid);
        if (state.tid && GetGUIThreadInfo(state.tid, &gui)) {
            state.gui_ok = TRUE;
            state.active = gui.hwndActive;
            state.focus = gui.hwndFocus;
            state.focus_root = gui.hwndFocus ? GetAncestor(gui.hwndFocus, GA_ROOT) : NULL;
        }
    }
    return state;
}

static void write_state(State state) {
    printf(",\"foreground\":");
    window_info(state.foreground);
    printf(",\"gui_ok\":%s,\"active\":\"%p\",\"focus\":",
           state.gui_ok ? "true" : "false", (void*)state.active);
    window_info(state.focus);
    printf(",\"focus_root\":\"%p\"}", (void*)state.focus_root);
    putchar('\n');
}

static void CALLBACK on_foreground(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                    LONG object, LONG child, DWORD tid, DWORD time) {
    (void)hook;
    header("EVENT");
    printf(",\"event\":%lu,\"object\":%ld,\"child\":%ld,"
           "\"event_tid\":%lu,\"event_time_ms32\":%lu,\"window\":",
           (unsigned long)event, (long)object, (long)child,
           (unsigned long)tid, (unsigned long)time);
    window_info(hwnd);
    write_state(read_state());
}

/* TEMP_FOCUS_TRACE: a hidden top-level window receives shell notifications;
 * it is never shown or activated. This is separate from WinEvent delivery. */
static LRESULT CALLBACK shell_proc(HWND hwnd, UINT message, WPARAM wparam,
                                   LPARAM lparam) {
    if (shell_message && message == shell_message &&
        (wparam == HSHELL_WINDOWACTIVATED || wparam == HSHELL_RUDEAPPACTIVATED)) {
        header("SHELL");
        printf(",\"event\":%llu,\"window\":", (unsigned long long)wparam);
        window_info((HWND)lparam);
        write_state(read_state());
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static DWORD WINAPI wait_for_stop(void* unused) {
    char byte;
    DWORD received;
    (void)unused;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &received, NULL);
    PostThreadMessageW(main_thread, WM_QUIT, 0, 0);
    return 0;
}

static BOOL WINAPI on_control(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    return PostThreadMessageW(main_thread, WM_QUIT, 0, 0);
}

int main(int argc, char** argv) {
    MSG msg;
    State previous = {0};
    HANDLE stop_thread = NULL;
    HWINEVENTHOOK focus_hook = NULL;
    HWND shell_window = NULL;
    BOOL shell_registered = FALSE;
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW shell_class = {0};
    int status = 0;
    main_thread = GetCurrentThreadId();
    setvbuf(stdout, NULL, _IONBF, 0);
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE); /* Create queue before stop worker. */
    SetConsoleCtrlHandler(on_control, TRUE);
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND, NULL, on_foreground, 0, 0, WINEVENT_OUTOFCONTEXT);
    focus_hook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS,
        NULL, on_foreground, 0, 0, WINEVENT_OUTOFCONTEXT);
    UINT_PTR timer = SetTimer(NULL, 0, 50, NULL);
    if (!hook || !focus_hook || !timer) {
        fprintf(stderr, "Observer hook/timer setup failed: %lu\n", (unsigned long)GetLastError());
        status = 1;
        goto cleanup;
    }
    shell_message = RegisterWindowMessageW(L"SHELLHOOK");
    shell_class.lpfnWndProc = shell_proc;
    shell_class.hInstance = instance;
    shell_class.lpszClassName = L"WhRecentFocusTraceObserver";
    if (shell_message && RegisterClassW(&shell_class)) {
        shell_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            shell_class.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0,
            NULL, NULL, instance, NULL);
        if (shell_window) shell_registered = RegisterShellHookWindow(shell_window);
    }
    if (!shell_registered) {
        fprintf(stderr, "Shell activation channel unavailable: %lu\n", (unsigned long)GetLastError());
        status = 1;
        goto cleanup;
    }
    if (argc == 2 && strcmp(argv[1], "--stdin-stop") == 0) {
        stop_thread = CreateThread(NULL, 0, wait_for_stop, NULL, 0, NULL);
        if (!stop_thread) { status = 1; goto cleanup; }
    }
    header("READY");
    printf(",\"sample_ms\":50,\"foreground_events\":true,\"keyboard_focus_events\":true,\"shell_activation_events\":true}\n");
    previous = read_state();
    header("SAMPLE");
    write_state(previous);
    for (;;) {
        BOOL result = GetMessageW(&msg, NULL, 0, 0);
        if (result <= 0) { if (result == -1) status = 1; break; }
        if (msg.message == WM_TIMER && msg.hwnd == NULL && msg.wParam == timer) {
            State current = read_state();
            if (current.foreground != previous.foreground || current.pid != previous.pid ||
                current.tid != previous.tid || current.focus != previous.focus ||
                current.focus_root != previous.focus_root || current.active != previous.active ||
                current.gui_ok != previous.gui_ok) {
                header("SAMPLE");
                write_state(current);
                previous = current;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
cleanup:
    if (shell_registered) DeregisterShellHookWindow(shell_window);
    if (shell_window) DestroyWindow(shell_window);
    if (shell_class.lpszClassName) UnregisterClassW(shell_class.lpszClassName, instance);
    if (focus_hook) UnhookWinEvent(focus_hook);
    if (hook) UnhookWinEvent(hook);
    if (timer) KillTimer(NULL, timer);
    if (stop_thread) {
        CancelSynchronousIo(stop_thread);
        /* Process exit also terminates a stop reader if cancellation raced its ReadFile. */
        WaitForSingleObject(stop_thread, 1000);
        CloseHandle(stop_thread);
    }
    header("STOP");
    printf(",\"status\":%d}\n", status);
    return status;
}
