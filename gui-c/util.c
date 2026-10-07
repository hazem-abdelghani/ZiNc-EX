#include "common.h"
#include <stdarg.h>

/* ---- integer lists ---- */
void il_clear(IntList *l) { free(l->v); l->v = NULL; l->n = l->cap = 0; }
void il_add(IntList *l, int x) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 16; l->v = (int *)realloc(l->v, l->cap * sizeof(int)); }
    l->v[l->n++] = x;
}
int il_has(const IntList *l, int x) { int i; for (i = 0; i < l->n; i++) if (l->v[i] == x) return 1; return 0; }
void il_remove(IntList *l, int x) {
    int i, j = 0;
    for (i = 0; i < l->n; i++) if (l->v[i] != x) l->v[j++] = l->v[i];
    l->n = j;
}
void il_copy(IntList *dst, const IntList *src) {
    int i;
    il_clear(dst);
    for (i = 0; i < src->n; i++) il_add(dst, src->v[i]);
}

/* ---- byte string builder ---- */
void buf_addn(Buf *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) { b->cap = (b->n + n + 1) * 2 + 64; b->s = (char *)realloc(b->s, b->cap); }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}
void buf_add(Buf *b, const char *s) { buf_addn(b, s, strlen(s)); }
void buf_fmt(Buf *b, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    buf_add(b, tmp);
}
void buf_free(Buf *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; }

/* ---- strings ---- */
wchar_t *wdup(const wchar_t *s) {
    size_t n = wcslen(s) + 1;
    wchar_t *r = (wchar_t *)malloc(n * sizeof(wchar_t));
    memcpy(r, s, n * sizeof(wchar_t));
    return r;
}
wchar_t *u8_to_w(const char *s, int len) {
    int n;
    wchar_t *w;
    if (len < 0) len = (int)strlen(s);
    n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    n = MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
    w[n] = 0;
    return w;
}
char *w_to_u8(const wchar_t *s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *r = (char *)malloc(n + 1);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, r, n, NULL, NULL);
    r[n] = 0;
    return r;
}
void trim_w(wchar_t *s) {
    size_t n = wcslen(s), a = 0;
    while (n > 0 && iswspace(s[n - 1])) s[--n] = 0;
    while (s[a] && iswspace(s[a])) a++;
    if (a) memmove(s, s + a, (n - a + 1) * sizeof(wchar_t));
}
char *trim_a(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}
int xatoi(const char *s, int *out) {
    char *e;
    long v;
    if (!*s) return 0;
    v = strtol(s, &e, 10);
    if (*e) return 0;
    *out = (int)v;
    return 1;
}

/* ---- files ---- */
char *read_file(const wchar_t *path, size_t *len) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, got = 0;
    char *buf;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    buf = (char *)malloc((size_t)sz + 1);
    if (!ReadFile(h, buf, sz, &got, NULL)) got = 0;
    CloseHandle(h);
    buf[got] = 0;
    if (len) *len = got;
    return buf;
}
/* written to a temporary file first and then moved over the old one: a crash or a full disk does not leave a half-written settings file */
int write_file(const wchar_t *path, const void *data, size_t len) {
    wchar_t tmp[1200];
    HANDLE h;
    DWORD put = 0;
    int ok;
    _snwprintf(tmp, 1200, L"%ls.tmp", path);
    tmp[1199] = 0;
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(h, data, (DWORD)len, &put, NULL) && put == len;
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (ok && !MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ok = 0;
    if (!ok) DeleteFileW(tmp);
    return ok;
}
int file_exists(const wchar_t *path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }
void path_join(wchar_t *out, size_t cap, const wchar_t *a, const wchar_t *b) {
    size_t n;
    wcsncpy(out, a, cap - 1);
    out[cap - 1] = 0;
    n = wcslen(out);
    if (n && out[n - 1] != L'\\' && out[n - 1] != L'/' && n + 1 < cap) out[n++] = L'\\';
    wcsncpy(out + n, b, cap - n - 1);
    out[cap - 1] = 0;
}
int is_abs_path(const wchar_t *p) {
    return (iswalpha(p[0]) && p[1] == L':') || (p[0] == L'\\' && p[1] == L'\\') || p[0] == L'\\' || p[0] == L'/';
}
