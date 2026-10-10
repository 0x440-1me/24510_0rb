#include <winsock2.h>
#include <ws2tcpip.h>
#include "../src/discord_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
static unsigned passed, failed;
static void check(BOOL ok, const char *name) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (ok) ++passed; else ++failed; }
typedef struct { int fail_stage, cancel_stage; unsigned calls[DISCORD_STAGE_COUNT], order[DISCORD_STAGE_COUNT], count, released; BOOL arguments_ok; } Mock;
static BOOL mock_stage(Mock *mock, unsigned stage, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) {
    ++mock->calls[stage]; mock->order[mock->count++] = stage;
    if (stage == DISCORD_DISCOVER) wcscpy(installation->executable, L"C:\\Fixture Space\\Discord\\app-1.2.3\\Discord.exe");
    if (stage == DISCORD_LAUNCH) {
        wchar_t command[512];
        mock->arguments_ok = discord_build_command(installation->executable, request->port, command, 512) &&
            !wcscmp(command, L"\"C:\\Fixture Space\\Discord\\app-1.2.3\\Discord.exe\" --remote-debugging-address=127.0.0.1 --remote-debugging-port=7733");
    }
    if ((int)stage == mock->cancel_stage) SetEvent(request->cancel_event);
    if ((int)stage == mock->fail_stage) { wcsncpy(error, L"Injected operation failure", capacity - 1); return FALSE; }
    return TRUE;
}
#define MOCK_OPERATION(name, stage) static BOOL name(void *context, const DiscordRestartRequest *request, DiscordInstallation *installation, wchar_t *error, size_t capacity) { return mock_stage(context, stage, request, installation, error, capacity); }
MOCK_OPERATION(mock_discover, DISCORD_DISCOVER)
MOCK_OPERATION(mock_preflight, DISCORD_PREFLIGHT)
MOCK_OPERATION(mock_shutdown, DISCORD_SHUTDOWN)
MOCK_OPERATION(mock_wait, DISCORD_WAIT_EXIT)
MOCK_OPERATION(mock_free, DISCORD_PORT_FREE)
MOCK_OPERATION(mock_launch, DISCORD_LAUNCH)
MOCK_OPERATION(mock_connect, DISCORD_CONNECT)
static void mock_release(void *context, DiscordInstallation *installation) { (void)installation; ++((Mock *)context)->released; }
static DiscordRestartOps mock_ops(Mock *mock) { DiscordRestartOps ops = {{mock_discover, mock_preflight, mock_shutdown, mock_wait, mock_free, mock_launch, mock_connect}, mock_release, mock}; return ops; }
static void state_tests(void) {
    HANDLE cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    DiscordRestartRequest request = {FALSE, NULL, L"C:\\Fixture", 7733, 1000, 1000, cancel};
    DiscordRestartResult result; Mock mock = {-1, -1, {0}, {0}, 0, 0, FALSE}; DiscordRestartOps ops = mock_ops(&mock);
    discord_restart(&request, &ops, &result);
    check(result.status == DISCORD_NO_CONSENT && mock.count == 0 && mock.released == 0 && !result.shutdown_attempted, "no-consent-zero-operations");
    request.consent = TRUE; SetEvent(cancel);
    discord_restart(&request, &ops, &result);
    check(result.status == DISCORD_CANCELLED && mock.count == 0 && mock.released == 0 && !result.shutdown_attempted, "pre-cancel-zero-operations");
    ResetEvent(cancel); discord_restart(&request, &ops, &result);
    BOOL order = mock.count == DISCORD_STAGE_COUNT;
    for (unsigned i = 0; i < mock.count; ++i) if (mock.order[i] != i) order = FALSE;
    check(result.status == DISCORD_RESTART_SUCCESS && order && mock.arguments_ok && mock.released == 1 && result.shutdown_attempted && result.shutdown_requested && result.launched, "successful-mock-order-and-exact-arguments");
    static const struct {const char *name; int stage; DiscordRestartStatus status;} cases[] = {
        {"missing-installation", DISCORD_DISCOVER, DISCORD_DISCOVERY_FAILED},
        {"ambiguous-installation", DISCORD_DISCOVER, DISCORD_DISCOVERY_FAILED},
        {"incomplete-process-inventory-fails-closed", DISCORD_DISCOVER, DISCORD_DISCOVERY_FAILED},
        {"incomplete-installation-inventory-fails-closed", DISCORD_DISCOVER, DISCORD_DISCOVERY_FAILED},
        {"foreign-port-before-shutdown", DISCORD_PREFLIGHT, DISCORD_PORT_CONFLICT},
        {"wildcard-port-before-shutdown", DISCORD_PREFLIGHT, DISCORD_PORT_CONFLICT},
        {"no-window-tray-shutdown-failure", DISCORD_SHUTDOWN, DISCORD_SHUTDOWN_FAILED},
        {"graceful-shutdown-failure", DISCORD_SHUTDOWN, DISCORD_SHUTDOWN_FAILED},
        {"partial-shutdown-enumeration-failure", DISCORD_SHUTDOWN, DISCORD_SHUTDOWN_FAILED},
        {"exit-timeout-no-launch", DISCORD_WAIT_EXIT, DISCORD_EXIT_TIMEOUT},
        {"port-acquired-after-exit-no-launch", DISCORD_PORT_FREE, DISCORD_PORT_CONFLICT},
        {"respawned-installation-no-launch", DISCORD_LAUNCH, DISCORD_LAUNCH_FAILED},
        {"launch-failure-no-connect", DISCORD_LAUNCH, DISCORD_LAUNCH_FAILED},
        {"foreign-listener-after-launch-no-success", DISCORD_CONNECT, DISCORD_ENDPOINT_TIMEOUT},
        {"endpoint-timeout", DISCORD_CONNECT, DISCORD_ENDPOINT_TIMEOUT}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Mock trial = {cases[i].stage, -1, {0}, {0}, 0, 0, FALSE}; ops = mock_ops(&trial);
        discord_restart(&request, &ops, &result);
        BOOL safe = result.status == cases[i].status && trial.count == (unsigned)cases[i].stage + 1 && trial.released == 1;
        for (unsigned j = (unsigned)cases[i].stage + 1; j < DISCORD_STAGE_COUNT; ++j) if (trial.calls[j]) safe = FALSE;
        if (cases[i].stage == DISCORD_SHUTDOWN && !result.shutdown_attempted) safe = FALSE;
        check(safe, cases[i].name);
    }
    for (unsigned stage = 0; stage < DISCORD_STAGE_COUNT; ++stage) {
        Mock trial = {-1, (int)stage, {0}, {0}, 0, 0, FALSE}; ops = mock_ops(&trial); ResetEvent(cancel);
        discord_restart(&request, &ops, &result);
        BOOL safe = result.status == DISCORD_CANCELLED && trial.count == stage + 1 && trial.released == 1;
        for (unsigned j = stage + 1; j < DISCORD_STAGE_COUNT; ++j) if (trial.calls[j]) safe = FALSE;
        if (stage >= DISCORD_SHUTDOWN && !result.shutdown_attempted) safe = FALSE;
        char name[80]; snprintf(name, sizeof(name), "cancel-stage-%u-no-subsequent-operation", stage); check(safe, name);
    }
    ResetEvent(cancel); Mock invalid = {-1, -1, {0}, {0}, 0, 0, FALSE}; ops = mock_ops(&invalid); request.port = 0;
    discord_restart(&request, &ops, &result);
    check(result.status == DISCORD_DISCOVERY_FAILED && invalid.count == 0, "invalid-configuration-no-operations");
    CloseHandle(cancel); wchar_t command[512];
    check(!discord_build_command(L"C:\\bad\"name.exe", 7733, command, 512), "quoted-path-injection-rejected");
    check(!discord_build_command(L"C:\\Discord.exe", 0, command, 512), "zero-command-port-rejected");
}
static void port_tests(void) {
    WSADATA data; if (WSAStartup(MAKEWORD(2,2), &data)) { check(FALSE, "socket-fixture-startup"); return; }
    DiscordInstallation *selected = calloc(1, sizeof(*selected)); DWORD length = DISCORD_PATH_CAP;
    if (!selected) { check(FALSE, "socket-fixture-allocation"); WSACleanup(); return; }
    QueryFullProcessImageNameW(GetCurrentProcess(), 0, selected->executable, &length);
    selected->processes[0] = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, GetCurrentProcessId());
    selected->pids[0] = GetCurrentProcessId(); selected->process_count = 1;
    for (unsigned family = 0; family < 2; ++family) for (unsigned wildcard = 0; wildcard < 2; ++wildcard) {
        SOCKET listener = socket(family ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP); BOOL bound = FALSE; unsigned short port = 0;
        if (!family) {
            struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(wildcard ? INADDR_ANY : INADDR_LOOPBACK);
            bound = bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0 && listen(listener, 1) == 0;
            int size = sizeof(address); getsockname(listener, (struct sockaddr *)&address, &size); port = ntohs(address.sin_port);
        } else {
            int only = 1; setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&only, sizeof(only));
            struct sockaddr_in6 address = {0}; address.sin6_family = AF_INET6; address.sin6_addr = wildcard ? in6addr_any : in6addr_loopback;
            bound = bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0 && listen(listener, 1) == 0;
            int size = sizeof(address); getsockname(listener, (struct sockaddr *)&address, &size); port = ntohs(address.sin6_port);
        }
        wchar_t error[512]; char name[100];
        snprintf(name, sizeof(name), "ipv%u-%s-selected-owner-policy", family ? 6 : 4, wildcard ? "wildcard" : "loopback");
        check(bound && discord_port_available(port, selected, FALSE, error, 512) == !wildcard, name);
        snprintf(name, sizeof(name), "ipv%u-%s-foreign-owner-rejected", family ? 6 : 4, wildcard ? "wildcard" : "loopback");
        check(bound && !discord_port_available(port, NULL, FALSE, error, 512), name);
        snprintf(name, sizeof(name), "ipv%u-%s-require-free-rejected", family ? 6 : 4, wildcard ? "wildcard" : "loopback");
        check(bound && !discord_port_available(port, selected, TRUE, error, 512), name);
        if (!wildcard) {
            DWORD saved = selected->pids[0]; selected->pids[0] = saved + 1;
            snprintf(name, sizeof(name), "ipv%u-reused-pid-mismatch-rejected", family ? 6 : 4);
            check(bound && !discord_listener_owned(port, selected, error, 512), name); selected->pids[0] = saved;
            wchar_t saved_char = selected->executable[0]; selected->executable[0] = L'!';
            snprintf(name, sizeof(name), "ipv%u-image-mismatch-rejected", family ? 6 : 4);
            check(bound && !discord_listener_owned(port, selected, error, 512), name); selected->executable[0] = saved_char;
        }
        closesocket(listener);
        snprintf(name, sizeof(name), "ipv%u-%s-free-after-close", family ? 6 : 4, wildcard ? "wildcard" : "loopback");
        check(bound && discord_port_available(port, selected, TRUE, error, 512), name);
        snprintf(name, sizeof(name), "ipv%u-%s-absent-listener-not-connected", family ? 6 : 4, wildcard ? "wildcard" : "loopback");
        check(bound && !discord_listener_owned(port, selected, error, 512), name);
    }
    discord_release(selected); free(selected); WSACleanup();
}
static void read_only_tests(unsigned short port) {
    DiscordInstallation *installation = calloc(1, sizeof(*installation)); wchar_t error[512];
    if (!installation) { check(FALSE, "read-only-allocation"); return; }
    BOOL found = discord_discover(NULL, installation, error, 512); check(found, "real-read-only-installation-discovery");
    printf("discovered_process_count=%zu\n", installation->process_count);
    BOOL exact = found;
    for (size_t i = 0; i < installation->process_count; ++i) {
        wchar_t image[DISCORD_PATH_CAP]; DWORD length = DISCORD_PATH_CAP;
        if (GetProcessId(installation->processes[i]) != installation->pids[i] || !QueryFullProcessImageNameW(installation->processes[i], 0, image, &length) || _wcsicmp(image, installation->executable)) exact = FALSE;
    }
    check(exact, "real-retained-pid-exact-image-validation");
    check(found && discord_listener_owned(port, installation, error, 512), "real-loopback-listener-selected-installation-owner");
    discord_release(installation);
    wchar_t own_exe[DISCORD_PATH_CAP]; GetModuleFileNameW(NULL, own_exe, DISCORD_PATH_CAP);
    check(!discord_discover(own_exe, installation, error, 512), "configured-unrelated-executable-rejected"); discord_release(installation);
    DWORD cold = 0, before = 0, after = 0; GetProcessHandleCount(GetCurrentProcess(), &cold);
    for (unsigned i = 0; i < 3; ++i) { discord_discover(NULL, installation, error, 512); discord_release(installation); }
    GetProcessHandleCount(GetCurrentProcess(), &before); printf("discovery_cold_handles=%lu discovery_warm_handles=%lu\n", cold, before);
    BOOL consistent = TRUE;
    for (unsigned i = 0; i < 100; ++i) {
        if (!discord_discover(NULL, installation, error, 512)) consistent = FALSE;
        discord_release(installation);
        if (i == 24 || i == 49 || i == 99) { DWORD count = 0; GetProcessHandleCount(GetCurrentProcess(), &count); printf("discovery_iteration=%u handles=%lu\n", i + 1, count); }
    }
    GetProcessHandleCount(GetCurrentProcess(), &after); check(consistent && before == after, "real-discovery-handle-plateau-after-warmup");
    free(installation);
}
int wmain(int argc, wchar_t **argv) {
    if (argc == 3 && !wcscmp(argv[1], L"--read-only")) read_only_tests((unsigned short)wcstoul(argv[2], NULL, 10));
    else { state_tests(); port_tests(); }
    printf("TOTAL passed=%u failed=%u\n", passed, failed); return failed ? 1 : 0;
}
