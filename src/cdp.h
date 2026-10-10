#ifndef ORB_CDP_H
#define ORB_CDP_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define CDP_OUTPUT_CAP 65536
typedef enum { CDP_CHECK, CDP_LIST, CDP_CONNECT, CDP_EVALUATE, CDP_DISPATCH, CDP_STOP_QUEST,
               CDP_ORB_STATUS, CDP_ORB_SCAN, CDP_ORB_START } CdpAction;
typedef enum { CDP_FINISHED, CDP_CANCELLED, CDP_TIMED_OUT, CDP_LAUNCH_FAILED, CDP_CONTROLLER_FAILED } CdpStatus;
typedef struct {
    const wchar_t *app_directory;
    const wchar_t *payload_path;
    unsigned short port;
    CdpAction action;
    DWORD timeout_ms;
    HANDLE cancel_event; /* Caller owns; must remain valid throughout cdp_run. */
    const wchar_t *discord_executable;
    const char *sha256; /* Required for dispatch, verified against outgoing bytes. */
    const char *orb_key; /* Opaque per-run marker, never emitted in logs. */
} CdpRequest;
typedef struct {
    CdpStatus status;
    DWORD child_exit_code;
    DWORD windows_error;
    DWORD child_pid;
    ULONGLONG elapsed_ms;
    size_t output_length;
    BOOL output_truncated;
    char output[CDP_OUTPUT_CAP]; /* UTF-8 helper diagnostics, always NUL terminated. */
} CdpResult;

/* Blocking worker-thread operation. Owns and cleans up only the spawned helper job.
   A successful connect is a completed handshake, not a persistent connection.
   Cancellation cannot undo JavaScript already sent/evaluated in Discord. */
void cdp_run(const CdpRequest *request, CdpResult *result);
#endif
