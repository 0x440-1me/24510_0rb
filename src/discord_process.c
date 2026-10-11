#define _WIN32_WINNT 0x0602
#include <winsock2.h>
#include <ws2tcpip.h>
#include "discord_process.h"
#include <iphlpapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static BOOL cancel_requested(HANDLE event) { return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0; }
static BOOL fail(wchar_t *error, size_t capacity, const wchar_t *message) {
    if (error && capacity) { wcsncpy(error, message, capacity - 1); error[capacity - 1] = 0; }
    return FALSE;
}

void discord_restart(const DiscordRestartRequest *request, const DiscordRestartOps *ops, DiscordRestartResult *result) {
    memset(result, 0, sizeof(*result));
    result->status = DISCORD_NO_CONSENT;
    if (!request || !request->consent) { fail(result->diagnostic, 512, L"Restart was not confirmed; Discord untouched."); return; }
    if (cancel_requested(request->cancel_event)) { result->status = DISCORD_CANCELLED; return; }
    result->status = DISCORD_DISCOVERY_FAILED;
    if (!ops || !ops->release || !request->port || !request->exit_timeout_ms || !request->endpoint_timeout_ms) {
        fail(result->diagnostic, 512, L"Invalid restart configuration."); return;
    }
    for (unsigned i = 0; i < DISCORD_STAGE_COUNT; ++i)
        if (!ops->stages[i]) { fail(result->diagnostic, 512, L"Incomplete restart operations."); return; }
    DiscordInstallation *installation = calloc(1, sizeof(*installation));
    if (!installation) { fail(result->diagnostic, 512, L"Out of memory."); return; }
    static const DiscordRestartStatus failures[] = {
        DISCORD_DISCOVERY_FAILED, DISCORD_PORT_CONFLICT, DISCORD_SHUTDOWN_FAILED,
        DISCORD_EXIT_TIMEOUT, DISCORD_PORT_CONFLICT, DISCORD_LAUNCH_FAILED, DISCORD_ENDPOINT_TIMEOUT
    };
    for (unsigned i = 0; i < DISCORD_STAGE_COUNT; ++i) {
        if (cancel_requested(request->cancel_event)) { result->status = DISCORD_CANCELLED; break; }
        result->last_stage = (DiscordRestartStage)i;
        if (i == DISCORD_SHUTDOWN) result->shutdown_attempted = TRUE;
        BOOL ok = ops->stages[i](ops->context, request, installation, result->diagnostic, 512);
        if (i == DISCORD_SHUTDOWN && ok) result->shutdown_requested = TRUE;
        if (i == DISCORD_LAUNCH && ok) result->launched = TRUE;
        if (cancel_requested(request->cancel_event)) { result->status = DISCORD_CANCELLED; break; }
        if (!ok) { result->status = failures[i]; break; }
        if (i == DISCORD_CONNECT) { result->status = DISCORD_RESTART_SUCCESS; fail(result->diagnostic, 512, L"Restart and renderer handshake verified."); }
    }
    ops->release(ops->context, installation);
    free(installation);
}

void discord_release(DiscordInstallation *installation) {
    for (size_t i = 0; i < installation->process_count; ++i) if (installation->processes[i]) CloseHandle(installation->processes[i]);
    if (installation->executable_handle) CloseHandle(installation->executable_handle);
    if (installation->launched_process) CloseHandle(installation->launched_process);
    memset(installation, 0, sizeof(*installation));
}

static BOOL version_valid(const wchar_t *text) {
    unsigned components = 0;
    while (*text) {
        if (*text < L'0' || *text > L'9') return FALSE;
        unsigned digits = 0;
        while (*text >= L'0' && *text <= L'9') { ++text; if (++digits > 9) return FALSE; }
        if (++components > 4) return FALSE;
        if (!*text) break;
        if (*text++ != L'.' || !*text) return FALSE;
    }
    return components >= 2;
}
static int version_compare(const wchar_t *a, const wchar_t *b) {
    for (unsigned i = 0; i < 4; ++i) {
        wchar_t *ae, *be;
        unsigned long av = wcstoul(a, &ae, 10), bv = wcstoul(b, &be, 10);
        if (av != bv) return av > bv ? 1 : -1;
        a = *ae == L'.' ? ae + 1 : ae; b = *be == L'.' ? be + 1 : be;
    }
    return 0;
}
static BOOL approved_path(const wchar_t *path, const wchar_t *local) {
    static const wchar_t *roots[] = {L"Discord", L"DiscordPTB", L"DiscordCanary"};
    static const wchar_t *names[] = {L"Discord.exe", L"DiscordPTB.exe", L"DiscordCanary.exe"};
    wchar_t prefix[MAX_PATH + 64];
    for (unsigned i = 0; i < 3; ++i) {
        swprintf(prefix, sizeof(prefix) / sizeof(*prefix), L"%ls\\%ls\\app-", local, roots[i]);
        size_t n = wcslen(prefix);
        if (_wcsnicmp(path, prefix, n)) continue;
        const wchar_t *slash = wcschr(path + n, L'\\');
        if (!slash || slash - (path + n) > 50) continue;
        wchar_t version[64]; wcsncpy(version, path + n, (size_t)(slash - (path + n))); version[slash - (path + n)] = 0;
        if (!version_valid(version)) continue;
        if (!_wcsicmp(slash + 1, names[i]) || !_wcsicmp(slash + 1, L"Discord.exe")) return TRUE;
    }
    return FALSE;
}
static BOOL canonical_file(const wchar_t *path, wchar_t *canonical, HANDLE *held_file) {
    HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD n = GetFinalPathNameByHandleW(file, canonical, DISCORD_PATH_CAP, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= DISCORD_PATH_CAP || !GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { CloseHandle(file); return FALSE; }
    if (!wcsncmp(canonical, L"\\\\?\\", 4)) memmove(canonical, canonical + 4, (wcslen(canonical + 4) + 1) * sizeof(*canonical));
    *held_file = file; return TRUE;
}
static BOOL image_matches(HANDLE process, DWORD expected_pid, const wchar_t *path) {
    wchar_t image[DISCORD_PATH_CAP]; DWORD n = DISCORD_PATH_CAP;
    return GetProcessId(process) == expected_pid && WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
           QueryFullProcessImageNameW(process, 0, image, &n) && !_wcsicmp(image, path);
}

BOOL discord_discover(const wchar_t *configured, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    memset(installation, 0, sizeof(*installation));
    wchar_t local[MAX_PATH], selected[DISCORD_PATH_CAP] = L"";
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, local))) return fail(error, capacity, L"Local application directory unavailable.");
    if (configured && *configured) {
        if (!canonical_file(configured, selected, &installation->executable_handle) || !approved_path(selected, local)) {
            discord_release(installation); return fail(error, capacity, L"Configured executable is outside an approved Discord installation.");
        }
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { discord_release(installation); return fail(error, capacity, L"Process inventory unavailable."); }
    PROCESSENTRY32W entry = {0}; entry.dwSize = sizeof(entry);
    BOOL scan_ok = TRUE, ambiguous = FALSE;
    SetLastError(ERROR_SUCCESS);
    BOOL first = Process32FirstW(snapshot, &entry);
    if (first) do {
        if (_wcsicmp(entry.szExeFile, L"Discord.exe") && _wcsicmp(entry.szExeFile, L"DiscordPTB.exe") && _wcsicmp(entry.szExeFile, L"DiscordCanary.exe")) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        wchar_t image[DISCORD_PATH_CAP]; DWORD length = DISCORD_PATH_CAP;
        if (!process || !QueryFullProcessImageNameW(process, 0, image, &length)) { if (process) CloseHandle(process); scan_ok = FALSE; break; }
        if (!approved_path(image, local)) { CloseHandle(process); continue; }
        if (!*selected) wcscpy(selected, image);
        if (_wcsicmp(selected, image)) { if (!configured || !*configured) ambiguous = TRUE; CloseHandle(process); continue; }
        if (!image_matches(process, entry.th32ProcessID, selected)) {
            BOOL exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
            CloseHandle(process);
            if (exited) continue;
            scan_ok = FALSE; break;
        }
        if (installation->process_count == DISCORD_PROCESS_CAP) { CloseHandle(process); scan_ok = FALSE; break; }
        size_t i = installation->process_count++;
        installation->processes[i] = process; installation->pids[i] = entry.th32ProcessID;
    } while (Process32NextW(snapshot, &entry));
    if (scan_ok && GetLastError() != ERROR_NO_MORE_FILES) scan_ok = FALSE;
    CloseHandle(snapshot);
    if (!scan_ok || ambiguous) { discord_release(installation); return fail(error, capacity, ambiguous ? L"Multiple running Discord installations; choose an executable explicitly." : L"Discord process inventory is incomplete or inaccessible; retry without launching."); }
    if (!*selected) {
        static const wchar_t *roots[] = {L"Discord", L"DiscordPTB", L"DiscordCanary"};
        static const wchar_t *names[] = {L"Discord.exe", L"DiscordPTB.exe", L"DiscordCanary.exe"};
        unsigned found = 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            wchar_t pattern[MAX_PATH + 64], best_version[64] = L"", best_path[DISCORD_PATH_CAP] = L"";
            swprintf(pattern, sizeof(pattern) / sizeof(*pattern), L"%ls\\%ls\\app-*", local, roots[channel]);
            WIN32_FIND_DATAW data; HANDLE search = FindFirstFileW(pattern, &data);
            if (search == INVALID_HANDLE_VALUE) {
                DWORD code = GetLastError();
                if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) continue;
                discord_release(installation); return fail(error, capacity, L"Installed Discord directory inventory is inaccessible; selection refused.");
            }
            do {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !version_valid(data.cFileName + 4)) continue;
                wchar_t candidate[DISCORD_PATH_CAP];
                swprintf(candidate, DISCORD_PATH_CAP, L"%ls\\%ls\\%ls\\%ls", local, roots[channel], data.cFileName, names[channel]);
                if (GetFileAttributesW(candidate) == INVALID_FILE_ATTRIBUTES && channel) swprintf(candidate, DISCORD_PATH_CAP, L"%ls\\%ls\\%ls\\Discord.exe", local, roots[channel], data.cFileName);
                DWORD attributes = GetFileAttributesW(candidate);
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (!*best_version || version_compare(data.cFileName + 4, best_version) > 0) { wcscpy(best_version, data.cFileName + 4); wcscpy(best_path, candidate); }
            } while (FindNextFileW(search, &data));
            DWORD enumeration_error = GetLastError();
            FindClose(search);
            if (enumeration_error != ERROR_NO_MORE_FILES) { discord_release(installation); return fail(error, capacity, L"Installed Discord directory inventory is incomplete; selection refused."); }
            if (*best_path) { ++found; wcscpy(selected, best_path); }
        }
        if (found != 1) return fail(error, capacity, found ? L"Multiple installed Discord channels; choose an executable explicitly." : L"No approved Discord installation found.");
    }
    wchar_t canonical[DISCORD_PATH_CAP];
    if (!installation->executable_handle && !canonical_file(selected, canonical, &installation->executable_handle)) { discord_release(installation); return fail(error, capacity, L"Cannot retain the selected Discord executable."); }
    if (installation->executable_handle && configured && *configured) wcscpy(canonical, selected);
    if (!approved_path(canonical, local) || _wcsicmp(canonical, selected)) { discord_release(installation); return fail(error, capacity, L"Executable path changed or resolves outside the selected installation."); }
    wcscpy(installation->executable, canonical); return TRUE;
}

static BOOL selected_owner(const DiscordInstallation *installation, DWORD owner) {
    for (size_t i = 0; installation && i < installation->process_count; ++i)
        if (installation->pids[i] == owner && image_matches(installation->processes[i], owner, installation->executable)) return TRUE;
    return FALSE;
}
static BOOL inspect_port(unsigned short port, const DiscordInstallation *installation, BOOL require_free, BOOL *found, wchar_t *error, size_t capacity) {
    if (found) *found = FALSE;
    if (!port) return fail(error, capacity, L"Invalid debugging port.");
    for (unsigned family = 0; family < 2; ++family) {
        DWORD size = 0, af = family ? AF_INET6 : AF_INET;
        DWORD code = GetExtendedTcpTable(NULL, &size, FALSE, af, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (code != ERROR_INSUFFICIENT_BUFFER && code != NO_ERROR) return fail(error, capacity, L"Cannot inspect debugging port ownership.");
        void *table = malloc(size ? size : 1);
        if (!table) return fail(error, capacity, L"Out of memory inspecting port.");
        code = GetExtendedTcpTable(table, &size, FALSE, af, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (code != NO_ERROR) { free(table); return fail(error, capacity, L"Port inventory changed; retry the operation."); }
        DWORD count = family ? ((MIB_TCP6TABLE_OWNER_PID *)table)->dwNumEntries : ((MIB_TCPTABLE_OWNER_PID *)table)->dwNumEntries;
        BOOL allowed = TRUE;
        for (DWORD i = 0; i < count; ++i) {
            DWORD owner, raw_port; BOOL loopback;
            if (!family) {
                MIB_TCPROW_OWNER_PID *row = &((MIB_TCPTABLE_OWNER_PID *)table)->table[i];
                raw_port = row->dwLocalPort; owner = row->dwOwningPid;
                const BYTE *address = (const BYTE *)&row->dwLocalAddr;
                loopback = address[0] == 127 && address[1] == 0 && address[2] == 0 && address[3] == 1;
            } else {
                MIB_TCP6ROW_OWNER_PID *row = &((MIB_TCP6TABLE_OWNER_PID *)table)->table[i];
                raw_port = row->dwLocalPort; owner = row->dwOwningPid; loopback = row->ucLocalAddr[15] == 1;
                for (unsigned j = 0; j < 15; ++j) if (row->ucLocalAddr[j]) loopback = FALSE;
            }
            if (ntohs((u_short)raw_port) == port) {
                if (found) *found = TRUE;
                if (require_free || !loopback || !selected_owner(installation, owner)) { allowed = FALSE; break; }
            }
        }
        free(table);
        if (!allowed) return fail(error, capacity, L"Debugging port has a foreign, wildcard, or still-running listener.");
    }
    return TRUE;
}
BOOL discord_port_available(unsigned short port, const DiscordInstallation *installation, BOOL require_free, wchar_t *error, size_t capacity) {
    return inspect_port(port, installation, require_free, NULL, error, capacity);
}
BOOL discord_listener_owned(unsigned short port, const DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    BOOL found = FALSE;
    if (!inspect_port(port, installation, FALSE, &found, error, capacity)) return FALSE;
    return found ? TRUE : fail(error, capacity, L"No selected Discord listener exists yet.");
}

static BOOL discover_op(void *context, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    (void)context; return discord_discover(request->configured_executable, installation, error, capacity);
}
static BOOL verify_updater_for_selected(const wchar_t *selected_exe, wchar_t *error, size_t capacity);

static BOOL preflight_op(void *context, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    (void)context;
    if (!discord_port_available(request->port, installation, FALSE, error, capacity)) return FALSE;
    return verify_updater_for_selected(installation->executable, error, capacity);
}
static BOOL free_op(void *context, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    (void)context; return discord_port_available(request->port, installation, TRUE, error, capacity);
}
typedef struct { const DiscordRestartRequest *request; DiscordInstallation *installation; unsigned closed; BOOL failed; } CloseContext;
static BOOL CALLBACK close_window(HWND window, LPARAM parameter) {
    CloseContext *context = (CloseContext *)parameter;
    if (cancel_requested(context->request->cancel_event)) { context->failed = TRUE; return FALSE; }
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (!selected_owner(context->installation, pid)) return TRUE;
    if (cancel_requested(context->request->cancel_event)) { context->failed = TRUE; return FALSE; }
    DWORD_PTR ignored;
    if (!SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &ignored)) { context->failed = TRUE; return FALSE; }
    ++context->closed; return TRUE;
}
static BOOL shutdown_op(void *unused, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    (void)unused;
    if (!installation->process_count) return TRUE;
    for (size_t i = 0; i < installation->process_count; ++i)
        if (WaitForSingleObject(installation->processes[i], 0) != WAIT_OBJECT_0 && !image_matches(installation->processes[i], installation->pids[i], installation->executable)) return fail(error, capacity, L"Discord process identity changed; shutdown refused.");
    CloseContext context = {request, installation, 0, FALSE};
    BOOL enumerated = EnumWindows(close_window, (LPARAM)&context);
    if (!enumerated && !context.failed) return fail(error, capacity, L"Window enumeration incomplete; no launch will follow.");
    if (context.failed) return fail(error, capacity, L"Graceful shutdown request failed or was cancelled.");
    if (!context.closed) return fail(error, capacity, L"No validated Discord window accepted graceful shutdown; use Discord Quit manually.");
    return TRUE;
}
static BOOL wait_op(void *unused, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    (void)unused; ULONGLONG start = GetTickCount64();
    for (;;) {
        if (cancel_requested(request->cancel_event)) return fail(error, capacity, L"Cancelled while waiting for Discord exit.");
        BOOL all_exited = TRUE;
        for (size_t i = 0; i < installation->process_count; ++i) if (WaitForSingleObject(installation->processes[i], 0) != WAIT_OBJECT_0) all_exited = FALSE;
        if (all_exited) return TRUE;
        if (GetTickCount64() - start >= request->exit_timeout_ms) return fail(error, capacity, L"Discord did not exit gracefully before timeout; no forced termination performed.");
        Sleep(20);
    }
}
static BOOL discord_updater_path(const wchar_t *selected_exe, wchar_t *updater, size_t cap) {
    if (!selected_exe || !updater || !cap) return FALSE;
    size_t path_len = wcslen(selected_exe);
    if (path_len < 3 || path_len >= DISCORD_PATH_CAP) return FALSE;
    if (!((selected_exe[0] >= L'A' && selected_exe[0] <= L'Z') ||
          (selected_exe[0] >= L'a' && selected_exe[0] <= L'z')) ||
        selected_exe[1] != L':' || selected_exe[2] != L'\\' ||
        wcschr(selected_exe, L'"') || wcschr(selected_exe, L'/') ||
        wcschr(selected_exe, L'\r') || wcschr(selected_exe, L'\n')) return FALSE;
    for (const wchar_t *p = selected_exe + 3; *p;) {
        const wchar_t *end = wcschr(p, L'\\');
        size_t length = end ? (size_t)(end - p) : wcslen(p);
        if (!length || (length == 1 && p[0] == L'.') ||
            (length == 2 && p[0] == L'.' && p[1] == L'.')) return FALSE;
        if (!end) break;
        p = end + 1;
        if (!*p) return FALSE;
    }
    const wchar_t *app_marker = NULL;
    for (const wchar_t *p = selected_exe; *p; ++p) {
        if (*p == L'\\' && p[1] && _wcsnicmp(p + 1, L"app-", 4) == 0)
            app_marker = p;
    }
    if (!app_marker) return FALSE;
    const wchar_t *version_start = app_marker + 5;
    const wchar_t *version_end = wcschr(version_start, L'\\');
    if (!version_end || !version_end[1] || wcschr(version_end + 1, L'\\')) return FALSE;
    size_t version_len = (size_t)(version_end - version_start);
    if (!version_len || version_len >= 64) return FALSE;
    wchar_t version[64];
    wmemcpy(version, version_start, version_len);
    version[version_len] = 0;
    if (!version_valid(version)) return FALSE;
    const wchar_t *channel_start = app_marker - 1;
    while (channel_start > selected_exe && channel_start[-1] != L'\\') --channel_start;
    size_t channel_len = (size_t)(app_marker - channel_start);
    BOOL discord = channel_len == 7 && !_wcsnicmp(channel_start, L"Discord", 7);
    BOOL ptb = channel_len == 10 && !_wcsnicmp(channel_start, L"DiscordPTB", 10);
    BOOL canary = channel_len == 13 && !_wcsnicmp(channel_start, L"DiscordCanary", 13);
    const wchar_t *basename = version_end + 1;
    BOOL exe_ok = (discord && !_wcsicmp(basename, L"Discord.exe")) ||
        (ptb && (!_wcsicmp(basename, L"DiscordPTB.exe") || !_wcsicmp(basename, L"Discord.exe"))) ||
        (canary && (!_wcsicmp(basename, L"DiscordCanary.exe") || !_wcsicmp(basename, L"Discord.exe")));
    if (!exe_ok) return FALSE;
    size_t root_len = (size_t)(app_marker - selected_exe);
    static const wchar_t suffix[] = L"\\Update.exe";
    const size_t suffix_len = sizeof(suffix) / sizeof(*suffix) - 1;
    if (root_len > (size_t)-1 - suffix_len - 1 || cap < root_len + suffix_len + 1) return FALSE;
    wmemcpy(updater, selected_exe, root_len);
    wmemcpy(updater + root_len, suffix, suffix_len + 1);
    return TRUE;
}
static BOOL verify_updater_for_selected(const wchar_t *selected_exe, wchar_t *error, size_t capacity) {
    const wchar_t *message = L"Discord Update.exe unavailable or outside approved installation; restart cannot continue.";
    wchar_t updater[DISCORD_PATH_CAP], real_path[DISCORD_PATH_CAP];
    HANDLE handle = NULL;
    if (!discord_updater_path(selected_exe, updater, DISCORD_PATH_CAP) ||
        !canonical_file(updater, real_path, &handle)) return fail(error, capacity, message);
    BOOL valid = !_wcsicmp(real_path, updater);
    CloseHandle(handle);
    return valid ? TRUE : fail(error, capacity, message);
}
BOOL discord_build_command(const wchar_t *executable, unsigned short port, wchar_t *output, size_t capacity) {
    if (!executable || !output || !capacity || !port) return FALSE;
    wchar_t updater[DISCORD_PATH_CAP];
    if (!discord_updater_path(executable, updater, DISCORD_PATH_CAP)) return FALSE;
    const wchar_t *basename = wcsrchr(executable, L'\\');
    if (!basename || !*++basename) return FALSE;
    int written = swprintf(output, capacity,
        L"\"%ls\" --processStart %ls --process-start-args \"--remote-debugging-address=127.0.0.1 --remote-debugging-port=%u\"",
        updater, basename, port);
    return written >= 0 && (size_t)written < capacity;
}
static BOOL launch_op(void *unused, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    (void)unused;
    if (cancel_requested(request->cancel_event)) return fail(error, capacity, L"Launch cancelled.");
    if (!discord_port_available(request->port, installation, TRUE, error, capacity)) return FALSE;
    DiscordInstallation *fresh = calloc(1, sizeof(*fresh));
    if (!fresh) return fail(error, capacity, L"Out of memory rechecking Discord identity.");
    BOOL inspected = discord_discover(installation->executable, fresh, error, capacity);
    BOOL running = fresh->process_count != 0;
    discord_release(fresh); free(fresh);
    if (!inspected) return FALSE;
    if (running) return fail(error, capacity, L"Selected Discord installation still runs or respawned; launch refused.");
    if (!verify_updater_for_selected(installation->executable, error, capacity)) return FALSE;
    wchar_t updater[DISCORD_PATH_CAP], current_directory[DISCORD_PATH_CAP];
    if (!discord_updater_path(installation->executable, updater, DISCORD_PATH_CAP)) return fail(error, capacity, L"Discord Update.exe unavailable or outside approved installation; restart cannot continue.");
    wcscpy(current_directory, updater);
    wchar_t *directory_sep = wcsrchr(current_directory, L'\\');
    if (!directory_sep) return fail(error, capacity, L"Discord updater directory is invalid; launch refused.");
    *directory_sep = 0;
    wchar_t command[DISCORD_PATH_CAP + 256];
    if (!discord_build_command(installation->executable, request->port, command, sizeof(command) / sizeof(*command))) return fail(error, capacity, L"Cannot construct exact Discord updater arguments.");
    STARTUPINFOW startup = {0}; startup.cb = sizeof(startup); PROCESS_INFORMATION process = {0};
    if (!discord_port_available(request->port, installation, TRUE, error, capacity)) return FALSE;
    if (cancel_requested(request->cancel_event)) return fail(error, capacity, L"Launch cancelled before process creation.");
    if (!CreateProcessW(updater, command, NULL, NULL, FALSE, 0, NULL, current_directory, &startup, &process)) {
        DWORD launch_error = GetLastError();
        if (!error || !capacity) return fail(error, capacity, L"Discord updater launch failed; no alternate launch attempted.");
        int written = swprintf(error, capacity, L"Discord updater launch failed (Windows error %lu); no alternate launch attempted.", (unsigned long)launch_error);
        if (written < 0 || (size_t)written >= capacity) return fail(error, capacity, L"Discord updater launch failed; no alternate launch attempted.");
        return FALSE;
    }
    CloseHandle(process.hThread); installation->launched_process = process.hProcess; return TRUE;
}
static BOOL connect_op(void *unused, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    (void)unused; ULONGLONG start = GetTickCount64();
    if (!request->app_directory) return fail(error, capacity, L"Native helper directory unavailable.");
    for (;;) {
        if (cancel_requested(request->cancel_event)) return fail(error, capacity, L"Renderer connection wait cancelled; Discord remains untouched.");
        ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= request->endpoint_timeout_ms) return fail(error, capacity, L"Debugging endpoint or renderer not ready before timeout.");
        DWORD remaining = request->endpoint_timeout_ms - (DWORD)elapsed;
        DiscordInstallation *fresh = calloc(1, sizeof(*fresh));
        if (!fresh) return fail(error, capacity, L"Out of memory verifying renderer ownership.");
        BOOL inspected = discord_discover(installation->executable, fresh, error, capacity);
        BOOL owned = inspected && fresh->process_count != 0 && discord_listener_owned(request->port, fresh, error, capacity);
        discord_release(fresh); free(fresh);
        if (!inspected) return FALSE;
        if (!owned) { Sleep(100); continue; }
        CdpRequest attempt = {request->app_directory, NULL, request->port, CDP_CONNECT, remaining < 1500 ? remaining : 1500, request->cancel_event, installation->executable, NULL, NULL};
        CdpResult *result = calloc(1, sizeof(*result));
        if (!result) return fail(error, capacity, L"Out of memory connecting renderer.");
        cdp_run(&attempt, result);
        BOOL ok = result->status == CDP_FINISHED && result->child_exit_code == 0;
        free(result);
        if (ok) {
            fresh = calloc(1, sizeof(*fresh));
            if (!fresh) return fail(error, capacity, L"Out of memory rechecking renderer ownership.");
            inspected = discord_discover(installation->executable, fresh, error, capacity);
            owned = inspected && fresh->process_count != 0 && discord_listener_owned(request->port, fresh, error, capacity);
            discord_release(fresh); free(fresh);
            if (owned) return TRUE;
            if (!inspected) return FALSE;
        }
        Sleep(100);
    }
}
static void release_op(void *unused, DiscordInstallation *installation) { (void)unused; discord_release(installation); }
const DiscordRestartOps *discord_production_ops(void) {
    static const DiscordRestartOps ops = {{discover_op, preflight_op, shutdown_op, wait_op, free_op, launch_op, connect_op}, release_op, NULL};
    return &ops;
}
