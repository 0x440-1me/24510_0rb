#ifndef ORB_DISCORD_PROCESS_H
#define ORB_DISCORD_PROCESS_H
#include "cdp.h"
#define DISCORD_PATH_CAP 32768
#define DISCORD_PROCESS_CAP 128
typedef struct {
    wchar_t executable[DISCORD_PATH_CAP];
    HANDLE executable_handle;
    HANDLE processes[DISCORD_PROCESS_CAP];
    DWORD pids[DISCORD_PROCESS_CAP];
    size_t process_count;
    HANDLE launched_process;
} DiscordInstallation;
typedef struct {
    BOOL consent;
    const wchar_t *configured_executable;
    const wchar_t *app_directory;
    unsigned short port;
    DWORD exit_timeout_ms;
    DWORD endpoint_timeout_ms;
    HANDLE cancel_event;
    BOOL allow_force_quit_on_timeout; /* Caller must obtain explicit user consent before setting TRUE. */
} DiscordRestartRequest;
typedef enum {
    DISCORD_RESTART_SUCCESS, DISCORD_NO_CONSENT, DISCORD_CANCELLED,
    DISCORD_DISCOVERY_FAILED, DISCORD_PORT_CONFLICT, DISCORD_SHUTDOWN_FAILED,
    DISCORD_EXIT_TIMEOUT, DISCORD_LAUNCH_FAILED, DISCORD_ENDPOINT_TIMEOUT
} DiscordRestartStatus;
typedef enum {
    DISCORD_DISCOVER, DISCORD_PREFLIGHT, DISCORD_SHUTDOWN, DISCORD_WAIT_EXIT,
    DISCORD_PORT_FREE, DISCORD_LAUNCH, DISCORD_CONNECT, DISCORD_STAGE_COUNT
} DiscordRestartStage;
typedef struct {
    DiscordRestartStatus status;
    DiscordRestartStage last_stage;
    BOOL shutdown_attempted;
    BOOL shutdown_requested;
    BOOL launched;
    wchar_t diagnostic[512];
} DiscordRestartResult;
typedef BOOL (*DiscordOperation)(void *context, const DiscordRestartRequest *request,
                                DiscordInstallation *installation, wchar_t *error, size_t capacity);
typedef struct {
    DiscordOperation stages[DISCORD_STAGE_COUNT];
    void (*release)(void *context, DiscordInstallation *installation);
    void *context;
} DiscordRestartOps;

/* Same orchestration for production and injected test operations. */
void discord_restart(const DiscordRestartRequest *, const DiscordRestartOps *, DiscordRestartResult *);
const DiscordRestartOps *discord_production_ops(void);
/* Read-only entry points; callers must release even after failed discovery. */
BOOL discord_discover(const wchar_t *configured, DiscordInstallation *, wchar_t *, size_t);
BOOL discord_port_available(unsigned short port, const DiscordInstallation *, BOOL require_free, wchar_t *, size_t);
BOOL discord_listener_owned(unsigned short port, const DiscordInstallation *, wchar_t *, size_t);
void discord_release(DiscordInstallation *);
BOOL discord_build_command(const wchar_t *executable, unsigned short port, wchar_t *output, size_t capacity);
/* Pure comparison of validated channel updater paths; runtime callers also verify both files canonically. */
BOOL discord_same_installation_channel(const wchar_t *original_exe, const wchar_t *candidate_exe);
#endif
