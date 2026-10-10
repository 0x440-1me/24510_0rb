#include "settings.h"
#include <shlobj.h>
#include <stdlib.h>
#include <wchar.h>
BOOL settings_location(wchar_t *path, size_t capacity) {
    wchar_t local[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, local))) return FALSE;
#ifdef ORB_UI_TEST_MOCK
    const wchar_t *name = L"24510_0rb-ui-test";
#else
    const wchar_t *name = L"24510_0rb";
#endif
    return capacity > wcslen(local) + wcslen(name) + 20 && swprintf(path, capacity, L"%ls\\%ls\\settings.ini", local, name) > 0;
}
BOOL settings_valid_number(const wchar_t *text, DWORD minimum, DWORD maximum, DWORD *value) {
    if (!text || !*text) return FALSE;
    for (const wchar_t *p = text; *p; ++p) if (*p < L'0' || *p > L'9') return FALSE;
    if (wcslen(text) > 9) return FALSE;
    wchar_t *end; unsigned long n = wcstoul(text, &end, 10);
    if (*end || n < minimum || n > maximum) return FALSE;
    *value = (DWORD)n; return TRUE;
}
void settings_defaults(AppSettings *settings, const wchar_t *directory) {
    memset(settings, 0, sizeof(*settings)); settings->port = 9223;
    settings->operation_timeout_ms = 15000; settings->exit_timeout_ms = 10000; settings->endpoint_timeout_ms = 20000;
    swprintf(settings->payload_path, DISCORD_PATH_CAP, L"%ls\\payloads\\hello.js", directory);
}
BOOL settings_load(AppSettings *settings, const wchar_t *directory) {
    settings_defaults(settings, directory); wchar_t file[DISCORD_PATH_CAP], text[64]; DWORD n;
    if (!settings_location(file, DISCORD_PATH_CAP)) return FALSE;
    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) return TRUE;
    const wchar_t *keys[] = {L"port", L"operation_timeout_ms", L"exit_timeout_ms", L"endpoint_timeout_ms"};
    DWORD minimum[] = {1, 500, 1000, 1000}, maximum[] = {65535, 120000, 60000, 120000};
    for (unsigned i = 0; i < 4; ++i) {
        GetPrivateProfileStringW(L"app", keys[i], L"", text, 64, file);
        if (!settings_valid_number(text, minimum[i], maximum[i], &n)) return FALSE;
        if (!i) settings->port = (unsigned short)n;
        else if (i == 1) settings->operation_timeout_ms = n;
        else if (i == 2) settings->exit_timeout_ms = n;
        else settings->endpoint_timeout_ms = n;
    }
    wchar_t default_payload[DISCORD_PATH_CAP]; wcscpy(default_payload, settings->payload_path);
    GetPrivateProfileStringW(L"app", L"payload", default_payload, settings->payload_path, DISCORD_PATH_CAP, file);
    GetPrivateProfileStringW(L"app", L"discord_executable", L"", settings->discord_executable, DISCORD_PATH_CAP, file);
    return TRUE;
}
BOOL settings_save(const AppSettings *settings) {
    wchar_t file[DISCORD_PATH_CAP], directory[DISCORD_PATH_CAP], temporary[DISCORD_PATH_CAP], text[64];
    if (!settings_location(file, DISCORD_PATH_CAP)) return FALSE;
    wcscpy(directory, file); wchar_t *slash = wcsrchr(directory, L'\\'); if (!slash) return FALSE; *slash = 0;
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    if (wcslen(file) + 32 >= DISCORD_PATH_CAP) return FALSE;
    swprintf(temporary, DISCORD_PATH_CAP, L"%ls.%lu.tmp", file, GetCurrentProcessId());
    HANDLE created = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (created == INVALID_HANDLE_VALUE) return FALSE;
    WORD bom = 0xfeff; DWORD written;
    BOOL ok = WriteFile(created, &bom, sizeof(bom), &written, NULL) && written == sizeof(bom);
    CloseHandle(created);
    if (!ok) { DeleteFileW(temporary); return FALSE; }
    const wchar_t *keys[] = {L"port", L"operation_timeout_ms", L"exit_timeout_ms", L"endpoint_timeout_ms"};
    DWORD values[] = {settings->port, settings->operation_timeout_ms, settings->exit_timeout_ms, settings->endpoint_timeout_ms};
    for (unsigned i = 0; i < 4; ++i) { swprintf(text, 64, L"%lu", values[i]); if (!WritePrivateProfileStringW(L"app", keys[i], text, temporary)) ok = FALSE; }
    ok = ok && WritePrivateProfileStringW(L"app", L"payload", settings->payload_path, temporary) && WritePrivateProfileStringW(L"app", L"discord_executable", settings->discord_executable, temporary);
    WritePrivateProfileStringW(NULL, NULL, NULL, temporary);
    if (ok) ok = MoveFileExW(temporary, file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temporary);
    return ok;
}
