#ifndef ORB_SETTINGS_H
#define ORB_SETTINGS_H
#include "discord_process.h"
typedef struct {
    unsigned short port;
    DWORD operation_timeout_ms, exit_timeout_ms, endpoint_timeout_ms;
    wchar_t payload_path[DISCORD_PATH_CAP];
    wchar_t discord_executable[DISCORD_PATH_CAP];
    BOOL passive_connect_on_start, quest_status_opt_in;
} AppSettings;
void settings_defaults(AppSettings *, const wchar_t *module_directory);
BOOL settings_load(AppSettings *, const wchar_t *module_directory);
BOOL settings_save(const AppSettings *);
BOOL settings_valid_number(const wchar_t *, DWORD minimum, DWORD maximum, DWORD *);
BOOL settings_location(wchar_t *, size_t);
#endif
