#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>

#define DEFAULT_PORT 9223
#define MAX_HTTP_BYTES (16u * 1024u * 1024u)
#define MAX_PAYLOAD_BYTES (64u * 1024u * 1024u)
#define MAX_WS_MESSAGE (80u * 1024u * 1024u)

typedef struct { const char *p; size_t n; } Span;
typedef struct {
    char *type, *title, *url, *ws;
} Target;

static int g_verbose = 0;
static void log_winhttp(const char *what);

static int harden_request(HINTERNET request) {
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) {
        log_winhttp("WinHttpSetOption(disable redirects/auth/cookies)"); return 0;
    }
    return 1;
}

static void log_winhttp(const char *what) {
    DWORD e = GetLastError();
    LPWSTR msg = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, (LPWSTR)&msg, 0, NULL);
    if (msg) {
        fwprintf(stderr, L"%S failed (Windows error %lu): %ls\n", what, e, msg);
        LocalFree(msg);
    } else fprintf(stderr, "%s failed (Windows error %lu)\n", what, e);
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "Out of memory.\n"); exit(8); }
    return q;
}

static void skip_ws(const char **p, const char *end) {
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')) ++*p;
}

static const char *scan_string(const char *p, const char *end) {
    if (p >= end || *p != '"') return NULL;
    ++p;
    while (p < end) {
        if (*p == '"') return p + 1;
        if ((unsigned char)*p < 0x20) return NULL;
        if (*p == '\\') {
            ++p;
            if (p >= end) return NULL;
            if (*p == 'u') {
                if (end - p < 5) return NULL;
                for (int i = 1; i <= 4; ++i) if (!isxdigit((unsigned char)p[i])) return NULL;
                p += 5;
            } else if (strchr("\\\"/bfnrt", *p)) ++p;
            else return NULL;
        } else ++p;
    }
    return NULL;
}

static const char *scan_value_depth(const char *p, const char *end, unsigned depth) {
    if (depth > 128) return NULL;
    skip_ws(&p, end);
    if (p >= end) return NULL;
    if (*p == '"') return scan_string(p, end);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        int object = *p == '{';
        ++p;
        skip_ws(&p, end);
        if (p < end && *p == close) return p + 1;
        for (;;) {
            if (object) {
                const char *k = scan_string(p, end);
                if (!k) return NULL;
                p = k; skip_ws(&p, end);
                if (p >= end || *p++ != ':') return NULL;
            }
            const char *v = scan_value_depth(p, end, depth + 1);
            if (!v) return NULL;
            p = v; skip_ws(&p, end);
            if (p >= end) return NULL;
            if (*p == close) return p + 1;
            if (*p++ != ',') return NULL;
            skip_ws(&p, end);
        }
    }
    const char *s = p;
    if (*p == 't' && end - p >= 4 && memcmp(p, "true", 4) == 0) p += 4;
    else if (*p == 'f' && end - p >= 5 && memcmp(p, "false", 5) == 0) p += 5;
    else if (*p == 'n' && end - p >= 4 && memcmp(p, "null", 4) == 0) p += 4;
    else if (*p == '-' || (*p >= '0' && *p <= '9')) {
        if (*p == '-') ++p;
        if (p >= end) return NULL;
        if (*p == '0') ++p;
        else {
            if (*p < '1' || *p > '9') return NULL;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        if (p < end && *p == '.') {
            ++p; if (p >= end || *p < '0' || *p > '9') return NULL;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p; if (p < end && (*p == '+' || *p == '-')) ++p;
            if (p >= end || *p < '0' || *p > '9') return NULL;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
    } else return NULL;
    if (p == s || (p < end && *p != ',' && *p != ']' && *p != '}' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')) return NULL;
    return p;
}

static int hex4(const char *p, uint32_t *v) {
    uint32_t x = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned char c = (unsigned char)p[i];
        x <<= 4;
        if (c >= '0' && c <= '9') x |= c - '0';
        else if (c >= 'a' && c <= 'f') x |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') x |= c - 'A' + 10;
        else return 0;
    }
    *v = x; return 1;
}

static size_t put_utf8(char *out, uint32_t c) {
    if (c <= 0x7f) { out[0] = (char)c; return 1; }
    if (c <= 0x7ff) { out[0] = (char)(0xc0 | (c >> 6)); out[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c <= 0xffff) { out[0] = (char)(0xe0 | (c >> 12)); out[1] = (char)(0x80 | ((c >> 6) & 63)); out[2] = (char)(0x80 | (c & 63)); return 3; }
    out[0] = (char)(0xf0 | (c >> 18)); out[1] = (char)(0x80 | ((c >> 12) & 63));
    out[2] = (char)(0x80 | ((c >> 6) & 63)); out[3] = (char)(0x80 | (c & 63)); return 4;
}

static char *decode_string(Span s) {
    if (s.n < 2 || s.p[0] != '"' || s.p[s.n - 1] != '"') return NULL;
    char *out = (char *)malloc(s.n * 4 + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 1; i + 1 < s.n; ++i) {
        unsigned char c = (unsigned char)s.p[i];
        if (c != '\\') { out[j++] = (char)c; continue; }
        c = (unsigned char)s.p[++i];
        switch (c) {
            case '"': case '\\': case '/': out[j++] = (char)c; break;
            case 'b': out[j++] = '\b'; break; case 'f': out[j++] = '\f'; break;
            case 'n': out[j++] = '\n'; break; case 'r': out[j++] = '\r'; break; case 't': out[j++] = '\t'; break;
            case 'u': {
                uint32_t cp;
                if (i + 4 >= s.n - 1 || !hex4(s.p + i + 1, &cp)) { free(out); return NULL; }
                i += 4;
                if (cp >= 0xd800 && cp <= 0xdbff && i + 6 < s.n - 1 && s.p[i + 1] == '\\' && s.p[i + 2] == 'u') {
                    uint32_t lo;
                    if (hex4(s.p + i + 3, &lo) && lo >= 0xdc00 && lo <= 0xdfff) {
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00); i += 6;
                    }
                }
                j += put_utf8(out + j, cp); break;
            }
            default: free(out); return NULL;
        }
    }
    out[j] = 0; return out;
}

static int json_property(Span obj, const char *wanted, Span *value) {
    const char *p = obj.p, *end = obj.p + obj.n;
    skip_ws(&p, end);
    if (p >= end || *p++ != '{') return 0;
    skip_ws(&p, end);
    while (p < end && *p != '}') {
        const char *ks = p, *ke = scan_string(p, end);
        if (!ke) return 0;
        Span key = { ks, (size_t)(ke - ks) };
        char *decoded = decode_string(key);
        if (!decoded) return 0;
        p = ke; skip_ws(&p, end);
        if (p >= end || *p++ != ':') { free(decoded); return 0; }
        skip_ws(&p, end);
        const char *vs = p, *ve = scan_value_depth(p, end, 0);
        if (!ve) { free(decoded); return 0; }
        if (strcmp(decoded, wanted) == 0) { free(decoded); value->p = vs; value->n = (size_t)(ve - vs); return 1; }
        free(decoded); p = ve; skip_ws(&p, end);
        if (p < end && *p == ',') { ++p; skip_ws(&p, end); }
        else break;
    }
    return 0;
}

static char *property_string(Span obj, const char *key) {
    Span s;
    if (!json_property(obj, key, &s) || !s.n || s.p[0] != '"') return NULL;
    return decode_string(s);
}

static int property_long(Span obj, const char *key, long *out) {
    Span s;
    if (!json_property(obj, key, &s) || !s.n) return 0;
    char tmp[64]; if (s.n >= sizeof(tmp)) return 0;
    memcpy(tmp, s.p, s.n); tmp[s.n] = 0;
    char *end = NULL; long n = strtol(tmp, &end, 10);
    if (end == tmp || *end) return 0;
    *out = n; return 1;
}

static wchar_t *utf8_to_wide(const char *s) {
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n) { cp = CP_ACP; n = MultiByteToWideChar(cp, 0, s, -1, NULL, 0); }
    if (!n) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w || !MultiByteToWideChar(cp, 0, s, -1, w, n)) { free(w); return NULL; }
    return w;
}

static char *http_get(unsigned short port, const wchar_t *path, DWORD *status) {
    HINTERNET session = WinHttpOpen(L"TSUKI-CDP/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = NULL, request = NULL;
    char *body = NULL; size_t used = 0, cap = 8192;
    if (!session) { log_winhttp("WinHttpOpen"); return NULL; }
    WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);
    connect = WinHttpConnect(session, L"127.0.0.1", port, 0);
    if (!connect) { log_winhttp("WinHttpConnect"); goto done; }
    request = WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) { log_winhttp("WinHttpOpenRequest"); goto done; }
    if (!harden_request(request)) goto done;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request, NULL)) {
        log_winhttp("WinHTTP GET"); goto done;
    }
    DWORD sc = 0, sz = sizeof(sc);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sz, WINHTTP_NO_HEADER_INDEX)) {
        if (status) *status = sc;
    }
    body = (char *)malloc(cap); if (!body) goto done;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) { log_winhttp("WinHttpQueryDataAvailable"); free(body); body = NULL; goto done; }
        if (!avail) break;
        if (used + avail > MAX_HTTP_BYTES) { fprintf(stderr, "CDP HTTP response exceeded 16 MiB.\n"); free(body); body = NULL; goto done; }
        if (used + avail + 1 > cap) { while (cap < used + avail + 1) cap *= 2; body = (char *)xrealloc(body, cap); }
        DWORD got = 0;
        if (!WinHttpReadData(request, body + used, avail, &got)) { log_winhttp("WinHttpReadData"); free(body); body = NULL; goto done; }
        used += got;
    }
    body[used] = 0;
done:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return body;
}

static void free_targets(Target *a, size_t n);

static int parse_targets(const char *json, Target **out, size_t *count) {
    const char *p = json, *end = json + strlen(json);
    skip_ws(&p, end); if (p >= end || *p++ != '[') return 0;
    Target *a = NULL; size_t n = 0, cap = 0; int closed = 0;
    for (;;) {
        skip_ws(&p, end); if (p >= end) break;
        if (*p == ']') { ++p; closed = 1; break; }
        const char *q = scan_value_depth(p, end, 0);
        if (!q) break;
        if (*p == '{') {
            if (n == cap) { cap = cap ? cap * 2 : 8; a = (Target *)xrealloc(a, cap * sizeof(*a)); }
            Span obj = { p, (size_t)(q - p) };
            a[n].type = property_string(obj, "type"); a[n].title = property_string(obj, "title");
            a[n].url = property_string(obj, "url"); a[n].ws = property_string(obj, "webSocketDebuggerUrl"); ++n;
        }
        p = q; skip_ws(&p, end);
        if (p < end && *p == ',') { ++p; skip_ws(&p, end); if (p >= end || *p == ']') break; }
        else if (p < end && *p == ']') { ++p; closed = 1; break; }
        else break;
    }
    skip_ws(&p, end);
    if (!closed || p != end) { free_targets(a, n); return 0; }
    *out = a; *count = n; return 1;
}

static void free_targets(Target *a, size_t n) {
    for (size_t i = 0; i < n; ++i) { free(a[i].type); free(a[i].title); free(a[i].url); free(a[i].ws); }
    free(a);
}

static int is_discord_target(const Target *t) {
    if (!t->type || strcmp(t->type, "page") != 0 || !t->ws) return 0;
    static const char *origins[] = {"https://discord.com", "https://discordapp.com",
                                   "https://ptb.discord.com", "https://canary.discord.com"};
    if (!t->url) return 0;
    for (size_t i = 0; i < sizeof(origins) / sizeof(origins[0]); ++i) {
        size_t n = strlen(origins[i]);
        if (!strncmp(t->url, origins[i], n) && (t->url[n] == '/' || t->url[n] == '\0')) return 1;
    }
    return 0;
}

static Target *choose_target(Target *a, size_t n) {
    for (size_t i = 0; i < n; ++i) if (is_discord_target(&a[i])) return &a[i];
    return NULL;
}

static int parse_ws_url(const char *url, wchar_t **host, unsigned short *port, wchar_t **path, int *secure) {
    const char *p;
    if (strncmp(url, "ws://", 5) == 0) { p = url + 5; *secure = 0; *port = 80; }
    else if (strncmp(url, "wss://", 6) == 0) { p = url + 6; *secure = 1; *port = 443; }
    else return 0;
    const char *slash = strchr(p, '/'); if (!slash) return 0;
    const char *colon = memchr(p, ':', (size_t)(slash - p));
    size_t hlen = (size_t)((colon ? colon : slash) - p);
    char *h = (char *)malloc(hlen + 1); if (!h) return 0;
    memcpy(h, p, hlen); h[hlen] = 0;
    if (colon) { char *e; long v = strtol(colon + 1, &e, 10); if (e != slash || v < 1 || v > 65535) { free(h); return 0; } *port = (unsigned short)v; }
    *host = utf8_to_wide(h); free(h);
    *path = utf8_to_wide(slash);
    return *host && *path;
}

static int valid_local_ws(const char *url, unsigned short expected_port) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "ws://127.0.0.1:%u/devtools/page/", expected_port);
    size_t n = strlen(prefix);
    if (strncmp(url, prefix, n) || !url[n]) return 0;
    for (const char *p = url + n; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return 0;
    return 1;
}

static HINTERNET websocket_connect(const char *url, unsigned short expected_port) {
    if (!valid_local_ws(url, expected_port)) {
        fprintf(stderr, "Rejected non-local or malformed renderer WebSocket endpoint.\n"); return NULL;
    }
    wchar_t *host = NULL, *path = NULL; unsigned short port; int secure;
    if (!parse_ws_url(url, &host, &port, &path, &secure)) { fprintf(stderr, "Invalid CDP WebSocket URL.\n"); return NULL; }
    HINTERNET session = WinHttpOpen(L"TSUKI-CDP/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { log_winhttp("WinHttpOpen"); free(host); free(path); return NULL; }
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 30000);
    HINTERNET conn = WinHttpConnect(session, host, port, 0);
    free(host);
    if (!conn) { log_winhttp("WinHttpConnect(WebSocket)"); WinHttpCloseHandle(session); free(path); return NULL; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    free(path);
    if (!req) { log_winhttp("WinHttpOpenRequest(WebSocket)"); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return NULL; }
    if (!harden_request(req)) { WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return NULL; }
    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
        log_winhttp("WinHttpSetOption(WebSocket upgrade)");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return NULL;
    }
    /* Native CDP clients have no browser origin. */
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, NULL)) {
        log_winhttp("WebSocket handshake"); WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return NULL;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) { fprintf(stderr, "WebSocket handshake returned HTTP %lu (expected 101).\n", status); WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return NULL; }
    HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    if (!ws) log_winhttp("WinHttpWebSocketCompleteUpgrade");
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
    return ws;
}

static int ws_send_text(HINTERNET ws, const char *s, size_t n) {
    if (n > MAX_WS_MESSAGE || n > MAXDWORD) return 0;
    DWORD e = WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)s, (DWORD)n);
    if (e != NO_ERROR) { SetLastError(e); log_winhttp("WinHttpWebSocketSend"); return 0; }
    return 1;
}

static char *ws_receive_message(HINTERNET ws, size_t *length) {
    BYTE buf[8192]; char *msg = NULL; size_t used = 0, cap = 0;
    for (;;) {
        DWORD got = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        DWORD e = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &got, &type);
        if (e != NO_ERROR) { SetLastError(e); log_winhttp("WinHttpWebSocketReceive"); free(msg); return NULL; }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) { free(msg); return NULL; }
        if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
            fprintf(stderr, "Received unexpected binary CDP WebSocket message.\n"); free(msg); return NULL;
        }
        if (used + got > MAX_WS_MESSAGE) { fprintf(stderr, "CDP WebSocket message exceeded 80 MiB.\n"); free(msg); return NULL; }
        if (used + got + 1 > cap) { cap = cap ? cap * 2 : 8192; while (cap < used + got + 1) cap *= 2; msg = (char *)xrealloc(msg, cap); }
        memcpy(msg + used, buf, got); used += got;
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) { msg[used] = 0; *length = used; return msg; }
    }
}

static char *read_file_utf8(const char *name, size_t *length) {
    wchar_t *wname = utf8_to_wide(name);
    if (!wname) { fprintf(stderr, "Invalid file path encoding.\n"); return NULL; }
    HANDLE f = CreateFileW(wname, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wname);
    if (f == INVALID_HANDLE_VALUE) { fprintf(stderr, "Cannot open payload '%s' (Windows error %lu).\n", name, GetLastError()); return NULL; }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart < 0 || (uint64_t)size.QuadPart > MAX_PAYLOAD_BYTES) {
        fprintf(stderr, "Payload must be at most 64 MiB.\n"); CloseHandle(f); return NULL;
    }
    size_t n = (size_t)size.QuadPart; char *data = (char *)malloc(n + 1);
    if (!data) { fprintf(stderr, "Out of memory reading payload.\n"); CloseHandle(f); return NULL; }
    size_t off = 0;
    while (off < n) {
        DWORD chunk = (DWORD)((n - off) > 1024 * 1024 ? 1024 * 1024 : n - off), got = 0;
        if (!ReadFile(f, data + off, chunk, &got, NULL) || got == 0) { fprintf(stderr, "Failed reading payload.\n"); free(data); CloseHandle(f); return NULL; }
        off += got;
    }
    CloseHandle(f); data[n] = 0; *length = n; return data;
}

static char *json_quote(const char *s, size_t n, size_t *out_n) {
    size_t cap = n * 6 + 3; char *out = (char *)malloc(cap); if (!out) return NULL;
    size_t j = 0; out[j++] = '"';
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { out[j++] = '\\'; out[j++] = (char)c; }
        else if (c == '\b') { out[j++] = '\\'; out[j++] = 'b'; }
        else if (c == '\f') { out[j++] = '\\'; out[j++] = 'f'; }
        else if (c == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
        else if (c == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
        else if (c == '\t') { out[j++] = '\\'; out[j++] = 't'; }
        else if (c < 0x20) { out[j++] = '\\'; out[j++] = 'u'; out[j++] = '0'; out[j++] = '0'; out[j++] = hex[c >> 4]; out[j++] = hex[c & 15]; }
        else out[j++] = (char)c;
    }
    out[j++] = '"'; out[j] = 0; *out_n = j; return out;
}

static int create_evaluate_request(unsigned long id, const char *payload, size_t n, char **out, size_t *out_n) {
    size_t qn; char *q = json_quote(payload, n, &qn); if (!q) return 0;
    const char *prefix = "{\"id\":"; const char *mid = ",\"method\":\"Runtime.evaluate\",\"params\":{\"expression\":";
    const char *suffix = ",\"awaitPromise\":true,\"returnByValue\":true,\"userGesture\":true}}";
    char number[32]; int nn = snprintf(number, sizeof(number), "%lu", id);
    size_t total = strlen(prefix) + (size_t)nn + strlen(mid) + qn + strlen(suffix);
    char *r = (char *)malloc(total + 1); if (!r) { free(q); return 0; }
    size_t at = 0; memcpy(r + at, prefix, strlen(prefix)); at += strlen(prefix);
    memcpy(r + at, number, (size_t)nn); at += (size_t)nn;
    memcpy(r + at, mid, strlen(mid)); at += strlen(mid);
    memcpy(r + at, q, qn); at += qn;
    memcpy(r + at, suffix, strlen(suffix)); at += strlen(suffix); r[at] = 0;
    free(q); *out = r; *out_n = at; return 1;
}

static int response_is_exception(Span obj, char **message) {
    Span result, ex;
    if (!json_property(obj, "result", &result) || !json_property(result, "exceptionDetails", &ex)) return 0;
    char *text = property_string(ex, "text");
    Span exception; char *description = NULL;
    if (json_property(ex, "exception", &exception)) description = property_string(exception, "description");
    if (text && description) {
        size_t n = strlen(text) + strlen(description) + 3;
        *message = (char *)malloc(n);
        if (*message) snprintf(*message, n, "%s: %s", text, description);
    } else *message = description ? description : text;
    if (description && text) free(description), free(text);
    return 1;
}

static void print_eval_result(Span obj) {
    Span outer, result;
    if (!json_property(obj, "result", &outer) || !json_property(outer, "result", &result)) {
        puts("JavaScript evaluated successfully (no return value).\n"); return;
    }
    char *type = property_string(result, "type");
    Span value;
    if (json_property(result, "value", &value)) {
        if (value.n && value.p[0] == '"') {
            char *s = decode_string(value); printf("Return value (%s): %s\n", type ? type : "string", s ? s : "<invalid string>"); free(s);
        } else printf("Return value (%s): %.*s\n", type ? type : "value", (int)value.n, value.p);
    } else {
        char *desc = property_string(result, "description");
        printf("Return value (%s): %s\n", type ? type : "undefined", desc ? desc : "<no by-value result>"); free(desc);
    }
    free(type);
}

static int valid_remote_object(Span remote) {
    char *type = property_string(remote, "type");
    static const char *allowed[] = {"object", "function", "undefined", "string", "number", "boolean", "symbol", "bigint"};
    int valid = 0;
    for (size_t i = 0; type && i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (!strcmp(type, allowed[i])) { valid = 1; break; }
    free(type); return valid;
}

static int run_eval(Target *t, unsigned short port, const char *payload, size_t n) {
    HINTERNET ws = websocket_connect(t->ws, port); if (!ws) return 4;
    const unsigned long request_id = 1;
    char *request = NULL; size_t request_n = 0;
    if (!create_evaluate_request(request_id, payload, n, &request, &request_n)) { fprintf(stderr, "Could not construct CDP request.\n"); WinHttpCloseHandle(ws); return 8; }
    if (g_verbose) fprintf(stderr, "Connected to renderer; sending Runtime.evaluate (%zu payload bytes).\n", n);
    int ok = ws_send_text(ws, request, request_n); free(request);
    if (!ok) { WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0); WinHttpCloseHandle(ws); return 4; }
    int exit_code = 4, matched_response = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        size_t message_n = 0; char *message = ws_receive_message(ws, &message_n);
        if (!message) break;
        Span obj = { message, message_n }; long id = -1;
        const char *object_start = message;
        skip_ws(&object_start, message + message_n);
        const char *validated_end = scan_value_depth(object_start, message + message_n, 0);
        if (validated_end) skip_ws(&validated_end, message + message_n);
        if (!validated_end || validated_end != message + message_n || object_start == message + message_n || *object_start != '{') {
            fprintf(stderr, "Malformed CDP message.\n"); free(message); break;
        }
        if (property_long(obj, "id", &id) && id == (long)request_id) {
            matched_response = 1;
            char *ex = NULL;
            Span protocol_error, response_result, remote_result;
            if (json_property(obj, "error", &protocol_error)) {
                char *error_message = property_string(protocol_error, "message");
                fprintf(stderr, "CDP command error: %s\n", error_message ? error_message : "(details unavailable)");
                free(error_message); exit_code = 4;
            }
            else if (response_is_exception(obj, &ex)) { fprintf(stderr, "JavaScript exception: %s\n", ex ? ex : "(details unavailable)"); free(ex); exit_code = 5; }
            else if (!json_property(obj, "result", &response_result) || response_result.n == 0 || response_result.p[0] != '{' ||
                     !json_property(response_result, "result", &remote_result) || remote_result.n == 0 || remote_result.p[0] != '{' ||
                     !valid_remote_object(remote_result)) {
                fprintf(stderr, "Malformed CDP evaluation response: result or error required.\n"); exit_code = 4;
            }
            else { print_eval_result(obj); exit_code = 0; }
            free(message); break;
        }
        if (g_verbose) {
            Span method;
            if (json_property(obj, "method", &method)) fprintf(stderr, "Ignoring unrelated CDP event (%.*s).\n", (int)method.n, method.p);
        }
        free(message);
    }
    WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
    WinHttpCloseHandle(ws);
    (void)port;
    if (exit_code == 4 && !matched_response) fprintf(stderr, "No matching CDP response received.\n");
    return exit_code;
}

static void usage(void) {
    puts("TSUKI native C CDP injector\n"
         "Usage:\n"
         "  orb-cdp.exe --check [--port N]\n"
         "  orb-cdp.exe --list-targets [--port N]\n"
         "  orb-cdp.exe --connect [--port N]\n"
         "  orb-cdp.exe --file PATH [--port N] [--verbose]\n"
         "  orb-cdp.exe --help\n\n"
         "The CDP connection is made only to 127.0.0.1.\n"
         "Exit codes: 0 success, 2 usage, 3 endpoint/target unavailable,\n"
         "4 WebSocket/CDP transport failure, 5 JavaScript exception, 6 file error.");
}

static int cli_main(int argc, char **argv) {
    unsigned short port = DEFAULT_PORT; const char *file = NULL;
    int check = 0, list = 0, connect_only = 0, help = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) help = 1;
        else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--list-targets")) list = 1;
        else if (!strcmp(argv[i], "--connect")) connect_only = 1;
        else if (!strcmp(argv[i], "--verbose")) g_verbose = 1;
        else if (!strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            char *end; long p = strtol(argv[++i], &end, 10);
            if (*end || p < 1 || p > 65535) { fprintf(stderr, "Invalid port.\n"); return 2; }
            port = (unsigned short)p;
        } else { fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]); usage(); return 2; }
    }
    if (help) { usage(); return 0; }
    if ((int)check + (int)list + (int)connect_only + (file != NULL) != 1) { usage(); return 2; }

    char *payload = NULL;
    size_t payload_n = 0;
    if (file) {
        payload = read_file_utf8(file, &payload_n);
        if (!payload) return 6;
    }

    wchar_t path[64]; DWORD status = 0;
    if (check) {
        swprintf(path, 64, L"/json/version");
        char *version = http_get(port, path, &status);
        if (!version) { fprintf(stderr, "CDP endpoint unavailable at 127.0.0.1:%u.\n", port); return 3; }
        if (status != 200) { fprintf(stderr, "CDP endpoint returned HTTP %lu.\n", status); free(version); return 3; }
        printf("CDP endpoint available at 127.0.0.1:%u.\n", port);
        printf("%s\n", version); free(version);
    }
    if (check || list || connect_only || file) {
        swprintf(path, 64, L"/json/list");
        char *json = http_get(port, path, &status);
        if (!json) { fprintf(stderr, "Could not retrieve CDP target list from 127.0.0.1:%u.\n", port); free(payload); return 3; }
        if (status != 200) { fprintf(stderr, "Target list returned HTTP %lu.\n", status); free(json); free(payload); return 3; }
        Target *targets = NULL; size_t count = 0;
        if (!parse_targets(json, &targets, &count)) { fprintf(stderr, "CDP returned malformed target JSON.\n"); free(json); free(payload); return 3; }
        free(json);
        if (list) {
            printf("Targets at 127.0.0.1:%u (%zu):\n", port, count);
            for (size_t i = 0; i < count; ++i)
                printf("  [%zu] type=%s websocket=%s discord-renderer=%s\n", i + 1,
                       targets[i].type ? targets[i].type : "?",
                       targets[i].ws ? "available" : "absent",
                       is_discord_target(&targets[i]) ? "yes" : "no");
            free_targets(targets, count); return 0;
        }
        if (check) {
            Target *discord = choose_target(targets, count);
            printf("Target list retrieved (%zu targets).\n", count);
            printf("Discord renderer: %s\n", discord ? "found" : "not found");
            free_targets(targets, count); return discord ? 0 : 3;
        }
        Target *target = choose_target(targets, count);
        if (!target) { fprintf(stderr, "No Discord renderer target found on this CDP endpoint. Use --list-targets to inspect targets.\n"); free_targets(targets, count); free(payload); return 3; }
        if (g_verbose) fprintf(stderr, "Selected Discord renderer (target title and URL omitted for privacy).\n");
        if (connect_only) {
            HINTERNET ws = websocket_connect(target->ws, port);
            if (ws) { puts("Renderer WebSocket handshake verified; connection closed."); WinHttpCloseHandle(ws); }
            free_targets(targets, count); return ws ? 0 : 4;
        }
        int rc = run_eval(target, port, payload, payload_n);
        free(payload); free_targets(targets, count); return rc;
    }
    return 0;
}

int wmain(int argc, wchar_t **wide_argv) {
    char **argv = (char **)calloc((size_t)argc, sizeof(*argv));
    if (!argv) return 8;
    for (int i = 0; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, NULL, 0, NULL, NULL);
        argv[i] = n ? (char *)malloc((size_t)n) : NULL;
        if (!argv[i] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, argv[i], n, NULL, NULL)) {
            for (int j = 0; j <= i; ++j) free(argv[j]);
            free(argv); return 2;
        }
    }
    int code = cli_main(argc, argv);
    for (int i = 0; i < argc; ++i) free(argv[i]);
    free(argv); return code;
}
