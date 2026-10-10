#include "../src/payload.h"
#include <stdio.h>
#include <wchar.h>
static unsigned total, failures;
static void check(BOOL ok, const char *label) {
    ++total; if (!ok) ++failures; printf("%s %s\n", ok ? "PASS" : "FAIL", label);
}
int wmain(void) {
    char hash[65];
    check(orb_hash("abc", 3, hash) && !strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256 known vector");
    wchar_t directory[MAX_PATH], path[MAX_PATH];
    GetTempPathW(MAX_PATH, directory); if (!GetTempFileNameW(directory, L"orb", 0, path)) return 1;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); DWORD written;
    const char bytes[] = "/* harmless fixture */\r\n'abc'";
    check(file != INVALID_HANDLE_VALUE && WriteFile(file, bytes, sizeof(bytes)-1, &written, NULL), "create synthetic file");
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    OrbPayload payload;
    check(orb_payload_open(path, &payload), "review UTF-8 file");
    check(payload.bytes && payload.length == sizeof(bytes)-1 && !memcmp(payload.bytes, bytes, sizeof(bytes)-1), "preserve exact CRLF bytes");
    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    check(file == INVALID_HANDLE_VALUE, "deny writes during review/consent"); if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    check(!DeleteFileW(path), "deny replacement during review/consent");
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    check(file != INVALID_HANDLE_VALUE, "helper can read locked bytes"); if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    orb_payload_close(&payload);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    const char invalid[] = {'x', 0, 'y'};
    if (file != INVALID_HANDLE_VALUE) { WriteFile(file, invalid, sizeof(invalid), &written, NULL); CloseHandle(file); }
    check(!orb_payload_open(path, &payload), "reject embedded NUL");
    check(!payload.bytes && !payload.lock, "failure releases review lock");
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    const unsigned char invalid_utf8[] = {0xc0,0xaf};
    if (file != INVALID_HANDLE_VALUE) { WriteFile(file, invalid_utf8, sizeof(invalid_utf8), &written, NULL); CloseHandle(file); }
    check(!orb_payload_open(path, &payload), "reject invalid UTF-8");
    check(DeleteFileW(path), "cleanup own synthetic file");
    printf("SUMMARY total=%u passed=%u failed=%u\n", total, total-failures, failures); return failures ? 1 : 0;
}
