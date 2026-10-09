#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include "gui.h"
#include "settings.h"
#include "resource.h"
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define WM_WORK_DONE (WM_APP + 1)
#define LOG_CAP 24000
typedef enum { WORK_CONNECT, WORK_SCRIPT, WORK_RESTART } WorkKind;
typedef struct Gui Gui;
typedef struct {
    AppSettings settings; wchar_t directory[DISCORD_PATH_CAP]; HANDLE cancel; HWND notify;
    volatile LONG *post_failed; WorkKind kind;
} Work;
typedef struct { WorkKind kind; CdpResult cdp; DiscordRestartResult restart; BOOL discord_found; size_t process_count; } WorkResult;
struct Gui {
    HINSTANCE instance; HWND window, title, subtitle, group, status, payload, activity, logs, footer;
    HWND buttons[7]; HFONT font, title_font; UINT dpi;
    AppSettings settings; wchar_t directory[DISCORD_PATH_CAP], log_text[LOG_CAP], process_text[128], endpoint_text[80], renderer_text[80], last_success[80], last_error[512];
    BOOL busy, close_pending, logs_visible; HANDLE thread, cancel; volatile LONG post_failed;
};
static int scaled(Gui *gui, int value) { return MulDiv(value, (int)gui->dpi, 96); }
static void append_log(Gui *gui, const wchar_t *text) {
    size_t current = wcslen(gui->log_text), incoming = wcslen(text);
    if (incoming > LOG_CAP / 2) { text += incoming - LOG_CAP / 2; incoming = LOG_CAP / 2; }
    if (current + incoming + 3 >= LOG_CAP) {
        size_t keep = current > LOG_CAP / 3 ? LOG_CAP / 3 : current;
        memmove(gui->log_text, gui->log_text + current - keep, (keep + 1) * sizeof(wchar_t)); current = keep;
    }
    memcpy(gui->log_text + current, text, incoming * sizeof(wchar_t));
    wcscpy(gui->log_text + current + incoming, L"\r\n");
    SetWindowTextW(gui->logs, gui->log_text); SendMessageW(gui->logs, EM_SETSEL, (WPARAM)-1, (LPARAM)-1); SendMessageW(gui->logs, EM_SCROLLCARET, 0, 0);
}
static void refresh_status(Gui *gui) {
    wchar_t text[1600];
    swprintf(text, 1600, L"Discord (last check): %ls\r\nLocal endpoint: %ls   |   Port: %u\r\nRenderer: %ls\r\nLast success: %ls\r\nLast error: %ls", gui->process_text, gui->endpoint_text, gui->settings.port, gui->renderer_text, *gui->last_success ? gui->last_success : L"None", *gui->last_error ? gui->last_error : L"None");
    SetWindowTextW(gui->status, text);
    const wchar_t *file = wcsrchr(gui->settings.payload_path, L'\\'); if (!file) file = wcsrchr(gui->settings.payload_path, L'/'); file = file ? file + 1 : gui->settings.payload_path;
    wchar_t selected[512]; swprintf(selected, 512, L"Selected script: %.400ls", file); SetWindowTextW(gui->payload, selected);
}
static void detected_text(Gui *gui, BOOL found, size_t count) {
    if (found) swprintf(gui->process_text, 128, count ? L"Running (%zu processes)" : L"Stopped", count);
    else wcscpy(gui->process_text, L"Installation not available or selection required");
}
static void layout(Gui *gui) {
    RECT rectangle; GetClientRect(gui->window, &rectangle);
    int width = rectangle.right, height = rectangle.bottom, margin = scaled(gui, 24), gap = scaled(gui, 10);
    MoveWindow(gui->title, margin, scaled(gui, 22), width - margin * 2, scaled(gui, 34), TRUE);
    MoveWindow(gui->subtitle, margin, scaled(gui, 60), width - margin * 2, scaled(gui, 28), TRUE);
    MoveWindow(gui->group, margin, scaled(gui, 100), width - margin * 2, scaled(gui, 190), TRUE);
    MoveWindow(gui->status, margin + scaled(gui, 18), scaled(gui, 128), width - margin * 2 - scaled(gui, 36), scaled(gui, 148), TRUE);
    MoveWindow(gui->payload, margin, scaled(gui, 305), width - margin * 2, scaled(gui, 28), TRUE);
    int button_width = (width - margin * 2 - gap * 6) / 7;
    for (unsigned i = 0; i < 7; ++i) MoveWindow(gui->buttons[i], margin + (button_width + gap) * (int)i, scaled(gui, 342), button_width, scaled(gui, 38), TRUE);
    MoveWindow(gui->activity, margin, scaled(gui, 396), width - margin * 2, scaled(gui, 28), TRUE);
    int log_height = height - scaled(gui, 472); if (log_height < scaled(gui, 50)) log_height = scaled(gui, 50);
    MoveWindow(gui->logs, margin, scaled(gui, 430), width - margin * 2, log_height, TRUE);
    MoveWindow(gui->footer, margin, height - scaled(gui, 32), width - margin * 2, scaled(gui, 26), TRUE);
}
static BOOL CALLBACK set_font(HWND child, LPARAM font) { SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE); return TRUE; }
static void update_fonts(Gui *gui) {
    HFONT old = gui->font, old_title = gui->title_font;
    gui->font = CreateFontW(-scaled(gui, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    gui->title_font = CreateFontW(-scaled(gui, 25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    EnumChildWindows(gui->window, set_font, (LPARAM)gui->font); SendMessageW(gui->title, WM_SETFONT, (WPARAM)gui->title_font, TRUE);
    if (old) DeleteObject(old);
    if (old_title) DeleteObject(old_title);
}
static INT_PTR CALLBACK confirm_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(dialog, ID_CONFIRM_TEXT, L"Restart Discord with local debugging enabled?\nDiscord will close and reopen. Active calls, streams, or other activities may be interrupted.\nDo you want to continue?");
#ifdef ORB_UI_TEST_MOCK
        SetWindowTextW(dialog, L"Restart Discord (MOCK TEST)");
#endif
        SendMessageW(dialog, DM_SETDEFID, IDCANCEL, 0); SetFocus(GetDlgItem(dialog, IDCANCEL)); return FALSE;
    }
    if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) { EndDialog(dialog, LOWORD(wparam)); return TRUE; }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}
static INT_PTR CALLBACK settings_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    Gui *gui = (Gui *)GetWindowLongPtrW(dialog, DWLP_USER);
    if (message == WM_INITDIALOG) {
        gui = (Gui *)lparam; SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)gui);
        SetDlgItemInt(dialog, ID_PORT_EDIT, gui->settings.port, FALSE); SetDlgItemInt(dialog, ID_OPERATION_EDIT, gui->settings.operation_timeout_ms, FALSE);
        SetDlgItemInt(dialog, ID_EXIT_EDIT, gui->settings.exit_timeout_ms, FALSE); SetDlgItemInt(dialog, ID_ENDPOINT_EDIT, gui->settings.endpoint_timeout_ms, FALSE);
        SetDlgItemTextW(dialog, ID_DISCORD_EDIT, gui->settings.discord_executable); SetDlgItemTextW(dialog, ID_SCRIPT_EDIT, gui->settings.payload_path);
        for (int id = ID_PORT_EDIT; id <= ID_ENDPOINT_EDIT; ++id) SendDlgItemMessageW(dialog, id, EM_SETLIMITTEXT, 9, 0);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        AppSettings *next = malloc(sizeof(*next)); if (!next) return TRUE; *next = gui->settings;
        int ids[] = {ID_PORT_EDIT, ID_OPERATION_EDIT, ID_EXIT_EDIT, ID_ENDPOINT_EDIT}; DWORD mins[] = {1,500,1000,1000}, maxs[] = {65535,120000,60000,120000};
        const wchar_t *errors[] = {L"Port must be between 1 and 65535.", L"Operation timeout must be 500 to 120000 ms.", L"Exit timeout must be 1000 to 60000 ms.", L"Renderer timeout must be 1000 to 120000 ms."};
        BOOL valid = TRUE; DWORD values[4]; wchar_t text[64];
        for (unsigned i = 0; i < 4; ++i) { GetDlgItemTextW(dialog, ids[i], text, 64); if (!settings_valid_number(text, mins[i], maxs[i], &values[i])) { SetDlgItemTextW(dialog, ID_SETTINGS_ERROR, errors[i]); valid = FALSE; break; } }
        if (valid) {
            next->port = (unsigned short)values[0]; next->operation_timeout_ms = values[1]; next->exit_timeout_ms = values[2]; next->endpoint_timeout_ms = values[3];
            GetDlgItemTextW(dialog, ID_SCRIPT_EDIT, next->payload_path, DISCORD_PATH_CAP); GetDlgItemTextW(dialog, ID_DISCORD_EDIT, next->discord_executable, DISCORD_PATH_CAP);
            if (!*next->payload_path) { SetDlgItemTextW(dialog, ID_SETTINGS_ERROR, L"Choose a JavaScript file."); valid = FALSE; }
            if (*next->discord_executable) {
                DiscordInstallation *installation = calloc(1, sizeof(*installation)); wchar_t error[512];
                if (!installation || !discord_discover(next->discord_executable, installation, error, 512)) { SetDlgItemTextW(dialog, ID_SETTINGS_ERROR, L"Discord executable must be an approved installed image."); valid = FALSE; }
                if (installation) { discord_release(installation); free(installation); }
            }
        }
        if (valid && !settings_save(next)) { SetDlgItemTextW(dialog, ID_SETTINGS_ERROR, L"Preferences could not be saved."); valid = FALSE; }
        if (valid) {
            if (gui->settings.port != next->port || wcscmp(gui->settings.discord_executable, next->discord_executable)) {
                wcscpy(gui->endpoint_text, L"Not checked"); wcscpy(gui->renderer_text, L"Not verified"); gui->last_success[0] = gui->last_error[0] = 0;
                append_log(gui, L"Connection settings changed; verify the new endpoint.");
            }
            gui->settings = *next; refresh_status(gui); append_log(gui, L"Settings saved."); EndDialog(dialog, IDOK);
        }
        free(next); return TRUE;
    }
    return FALSE;
}
#ifdef ORB_UI_TEST_MOCK
typedef struct { unsigned next_stage, fail_stage, hold_stage; } MockRestart;
static BOOL mock_restart_op(void *unused, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    MockRestart *mock = unused; unsigned stage = ++mock->next_stage; (void)installation;
    unsigned steps = stage == mock->hold_stage ? 125 : 4;
    for (unsigned i = 0; i < steps; ++i) { if (WaitForSingleObject(request->cancel_event, 0) == WAIT_OBJECT_0) return FALSE; Sleep(20); }
    if (stage == mock->fail_stage) { swprintf(error, capacity, L"MOCK failure at stage %u; no live restart occurred.", stage); return FALSE; }
    return TRUE;
}
static void mock_release(void *unused, DiscordInstallation *installation) { (void)unused; (void)installation; }
#endif
static DWORD WINAPI worker(void *parameter) {
    Work *work = parameter; WorkResult *result = calloc(1, sizeof(*result));
    if (!result) { InterlockedExchange(work->post_failed, 1); free(work); return 0; }
    result->kind = work->kind;
    DiscordInstallation *installation = calloc(1, sizeof(*installation)); wchar_t error[512];
    if (installation) { result->discord_found = discord_discover(work->settings.discord_executable, installation, error, 512); result->process_count = installation->process_count; discord_release(installation); free(installation); }
    if (work->kind == WORK_RESTART) {
        DiscordRestartRequest request = {TRUE, work->settings.discord_executable, work->directory, work->settings.port, work->settings.exit_timeout_ms, work->settings.endpoint_timeout_ms, work->cancel};
#ifdef ORB_UI_TEST_MOCK
        /* This test-build-only injection is absent from the production binary. */
        wchar_t selected_stage[16]; MockRestart mock = {0};
        if (GetEnvironmentVariableW(L"ORB_UI_MOCK_FAIL_STAGE", selected_stage, 16)) mock.fail_stage = (unsigned)wcstoul(selected_stage, NULL, 10);
        if (GetEnvironmentVariableW(L"ORB_UI_MOCK_HOLD_STAGE", selected_stage, 16)) mock.hold_stage = (unsigned)wcstoul(selected_stage, NULL, 10);
        const DiscordRestartOps ops = {{mock_restart_op,mock_restart_op,mock_restart_op,mock_restart_op,mock_restart_op,mock_restart_op,mock_restart_op},mock_release,&mock};
        discord_restart(&request, &ops, &result->restart);
#else
        discord_restart(&request, discord_production_ops(), &result->restart);
#endif
    } else {
        CdpRequest request = {work->directory, work->settings.payload_path, work->settings.port, work->kind == WORK_CONNECT ? CDP_CONNECT : CDP_EVALUATE, work->settings.operation_timeout_ms, work->cancel};
        cdp_run(&request, &result->cdp);
    }
    installation = calloc(1, sizeof(*installation));
    if (installation) { result->discord_found = discord_discover(work->settings.discord_executable, installation, error, 512); result->process_count = installation->process_count; discord_release(installation); free(installation); }
    if (!PostMessageW(work->notify, WM_WORK_DONE, 0, (LPARAM)result)) { free(result); InterlockedExchange(work->post_failed, 1); }
    free(work); return 0;
}
static void busy_controls(Gui *gui, BOOL busy) {
    gui->busy = busy;
    for (unsigned i = 0; i < 7; ++i) if (i != 5) EnableWindow(gui->buttons[i], i == 4 ? busy : !busy);
    SetWindowTextW(gui->activity, busy ? L"Working... Stop cancels the managed operation." : L"Ready. Scripts run only when you choose Run Script.");
}
static void begin_work(Gui *gui, WorkKind kind) {
    if (gui->busy) return;
    Work *work = calloc(1, sizeof(*work)); gui->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!work || !gui->cancel) { free(work); if (gui->cancel) CloseHandle(gui->cancel); gui->cancel = NULL; append_log(gui, L"Could not start operation."); return; }
    work->settings = gui->settings; wcscpy(work->directory, gui->directory); work->cancel = gui->cancel; work->notify = gui->window; work->post_failed = &gui->post_failed; work->kind = kind;
    InterlockedExchange(&gui->post_failed, 0); busy_controls(gui, TRUE);
    gui->thread = CreateThread(NULL, 0, worker, work, 0, NULL);
    if (!gui->thread) { free(work); CloseHandle(gui->cancel); gui->cancel = NULL; busy_controls(gui, FALSE); append_log(gui, L"Worker could not be created."); }
}
static void finish_work(Gui *gui) {
    if (gui->thread) CloseHandle(gui->thread);
    if (gui->cancel) CloseHandle(gui->cancel);
    gui->thread = gui->cancel = NULL; busy_controls(gui, FALSE);
    if (gui->close_pending) DestroyWindow(gui->window);
}
static void timestamp(wchar_t *text, size_t capacity) { SYSTEMTIME time; GetLocalTime(&time); swprintf(text, capacity, L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond); }
static void completed(Gui *gui, WorkResult *result) {
    BOOL success = FALSE, reachable = FALSE;
    detected_text(gui, result->discord_found, result->process_count);
    if (result->kind == WORK_RESTART) {
        success = result->restart.status == DISCORD_RESTART_SUCCESS; reachable = success;
        wcsncpy(gui->last_error, success ? L"" : result->restart.diagnostic, 511); gui->last_error[511] = 0;
        if (result->restart.status == DISCORD_CANCELLED) wcscpy(gui->last_error, result->restart.launched ? L"Restart cancelled after launch; Discord was launched. Check the process state above." : result->restart.shutdown_attempted ? L"Restart cancelled after shutdown was attempted; Discord may be stopped." : L"Restart cancelled before shutdown; Discord untouched.");
        else if (!success && result->restart.shutdown_attempted) {
            wchar_t diagnostic[512]; wcsncpy(diagnostic, gui->last_error, 511); diagnostic[511] = 0;
            swprintf(gui->last_error, 512, L"%.330ls %ls", diagnostic, result->restart.launched ? L"Discord was launched; check the process state above." : L"Discord may be stopped.");
        }
        append_log(gui, result->restart.diagnostic);
#ifdef ORB_UI_TEST_MOCK
        append_log(gui, L"MOCK restart test: no Discord shutdown or launch occurred.");
#endif
    } else {
        CdpResult *cdp = &result->cdp;
        success = cdp->status == CDP_FINISHED && cdp->child_exit_code == 0;
        reachable = cdp->status == CDP_FINISHED && (cdp->child_exit_code == 0 || cdp->child_exit_code == 5);
        const wchar_t *error = cdp->status == CDP_CANCELLED ? L"Cancelled. A script may already have run." : cdp->status == CDP_TIMED_OUT ? L"Operation timed out. A script may already have run." : cdp->status == CDP_LAUNCH_FAILED ? L"Native controller could not start." : cdp->child_exit_code == 5 ? L"JavaScript exception; see logs." : L"Connection or execution failed; see logs.";
        wcscpy(gui->last_error, success ? L"" : error);
        if (cdp->output_length) {
            int n = MultiByteToWideChar(CP_UTF8, 0, cdp->output, -1, NULL, 0); wchar_t *output = n ? malloc((size_t)n * sizeof(*output)) : NULL;
            if (output) { MultiByteToWideChar(CP_UTF8, 0, cdp->output, -1, output, n); append_log(gui, output); free(output); }
        }
        if (!success) append_log(gui, error);
    }
    wcscpy(gui->endpoint_text, reachable ? L"Reachable at last check" : L"Not verified"); wcscpy(gui->renderer_text, reachable ? L"Verified on demand; connection closed" : L"Not verified");
#ifdef ORB_UI_TEST_MOCK
    if (result->kind == WORK_RESTART && success) { wcscpy(gui->endpoint_text, L"Simulated by mock restart"); wcscpy(gui->renderer_text, L"Mock verification; no live restart"); }
#endif
    if (reachable) timestamp(gui->last_success, 80);
    refresh_status(gui); free(result); finish_work(gui);
}
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Gui *gui = (Gui *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) { gui = ((CREATESTRUCTW *)lparam)->lpCreateParams; gui->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)gui); }
    if (!gui) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        gui->dpi = GetDpiForWindow(window); if (!gui->dpi) gui->dpi = 96; gui->logs_visible = TRUE;
        gui->title = CreateWindowW(L"STATIC", L"24510_0rb", WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
#ifdef ORB_UI_TEST_MOCK
        const wchar_t *subtitle = L"MOCK RESTART TEST BUILD - Discord restart operations are simulated.";
#else
        const wchar_t *subtitle = L"Native Discord JavaScript Controller";
#endif
        gui->subtitle = CreateWindowW(L"STATIC",subtitle,WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->group = CreateWindowW(L"BUTTON",L"Connection",WS_CHILD|WS_VISIBLE|BS_GROUPBOX,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->status = CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,(HMENU)ID_STATUS,gui->instance,NULL);
        gui->payload = CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,(HMENU)ID_PAYLOAD,gui->instance,NULL);
        const wchar_t *labels[] = {L"Connect",L"Restart Discord",L"Load Script",L"Run Script",L"Stop",L"Logs",L"Settings"};
        for (unsigned i=0;i<7;++i) gui->buttons[i]=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,window,(HMENU)(INT_PTR)(ID_CONNECT+i),gui->instance,NULL);
        gui->activity = CreateWindowW(L"STATIC",L"Ready. Scripts run only when you choose Run Script.",WS_CHILD|WS_VISIBLE,0,0,0,0,window,(HMENU)ID_ACTIVITY,gui->instance,NULL);
        gui->logs = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,0,0,window,(HMENU)ID_LOG_EDIT,gui->instance,NULL);
        gui->footer = CreateWindowW(L"STATIC",L"Only run scripts you trust. Stop cannot undo script changes.",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
        update_fonts(gui); busy_controls(gui,FALSE); wcscpy(gui->endpoint_text,L"Not checked"); wcscpy(gui->renderer_text,L"Not verified");
        DiscordInstallation *installation=calloc(1,sizeof(*installation)); wchar_t error[512]; BOOL found=installation&&discord_discover(gui->settings.discord_executable,installation,error,512);
        detected_text(gui,found,installation?installation->process_count:0); if(installation){discord_release(installation);free(installation);} refresh_status(gui);
        if (!gui->title || !gui->subtitle || !gui->group || !gui->status || !gui->payload || !gui->activity || !gui->logs || !gui->footer || !gui->font || !gui->title_font) return -1;
        for (unsigned i = 0; i < 7; ++i) if (!gui->buttons[i]) return -1;
        append_log(gui,L"Ready. No script runs automatically."); if (!SetTimer(window,1,100,NULL)) return -1; return 0;
    }
    case WM_SIZE: layout(gui); return 0;
    case WM_DPICHANGED: { gui->dpi=HIWORD(wparam); update_fonts(gui); RECT *r=(RECT *)lparam; SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER); layout(gui); return 0; }
    case WM_GETMINMAXINFO: { MINMAXINFO *size=(MINMAXINFO *)lparam; size->ptMinTrackSize.x=scaled(gui,850); size->ptMinTrackSize.y=scaled(gui,620); return 0; }
    case WM_COMMAND:
        if (LOWORD(wparam)==ID_STOP && gui->busy) { SetEvent(gui->cancel); SetWindowTextW(gui->activity,L"Cancelling managed operation..."); return 0; }
        if (LOWORD(wparam)==ID_LOGS) { gui->logs_visible=!gui->logs_visible; ShowWindow(gui->logs,gui->logs_visible?SW_SHOW:SW_HIDE); return 0; }
        if (gui->busy) return 0;
        if (LOWORD(wparam)==ID_CONNECT) begin_work(gui,WORK_CONNECT);
        else if (LOWORD(wparam)==ID_RUN) begin_work(gui,WORK_SCRIPT);
        else if (LOWORD(wparam)==ID_RESTART) { INT_PTR consent=DialogBoxParamW(gui->instance,MAKEINTRESOURCEW(IDD_RESTART),window,confirm_dialog,0); if(consent==IDOK)begin_work(gui,WORK_RESTART);else append_log(gui,L"Restart cancelled; Discord untouched."); }
        else if (LOWORD(wparam)==ID_SETTINGS) DialogBoxParamW(gui->instance,MAKEINTRESOURCEW(IDD_SETTINGS),window,settings_dialog,(LPARAM)gui);
        else if (LOWORD(wparam)==ID_LOAD) {
            wchar_t *file=calloc(DISCORD_PATH_CAP,sizeof(wchar_t)); if(!file)return 0; OPENFILENAMEW open={0};open.lStructSize=sizeof(open);open.hwndOwner=window;open.lpstrFilter=L"JavaScript files (*.js)\0*.js\0All files\0*.*\0\0";open.lpstrFile=file;open.nMaxFile=DISCORD_PATH_CAP;open.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
            if(GetOpenFileNameW(&open)){wcscpy(gui->settings.payload_path,file);refresh_status(gui);if(!settings_save(&gui->settings))append_log(gui,L"Script selected; preferences could not be saved.");else append_log(gui,L"Script selected. Choose Run Script to execute.");}free(file);
        }
        return 0;
    case WM_WORK_DONE: completed(gui,(WorkResult *)lparam); return 0;
    case WM_TIMER: if(gui->busy && gui->thread && WaitForSingleObject(gui->thread,0)==WAIT_OBJECT_0 && InterlockedCompareExchange(&gui->post_failed,0,0)){append_log(gui,L"Worker result could not be delivered.");finish_work(gui);} return 0;
    case WM_CLOSE: if(gui->busy){gui->close_pending=TRUE;SetEvent(gui->cancel);SetWindowTextW(gui->activity,L"Cancelling before close...");}else DestroyWindow(window); return 0;
    case WM_DESTROY: KillTimer(window,1); if(gui->font)DeleteObject(gui->font);if(gui->title_font)DeleteObject(gui->title_font); PostQuitMessage(0); return 0;
    case WM_NCDESTROY: SetWindowLongPtrW(window,GWLP_USERDATA,0); return DefWindowProcW(window,message,wparam,lparam);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
int gui_run(HINSTANCE instance, int show) {
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    Gui *gui=calloc(1,sizeof(*gui));if(!gui)return 8;gui->instance=instance;gui->dpi=96;
    GetModuleFileNameW(NULL,gui->directory,DISCORD_PATH_CAP);wchar_t *slash=wcsrchr(gui->directory,L'\\');if(!slash){free(gui);return 1;}*slash=0;
    if(!settings_load(&gui->settings,gui->directory))settings_defaults(&gui->settings,gui->directory);
#ifdef ORB_UI_TEST_MOCK
    const wchar_t *class_name=L"24510_ORB_Mock_Window", *title=L"24510_0rb - MOCK RESTART TEST";
#else
    const wchar_t *class_name=L"24510_ORB_Window", *title=L"24510_0rb - Native Discord Controller";
#endif
    WNDCLASSEXW window_class={0};window_class.cbSize=sizeof(window_class);window_class.lpfnWndProc=window_proc;window_class.hInstance=instance;window_class.hCursor=LoadCursorW(NULL,IDC_ARROW);window_class.hIcon=LoadIconW(NULL,IDI_APPLICATION);window_class.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);window_class.lpszClassName=class_name;
    if(!RegisterClassExW(&window_class)){free(gui);return 1;}
    HWND window=CreateWindowExW(0,class_name,title,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,990,730,NULL,NULL,instance,gui);
    if(!window){free(gui);return 1;}ShowWindow(window,show);UpdateWindow(window);
    MSG message={0}; BOOL received;while((received=GetMessageW(&message,NULL,0,0))>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    free(gui); return received < 0 ? 1 : (int)message.wParam;
}
