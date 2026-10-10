#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include "gui.h"
#include "settings.h"
#include "resource.h"
#include "payload.h"
#include "version.h"
#include "orb_state.h"
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#define WM_WORK_DONE (WM_APP + 1)
#define LOG_CAP 24000
typedef enum { WORK_CONNECT, WORK_SCRIPT, WORK_ORIGINAL, WORK_ORB_STOP, WORK_RESTART, WORK_STATUS, WORK_SCAN } WorkKind;
typedef struct Gui Gui;
typedef struct {
    AppSettings settings; wchar_t directory[DISCORD_PATH_CAP]; HANDLE cancel; HWND notify;
    volatile LONG *post_failed; WorkKind kind; OrbPayload reviewed; char orb_key[33];
} Work;
typedef struct { WorkKind kind; CdpResult cdp; DiscordRestartResult restart; BOOL discord_found; size_t process_count; } WorkResult;
struct Gui {
    OrbState model; ULONGLONG run_started, last_route_check; BOOL showing_details; char orb_key[33];
    HWND heading, copy, indicator, primary, progress, elapsed, refresh, scan, details, tabs;
    HBRUSH background_brush, panel_brush; HWND hover;
    HINSTANCE instance; HWND window, title, subtitle, group, status, payload, activity, logs, footer;
    HWND buttons[7], original_button, orb_stop_button, about_button; HFONT font, title_font; UINT dpi;
    AppSettings settings; wchar_t directory[DISCORD_PATH_CAP], log_text[LOG_CAP], process_text[128], endpoint_text[80], renderer_text[80], last_success[80], last_error[512];
    BOOL busy, close_pending, logs_visible; OrbPayload approved; wchar_t original_path[DISCORD_PATH_CAP]; HANDLE thread, cancel; volatile LONG post_failed;
};
static void render_state(Gui *gui);
static void begin_work(Gui *gui, WorkKind kind);
static BOOL choose_script(HWND owner, wchar_t *path);
static BOOL repo_original(Gui *gui, wchar_t *path);
static int scaled(Gui *gui, int value) { return MulDiv(value, (int)gui->dpi, 96); }
static void append_log(Gui *gui, const wchar_t *text) {
    SYSTEMTIME now; GetLocalTime(&now); wchar_t bounded[12000];
    swprintf(bounded,12000,L"[%02u:%02u:%02u] %.11970ls",now.wHour,now.wMinute,now.wSecond,text);text=bounded;
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
    wchar_t selected[512]; swprintf(selected, 512, L"Selected script: %.400ls", file); SetWindowTextW(gui->payload, selected); render_state(gui);
}
static void detected_text(Gui *gui, BOOL found, size_t count) {
    if (found) swprintf(gui->process_text, 128, count ? L"Running (%zu processes)" : L"Stopped", count);
    else wcscpy(gui->process_text, L"Installation not available or selection required");
}
static BOOL running_pane(Gui *gui) {
    return gui->model.state == RUNNING || gui->model.state == STOP_REQUESTED ||
           (gui->model.start_locked && (gui->model.state == UNKNOWN || gui->model.state == VERIFYING || gui->model.state == DISPATCHING));
}
static void layout(Gui *gui) {
    RECT r; GetClientRect(gui->window,&r); int m=scaled(gui,32), width=r.right;
    BOOL pane=running_pane(gui); int right=scaled(gui,335), left=width-m*2-(pane?right+scaled(gui,32):0);
    MoveWindow(gui->title,m,scaled(gui,30),width-m*2-scaled(gui,130),scaled(gui,38),TRUE);
    MoveWindow(gui->subtitle,m,scaled(gui,72),width-m*2,scaled(gui,26),TRUE);
    MoveWindow(gui->buttons[6],width-m-scaled(gui,116),scaled(gui,34),scaled(gui,116),scaled(gui,36),TRUE);
    MoveWindow(gui->heading,m,scaled(gui,185),left,scaled(gui,54),TRUE);
    MoveWindow(gui->copy,m,scaled(gui,250),left,scaled(gui,95),TRUE);
    MoveWindow(gui->indicator,m,scaled(gui,358),left,scaled(gui,24),TRUE);
    MoveWindow(gui->primary,m,scaled(gui,410),left<scaled(gui,420)?left:scaled(gui,420),scaled(gui,62),TRUE);
    MoveWindow(gui->progress,m,scaled(gui,490),left,scaled(gui,10),TRUE);
    MoveWindow(gui->elapsed,m,scaled(gui,517),left,scaled(gui,24),TRUE);
    MoveWindow(gui->refresh,m,r.bottom-scaled(gui,98),scaled(gui,185),scaled(gui,32),TRUE);
    MoveWindow(gui->scan,m+scaled(gui,195),r.bottom-scaled(gui,98),scaled(gui,200),scaled(gui,32),TRUE);
    MoveWindow(gui->about_button,m,r.bottom-scaled(gui,48),scaled(gui,180),scaled(gui,30),TRUE);
    MoveWindow(gui->footer,m+scaled(gui,192),r.bottom-scaled(gui,44),width-m*2-scaled(gui,192),scaled(gui,26),TRUE);
    int x=width-m-right;
    MoveWindow(gui->tabs,x,scaled(gui,145),right,scaled(gui,30),TRUE);
    MoveWindow(gui->logs,x,scaled(gui,185),right,r.bottom-scaled(gui,255),TRUE);
    MoveWindow(gui->details,x,scaled(gui,185),right,r.bottom-scaled(gui,255),TRUE);
}
static void render_state(Gui *gui) {
    if(!gui->heading)return;
    const wchar_t *heading=L"State unknown", *copy=L"Renderer work may continue. Refresh status or use Settings to restart safely.", *cta=L"Refresh status";
    BOOL enabled=!gui->busy;
    switch(gui->model.state) {
    case DISCONNECTED: heading=L"Discord not connected";copy=L"Restart Discord with local debugging enabled so 24510_0rb can connect.";cta=L"Restart Discord to Connect";break;
    case RESTARTING: heading=L"Restarting Discord...";copy=L"Waiting for a verified local connection. Orb will not start automatically.";cta=L"Restarting...";enabled=FALSE;break;
    case READY: heading=L"Ready to start";copy=gui->model.observation.route==2?L"Open Discord \x2192 Quests and return here to start":L"Connected to Discord. Ready when you are.";cta=L"Start Orb";break;
    case START_CONFIRM: heading=L"Ready to start";copy=L"Confirm this run or Cancel. Nothing has been dispatched.";cta=L"Start Orb";enabled=FALSE;break;
    case DISPATCHING: heading=L"Verifying Discord Quest Home...";copy=L"Only the approved bytes can be sent, after the owned renderer route is verified.";cta=L"Verifying...";enabled=FALSE;break;
    case RUNNING: heading=L"Orb running";copy=L"Keep Discord Quest Home open while Orb runs";cta=L"Stop Orb";break;
    case STOP_REQUESTED: heading=L"Stop requested \x2014 awaiting confirmation";copy=L"The cooperative hook cannot prove cleanup or rollback. Refresh status to observe the renderer.";cta=L"Stop Orb";enabled=FALSE;break;
    case VERIFYING: heading=L"Verifying status...";copy=L"Read-only observation. This never starts Orb.";cta=L"Verifying...";enabled=FALSE;break;
    case ALL_COMPLETED: heading=L"All completed!";copy=L"All eligible available Discord Quests have been completed.";cta=L"Retry";break;
    case NO_ELIGIBLE: heading=L"No eligible quests";copy=L"No eligible unfinished quests were found in this observation. No run completion is claimed.";cta=L"Check again";break;
    case UNKNOWN:
        if(gui->model.ever_started && (gui->model.observation.route!=1 || !gui->model.observation.signal))copy=L"Quest Home no longer open \x2014 status unknown";
        else if(gui->model.observation.error==2)copy=L"Rate limited (429). State unknown; no automatic retry. Check later.";
        break;
    case ERROR: heading=L"Connection error";copy=L"Check the helper, selected Discord installation and port in Settings.";cta=L"Check connection";break;
    }
    SetWindowTextW(gui->heading,heading);SetWindowTextW(gui->copy,copy);SetWindowTextW(gui->primary,cta);EnableWindow(gui->primary,enabled);
    SetWindowTextW(gui->indicator,gui->model.connected?L"Connected to Discord":L"Not connected");
    BOOL pane=running_pane(gui);
    ShowWindow(gui->tabs,pane?SW_SHOW:SW_HIDE);ShowWindow(gui->logs,pane&&!gui->showing_details?SW_SHOW:SW_HIDE);ShowWindow(gui->details,pane&&gui->showing_details?SW_SHOW:SW_HIDE);
    ShowWindow(gui->progress,pane?SW_SHOW:SW_HIDE);ShowWindow(gui->elapsed,(pane||gui->model.state==ALL_COMPLETED)?SW_SHOW:SW_HIDE);
    EnableWindow(gui->refresh,!gui->busy);EnableWindow(gui->scan,!gui->busy&&gui->model.connected);
    SetWindowTextW(gui->refresh,gui->model.connected?L"Refresh renderer status":L"Check connection");
    OrbSnapshot *o=&gui->model.observation; wchar_t details[500];
    swprintf(details,500,L"Allowlisted observation\r\n\r\nRenderer signal: %ls\r\nStop hook: %ls\r\nEligibility scan: %ls\r\n%ls",o->signal==1?L"Program pending":o->signal==2?L"Program returned; cleanup unknown":L"Unknown",o->hook?L"Present":L"Absent/unknown",o->reliable?L"Successful aggregate response":L"Not verified",gui->model.state==ALL_COMPLETED?L"All observed eligible quests complete.":L"No completion inferred from helper exit.");
    SetWindowTextW(gui->details,details);
    BOOL numeric=gui->model.state==RUNNING&&o->route==1&&o->reliable&&o->progress>=0&&o->target>0;
    LONG_PTR style=GetWindowLongPtrW(gui->progress,GWL_STYLE);
    SetWindowLongPtrW(gui->progress,GWL_STYLE,numeric?style&~PBS_MARQUEE:style|PBS_MARQUEE);
    SendMessageW(gui->progress,PBM_SETMARQUEE,!numeric,50);
    if(numeric){SendMessageW(gui->progress,PBM_SETRANGE32,0,100);SendMessageW(gui->progress,PBM_SETPOS,(WPARAM)((long long)o->progress*100/o->target),0);}
    wchar_t elapsed[240];ULONGLONG seconds=gui->run_started?(GetTickCount64()-gui->run_started)/1000:0;
    const wchar_t *types[]={L"Activity unavailable",L"Video",L"Desktop play",L"Desktop stream",L"Activity play"};
    if(gui->model.state==ALL_COMPLETED)swprintf(elapsed,240,L"Observed completed: %u | Elapsed: %llu:%02llu",gui->model.completed_count,seconds/60,seconds%60);
    else swprintf(elapsed,240,L"%ls | Elapsed: %llu:%02llu",types[gui->model.state==RUNNING&&o->route==1&&o->activity>=0&&o->activity<=4?o->activity:0],seconds/60,seconds%60);
    SetWindowTextW(gui->elapsed,elapsed);layout(gui);InvalidateRect(gui->window,NULL,FALSE);
}
static LRESULT CALLBACK button_style(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    (void)id;Gui *gui=(Gui *)data;
    if(message==WM_MOUSEMOVE){if(gui->hover!=window){gui->hover=window;TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);InvalidateRect(window,NULL,FALSE);}}
    if(message==WM_MOUSELEAVE){gui->hover=NULL;InvalidateRect(window,NULL,FALSE);}
    return DefSubclassProc(window,message,wp,lp);
}
static BOOL CALLBACK set_font(HWND child, LPARAM font) { SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE); return TRUE; }
static void update_fonts(Gui *gui) {
    HFONT old = gui->font, old_title = gui->title_font;
    gui->font = CreateFontW(-scaled(gui, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    gui->title_font = CreateFontW(-scaled(gui, 25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    EnumChildWindows(gui->window, set_font, (LPARAM)gui->font); SendMessageW(gui->heading, WM_SETFONT, (WPARAM)gui->title_font, TRUE); SendMessageW(gui->title, WM_SETFONT, (WPARAM)gui->title_font, TRUE);
    if (old) DeleteObject(old);
    if (old_title) DeleteObject(old_title);
}
static INT_PTR CALLBACK confirm_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(dialog, ID_CONFIRM_TEXT, L"Discord will close and reopen with loopback-only local debugging enabled (127.0.0.1). Active calls or streams may be interrupted. Orb will not start automatically.\nDo you want to restart and connect?");
#ifdef ORB_UI_TEST_MOCK
        SetWindowTextW(dialog, L"Restart Discord to connect? (MOCK TEST)");
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
        CheckDlgButton(dialog,ID_SETTINGS_AUTO_CONNECT,gui->settings.passive_connect_on_start?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(dialog,ID_SETTINGS_SCAN,gui->settings.quest_status_opt_in?BST_CHECKED:BST_UNCHECKED);
        for (int id = ID_PORT_EDIT; id <= ID_ENDPOINT_EDIT; ++id) SendDlgItemMessageW(dialog, id, EM_SETLIMITTEXT, 9, 0);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if(message==WM_COMMAND && (LOWORD(wparam)==ID_SETTINGS_SCRIPT_BROWSE || LOWORD(wparam)==ID_SETTINGS_DISCORD_BROWSE)) {
        wchar_t *path=calloc(DISCORD_PATH_CAP,sizeof(wchar_t));if(!path)return TRUE;
        OPENFILENAMEW open={0};open.lStructSize=sizeof(open);open.hwndOwner=dialog;open.lpstrFile=path;open.nMaxFile=DISCORD_PATH_CAP;
        open.lpstrFilter=LOWORD(wparam)==ID_SETTINGS_DISCORD_BROWSE?L"Executable files\0*.exe\0\0":L"JavaScript files\0*.js\0All files\0*.*\0\0";
        open.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
        if(GetOpenFileNameW(&open))SetDlgItemTextW(dialog,LOWORD(wparam)==ID_SETTINGS_DISCORD_BROWSE?ID_DISCORD_EDIT:ID_SCRIPT_EDIT,path);
        free(path);return TRUE;
    }
    if(message==WM_COMMAND && LOWORD(wparam)==ID_SETTINGS_ORIGINAL){
        EndDialog(dialog,IDCANCEL);PostMessageW(gui->window,WM_COMMAND,ID_ORIGINAL,0);return TRUE;
    }
    if(message==WM_COMMAND && (LOWORD(wparam)==ID_SETTINGS_CONNECT || LOWORD(wparam)==ID_SETTINGS_RESTART || LOWORD(wparam)==ID_SETTINGS_RUN || LOWORD(wparam)==ID_SETTINGS_CANCEL)){
        int next=LOWORD(wparam)==ID_SETTINGS_CONNECT?ID_CONNECT:LOWORD(wparam)==ID_SETTINGS_RESTART?ID_RESTART:LOWORD(wparam)==ID_SETTINGS_RUN?ID_RUN:ID_STOP;
        EndDialog(dialog,IDCANCEL);PostMessageW(gui->window,WM_COMMAND,next,0);return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        AppSettings *next = malloc(sizeof(*next)); if (!next) return TRUE; *next = gui->settings;
        int ids[] = {ID_PORT_EDIT, ID_OPERATION_EDIT, ID_EXIT_EDIT, ID_ENDPOINT_EDIT}; DWORD mins[] = {1,500,1000,1000}, maxs[] = {65535,120000,60000,120000};
        const wchar_t *errors[] = {L"Port must be between 1 and 65535.", L"Operation timeout must be 500 to 120000 ms.", L"Exit timeout must be 1000 to 60000 ms.", L"Renderer timeout must be 1000 to 120000 ms."};
        BOOL valid = TRUE; DWORD values[4]; wchar_t text[64];
        for (unsigned i = 0; i < 4; ++i) { GetDlgItemTextW(dialog, ids[i], text, 64); if (!settings_valid_number(text, mins[i], maxs[i], &values[i])) { SetDlgItemTextW(dialog, ID_SETTINGS_ERROR, errors[i]); valid = FALSE; break; } }
        if (valid) {
            next->passive_connect_on_start=IsDlgButtonChecked(dialog,ID_SETTINGS_AUTO_CONNECT)==BST_CHECKED;
            next->quest_status_opt_in=IsDlgButtonChecked(dialog,ID_SETTINGS_SCAN)==BST_CHECKED;
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
                orb_connection(&gui->model,false);
            }
            gui->settings = *next; refresh_status(gui); append_log(gui, L"Settings saved."); EndDialog(dialog, IDOK);
        }
        free(next); return TRUE;
    }
    return FALSE;
}
static BOOL choose_script(HWND owner, wchar_t *path) {
    wchar_t *file = calloc(DISCORD_PATH_CAP, sizeof(wchar_t)); if (!file) return FALSE;
    OPENFILENAMEW open = {0}; open.lStructSize = sizeof(open); open.hwndOwner = owner;
    open.lpstrFilter = L"JavaScript files (*.js)\0*.js\0All files\0*.*\0\0";
    open.lpstrFile = file; open.nMaxFile = DISCORD_PATH_CAP;
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    BOOL selected = GetOpenFileNameW(&open); if (selected) wcscpy(path, file);
    free(file); return selected;
}
static BOOL repo_original(Gui *gui, wchar_t *path) {
    swprintf(path,DISCORD_PATH_CAP,L"%ls\\payloads\\original-orb.js",gui->directory);
    if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES)return TRUE;
    /* Portable package uses this exact attributed file; source builds also
       resolve the repository copy above their bin directory. Selection only. */
    swprintf(path, DISCORD_PATH_CAP, L"%ls\\legacy\\dc_cmd_script.js", gui->directory);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return TRUE;
    swprintf(path, DISCORD_PATH_CAP, L"%ls\\..\\legacy\\dc_cmd_script.js", gui->directory);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
typedef struct { Gui *gui; OrbPayload payload; wchar_t path[DISCORD_PATH_CAP]; } OrbReview;
static void review_load(HWND dialog, OrbReview *review) {
    orb_payload_close(&review->payload);
    BOOL ok = orb_payload_open(review->path, &review->payload);
    wchar_t info[256];
    if (ok) {
        swprintf(info, 256, L"%zu bytes | SHA-256: %hs\r\n%ls", review->payload.length, review->payload.sha256,
                 !strcmp(review->payload.sha256, ORB_ORIGINAL_SHA) ? L"Exact preserved original (0x440_1me)." : L"User-selected payload; original identity not verified.");
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, review->payload.bytes, -1, NULL, 0);
        wchar_t *source = calloc((size_t)count, sizeof(wchar_t));
        if (source) { MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, review->payload.bytes, -1, source, count); SetDlgItemTextW(dialog, ID_ORB_SOURCE, source); free(source); }
        else { ok = FALSE; orb_payload_close(&review->payload); }
    } else wcscpy(info, L"Select a readable UTF-8 JavaScript file (1 byte to 4 MiB; no NUL bytes).");
    if (!ok) SetDlgItemTextW(dialog, ID_ORB_SOURCE, L"");
    SetDlgItemTextW(dialog, ID_ORB_INFO, ok ? info : L"Cannot review file. Select a readable UTF-8 .js file, at most 4 MiB.");
    EnableWindow(GetDlgItem(dialog, IDOK), ok && !review->gui->model.start_locked);
}
static INT_PTR CALLBACK script_confirmation(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(dialog, ID_CONFIRM_TEXT, (const wchar_t *)lparam);
        SendMessageW(dialog, DM_SETDEFID, IDCANCEL, 0); SetFocus(GetDlgItem(dialog, IDCANCEL)); return FALSE;
    }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) { EndDialog(dialog, LOWORD(wparam)); return TRUE; }
    return FALSE;
}
static BOOL consent_script(Gui *gui, OrbPayload *payload, BOOL original) {
    wchar_t text[1200];
    swprintf(text, 1200, L"%ls\r\n\r\n%zu bytes | SHA-256: %hs\r\n\r\nThese exact bytes will execute using the authenticated Discord client. Arbitrary scripts can affect your account. Stop only requests cancellation; renderer state remains unknown.\r\n\r\nRun once now? Cancel is the default.",
        original ? L"Original Orb automatically enrolls/updates quests and fakes activity/progress using unofficial client internals and logged-in client permissions. It may violate Discord rules. Stop is best-effort. Keep Discord Quest Home open while it runs. Author: 0x440_1me; separately attributed, excluded from MIT." : L"Run selected JavaScript? Quest scripts may automatically enroll/update quests and fake activity, potentially violating Discord rules.",
        payload->length, payload->sha256);
    return DialogBoxParamW(gui->instance, MAKEINTRESOURCEW(IDD_SCRIPT_CONFIRM), GetLastActivePopup(gui->window), script_confirmation, (LPARAM)text) == IDOK;
}
static INT_PTR CALLBACK original_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    OrbReview *review = (OrbReview *)GetWindowLongPtrW(dialog, DWLP_USER);
    if (message == WM_INITDIALOG) {
        review = (OrbReview *)lparam; SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)review);
        SendDlgItemMessageW(dialog, ID_ORB_SOURCE, EM_SETLIMITTEXT, ORB_PAYLOAD_LIMIT, 0);
        if (!*review->path) repo_original(review->gui, review->path);
        review_load(dialog, review); SendMessageW(dialog, DM_SETDEFID, IDCANCEL, 0); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == ID_ORB_SELECT) { if (choose_script(dialog, review->path)) review_load(dialog, review); return TRUE; }
        if (LOWORD(wparam) == ID_ORB_DEFAULT) {
            if (repo_original(review->gui, review->path)) review_load(dialog, review);
            else SetDlgItemTextW(dialog, ID_ORB_INFO, L"Original absent. Re-extract the complete portable package or select a local file.");
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK && review->payload.bytes && !review->gui->model.start_locked && consent_script(review->gui, &review->payload, TRUE)) { EndDialog(dialog, IDOK); return TRUE; }
    }
    return FALSE;
}
static BOOL helper_present(Gui *gui) {
    wchar_t path[DISCORD_PATH_CAP]; swprintf(path, DISCORD_PATH_CAP, L"%ls\\orb-cdp.exe", gui->directory);
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        append_log(gui, L"Missing orb-cdp.exe. Extract both executables into the same folder.");
        SetWindowTextW(gui->activity, L"Missing helper: extract orb-cdp.exe beside this app."); return FALSE;
    }
    return TRUE;
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
    if (!result) { orb_payload_close(&work->reviewed); InterlockedExchange(work->post_failed, 1); free(work); return 0; }
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
        CdpRequest request = {work->directory, work->settings.payload_path, work->settings.port, work->kind == WORK_CONNECT ? CDP_CONNECT : work->kind == WORK_ORB_STOP ? CDP_STOP_QUEST : work->kind == WORK_ORIGINAL ? CDP_ORB_START : work->kind == WORK_STATUS ? CDP_ORB_STATUS : work->kind == WORK_SCAN ? CDP_ORB_SCAN : CDP_EVALUATE, work->settings.operation_timeout_ms, work->cancel, work->settings.discord_executable, work->reviewed.bytes ? work->reviewed.sha256 : NULL, *work->orb_key ? work->orb_key : NULL};
        cdp_run(&request, &result->cdp);
    }
    installation = calloc(1, sizeof(*installation));
    if (installation) { result->discord_found = discord_discover(work->settings.discord_executable, installation, error, 512); result->process_count = installation->process_count; discord_release(installation); free(installation); }
    if (!PostMessageW(work->notify, WM_WORK_DONE, 0, (LPARAM)result)) { free(result); InterlockedExchange(work->post_failed, 1); }
    orb_payload_close(&work->reviewed); free(work); return 0;
}
static void busy_controls(Gui *gui, BOOL busy) {
    gui->busy = busy;
    for (unsigned i = 0; i < 7; ++i) if (i != 5) EnableWindow(gui->buttons[i], i == 4 ? busy : !busy);
    EnableWindow(gui->original_button, !busy);
    EnableWindow(gui->orb_stop_button, !busy);
    EnableWindow(gui->about_button, !busy);
    render_state(gui);
}
static void begin_work(Gui *gui, WorkKind kind) {
    if (gui->busy) return;
    if ((kind == WORK_ORIGINAL || kind == WORK_SCRIPT) && gui->model.start_locked) {
        orb_payload_close(&gui->approved);append_log(gui, L"Start refused: Original Orb state unknown. Request Orb stop; verified restart is required before another run."); return;
    }
    if (!helper_present(gui)) { orb_payload_close(&gui->approved); return; }
    Work *work = calloc(1, sizeof(*work)); gui->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!work || !gui->cancel) { free(work); if (gui->cancel) CloseHandle(gui->cancel); gui->cancel = NULL; orb_payload_close(&gui->approved); append_log(gui, L"Could not start operation."); return; }
    work->settings = gui->settings; wcscpy(work->directory, gui->directory); work->cancel = gui->cancel; work->notify = gui->window; work->post_failed = &gui->post_failed; work->kind = kind;
    work->reviewed = gui->approved; memset(&gui->approved, 0, sizeof(gui->approved));
    if(kind==WORK_ORIGINAL){
        if(!gui->model.connected || gui->model.start_locked){orb_payload_close(&work->reviewed);free(work);CloseHandle(gui->cancel);gui->cancel=NULL;append_log(gui,L"Start refused: connect first or restart to reset unknown renderer state.");return;}
        if(gui->model.state!=START_CONFIRM)orb_start_confirm(&gui->model);
        if(!orb_start(&gui->model)){orb_payload_close(&work->reviewed);free(work);CloseHandle(gui->cancel);gui->cancel=NULL;return;}
        unsigned char nonce[16];
        if(BCryptGenRandom(NULL,nonce,sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0){orb_preflight_refused(&gui->model);orb_payload_close(&work->reviewed);free(work);CloseHandle(gui->cancel);gui->cancel=NULL;render_state(gui);return;}
        for(unsigned i=0;i<16;++i)sprintf(gui->orb_key+i*2,"%02x",nonce[i]);
        gui->run_started=GetTickCount64(); gui->last_route_check=gui->run_started;
    }
    strcpy(work->orb_key,gui->orb_key);
    if(kind==WORK_RESTART)gui->model.state=RESTARTING;
    if(kind==WORK_STATUS||kind==WORK_SCAN)orb_verify(&gui->model);
    if(kind==WORK_ORB_STOP)orb_request_stop(&gui->model);
    InterlockedExchange(&gui->post_failed, 0); busy_controls(gui, TRUE);
    gui->thread = CreateThread(NULL, 0, worker, work, 0, NULL);
    if (!gui->thread) { orb_payload_close(&work->reviewed); free(work); CloseHandle(gui->cancel); gui->cancel = NULL; gui->model.state=UNKNOWN;busy_controls(gui, FALSE); append_log(gui, L"Worker could not be created."); }
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
        if (success) {orb_restart_verified(&gui->model);gui->orb_key[0]=0;gui->run_started=0;}else gui->model.state=ERROR;
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
        if(result->kind==WORK_CONNECT)orb_connection(&gui->model,success!=FALSE);
        if(result->kind==WORK_ORIGINAL){
            if(cdp->status==CDP_FINISHED && cdp->child_exit_code==9 && strstr(cdp->output,"ORB_PREFLIGHT refused; zero payload dispatch.")){
                orb_preflight_refused(&gui->model);gui->run_started=0;gui->orb_key[0]=0;
            }else{int signal=0;const char *line=strstr(cdp->output,"ORB_LIFECYCLE signal=");if(line)sscanf(line,"ORB_LIFECYCLE signal=%d",&signal);orb_dispatch_result(&gui->model,success!=FALSE,signal);}
        }
        if(result->kind==WORK_STATUS||result->kind==WORK_SCAN){
            OrbSnapshot o={0};o.progress=o.target=-1;const char *line=strstr(cdp->output,"ORB_STATUS ");
            int fields=line?sscanf(line,"ORB_STATUS signal=%d hook=%d active=%d reliable=%d error=%d eligible=%d completed=%d unaccepted=%d unfinished=%d progress=%d target=%d activity=%d route=%d",&o.signal,&o.hook,&o.active,&o.reliable,&o.error,&o.eligible,&o.completed,&o.unaccepted,&o.unfinished,&o.progress,&o.target,&o.activity,&o.route):0;
            if(!success||fields!=13){gui->model.state=UNKNOWN;gui->model.start_locked=true;gui->model.observation.route=0;gui->model.observation.progress=gui->model.observation.target=-1;gui->model.observation.reliable=0;}
            else orb_observe(&gui->model,&o,result->kind==WORK_SCAN);
        }
        if(result->kind==WORK_ORB_STOP){if(success){if(gui->model.start_locked)gui->model.state=STOP_REQUESTED;}else{gui->model.state=UNKNOWN;gui->model.start_locked=true;}}
        if (result->kind == WORK_ORIGINAL) append_log(gui, L"Original Orb dispatch attempted / renderer state unknown. Helper completion does not mean the program finished.");
        if (result->kind == WORK_ORB_STOP) append_log(gui, success ? L"Stop requested / state unknown. Repeat starts remain blocked." : L"Stop delivery failed / state unknown. Repeat starts remain blocked.");
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
        const wchar_t *subtitle = L"Discord Quest Runner | Account-free mock test build";
#else
        const wchar_t *subtitle = L"Discord Quest Runner";
#endif
        gui->subtitle = CreateWindowW(L"STATIC",subtitle,WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->background_brush=CreateSolidBrush(RGB(19,27,40));gui->panel_brush=CreateSolidBrush(RGB(29,41,56));
        gui->group = CreateWindowW(L"BUTTON",L"Connection",WS_CHILD|BS_GROUPBOX,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->status = CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,window,(HMENU)ID_STATUS,gui->instance,NULL);
        gui->payload = CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,window,(HMENU)ID_PAYLOAD,gui->instance,NULL);
        const wchar_t *labels[] = {L"Connect",L"Restart Discord",L"Load Script",L"Run Script",L"Cancel helper",L"Logs",L"Settings"};
        for (unsigned i=0;i<7;++i) gui->buttons[i]=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|(i==6?WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW:BS_PUSHBUTTON),0,0,0,0,window,(HMENU)(INT_PTR)(ID_CONNECT+i),gui->instance,NULL);
        gui->original_button = CreateWindowW(L"BUTTON", L"Original Orb (experimental)", WS_CHILD,0,0,0,0,window,(HMENU)ID_ORIGINAL,gui->instance,NULL);
        gui->orb_stop_button = CreateWindowW(L"BUTTON", L"Request Orb stop", WS_CHILD,0,0,0,0,window,(HMENU)ID_ORB_STOP,gui->instance,NULL);
        gui->about_button = CreateWindowW(L"BUTTON", L"Help / About", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,(HMENU)ID_ABOUT,gui->instance,NULL);
        gui->activity = CreateWindowW(L"STATIC",L"Ready. Scripts run only when you choose Run Script.",WS_CHILD,0,0,0,0,window,(HMENU)ID_ACTIVITY,gui->instance,NULL);
        gui->logs = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,0,0,window,(HMENU)ID_LOG_EDIT,gui->instance,NULL);
        gui->footer = CreateWindowW(L"STATIC",L"1.0.0 | Unofficial | Original Orb is opt-in",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->heading=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->copy=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->indicator=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,NULL,gui->instance,NULL);
        gui->primary=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,(HMENU)ID_PRIMARY,gui->instance,NULL);
        gui->progress=CreateWindowW(PROGRESS_CLASSW,L"",WS_CHILD|PBS_MARQUEE,0,0,0,0,window,(HMENU)ID_PROGRESS,gui->instance,NULL);
        SendMessageW(gui->progress,PBM_SETBARCOLOR,0,RGB(119,221,176));SendMessageW(gui->progress,PBM_SETBKCOLOR,0,RGB(29,41,56));
        gui->elapsed=CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,window,(HMENU)ID_ELAPSED,gui->instance,NULL);
        gui->refresh=CreateWindowW(L"BUTTON",L"Check connection",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,(HMENU)ID_REFRESH,gui->instance,NULL);
        gui->scan=CreateWindowW(L"BUTTON",L"Read-only quest scan",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,(HMENU)ID_SCAN,gui->instance,NULL);
        gui->details=CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,0,0,window,(HMENU)ID_DETAILS,gui->instance,NULL);
        gui->tabs=CreateWindowW(L"BUTTON",L"Logs / Details",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,(HMENU)ID_TABS,gui->instance,NULL);
        HWND styled[]={gui->primary,gui->buttons[6],gui->about_button,gui->refresh,gui->scan,gui->tabs};
        for(unsigned i=0;i<sizeof(styled)/sizeof(styled[0]);++i)SetWindowSubclass(styled[i],button_style,1,(DWORD_PTR)gui);
        SetWindowTextW(gui->buttons[6],L"Settings");
        update_fonts(gui); busy_controls(gui,FALSE); wcscpy(gui->endpoint_text,L"Not checked"); wcscpy(gui->renderer_text,L"Not verified");
        DiscordInstallation *installation=calloc(1,sizeof(*installation)); wchar_t error[512]; BOOL found=installation&&discord_discover(gui->settings.discord_executable,installation,error,512);
        detected_text(gui,found,installation?installation->process_count:0); if(installation){discord_release(installation);free(installation);} refresh_status(gui);
        if (!gui->title || !gui->subtitle || !gui->group || !gui->status || !gui->payload || !gui->activity || !gui->logs || !gui->footer || !gui->font || !gui->title_font) return -1;
        for (unsigned i = 0; i < 7; ++i) if (!gui->buttons[i]) return -1;
        append_log(gui,L"Ready. No script runs automatically."); helper_present(gui); if (!SetTimer(window,1,100,NULL)) return -1; return 0;
    }
    case WM_ERASEBKGND: {RECT r;GetClientRect(window,&r);FillRect((HDC)wparam,&r,gui->background_brush);return 1;}
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
        HDC dc=(HDC)wparam;HWND child=(HWND)lparam;
        BOOL panel=child==gui->logs||child==gui->details;
        SetBkColor(dc,panel?RGB(29,41,56):RGB(19,27,40));
        SetTextColor(dc,child==gui->indicator&&gui->model.connected?RGB(119,221,176):child==gui->heading||child==gui->title?RGB(243,246,251):RGB(174,186,206));
        return (LRESULT)(panel?gui->panel_brush:gui->background_brush);
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT *)lparam;if(draw->CtlType!=ODT_BUTTON)break;
        BOOL primary=draw->hwndItem==gui->primary, disabled=(draw->itemState&ODS_DISABLED)!=0;
        COLORREF color=primary?(gui->hover==draw->hwndItem?RGB(255,165,202):RGB(245,142,184)):RGB(29,41,56);
        if(disabled)color=RGB(48,58,75);
        HBRUSH brush=CreateSolidBrush(color);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
        SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,disabled?RGB(174,186,206):primary?RGB(19,27,40):RGB(243,246,251));
        wchar_t text[256];GetWindowTextW(draw->hwndItem,text,256);SelectObject(draw->hDC,gui->font);RECT text_rect=draw->rcItem;DrawTextW(draw->hDC,text,-1,&text_rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(draw->itemState&ODS_FOCUS){RECT focus=draw->rcItem;InflateRect(&focus,-5,-5);DrawFocusRect(draw->hDC,&focus);}return TRUE;
    }
    case WM_SIZE: layout(gui); return 0;
    case WM_DPICHANGED: { gui->dpi=HIWORD(wparam); update_fonts(gui); RECT *r=(RECT *)lparam; SetWindowPos(window,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER); layout(gui); return 0; }
    case WM_GETMINMAXINFO: { MINMAXINFO *size=(MINMAXINFO *)lparam; size->ptMinTrackSize.x=scaled(gui,850); size->ptMinTrackSize.y=scaled(gui,620); return 0; }
    case WM_COMMAND:
        if(LOWORD(wparam)==ID_TABS){gui->showing_details=!gui->showing_details;render_state(gui);return 0;}
        if(LOWORD(wparam)==ID_PRIMARY&&!gui->busy){
            if(gui->model.state==DISCONNECTED||gui->model.state==ERROR){
                INT_PTR consent=DialogBoxParamW(gui->instance,MAKEINTRESOURCEW(IDD_RESTART),window,confirm_dialog,0);
                if(consent==IDOK)begin_work(gui,WORK_RESTART);else append_log(gui,L"Restart cancelled; Discord untouched.");
            }else if(gui->model.state==READY){
                if(!helper_present(gui)||!orb_start_confirm(&gui->model))return 0;
                render_state(gui);
                if(!orb_payload_open(gui->settings.payload_path,&gui->approved)){orb_cancel_confirm(&gui->model);append_log(gui,L"Payload missing/invalid. Re-extract the package or select a local file in Advanced.");render_state(gui);return 0;}
                BOOL original=!strcmp(gui->approved.sha256,ORB_ORIGINAL_SHA);
                if(consent_script(gui,&gui->approved,original))begin_work(gui,WORK_ORIGINAL);
                else{orb_payload_close(&gui->approved);orb_cancel_confirm(&gui->model);append_log(gui,L"Run cancelled; nothing sent.");render_state(gui);}
            }else if(gui->model.state==RUNNING)begin_work(gui,WORK_ORB_STOP);
            else if(gui->model.state==ALL_COMPLETED||gui->model.state==NO_ELIGIBLE)begin_work(gui,WORK_SCAN);
            else if(gui->model.state==UNKNOWN)begin_work(gui,gui->model.connected?WORK_STATUS:WORK_CONNECT);
            return 0;
        }
        if(LOWORD(wparam)==ID_REFRESH&&!gui->busy){begin_work(gui,gui->model.connected?WORK_STATUS:WORK_CONNECT);return 0;}
        if(LOWORD(wparam)==ID_SCAN&&!gui->busy){begin_work(gui,WORK_SCAN);return 0;}
        if (LOWORD(wparam)==ID_STOP && gui->busy) { SetEvent(gui->cancel); SetWindowTextW(gui->activity,L"Cancelling managed operation..."); return 0; }
        if (LOWORD(wparam)==ID_LOGS) { gui->logs_visible=!gui->logs_visible; ShowWindow(gui->logs,gui->logs_visible?SW_SHOW:SW_HIDE); return 0; }
        if (gui->busy) return 0;
        if (LOWORD(wparam)==ID_CONNECT) begin_work(gui,WORK_CONNECT);
        else if (LOWORD(wparam)==ID_RUN) {
            if (gui->model.start_locked) { append_log(gui,L"Start refused: renderer state unknown; verified restart required."); return 0; }
            if (!helper_present(gui)) return 0;
            if (!orb_payload_open(gui->settings.payload_path, &gui->approved)) { append_log(gui,L"Cannot review payload. Select a readable UTF-8 file, at most 4 MiB."); return 0; }
            BOOL original = !strcmp(gui->approved.sha256, ORB_ORIGINAL_SHA);
            if (consent_script(gui, &gui->approved, original)) begin_work(gui, original ? WORK_ORIGINAL : WORK_SCRIPT);
            else { orb_payload_close(&gui->approved); append_log(gui,L"Run cancelled; nothing sent."); }
        }
        else if (LOWORD(wparam)==ID_ORB_STOP) begin_work(gui,WORK_ORB_STOP);
        else if (LOWORD(wparam)==ID_ABOUT) MessageBoxW(window,
            L"24510_0rb " ORB_VERSION_LABEL L"\n\nKeep orb-cdp.exe beside the app. Set your port and approved Discord image in Settings; Connect verifies the endpoint. Load Script selects a local .js; Run requires confirmation.\n\nOriginal Orb (experimental) opens local import/review. The unchanged script is bundled under legacy/ with owner-authorized distribution and separate notices. Never runs on startup, Connect, restart or selection.\n\nCancel helper only cancels the native worker. Request Orb stop calls window.stopQuestProgram(); renderer state remains unknown.\n\nUnofficial. Unsigned. Original author: 0x440_1me; legacy excluded from MIT. See legacy/NOTICE.md.", L"Quick start / About", MB_OK|MB_ICONINFORMATION);
        else if (LOWORD(wparam)==ID_ORIGINAL) {
            if (!helper_present(gui)) return 0;
            OrbReview *review = calloc(1, sizeof(*review)); if (!review) return 0;
            review->gui = gui; wcscpy(review->path, gui->original_path);
            INT_PTR answer = DialogBoxParamW(gui->instance, MAKEINTRESOURCEW(IDD_ORIGINAL), window, original_dialog, (LPARAM)review);
            if (answer == IDOK) {
                wcscpy(gui->original_path, review->path); wcscpy(gui->settings.payload_path, review->path);
                gui->approved = review->payload; memset(&review->payload, 0, sizeof(review->payload));
                refresh_status(gui); begin_work(gui,WORK_ORIGINAL);
            } else append_log(gui,L"Original Orb review closed; nothing sent.");
            orb_payload_close(&review->payload); free(review);
        }
        else if (LOWORD(wparam)==ID_RESTART) { INT_PTR consent=DialogBoxParamW(gui->instance,MAKEINTRESOURCEW(IDD_RESTART),window,confirm_dialog,0); if(consent==IDOK)begin_work(gui,WORK_RESTART);else append_log(gui,L"Restart cancelled; Discord untouched."); }
        else if (LOWORD(wparam)==ID_SETTINGS) DialogBoxParamW(gui->instance,MAKEINTRESOURCEW(IDD_SETTINGS),window,settings_dialog,(LPARAM)gui);
        else if (LOWORD(wparam)==ID_LOAD) {
            wchar_t *file=calloc(DISCORD_PATH_CAP,sizeof(wchar_t)); if(!file)return 0; OPENFILENAMEW open={0};open.lStructSize=sizeof(open);open.hwndOwner=window;open.lpstrFilter=L"JavaScript files (*.js)\0*.js\0All files\0*.*\0\0";open.lpstrFile=file;open.nMaxFile=DISCORD_PATH_CAP;open.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
            if(GetOpenFileNameW(&open)){wcscpy(gui->settings.payload_path,file);refresh_status(gui);if(!settings_save(&gui->settings))append_log(gui,L"Script selected; preferences could not be saved.");else append_log(gui,L"Script selected. Choose Run Script to execute.");}free(file);
        }
        return 0;
 #ifdef ORB_UI_TEST_MOCK
    case (WM_APP + 70): {
        if (!gui || gui->busy || wparam < 1 || wparam > 3) return 0;
        orb_state_init(&gui->model);
        gui->model.connected=true;
        gui->model.observation.route=1;
        if(wparam==1){gui->model.state=READY;}
        else if(wparam==2){gui->model.state=RUNNING;gui->model.ever_started=true;gui->model.start_locked=true;gui->model.observation.signal=1;gui->model.observation.hook=1;gui->model.observation.active=1;gui->model.observation.reliable=1;gui->model.observation.eligible=3;gui->model.observation.completed=1;gui->model.observation.unfinished=2;gui->model.observation.progress=40;gui->model.observation.target=100;gui->model.observation.activity=1;gui->run_started=GetTickCount64()-125000;}
        else {gui->model.state=ALL_COMPLETED;gui->model.ever_started=true;gui->model.start_locked=true;gui->model.observation.signal=2;gui->model.observation.reliable=1;gui->model.observation.eligible=3;gui->model.observation.completed=3;gui->model.completed_count=3;gui->run_started=GetTickCount64()-125000;}
        render_state(gui);
        return 1;
    }
 #endif
    case WM_WORK_DONE: completed(gui,(WorkResult *)lparam); return 0;
    case WM_TIMER:
        if(!gui->busy && gui->model.state==RUNNING && GetTickCount64()-gui->last_route_check>=5000){gui->last_route_check=GetTickCount64();begin_work(gui,WORK_STATUS);}
        if(!gui->busy&&gui->run_started)render_state(gui);
        if(gui->busy && gui->thread && WaitForSingleObject(gui->thread,0)==WAIT_OBJECT_0 && InterlockedCompareExchange(&gui->post_failed,0,0)){gui->model.state=UNKNOWN;append_log(gui,L"Worker result could not be delivered.");finish_work(gui);}return 0;
    case WM_CLOSE: if(gui->busy){gui->close_pending=TRUE;SetEvent(gui->cancel);SetWindowTextW(gui->activity,L"Cancelling before close...");}else DestroyWindow(window); return 0;
    case WM_DESTROY: if(gui->background_brush)DeleteObject(gui->background_brush);if(gui->panel_brush)DeleteObject(gui->panel_brush); KillTimer(window,1); if(gui->font)DeleteObject(gui->font);if(gui->title_font)DeleteObject(gui->title_font); PostQuitMessage(0); return 0;
    case WM_NCDESTROY: SetWindowLongPtrW(window,GWLP_USERDATA,0); return DefWindowProcW(window,message,wparam,lparam);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
int gui_run(HINSTANCE instance, int show) {
#ifdef ORB_UI_TEST_MOCK
    const wchar_t *mutex_name = L"Local\\24510_0rb_mock_controller";
#else
    const wchar_t *mutex_name = L"Local\\24510_0rb_controller";
#endif
    HANDLE singleton = CreateMutexW(NULL, FALSE, mutex_name);
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singleton) CloseHandle(singleton);
        MessageBoxW(NULL,L"A controller is already open. Use that window to avoid duplicate starts.",L"24510_0rb",MB_OK|MB_ICONINFORMATION); return 1;
    }
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    Gui *gui=calloc(1,sizeof(*gui));if(!gui)return 8;gui->instance=instance;gui->dpi=96;orb_state_init(&gui->model);
    GetModuleFileNameW(NULL,gui->directory,DISCORD_PATH_CAP);wchar_t *slash=wcsrchr(gui->directory,L'\\');if(!slash){free(gui);return 1;}*slash=0;
    if(!settings_load(&gui->settings,gui->directory))settings_defaults(&gui->settings,gui->directory);
#ifdef ORB_UI_TEST_MOCK
    const wchar_t *class_name=L"24510_ORB_Mock_Window", *title=L"24510_0rb - MOCK RESTART TEST";
#else
    const wchar_t *class_name=L"24510_ORB_Window", *title=L"24510_0rb";
#endif
    WNDCLASSEXW window_class={0};window_class.cbSize=sizeof(window_class);window_class.lpfnWndProc=window_proc;window_class.hInstance=instance;window_class.hCursor=LoadCursorW(NULL,IDC_ARROW);window_class.hIcon=LoadIconW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDI_ORB));if(!window_class.hIcon)window_class.hIcon=LoadIconW(NULL,IDI_APPLICATION);window_class.hIconSm=window_class.hIcon;window_class.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);window_class.lpszClassName=class_name;
    if(!RegisterClassExW(&window_class)){free(gui);return 1;}
    HWND window=CreateWindowExW(0,class_name,title,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,990,730,NULL,NULL,instance,gui);
    if(!window){free(gui);return 1;}ShowWindow(window,show);UpdateWindow(window);
    MSG message={0}; BOOL received;while((received=GetMessageW(&message,NULL,0,0))>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    orb_payload_close(&gui->approved); free(gui); CloseHandle(singleton); return received < 0 ? 1 : (int)message.wParam;
}
