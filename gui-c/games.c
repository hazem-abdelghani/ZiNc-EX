#include "common.h"
#include <initguid.h>
#include <shobjidl.h>

Game *g_games;
int g_ngames;

/* ---------------------------------------------------------------- parsing the ZiNc listing */
static void strip_inplace_w(wchar_t *s) { trim_w(s); }

/* every [bracketed] part of a title goes into the info column; the rest is the title */
static void split_title(const wchar_t *raw, wchar_t **title, wchar_t **info) {
    size_t n = wcslen(raw), i;
    wchar_t *t = (wchar_t *)calloc(n + 2, sizeof(wchar_t)), *inf = (wchar_t *)calloc(n + 2, sizeof(wchar_t));
    size_t tl = 0, il = 0;
    int in = 0, space = 1;
    wchar_t part[512];
    size_t pl = 0;
    for (i = 0; i < n; i++) {
        wchar_t c = raw[i];
        if (!in && c == L'[') { in = 1; pl = 0; if (!space) { t[tl++] = L' '; space = 1; } continue; }
        if (in) {
            if (c == L']') {
                in = 0;
                part[pl] = 0;
                trim_w(part);
                if (part[0]) { if (il) { inf[il++] = L','; inf[il++] = L' '; } wcscpy(inf + il, part); il += wcslen(part); }
            } else if (pl < 510) part[pl++] = c;
            continue;
        }
        if (iswspace(c)) { if (!space) { t[tl++] = L' '; space = 1; } }
        else { t[tl++] = c; space = 0; }
    }
    t[tl] = 0;
    strip_inplace_w(t);
    if (!t[0]) { wcscpy(t, raw); trim_w(t); }
    inf[il] = 0;
    *title = t;
    *info = inf;
}

/* the last [...] group is "set, parent X, BIOS Y" */
static void parse_set_info(const wchar_t *raw, wchar_t **set, wchar_t **parent, wchar_t **bios) {
    const wchar_t *a = NULL, *b = NULL, *p;
    wchar_t *grp, *tok, *save = NULL;
    int idx = 0;
    *set = wdup(L""); *parent = wdup(L""); *bios = wdup(L"");
    for (p = raw; (p = wcschr(p, L'[')) != NULL; ) {
        const wchar_t *e = wcschr(p, L']');
        if (!e) break;
        a = p + 1; b = e; p = e + 1;
    }
    if (!a) return;
    grp = (wchar_t *)calloc(b - a + 1, sizeof(wchar_t));
    wcsncpy(grp, a, b - a);
    for (tok = wcstok_s(grp, L",", &save); tok; tok = wcstok_s(NULL, L",", &save), idx++) {
        while (iswspace(*tok)) tok++;
        trim_w(tok);
        if (idx == 0) { free(*set); *set = wdup(tok); }
        else if (!wcsncmp(tok, L"parent ", 7)) { free(*parent); *parent = wdup(tok + 7); trim_w(*parent); }
        else if (!wcsncmp(tok, L"BIOS ", 5)) { free(*bios); *bios = wdup(tok + 5); trim_w(*bios); }
    }
    free(grp);
}

/* "  12  Name [..]" -> id and text */
static int game_line(const char *l, int *id, const char **text) {
    const char *p = l;
    int a = 0, b = 0, c = 0;
    const char *q;
    while (isspace((unsigned char)*p)) p++;
    if (!isdigit((unsigned char)*p)) return 0;
    *id = atoi(p);
    while (isdigit((unsigned char)*p)) p++;
    q = p;
    while (*p == ' ' || *p == '\t') { p++; a++; }
    while (*p && strchr("-:.)]", *p)) { p++; b++; }
    while (*p == ' ' || *p == '\t') { p++; c++; }
    if (c >= 1 || (b == 0 && a >= 1)) { if (!*p) return 0; *text = p; return 1; }
    if (a >= 1) { *text = q + 1; return *text[0] != 0; }   /* the punctuation belongs to the title */
    return 0;
}

typedef struct { HANDLE rd; DWORD pid; } Dummy;

int games_fetch(Game **out, int *n) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd = NULL, wr = NULL;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t cl[700];
    Buf txt = {0};
    DWORD t0 = GetTickCount(), got;
    char tmp[4096];
    int exited = 0, ok;
    char *line, *save = NULL;
    Game *list = NULL;
    int cnt = 0, cap = 0;

    *out = NULL;
    *n = 0;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    _snwprintf(cl, 700, L"\"%ls\" --list-games", g_exePath);
    ok = CreateProcessW(g_exePath, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g_root, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return 2; }
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break;
        if (avail) {
            if (ReadFile(rd, tmp, sizeof tmp, &got, NULL) && got) buf_addn(&txt, tmp, got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 40) == WAIT_OBJECT_0) {
            if (exited) break;
            exited = 1;               /* one more pass to drain what is left */
            continue;
        }
        if (GetTickCount() - t0 > 20000) { TerminateProcess(pi.hProcess, 1); buf_free(&txt); CloseHandle(rd); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 3; }
    }
    CloseHandle(rd); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (!txt.s) return 4;
    for (line = strtok_s(txt.s, "\n", &save); line; line = strtok_s(NULL, "\n", &save)) {
        int id;
        const char *text;
        char *cr = strchr(line, '\r');
        if (cr) *cr = 0;
        if (!game_line(line, &id, &text)) continue;
        {
            wchar_t *raw = (wchar_t *)malloc((strlen(text) + 1) * sizeof(wchar_t)), *t, *inf;
            Game g;
            MultiByteToWideChar(CP_ACP, 0, text, -1, raw, (int)strlen(text) + 1);
            trim_w(raw);
            split_title(raw, &t, &inf);
            g.id = id; g.title = t; g.info = inf;
            parse_set_info(raw, &g.set, &g.parent, &g.bios);
            free(raw);
            if (cnt == cap) { cap = cap ? cap * 2 : 128; list = (Game *)realloc(list, cap * sizeof(Game)); }
            list[cnt++] = g;
        }
    }
    buf_free(&txt);
    if (cnt == 0) { free(list); return 5; }
    *out = list;
    *n = cnt;
    return 0;
}

void games_free(Game *g, int n) {
    int i;
    for (i = 0; i < n; i++) { free(g[i].title); free(g[i].info); free(g[i].set); free(g[i].parent); free(g[i].bios); }
    free(g);
}

static int cmp_game(const void *a, const void *b) {
    const Game *x = (const Game *)a, *y = (const Game *)b;
    int c = _wcsicmp(x->title, y->title);
    if (!c) c = _wcsicmp(x->info, y->info);
    if (!c) c = x->id - y->id;
    return c;
}
void games_sort_default(Game *g, int n) { qsort(g, n, sizeof(Game), cmp_game); }

/* ---------------------------------------------------------------- which ROM sets exist */
static wchar_t **g_have;
static int g_nhave;

int rom_available_init(const wchar_t *dir) {
    wchar_t full[600], pat[640];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int cap = 0;
    rom_available_done();
    if (is_abs_path(dir)) wcsncpy(full, dir, 599); else path_join(full, 600, g_root, dir);
    full[599] = 0;
    _snwprintf(pat, 640, L"%ls\\*", full);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t *n = wdup(fd.cFileName);
        size_t l = wcslen(n);
        if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) { free(n); continue; }
        _wcslwr(n);
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (l > 4 && !wcscmp(n + l - 4, L".zip")) n[l - 4] = 0; else { free(n); continue; }
        }
        if (g_nhave == cap) { cap = cap ? cap * 2 : 128; g_have = (wchar_t **)realloc(g_have, cap * sizeof(wchar_t *)); }
        g_have[g_nhave++] = n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return g_nhave;
}
static int have_name(const wchar_t *n) {
    wchar_t *l = wdup(n);
    int i, r = 0;
    _wcslwr(l);
    for (i = 0; i < g_nhave && !r; i++) if (!wcscmp(g_have[i], l)) r = 1;
    free(l);
    return r;
}
int rom_available(const Game *g) {
    const wchar_t *names[3];
    int i;
    if (!g->set[0]) return 1;
    names[0] = g->set; names[1] = g->parent; names[2] = g->bios;
    for (i = 0; i < 3; i++) if (names[i][0] && !have_name(names[i])) return 0;
    return 1;
}
int rom_missing(const Game *g, wchar_t *out, size_t cap) {
    const wchar_t *names[3];
    int i, n = 0;
    out[0] = 0;
    if (!g->set[0]) return 0;
    names[0] = g->set; names[1] = g->parent; names[2] = g->bios;
    for (i = 0; i < 3; i++) {
        if (!names[i][0] || have_name(names[i])) continue;
        if (n++) wcsncat(out, L", ", cap - wcslen(out) - 1);
        wcsncat(out, names[i], cap - wcslen(out) - 1);
        wcsncat(out, L".zip", cap - wcslen(out) - 1);
    }
    return n;
}
void rom_available_done(void) {
    int i;
    for (i = 0; i < g_nhave; i++) free(g_have[i]);
    free(g_have);
    g_have = NULL;
    g_nhave = 0;
}

/* ---------------------------------------------------------------- shortcut names */
/* "Street Fighter EX Plus (US 970407)" -> "Street Fighter EX Plus (US)": dates, revisions and "Ver. x" are dropped;
   keepVer keeps a version in the name part ("G-Darius Ver.2 (JP)") */
static void drop_trailing_ver(wchar_t *s) {
    size_t i, n = wcslen(s);
    for (i = 0; i < n; i++) {
        size_t k, m;
        if (!iswspace(s[i])) continue;
        k = i;
        while (iswspace(s[k])) k++;
        if (_wcsnicmp(s + k, L"Ver", 3)) { i = k - 1; continue; }
        m = k + 3;
        if (s[m] == L'.') m++;
        while (iswspace(s[m])) m++;
        if (s[m]) {
            size_t e = m;
            while (s[e] && !iswspace(s[e])) e++;
            if (!s[e]) { s[i] = 0; return; }
        }
        i = k - 1;
    }
}
static void squeeze_spaces(wchar_t *s) {
    wchar_t *r = s, *w = s;
    int sp = 0;
    for (; *r; r++) {
        if (iswspace(*r)) { if (!sp) *w++ = L' '; sp = 1; } else { *w++ = *r; sp = 0; }
    }
    *w = 0;
}

void short_title(const wchar_t *title, int keepVer, wchar_t *out, size_t cap) {
    const wchar_t *p;
    wchar_t name[400], region[80];
    for (p = title; (p = wcschr(p, L'(')) != NULL; p++) {
        const wchar_t *q = p + 1, *e;
        size_t rl = 0;
        while (iswspace(*q)) q++;
        while (*q && !iswspace(*q) && *q != L')') { if (rl < 78) region[rl++] = *q; q++; }
        if (rl == 0) continue;
        e = wcschr(q, L')');
        if (!e) continue;
        region[rl] = 0;
        wcsncpy(name, title, p - title);
        name[p - title] = 0;
        trim_w(name);
        if (!keepVer) drop_trailing_ver(name);
        _snwprintf(out, cap, L"%ls (%ls)", name, region);
        out[cap - 1] = 0;
        squeeze_spaces(out);
        return;
    }
    wcsncpy(name, title, 399);
    name[399] = 0;
    trim_w(name);
    if (!keepVer) { drop_trailing_ver(name); trim_w(name); }
    wcsncpy(out, name, cap - 1);
    out[cap - 1] = 0;
}

/* shortTitle, with the version kept only when two games would otherwise get the same name */
void shortcut_name(const Game *g, wchar_t *out, size_t cap) {
    wchar_t n[400], o[400], v[400];
    int i;
    short_title(g->title, 0, n, 400);
    for (i = 0; i < g_ngames; i++) {
        if (g_games[i].id == g->id) continue;
        short_title(g_games[i].title, 0, o, 400);
        if (!_wcsicmp(o, n)) {
            short_title(g->title, 1, v, 400);
            if (_wcsicmp(v, n)) { wcsncpy(out, v, cap - 1); out[cap - 1] = 0; return; }
            wcsncpy(out, g->title, cap - 1);
            out[cap - 1] = 0;
            trim_w(out);
            return;
        }
    }
    wcsncpy(out, n, cap - 1);
    out[cap - 1] = 0;
}

/* ---------------------------------------------------------------- desktop icon */
/* A shortcut on the desktop that starts ZiNc.exe directly with the current settings (fullscreen) and the game's own settings. */
int create_desktop_icon(const Game *g, wchar_t *err, size_t errCap) {
    wchar_t name[400], desk[MAX_PATH], lnk[800], self[560], *joined;
    ArgList a;
    int i;
    size_t cap = 64;
    HRESULT hr;
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    err[0] = 0;
    shortcut_name(g, name, 400);
    for (i = 0; name[i]; i++) if (wcschr(L"\\/:*?\"<>|", name[i])) { memmove(name + i, name + i + 1, (wcslen(name + i)) * sizeof(wchar_t)); i--; }
    {   /* the shortcut starts the game fullscreen with the game's settings; its own copy of renderer.cfg keeps the normal one untouched */
        PlayOpts po;
        memset(&po, 0, sizeof po);
        po.fullscreen = 1;
        po.tag = L"-icon";
        zinc_args_ex(g->id, &po, &a, NULL, 0);
    }
    for (i = 0; i < a.n; i++) cap += wcslen(a.a[i]) + 4;
    joined = (wchar_t *)calloc(cap, sizeof(wchar_t));
    for (i = 0; i < a.n; i++) {
        const wchar_t *x = a.a[i];
        int blank = wcschr(x, L' ') || wcschr(x, L'\t');
        if (i) wcscat(joined, L" ");
        if (blank && x[0] == L'-' && x[1] == L'-' && wcschr(x, L'=')) {
            size_t k = wcschr(x, L'=') - x + 1;
            wcsncat(joined, x, k);
            wcscat(joined, L"\"");
            wcscat(joined, x + k);
            wcscat(joined, L"\"");
        } else if (blank) { wcscat(joined, L"\""); wcscat(joined, x); wcscat(joined, L"\""); }
        else wcscat(joined, x);
    }
    GetModuleFileNameW(NULL, self, 560);
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk))) { _snwprintf(err, errCap, L"Desktop folder not found"); free(joined); args_free(&a); return 0; }
    _snwprintf(lnk, 800, L"%ls\\%ls.lnk", desk, name);
    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl);
    if (SUCCEEDED(hr)) {
        sl->lpVtbl->SetPath(sl, g_exePath);
        sl->lpVtbl->SetArguments(sl, joined);
        sl->lpVtbl->SetWorkingDirectory(sl, g_root);
        sl->lpVtbl->SetIconLocation(sl, self, 0);
        sl->lpVtbl->SetDescription(sl, g->title);
        hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
        if (SUCCEEDED(hr)) { hr = pf->lpVtbl->Save(pf, lnk, TRUE); pf->lpVtbl->Release(pf); }
        sl->lpVtbl->Release(sl);
    }
    CoUninitialize();
    free(joined);
    args_free(&a);
    if (FAILED(hr)) { _snwprintf(err, errCap, L"Windows could not create the shortcut (error 0x%08lX).", (unsigned long)hr); return 0; }
    return 1;
}
