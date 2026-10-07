/* Backup and restore: one zip with the settings, the input config, the profiles, the per-game settings and the ZiNc saves (cfg folder). */
#include "common.h"
#include "zipio.h"

static const wchar_t *TOP_FILES[] = {L"zinc-settings.cfg", L"zinc-input.cfg", L"renderer.cfg", L"zinc-games.cfg", NULL};
static const wchar_t *TOP_DIRS[] = {L"profiles", L"cfg", L"covers", L"bezels", NULL};

static void add_file(ZipWriter *z, const wchar_t *rel, int *count) {
    wchar_t full[700];
    size_t n = 0;
    char *data, *name;
    path_join(full, 700, g_root, rel);
    data = read_file(full, &n);
    if (!data) return;
    name = w_to_u8(rel);
    { char *p; for (p = name; *p; p++) if (*p == '\\') *p = '/'; }
    if (zw_add(z, name, data, n)) (*count)++;
    free(name);
    free(data);
}

static void add_dir(ZipWriter *z, const wchar_t *rel, int *count, int depth) {
    wchar_t pat[700];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, 700, L"%ls\\%ls\\*", g_root, rel);
    pat[699] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t sub[700];
        if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
        _snwprintf(sub, 700, L"%ls\\%ls", rel, fd.cFileName);
        sub[699] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 4) add_dir(z, sub, count, depth + 1); }
        else if (fd.nFileSizeHigh == 0 && fd.nFileSizeLow < 64UL * 1024 * 1024) add_file(z, sub, count);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* writes the backup zip; returns the number of files (0 = nothing written) */
int backup_create(const wchar_t *zipPath) {
    ZipWriter z;
    int count = 0, i;
    zw_init(&z);
    for (i = 0; TOP_FILES[i]; i++) add_file(&z, TOP_FILES[i], &count);
    for (i = 0; TOP_DIRS[i]; i++) add_dir(&z, TOP_DIRS[i], &count, 0);
    if (count == 0 || !zw_finish(&z) || !write_file(zipPath, z.data, z.n)) count = 0;
    zw_free(&z);
    return count;
}

/* names a backup may contain (and where): no way out of the ZiNc folder */
static int allowed(const char *name) {
    int i;
    if (strstr(name, "..") || strchr(name, ':') || name[0] == '/' || name[0] == '\\') return 0;
    for (i = 0; TOP_FILES[i]; i++) { char *u = w_to_u8(TOP_FILES[i]); int ok = !strcmp(u, name); free(u); if (ok) return 1; }
    for (i = 0; TOP_DIRS[i]; i++) { char *u = w_to_u8(TOP_DIRS[i]); size_t l = strlen(u); int ok = !strncmp(u, name, l) && name[l] == '/' && name[l + 1]; free(u); if (ok) return 1; }
    return 0;
}

static void make_parents(const wchar_t *full) {
    wchar_t tmp[700];
    size_t i, n = wcslen(full);
    wcsncpy(tmp, full, 699);
    tmp[699] = 0;
    for (i = wcslen(g_root) + 1; i < n; i++) if (tmp[i] == L'\\') { tmp[i] = 0; CreateDirectoryW(tmp, NULL); tmp[i] = L'\\'; }
}

/* restores a backup zip; returns the number of files written, or -1 when the zip is not a readable backup */
int backup_restore(const wchar_t *zipPath) {
    size_t len = 0;
    unsigned char *buf = (unsigned char *)read_file(zipPath, &len);
    ZipEntry *e;
    ZipReport rep;
    int n, i, done = 0;
    if (!buf) return -1;
    if (!zip_verify(buf, len, &rep)) { free(buf); return -1; }
    e = (ZipEntry *)malloc(sizeof(ZipEntry) * 4096);
    n = e ? zip_entries(buf, len, e, 4096) : -1;
    if (n < 0) { free(e); free(buf); return -1; }
    if (n > 4096) n = 4096;
    for (i = 0; i < n; i++) {
        unsigned char *data;
        wchar_t *rel, full[700];
        size_t k;
        if (!allowed(e[i].name) || e[i].usize > 256UL * 1024 * 1024) continue;
        data = (unsigned char *)malloc(e[i].usize + 1);
        if (!data) continue;
        if (zip_extract(buf, len, &e[i], data)) {
            rel = u8_to_w(e[i].name, -1);
            for (k = 0; rel[k]; k++) if (rel[k] == L'/') rel[k] = L'\\';
            path_join(full, 700, g_root, rel);
            make_parents(full);
            if (write_file(full, data, e[i].usize)) done++;
            free(rel);
        }
        free(data);
    }
    free(e);
    free(buf);
    return done;
}
