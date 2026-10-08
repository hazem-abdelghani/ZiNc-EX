/* Controls profiles: named layouts of the Controls tab, one file each in the "profiles" folder (profiles\<name>.cfg). */
#include "common.h"

static void dir_path(wchar_t *out, size_t cap) { path_join(out, cap, g_root, L"profiles"); }

/* file name characters only */
void profile_clean_name(wchar_t *name) {
    wchar_t *r = name, *w = name;
    for (; *r; r++) if (!wcschr(L"\\/:*?\"<>|", *r) && *r >= 32) *w++ = *r;
    *w = 0;
    trim_w(name);
}

static void file_path(const wchar_t *name, wchar_t *out, size_t cap) {
    wchar_t d[560], f[200];
    dir_path(d, 560);
    _snwprintf(f, 200, L"%ls.cfg", name);
    f[199] = 0;
    path_join(out, cap, d, f);
}

static int cmp_names(const void *a, const void *b) { return _wcsicmp((const wchar_t *)a, (const wchar_t *)b); }

int profile_names(wchar_t names[][64], int max) {
    wchar_t pat[600], d[560];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;
    dir_path(d, 560);
    _snwprintf(pat, 600, L"%ls\\*.cfg", d);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        size_t l = wcslen(fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (l < 5 || l - 4 > 63 || n >= max) continue;
        wcsncpy(names[n], fd.cFileName, l - 4);
        names[n][l - 4] = 0;
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(names, (size_t)n, 64 * sizeof(wchar_t), cmp_names);
    return n;
}

char *profile_load(const wchar_t *name) {
    wchar_t p[600];
    file_path(name, p, 600);
    return read_file(p, NULL);
}

int profile_save(const wchar_t *name, const char *text) {
    wchar_t d[560], p[600];
    dir_path(d, 560);
    CreateDirectoryW(d, NULL);
    file_path(name, p, 600);
    return write_file(p, text, strlen(text));
}

/* renames the file of a profile; 0 when the new name is taken or the file cannot be moved */
int profile_rename(const wchar_t *from, const wchar_t *to) {
    wchar_t a[600], b[600];
    file_path(from, a, 600);
    file_path(to, b, 600);
    return MoveFileExW(a, b, 0);
}

int profile_delete(const wchar_t *name) {
    wchar_t p[600];
    file_path(name, p, 600);
    return DeleteFileW(p);
}
