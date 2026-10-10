#define _WIN32_WINNT 0x0602
#include "cdp.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static BOOL cancelled(HANDLE event) {
    return event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

/* Microsoft command-line quoting: double backslashes before quotes/end quote. */
static wchar_t *quote_arg(const wchar_t *value) {
    size_t n = wcslen(value);
    if (n > 32760) return NULL;
    wchar_t *out = malloc((n * 2 + 3) * sizeof(*out));
    if (!out) return NULL;
    size_t at = 0, slashes = 0;
    out[at++] = L'"';
    for (size_t i = 0; i < n; ++i) {
        if (value[i] == L'\\') { ++slashes; continue; }
        if (value[i] == L'"') {
            for (size_t j = 0; j < slashes * 2 + 1; ++j) out[at++] = L'\\';
        } else {
            for (size_t j = 0; j < slashes; ++j) out[at++] = L'\\';
        }
        slashes = 0;
        out[at++] = value[i];
    }
    for (size_t j = 0; j < slashes * 2; ++j) out[at++] = L'\\';
    out[at++] = L'"'; out[at] = 0;
    return out;
}

static BOOL drain(HANDLE pipe, CdpResult *result) {
    char buffer[8192];
    /* Bound each poll so a noisy child cannot starve cancellation/deadlines. */
    for (unsigned i = 0; i < 16; ++i) {
        DWORD available = 0, got = 0;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL))
            return GetLastError() == ERROR_BROKEN_PIPE;
        if (!available) return TRUE;
        DWORD amount = available < sizeof(buffer) ? available : (DWORD)sizeof(buffer);
        if (!ReadFile(pipe, buffer, amount, &got, NULL)) return GetLastError() == ERROR_BROKEN_PIPE;
        size_t remaining = CDP_OUTPUT_CAP - 1 - result->output_length;
        size_t keep = got < remaining ? got : remaining;
        memcpy(result->output + result->output_length, buffer, keep);
        result->output_length += keep;
        result->output[result->output_length] = 0;
        if (keep < got) result->output_truncated = TRUE;
    }
    return TRUE;
}

void cdp_run(const CdpRequest *request, CdpResult *result) {
    ULONGLONG started = GetTickCount64();
    HANDLE read_pipe = NULL, write_pipe = NULL, input = INVALID_HANDLE_VALUE, job = NULL;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    PROCESS_INFORMATION process = {0};
    wchar_t *exe = NULL, *quoted_exe = NULL, *quoted_file = NULL, *quoted_discord = NULL, *command = NULL;
    BOOL process_created = FALSE, child_finished = FALSE;
    memset(result, 0, sizeof(*result));
    result->status = CDP_CONTROLLER_FAILED;
    result->child_exit_code = STILL_ACTIVE;
    if (!request || !request->app_directory || !request->port || !request->timeout_ms ||
        request->action > CDP_ORB_START || request->action < CDP_CHECK ||
        ((request->action == CDP_EVALUATE || (request->action == CDP_DISPATCH || request->action == CDP_ORB_START)) && !request->payload_path) ||
        ((request->action == CDP_DISPATCH || request->action == CDP_ORB_START) && !request->sha256)) {
        result->windows_error = ERROR_INVALID_PARAMETER; goto cleanup;
    }
    if (cancelled(request->cancel_event)) { result->status = CDP_CANCELLED; goto cleanup; }
    size_t directory_length = wcslen(request->app_directory);
    if (directory_length > 30000) { result->windows_error = ERROR_FILENAME_EXCED_RANGE; goto cleanup; }
    exe = malloc((directory_length + 14) * sizeof(*exe));
    if (!exe) { result->windows_error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup; }
    swprintf(exe, directory_length + 14, L"%ls\\orb-cdp.exe", request->app_directory);
    /* lpApplicationName is exact and fully qualified; never search PATH or a shell. */
    if (!((exe[0] && exe[1] == L':' && (exe[2] == L'\\' || exe[2] == L'/')) ||
          (exe[0] == L'\\' && exe[1] == L'\\'))) {
        result->windows_error = ERROR_BAD_PATHNAME; goto cleanup;
    }
    quoted_exe = quote_arg(exe);
    if (request->action == CDP_EVALUATE || (request->action == CDP_DISPATCH || request->action == CDP_ORB_START)) quoted_file = quote_arg(request->payload_path);
    if (!quoted_exe || ((request->action == CDP_EVALUATE || (request->action == CDP_DISPATCH || request->action == CDP_ORB_START)) && !quoted_file)) {
        result->windows_error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup;
    }
    if (request->discord_executable && *request->discord_executable) {
        quoted_discord = quote_arg(request->discord_executable);
        if (!quoted_discord) { result->windows_error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup; }
    }
    if (request->sha256) {
        if (strlen(request->sha256) != 64 || strspn(request->sha256, "0123456789abcdef") != 64) {
            result->windows_error = ERROR_INVALID_PARAMETER; goto cleanup;
        }
    }
    if (request->orb_key && (strlen(request->orb_key) != 32 || strspn(request->orb_key,"0123456789abcdef") != 32)) {
        result->windows_error = ERROR_INVALID_PARAMETER; goto cleanup;
    }
    const wchar_t *action = request->action == CDP_CHECK ? L"--check" :
                            request->action == CDP_LIST ? L"--list-targets" :
                            request->action == CDP_CONNECT ? L"--connect" : request->action == CDP_STOP_QUEST ? L"--stop-quest" : (request->action == CDP_ORB_STATUS || request->action == CDP_ORB_SCAN) ? L"--orb-status" : L"--file";
    size_t capacity = wcslen(quoted_exe) + (quoted_file ? wcslen(quoted_file) : 0) + (quoted_discord ? wcslen(quoted_discord) : 0) + 280;
    if (capacity > 32767) { result->windows_error = ERROR_FILENAME_EXCED_RANGE; goto cleanup; }
    command = malloc(capacity * sizeof(*command));
    if (!command) { result->windows_error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup; }
    swprintf(command, capacity, L"%ls %ls %ls --port %u %ls %ls %ls %ls %hs %ls %ls %hs", quoted_exe, action,
             quoted_file ? quoted_file : L"", request->port,
             (request->action == CDP_DISPATCH || request->action == CDP_ORB_START) ? L"--dispatch" : L"",
             quoted_discord ? L"--discord-executable" : L"", quoted_discord ? quoted_discord : L"",
             request->sha256 ? L"--sha256" : L"", request->sha256 ? request->sha256 : "", request->action == CDP_ORB_START ? L"--track-orb" : request->action == CDP_ORB_SCAN ? L"--scan-quests" : L"",
             request->orb_key ? L"--orb-key" : L"", request->orb_key ? request->orb_key : "");
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0) ||
        !SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) goto windows_failure;
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
    if (input == INVALID_HANDLE_VALUE) goto windows_failure;
    job = CreateJobObjectW(NULL, NULL);
    if (!job) goto windows_failure;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) goto windows_failure;
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    attributes = malloc(attribute_bytes);
    if (!attributes) { result->windows_error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup; }
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) {
        free(attributes); attributes = NULL; goto windows_failure;
    }
    HANDLE inherited[] = {write_pipe, input};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), NULL, NULL)) goto windows_failure;
    STARTUPINFOEXW startup = {0};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = write_pipe;
    startup.lpAttributeList = attributes;
    if (!CreateProcessW(exe, command, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                        NULL, request->app_directory, &startup.StartupInfo, &process)) {
        result->windows_error = GetLastError(); result->status = CDP_LAUNCH_FAILED; goto cleanup;
    }
    process_created = TRUE; result->child_pid = process.dwProcessId;
    if (!AssignProcessToJobObject(job, process.hProcess)) goto windows_failure;
    CloseHandle(write_pipe); write_pipe = NULL;
    CloseHandle(input); input = INVALID_HANDLE_VALUE;
    if (ResumeThread(process.hThread) == (DWORD)-1) goto windows_failure;
    for (;;) {
        if (!drain(read_pipe, result)) goto windows_failure;
        DWORD waited = WaitForSingleObject(process.hProcess, 0);
        if (waited == WAIT_OBJECT_0) {
            child_finished = TRUE;
            /* Remaining pipe data is bounded by the pipe capacity after child exit. */
            if (!drain(read_pipe, result) || !GetExitCodeProcess(process.hProcess, &result->child_exit_code)) goto windows_failure;
            result->status = CDP_FINISHED; break;
        }
        if (waited == WAIT_FAILED) goto windows_failure;
        if (cancelled(request->cancel_event)) { result->status = CDP_CANCELLED; break; }
        if (GetTickCount64() - started >= request->timeout_ms) { result->status = CDP_TIMED_OUT; break; }
        Sleep(20);
    }
    goto cleanup;
windows_failure:
    result->windows_error = GetLastError();
cleanup:
    if (process_created && !child_finished) {
        /* Only handles created above are reachable here; Discord is never assigned. */
        if (job) TerminateJobObject(job, ERROR_OPERATION_ABORTED);
        /* Covers the suspended, unassigned child if assignment failed. */
        if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, ERROR_OPERATION_ABORTED);
        WaitForSingleObject(process.hProcess, 5000);
        GetExitCodeProcess(process.hProcess, &result->child_exit_code);
    }
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    if (job) CloseHandle(job);
    if (read_pipe) CloseHandle(read_pipe);
    if (write_pipe) CloseHandle(write_pipe);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (attributes) { DeleteProcThreadAttributeList(attributes); free(attributes); }
    free(quoted_discord); free(command); free(quoted_file); free(quoted_exe); free(exe);
    result->elapsed_ms = GetTickCount64() - started;
}
