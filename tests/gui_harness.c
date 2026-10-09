#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commdlg.h>
#include <dlgs.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "../src/resource.h"

/* This harness only sends messages to windows belonging to its freshly launched
   application PID. Production restart confirmation is always cancelled. */
typedef struct { DWORD pid; HWND main, dialog; BOOL mock; int dialog_control; } Windows;
static unsigned checks, failures;
static void check(BOOL ok, const char *name) { ++checks; if (!ok) ++failures; printf("%s %s\n", ok ? "PASS" : "FAIL", name); fflush(stdout); }
static BOOL belongs(HWND window, DWORD pid) { DWORD current = 0; GetWindowThreadProcessId(window, &current); return current == pid; }
static BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
    Windows *windows = (Windows *)parameter; wchar_t name[128];
    if (!belongs(window, windows->pid) || !IsWindowVisible(window)) return TRUE;
    GetClassNameW(window, name, 128);
    if (!wcscmp(name, windows->mock ? L"24510_ORB_Mock_Window" : L"24510_ORB_Window")) windows->main = window;
    if (!wcscmp(name, L"#32770") && (!windows->dialog_control || GetDlgItem(window, windows->dialog_control))) windows->dialog = window;
    return TRUE;
}
static BOOL CALLBACK control_inventory(HWND window, LPARAM unused) {
    (void)unused; wchar_t name[64]; char utf8[128]; GetClassNameW(window,name,64); WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,128,NULL,NULL);
    printf("FILE_CONTROL id=%d class=%s\n",GetDlgCtrlID(window),utf8); return TRUE;
}
static BOOL CALLBACK find_file_edit(HWND window, LPARAM output) {
    wchar_t name[64]; GetClassNameW(window,name,64);
    if (GetDlgCtrlID(window) == cmb13 && !wcscmp(name,L"Edit")) *(HWND *)output = window;
    return TRUE;
}
static HWND wait_window(Windows *windows, BOOL dialog, int control, DWORD timeout) {
    ULONGLONG end = GetTickCount64() + timeout; windows->dialog_control = control;
    do { windows->dialog = NULL; EnumWindows(find_window, (LPARAM)windows); HWND found = dialog ? windows->dialog : windows->main; if (found) return found; Sleep(20); } while (GetTickCount64() < end);
    return NULL;
}
static BOOL text(HWND window, wchar_t *buffer, size_t capacity) {
    DWORD_PTR result; buffer[0] = 0;
    return SendMessageTimeoutW(window, WM_GETTEXT, capacity, (LPARAM)buffer, SMTO_ABORTIFHUNG, 500, &result) != 0;
}
static BOOL set_text(HWND dialog, int id, const wchar_t *value) {
    /* SetWindowText/SetDlgItemText cannot change another process's edit text.
       WM_SETTEXT is a system message and marshals its Unicode string. */
    DWORD_PTR result = 0; HWND control = GetDlgItem(dialog, id);
    return control && SendMessageTimeoutW(control, WM_SETTEXT, 0, (LPARAM)value, SMTO_ABORTIFHUNG, 500, &result) && result;
}
static BOOL contains(HWND window, const wchar_t *expected) {
    wchar_t *buffer = calloc(25000, sizeof(*buffer)); if (!buffer) return FALSE;
    BOOL ok = text(window, buffer, 25000) && wcsstr(buffer, expected) != NULL; free(buffer); return ok;
}
static void dump_control(HWND window, const char *name) {
    wchar_t bounded[6001]; if (!text(window, bounded, 6001)) return;
    int size = WideCharToMultiByte(CP_UTF8, 0, bounded, -1, NULL, 0, NULL, NULL);
    char *utf8 = size > 0 ? malloc((size_t)size) : NULL;
    if (utf8) { WideCharToMultiByte(CP_UTF8, 0, bounded, -1, utf8, size, NULL, NULL); printf("APP_%s_BEGIN\n%s\nAPP_%s_END\n", name, utf8, name); free(utf8); }
}
static BOOL click(Windows *windows, HWND parent, int id) {
    HWND control = GetDlgItem(parent, id);
    if (!belongs(parent, windows->pid) || !control || !IsWindowEnabled(control)) return FALSE;
    return PostMessageW(parent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)control);
}
static BOOL wait_idle(Windows *windows, DWORD timeout) {
    ULONGLONG end = GetTickCount64() + timeout;
    do { if (!IsWindow(windows->main)) return FALSE; if (!IsWindowEnabled(GetDlgItem(windows->main, ID_STOP))) return TRUE; Sleep(20); } while (GetTickCount64() < end); return FALSE;
}
static BOOL wait_busy(Windows *windows, DWORD timeout) {
    ULONGLONG end = GetTickCount64() + timeout;
    do { if (IsWindowEnabled(GetDlgItem(windows->main, ID_STOP))) return TRUE; Sleep(10); } while (GetTickCount64() < end); return FALSE;
}
static HWND open_settings(Windows *windows) { if (!click(windows, windows->main, ID_SETTINGS)) return NULL; HWND dialog = wait_window(windows, TRUE, ID_PORT_EDIT, 3000); if (dialog) Sleep(150); return dialog; }
static BOOL configure(Windows *windows, const wchar_t *port, const wchar_t *payload) {
    HWND dialog = open_settings(windows); if (!dialog) return FALSE;
    set_text(dialog, ID_PORT_EDIT, port); set_text(dialog, ID_OPERATION_EDIT, L"5000");
    if (payload && *payload) set_text(dialog, ID_SCRIPT_EDIT, payload);
    set_text(dialog, ID_DISCORD_EDIT, L""); click(windows, dialog, IDOK);
    ULONGLONG end = GetTickCount64() + 2000; while (IsWindow(dialog) && GetTickCount64() < end) Sleep(20);
    if (IsWindow(dialog)) { click(windows, dialog, IDCANCEL); return FALSE; } return TRUE;
}
static BOOL capture(HWND window, const wchar_t *file) {
    RECT rectangle; if (!GetWindowRect(window, &rectangle)) return FALSE; int width = rectangle.right - rectangle.left, height = rectangle.bottom - rectangle.top;
    HDC source = GetDC(window), memory = CreateCompatibleDC(source); if (!source || !memory) { if (source) ReleaseDC(window, source); return FALSE; }
    BITMAPINFO info = {0}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void *pixels = NULL; HBITMAP bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, NULL, 0); BOOL ok = FALSE;
    if (bitmap) {
        HGDIOBJ old = SelectObject(memory, bitmap); ok = PrintWindow(window, memory, 2); SelectObject(memory, old);
        if (ok) {
            DWORD size = (DWORD)width * (DWORD)height * 4, written; BITMAPFILEHEADER header = {0}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader); header.bfSize = header.bfOffBits + size;
            HANDLE output = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (output == INVALID_HANDLE_VALUE) ok = FALSE;
            else { ok = WriteFile(output, &header, sizeof(header), &written, NULL) && written == sizeof(header) && WriteFile(output, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL) && written == sizeof(info.bmiHeader) && WriteFile(output, pixels, size, &written, NULL) && written == size; CloseHandle(output); }
        }
        DeleteObject(bitmap);
    }
    DeleteDC(memory); ReleaseDC(window, source); return ok;
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 2 && !wcscmp(argv[1],L"--sentinel")) { printf("OWNED_SENTINEL_PID=%lu\n",GetCurrentProcessId()); fflush(stdout); Sleep(30000); return 0; }
    if (argc < 3) { fputs("usage: gui-harness.exe <exact GUI executable> <action> [port] [payload] [expected] [bmp]\n", stderr); return 2; }
    wchar_t *command = calloc(wcslen(argv[1]) + 4, sizeof(*command)); if (!command) return 8; swprintf(command, wcslen(argv[1]) + 4, L"\"%ls\"", argv[1]);
    STARTUPINFOW startup = {0}; PROCESS_INFORMATION process = {0}; startup.cb = sizeof(startup);
    BOOL launched = CreateProcessW(argv[1], command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process); free(command); check(launched, "launch-own-GUI"); if (!launched) return 1; CloseHandle(process.hThread);
    const wchar_t *base = wcsrchr(argv[1], L'\\'); base = base ? base + 1 : argv[1]; Windows windows = {process.dwProcessId, NULL, NULL, !wcscmp(base, L"24510_0rb-ui-mock.exe"), 0};
    HWND main = wait_window(&windows, FALSE, 0, 5000); check(main != NULL, "exact-PID-class-window"); if (!main) goto finish;
    check(contains(GetDlgItem(main, ID_ACTIVITY), L"Ready."), "startup-idle");
    check(contains(GetDlgItem(main, ID_LOG_EDIT), L"No script runs automatically."), "startup-no-evaluation");
    check(contains(GetDlgItem(main, ID_STATUS), L"Discord (last check):"), "startup-process-snapshot-label");
    DWORD_PTR reply; check(SendMessageTimeoutW(main, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &reply) != 0, "initial-responsiveness");
    const wchar_t *mode = argv[2], *port = argc > 3 ? argv[3] : L"9223", *payload = argc > 4 ? argv[4] : L"", *expected = argc > 5 ? argv[5] : L"JURI INJECTION SUCCESS"; BOOL captured = FALSE;
    if (!wcscmp(mode, L"settings")) {
        HWND dialog = open_settings(&windows); check(dialog != NULL, "settings-open");
        if (dialog) {
            if (argc > 6) { set_text(dialog, ID_SCRIPT_EDIT, L"hello.js"); captured = TRUE; check(capture(dialog, argv[6]), "own-settings-only-capture"); }
            BOOL edited = set_text(dialog, ID_PORT_EDIT, L"0"); wchar_t value[64]; text(GetDlgItem(dialog, ID_PORT_EDIT), value, 64); printf("PORT_AFTER_EDIT=%lu\n", wcstoul(value, NULL, 10));
            BOOL clicked = click(&windows, dialog, IDOK); Sleep(300);
            wchar_t diagnostic[256]; text(GetDlgItem(dialog, ID_SETTINGS_ERROR), diagnostic, 256); printf("SETTINGS_EDIT=%d CLICK=%d ERROR_LENGTH=%zu WINDOW=%d\n", edited, clicked, wcslen(diagnostic), IsWindow(dialog));
            check(IsWindow(dialog) && contains(GetDlgItem(dialog, ID_SETTINGS_ERROR), L"Port must"), "reject-zero-port");
            set_text(dialog, ID_PORT_EDIT, L"65536"); click(&windows, dialog, IDOK); Sleep(100); check(IsWindow(dialog) && contains(GetDlgItem(dialog, ID_SETTINGS_ERROR), L"Port must"), "reject-large-port");
            set_text(dialog, ID_PORT_EDIT, port); set_text(dialog, ID_OPERATION_EDIT, L"499"); click(&windows, dialog, IDOK); Sleep(100); check(contains(GetDlgItem(dialog, ID_SETTINGS_ERROR), L"Operation timeout"), "reject-short-timeout");
            set_text(dialog, ID_OPERATION_EDIT, L"5000"); set_text(dialog, ID_DISCORD_EDIT, argv[1]); click(&windows, dialog, IDOK); Sleep(100); check(contains(GetDlgItem(dialog, ID_SETTINGS_ERROR), L"approved installed image"), "reject-unrelated-executable"); click(&windows, dialog, IDCANCEL);
        }
    } else if (!wcscmp(mode, L"fallback")) {
        check(contains(GetDlgItem(main, ID_STATUS), L"Port: 9223") && contains(GetDlgItem(main, ID_PAYLOAD), L"hello.js"), "corrupt-preferences-default-fallback");
    } else if (!wcscmp(mode, L"persist")) {
        HWND dialog = open_settings(&windows); wchar_t actual[32768]; BOOL ok = dialog && text(GetDlgItem(dialog, ID_SCRIPT_EDIT), actual, 32768) && !wcscmp(actual, payload); check(ok, "Unicode-path-persisted-exactly"); if (dialog) click(&windows, dialog, IDCANCEL);
    } else if (!wcsncmp(mode, L"cancel", 6) || !wcsncmp(mode, L"mock-", 5)) {
        click(&windows, main, ID_RESTART); HWND dialog = wait_window(&windows, TRUE, ID_CONFIRM_TEXT, 3000); check(dialog != NULL, "restart-confirmation-open");
        if (dialog) {
            if (argc > 6) { captured = TRUE; check(capture(dialog, argv[6]), "own-consent-only-capture"); }
            wchar_t actual[512]; text(GetDlgItem(dialog, ID_CONFIRM_TEXT), actual, 512);
            check(!wcscmp(actual, L"Restart Discord with local debugging enabled?\nDiscord will close and reopen. Active calls, streams, or other activities may be interrupted.\nDo you want to continue?"), "exact-consent-text");
            check(LOWORD(SendMessageW(dialog, DM_GETDEFID, 0, 0)) == IDCANCEL, "default-Cancel");
            if (!wcsncmp(mode, L"mock-", 5)) {
                check(windows.mock, "affirmation-only-mock-class"); if (windows.mock) {
                    click(&windows, dialog, IDOK); check(wait_busy(&windows, 1000), "mock-restart-dispatched");
                    if (!wcscmp(mode, L"mock-stop")) { Sleep((DWORD)wcstoul(port,NULL,10)); click(&windows,main,ID_STOP); }
                    check(wait_idle(&windows, 5000), "mock-restart-completed"); check(contains(GetDlgItem(main, ID_LOG_EDIT), L"MOCK restart test: no Discord shutdown or launch occurred."), "mock-no-live-effects-label");
                    check(contains(GetDlgItem(main, ID_STATUS), L"Discord (last check):"), "completed-process-snapshot-label");
                    if (wcscmp(mode,L"mock-restart")) check(contains(GetDlgItem(main, ID_STATUS), expected), "mock-error-or-cancel-status");
                } else click(&windows, dialog, IDCANCEL);
            } else {
                if (!wcscmp(mode, L"cancel-x")) PostMessageW(dialog, WM_CLOSE, 0, 0);
                else if (!wcscmp(mode, L"cancel-escape")) { PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0); PostMessageW(dialog, WM_KEYUP, VK_ESCAPE, 0); }
                else click(&windows, dialog, IDCANCEL);
                Sleep(200); check(!IsWindow(dialog), "confirmation-cancel-closed"); check(contains(GetDlgItem(main, ID_LOG_EDIT), L"Restart cancelled; Discord untouched."), "cancel-no-worker"); check(!IsWindowEnabled(GetDlgItem(main, ID_STOP)), "cancel-remains-idle");
            }
        }
    } else if (!wcscmp(mode, L"minimum")) {
        MINMAXINFO minimum = {0}; SendMessageW(main, WM_GETMINMAXINFO, 0, (LPARAM)&minimum);
        SetWindowPos(main, NULL, 0, 0, minimum.ptMinTrackSize.x, minimum.ptMinTrackSize.y, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        RECT client, last; GetClientRect(main, &client); GetWindowRect(GetDlgItem(main, ID_SETTINGS), &last); MapWindowPoints(NULL, main, (POINT *)&last, 2);
        check(last.right <= client.right && last.bottom <= client.bottom, "minimum-controls-visible");
    } else if (!wcscmp(mode, L"dpi")) {
        MINMAXINFO before = {0}, after = {0}; SendMessageW(main, WM_GETMINMAXINFO, 0, (LPARAM)&before); RECT rectangle; GetWindowRect(main, &rectangle); rectangle.right = rectangle.left + 1350; rectangle.bottom = rectangle.top + 1000;
        check(SendMessageTimeoutW(main, WM_DPICHANGED, MAKEWPARAM(144, 144), (LPARAM)&rectangle, SMTO_ABORTIFHUNG, 500, &reply) != 0, "owned-window-DPI-message");
        SendMessageW(main, WM_GETMINMAXINFO, 0, (LPARAM)&after);
        printf("DPI_MIN_BEFORE=%ld,%ld AFTER=%ld,%ld\n", before.ptMinTrackSize.x,before.ptMinTrackSize.y,after.ptMinTrackSize.x,after.ptMinTrackSize.y);
        check(after.ptMinTrackSize.x == 1275 && after.ptMinTrackSize.y == 930 && after.ptMinTrackSize.x >= before.ptMinTrackSize.x, "DPI-scaled-minimum");
        RECT button; GetWindowRect(GetDlgItem(main, ID_CONNECT), &button); check(button.bottom - button.top == 57, "DPI-scaled-control-height");
    } else if (wcscmp(mode, L"smoke")) {
        check(configure(&windows, port, !wcscmp(mode, L"load") ? L"" : payload), "valid-settings-saved");
        if (!wcscmp(mode, L"load")) {
            click(&windows, main, ID_LOAD); HWND dialog = wait_window(&windows, TRUE, 0, 3000); check(dialog != NULL, "Unicode-file-dialog-open");
            if (dialog) {
                Sleep(150);
                HWND edit = NULL; EnumChildWindows(dialog,find_file_edit,(LPARAM)&edit);
                DWORD_PTR set_result = 0;
                check(edit && belongs(edit,windows.pid) && SendMessageTimeoutW(edit,WM_SETTEXT,0,(LPARAM)payload,SMTO_ABORTIFHUNG,500,&set_result) && set_result, "owned-file-dialog-Unicode-edit");
                click(&windows, dialog, IDOK); ULONGLONG end = GetTickCount64() + 3000; while (IsWindow(dialog) && GetTickCount64() < end) Sleep(20); check(!IsWindow(dialog), "Unicode-file-selected"); if (IsWindow(dialog)) { EnumChildWindows(dialog,control_inventory,0); click(&windows, dialog, IDCANCEL); }
            }
        }
        click(&windows, main, !wcscmp(mode, L"connect") ? ID_CONNECT : ID_RUN); check(wait_busy(&windows, 1000), "worker-dispatched");
        if (!wcscmp(mode, L"stop") || !wcscmp(mode, L"close")) {
            Sleep(500); check(SendMessageTimeoutW(main, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &reply) != 0, "responsive-during-stalled-CDP");
            check(!IsWindowEnabled(GetDlgItem(main, ID_RESTART)) && !IsWindowEnabled(GetDlgItem(main, ID_SETTINGS)), "conflicting-controls-disabled"); ULONGLONG start = GetTickCount64();
            if (!wcscmp(mode, L"close")) { PostMessageW(main, WM_CLOSE, 0, 0); check(WaitForSingleObject(process.hProcess, 3000) == WAIT_OBJECT_0, "close-while-busy-completed"); }
            else { click(&windows, main, ID_STOP); check(wait_idle(&windows, 3000), "Stop-completed"); check(contains(GetDlgItem(main, ID_LOG_EDIT), L"Cancelled."), "Stop-result-owned"); }
            printf("CANCEL_ELAPSED_MS=%llu\n", GetTickCount64() - start);
        } else {
            check(wait_idle(&windows, 10000), "worker-completed"); check(contains(GetDlgItem(main, ID_LOG_EDIT), expected), "expected-result-in-log");
            if (!wcscmp(mode,L"repeat")) {
                for (unsigned i = 0; i < 2; ++i) { click(&windows,main,ID_RUN); check(wait_busy(&windows,1000),"same-window-repeat-dispatched"); check(wait_idle(&windows,10000),"same-window-repeat-completed"); }
                wchar_t logs[25000]; text(GetDlgItem(main,ID_LOG_EDIT),logs,25000); unsigned count = 0; const wchar_t *match = logs;
                while ((match = wcsstr(match,expected)) != NULL) { ++count; match += wcslen(expected); }
                check(count >= 3,"three-results-in-same-window-log");
            }
            if (!wcscmp(mode, L"invalidate")) { check(contains(GetDlgItem(main, ID_STATUS), L"Verified on demand"), "verification-before-settings-change"); unsigned n = (unsigned)wcstoul(port, NULL, 10); wchar_t next[16]; swprintf(next, 16, L"%u", n == 65535 ? n - 1 : n + 1); check(configure(&windows, next, payload), "new-port-saved"); check(contains(GetDlgItem(main, ID_STATUS), L"Not checked") && contains(GetDlgItem(main, ID_STATUS), L"Last success: None"), "stale-verification-cleared"); }
        }
    }
    if (argc > 6 && !captured && IsWindow(main)) check(capture(main, argv[6]), "own-window-only-capture");
finish:
    if (failures && windows.main && IsWindow(windows.main)) { dump_control(GetDlgItem(windows.main, ID_STATUS), "STATUS"); dump_control(GetDlgItem(windows.main, ID_LOG_EDIT), "LOG"); }
    if (windows.main && IsWindow(windows.main)) PostMessageW(windows.main, WM_CLOSE, 0, 0);
    check(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "own-GUI-clean-exit"); DWORD exit_code = STILL_ACTIVE; GetExitCodeProcess(process.hProcess, &exit_code); check(exit_code == 0, "own-GUI-exit-zero"); CloseHandle(process.hProcess);
    printf("SUMMARY checks=%u failures=%u\n", checks, failures); return failures ? 1 : 0;
}
