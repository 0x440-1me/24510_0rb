#include "../src/cdp.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct { HANDLE event; DWORD delay; } CancelRequest;
static DWORD WINAPI cancel_later(void *parameter) {
    CancelRequest *cancel = parameter;
    Sleep(cancel->delay); SetEvent(cancel->event); return 0;
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 2 && !wcscmp(argv[1], L"--sentinel")) { Sleep(6000); return 0; }
    if (argc == 3 && !wcscmp(argv[1], L"--handles")) {
        DWORD cold = 0, before = 0, after = 0;
        CdpRequest request = {argv[2], L"Z:\\orb-nonexistent-fixture\\missing.js", 1, CDP_EVALUATE, 2000, NULL, NULL, NULL, NULL};
        CdpResult *result = calloc(1, sizeof(*result));
        if (!result || !GetProcessHandleCount(GetCurrentProcess(), &cold)) { free(result); return 8; }
        /* Retain the cold observation and compare repeated-operation growth
           after three warmups, including any bounded first-use overhead. */
        for (unsigned i = 0; i < 3; ++i) cdp_run(&request, result);
        GetProcessHandleCount(GetCurrentProcess(), &before);
        printf("cold_handles=%lu warm_handles=%lu warmups=3\n", cold, before);
        for (unsigned i = 0; i < 100; ++i) {
            cdp_run(&request, result);
            if (!((result->status == CDP_FINISHED && result->child_exit_code == 6) || result->status == CDP_LAUNCH_FAILED)) {
                free(result); return 1;
            }
            if (i == 24 || i == 49 || i == 99) {
                DWORD count = 0; GetProcessHandleCount(GetCurrentProcess(), &count);
                printf("iteration=%u handles=%lu\n", i + 1, count);
            }
        }
        GetProcessHandleCount(GetCurrentProcess(), &after);
        printf("handles_before=%lu handles_after=%lu iterations=100\n", before, after);
        free(result); return before == after ? 0 : 1;
    }
    if (argc < 5 || argc > 7) return 2;
    CdpRequest request = {0}; CdpResult *result = calloc(1, sizeof(*result));
    if (!result) return 8;
    request.app_directory = argv[1]; request.port = (unsigned short)wcstoul(argv[2], NULL, 10);
    request.timeout_ms = wcstoul(argv[3], NULL, 10);
    request.action = !wcscmp(argv[4], L"check") ? CDP_CHECK : !wcscmp(argv[4], L"connect") ? CDP_CONNECT : CDP_EVALUATE;
    if (argc >= 6) request.payload_path = argv[5];
    CancelRequest cancel = {CreateEventW(NULL, TRUE, FALSE, NULL), argc == 7 ? wcstoul(argv[6], NULL, 10) : 0};
    request.cancel_event = cancel.event;
    HANDLE thread = cancel.delay ? CreateThread(NULL, 0, cancel_later, &cancel, 0, NULL) : NULL;
    cdp_run(&request, result);
    printf("status=%d child_exit=%lu error=%lu pid=%lu elapsed_ms=%llu bytes=%zu truncated=%d\n",
           result->status, result->child_exit_code, result->windows_error, result->child_pid,
           (unsigned long long)result->elapsed_ms, result->output_length, result->output_truncated);
    fwrite(result->output, 1, result->output_length, stdout);
    int code = result->status == CDP_FINISHED ? (int)result->child_exit_code : 10 + (int)result->status;
    if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    if (cancel.event) CloseHandle(cancel.event);
    free(result); return code;
}
