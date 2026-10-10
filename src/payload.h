#ifndef ORB_PAYLOAD_H
#define ORB_PAYLOAD_H
#include <windows.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>

#define ORB_PAYLOAD_LIMIT (4u * 1024u * 1024u)
#define ORB_ORIGINAL_SHA "ef4b9fd9146ddc4da80b4037158d12e12641d91e6aee4babfa9b976ef96205b5"
typedef struct { HANDLE lock; char *bytes; size_t length; char sha256[65]; } OrbPayload;

static inline BOOL orb_hash(const char *bytes, size_t length, char hex[65]) {
    BCRYPT_ALG_HANDLE algorithm = NULL; BCRYPT_HASH_HANDLE hash = NULL; BYTE digest[32];
    BOOL ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) == 0;
    if (ok) ok = BCryptHashData(hash, (PUCHAR)bytes, (ULONG)length, 0) == 0;
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (ok) {
        const char *digits = "0123456789abcdef";
        for (unsigned i = 0; i < 32; ++i) { hex[i*2] = digits[digest[i] >> 4]; hex[i*2+1] = digits[digest[i] & 15]; }
        hex[64] = 0;
    }
    return ok;
}
static inline void orb_payload_close(OrbPayload *payload) {
    if (payload->lock && payload->lock != INVALID_HANDLE_VALUE) CloseHandle(payload->lock);
    free(payload->bytes); memset(payload, 0, sizeof(*payload));
}
/* Retain the read handle through consent and dispatch: writes/deletion/replacement
   are denied. The helper additionally hashes its own exact outgoing byte buffer. */
static inline BOOL orb_payload_open(const wchar_t *path, OrbPayload *payload) {
    memset(payload, 0, sizeof(*payload));
    payload->lock = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER length; DWORD got = 0;
    if (payload->lock == INVALID_HANDLE_VALUE || !GetFileSizeEx(payload->lock, &length) ||
        length.QuadPart <= 0 || length.QuadPart > ORB_PAYLOAD_LIMIT) goto fail;
    payload->length = (size_t)length.QuadPart;
    payload->bytes = malloc(payload->length + 1);
    if (!payload->bytes || !ReadFile(payload->lock, payload->bytes, (DWORD)payload->length, &got, NULL) || got != payload->length) goto fail;
    payload->bytes[payload->length] = 0;
    if (memchr(payload->bytes, 0, payload->length) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, payload->bytes, (int)payload->length, NULL, 0) ||
        !orb_hash(payload->bytes, payload->length, payload->sha256)) goto fail;
    return TRUE;
fail:
    orb_payload_close(payload); return FALSE;
}
#endif
