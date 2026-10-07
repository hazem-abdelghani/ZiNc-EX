#include "common.h"

Settings g_set;
WinRect g_winRect, g_optRect;
wchar_t g_root[560], g_exePath[560], g_settingsFile[560], g_rendererCfg[560];

const unsigned char *embedded_input(size_t *n);
const unsigned char *embedded_d3d11(size_t *n);
const unsigned char *embedded_renderer_cfg(size_t *n);

/* ---------------------------------------------------------------- key / value map (renderer.cfg values) */
int kv_has(const KVMap *m, const char *key) { int i; for (i = 0; i < m->n; i++) if (!strcmp(m->kv[i].key, key)) return 1; return 0; }
int kv_get(const KVMap *m, const char *key) { int i; for (i = 0; i < m->n; i++) if (!strcmp(m->kv[i].key, key)) return m->kv[i].val; return 0; }
void kv_set(KVMap *m, const char *key, int v) {
    int i;
    for (i = 0; i < m->n; i++) if (!strcmp(m->kv[i].key, key)) { m->kv[i].val = v; return; }
    if (m->n < 48) { strncpy(m->kv[m->n].key, key, 31); m->kv[m->n].key[31] = 0; m->kv[m->n++].val = v; }
}

/* ---------------------------------------------------------------- theme */
static int system_dark(void) {   /* the "apps" colour mode of Windows */
    HKEY k;
    DWORD v = 1, n = sizeof v;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"AppsUseLightTheme", NULL, NULL, (BYTE *)&v, &n);
        RegCloseKey(k);
    }
    return v == 0;
}
void settings_resolve_theme(void) { g_set.dark = g_set.themeMode == 2 || (g_set.themeMode == 0 && system_dark()); }

/* ---------------------------------------------------------------- defaults */
void settings_defaults(Settings *s, const Settings *keep) {
    Settings n;
    memset(&n, 0, sizeof n);
    wcscpy(n.roms, L"roms");
    n.useSound = 1;
    n.soundFilter = 1;   /* the sound filter is on by default */
    wcscpy(n.renderer, L"Direct3D11");
    n.hideConsole = 1;
    n.rotate = -1;
    n.cutoff = 22050;
    n.surroundMul = 40;
    n.enhanced = n.xinputOn = n.dinputOn = n.analog = 1;
    n.deadzone = 50;
    n.logs = 0;
    n.sortCol = 1; n.sortAsc = 1;
    n.colsHidden = 8 | 64;   /* Info and Year are hidden by default */
    n.hideSearch = n.hideFilters = 1;   /* the search bar and the filter bar are off by default */
    n.optWindow = 1;   /* the settings are in a window of their own by default */
    if (keep) {
        n.colsHidden = keep->colsHidden; n.optWindow = keep->optWindow;
        n.themeMode = keep->themeMode; n.hideSearch = keep->hideSearch; n.hideFilters = keep->hideFilters; n.hideToolbar = keep->hideToolbar;
        wcscpy(n.roms, keep->roms);
        n.onlyAvail = keep->onlyAvail; n.favOnly = keep->favOnly;
        n.sortCol = keep->sortCol; n.sortAsc = keep->sortAsc;
        wcscpy(n.fRegion, keep->fRegion); wcscpy(n.fMaker, keep->fMaker); wcscpy(n.fGenre, keep->fGenre);
        wcscpy(n.trainer, keep->trainer);   /* the trainer program is kept like the ROMs folder; "start trainer" and the dark theme go back to off */
        memcpy(n.colsGame, keep->colsGame, sizeof n.colsGame); memcpy(n.colsCtl, keep->colsCtl, sizeof n.colsCtl); memcpy(n.colsCmb, keep->colsCmb, sizeof n.colsCmb); n.splitX = keep->splitX;
        n.enhanced = keep->enhanced; n.analog = keep->analog; n.deadzone = keep->deadzone; n.pad1 = keep->pad1; n.pad2 = keep->pad2;   /* the Controls tab stays as it is */
        il_copy(&n.fav, &keep->fav);
        il_copy(&n.recent, &keep->recent);
    }
    il_clear(&s->fav);
    il_clear(&s->recent);
    *s = n;
    n.dark = n.themeMode == 2 || (n.themeMode == 0 && system_dark());
    s->dark = n.dark;
}

typedef struct { const char *key; int kind; void *p; int cap; } IniField;   /* kind 0: text, 1: number, 2: flag */

static int ini_fields(IniField *f) {
    Settings *s = &g_set;
    int n = 0;
#define TXT(k, v) f[n].key = k, f[n].kind = 0, f[n].p = v, f[n].cap = (int)(sizeof v / sizeof(wchar_t)), n++
#define NUM(k, v) f[n].key = k, f[n].kind = 1, f[n].p = &(v), n++
#define FLG(k, v) f[n].key = k, f[n].kind = 2, f[n].p = &(v), n++
    TXT("roms_directory", s->roms); TXT("renderer", s->renderer);
    FLG("use_sound", s->useSound); NUM("rotate", s->rotate);
    FLG("sound_filter", s->soundFilter); NUM("sound_filter_cutoff", s->cutoff);
    FLG("surround_lite", s->surround); NUM("surround_multiplier", s->surroundMul);
    FLG("stereo_exciter", s->exciter); FLG("slow_geometry", s->slowGeometry);
    FLG("hide_console", s->hideConsole); NUM("on_game_start", s->onStart);
    FLG("enhanced_input", s->enhanced); FLG("xinput", s->xinputOn);
    FLG("directinput", s->dinputOn); FLG("pad_directions", s->analog);
    NUM("stick_threshold", s->deadzone); NUM("pad1", s->pad1); NUM("pad2", s->pad2);
    FLG("only_available", s->onlyAvail); FLG("logs", s->logs);
    TXT("trainer_path", s->trainer); FLG("trainer_enabled", s->trainerOn); FLG("dark_theme", s->dark);
    return n;
}

void canonical_renderer(wchar_t *name) {
    static const wchar_t *from[] = {L"winogl", L"wind3d", L"winsoft", L"Software"};
    static const wchar_t *to[] = {L"OpenGL", L"Direct3D", L"OpenGL", L"OpenGL"};
    int i;
    for (i = 0; i < 4; i++) if (!_wcsicmp(name, from[i])) { wcscpy(name, to[i]); return; }
}

static void parse_ints(const char *v, int *arr, int n) {
    char *copy = _strdup(v), *tok, *save = NULL;
    int i = 0;
    for (tok = strtok_s(copy, ",", &save); tok && i < n; tok = strtok_s(NULL, ",", &save), i++) { int x; if (xatoi(trim_a(tok), &x) && x >= 0 && x < 4000) arr[i] = x; }
    free(copy);
}

static void add_ints(Buf *b, const char *name, const int *arr, int n) {
    int i, any = 0;
    for (i = 0; i < n; i++) if (arr[i] > 0) any = 1;
    if (!any) return;
    buf_fmt(b, "%s=", name);
    for (i = 0; i < n; i++) buf_fmt(b, "%s%d", i ? "," : "", arr[i]);
    buf_add(b, "\r\n");
}

static void parse_int_list(const char *v, IntList *l) {
    char *copy = _strdup(v), *tok, *save = NULL;
    il_clear(l);
    for (tok = strtok_s(copy, ",", &save); tok; tok = strtok_s(NULL, ",", &save)) {
        int x;
        if (xatoi(trim_a(tok), &x)) il_add(l, x);
    }
    free(copy);
}

void settings_load(void) {
    char *txt = read_file(g_settingsFile, NULL), *line, *save = NULL;
    IniField f[40];
    int nf = ini_fields(f), i, version = 0, haveFav = 0, haveRecent = 0;
    char favs[4096] = "", recents[512] = "";
    int wx = 0, wy = 0, ww = 0, wh = 0, haveTheme = 0, haveDark = 0;
    if (!txt) { settings_resolve_theme(); return; }
    for (line = strtok_s(txt, "\n", &save); line; line = strtok_s(NULL, "\n", &save)) {
        char *eq, *key, *val;
        line = trim_a(line);
        if (!*line || *line == ';' || *line == '#' || *line == '[') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        key = trim_a(line);
        val = trim_a(eq + 1);
        _strlwr(key);
        for (i = 0; i < nf; i++) {
            if (strcmp(f[i].key, key)) continue;
            if (f[i].kind == 0) { wchar_t *w = u8_to_w(val, -1); wcsncpy((wchar_t *)f[i].p, w, f[i].cap - 1); ((wchar_t *)f[i].p)[f[i].cap - 1] = 0; free(w); }
            else if (f[i].kind == 1) { int x; if (xatoi(val, &x)) *(int *)f[i].p = x; }
            else *(int *)f[i].p = !strcmp(val, "1") || !_stricmp(val, "yes") || !_stricmp(val, "true");
        }
        if (!strcmp(key, "dark_theme")) haveDark = 1;
        if (!strcmp(key, "settings_version")) xatoi(val, &version);
        else if (!strcmp(key, "theme_mode")) { xatoi(val, &g_set.themeMode); haveTheme = 1; }
        else if (!strcmp(key, "search_bar")) g_set.hideSearch = !strcmp(val, "0");
        else if (!strcmp(key, "filters_bar")) g_set.hideFilters = !strcmp(val, "0");
        else if (!strcmp(key, "toolbar")) g_set.hideToolbar = !strcmp(val, "0");
        else if (!strcmp(key, "favorites_only")) g_set.favOnly = !strcmp(val, "1");
        else if (!strcmp(key, "filter_region")) { wchar_t *w = u8_to_w(val, -1); wcsncpy(g_set.fRegion, w, 7); free(w); }
        else if (!strcmp(key, "filter_maker")) { wchar_t *w = u8_to_w(val, -1); wcsncpy(g_set.fMaker, w, 39); free(w); }
        else if (!strcmp(key, "filter_genre")) { wchar_t *w = u8_to_w(val, -1); wcsncpy(g_set.fGenre, w, 23); free(w); }
        else if (!strcmp(key, "sort_column")) xatoi(val, &g_set.sortCol);
        else if (!strcmp(key, "sort_ascending")) g_set.sortAsc = !strcmp(val, "1");
        else if (!strcmp(key, "columns_games3")) parse_ints(val, g_set.colsGame, 7);
        else if (!strcmp(key, "columns_controls")) parse_ints(val, g_set.colsCtl, 5);
        else if (!strcmp(key, "columns_combos")) parse_ints(val, g_set.colsCmb, 9);
        else if (!strcmp(key, "splitter")) xatoi(val, &g_set.splitX);
        else if (!strcmp(key, "columns_hidden2")) xatoi(val, &g_set.colsHidden);
        else if (!strcmp(key, "window_x")) xatoi(val, &wx);
        else if (!strcmp(key, "window_y")) xatoi(val, &wy);
        else if (!strcmp(key, "window_w")) xatoi(val, &ww);
        else if (!strcmp(key, "window_h")) xatoi(val, &wh);
        else if (!strcmp(key, "options_window")) g_set.optWindow = !strcmp(val, "1");
        else if (!strcmp(key, "optwin_x")) xatoi(val, &g_optRect.x);
        else if (!strcmp(key, "optwin_y")) xatoi(val, &g_optRect.y);
        else if (!strcmp(key, "optwin_w")) xatoi(val, &g_optRect.w);
        else if (!strcmp(key, "optwin_h")) xatoi(val, &g_optRect.h);
        else if (!strcmp(key, "favorites")) { strncpy(favs, val, sizeof favs - 1); haveFav = 1; }
        else if (!strcmp(key, "recent")) { strncpy(recents, val, sizeof recents - 1); haveRecent = 1; }
    }
    free(txt);
    if (!haveTheme && haveDark) g_set.themeMode = g_set.dark ? 2 : 1;   /* a settings file of an older version: its dark / light choice stays */
    if (g_set.themeMode < 0 || g_set.themeMode > 2) g_set.themeMode = 0;
    settings_resolve_theme();
    if (ww > 0) { g_winRect.w = ww; g_winRect.x = wx; g_winRect.y = wy; g_winRect.h = wh; }
    /* settings files before version 2 could have the input options switched off by a startup bug: back to the defaults */
    if (version != 2) g_set.enhanced = 1;
    g_set.xinputOn = g_set.dinputOn = g_set.analog = 1;   /* always on: there are no check boxes for them */
    if (haveFav) parse_int_list(favs, &g_set.fav);
    if (haveRecent) parse_int_list(recents, &g_set.recent);
    if (g_set.sortCol < 0 || g_set.sortCol > 6) g_set.sortCol = 1;
    if (g_set.onStart < 0 || g_set.onStart > 2) g_set.onStart = 0;
    if (g_set.themeMode < 0 || g_set.themeMode > 2) g_set.themeMode = 0;
    g_set.colsHidden &= 127;
    g_set.optWindow = g_set.optWindow != 0;
    canonical_renderer(g_set.renderer);
}

static void add_list(Buf *b, const char *name, const IntList *l) {
    int i;
    buf_fmt(b, "%s=", name);
    for (i = 0; i < l->n; i++) buf_fmt(b, "%s%d", i ? "," : "", l->v[i]);
    buf_add(b, "\r\n");
}

int settings_save(void) {
    Buf b = {0};
    IniField f[40];
    int nf = ini_fields(f), i, ok;
    buf_add(&b, "; ZiNc EX settings (written by ZiNc EX; safe to edit by hand)\r\n[ZiNc-EX]\r\nsettings_version=2\r\n");
    for (i = 0; i < nf; i++) {
        if (f[i].kind == 0) { char *u = w_to_u8((wchar_t *)f[i].p); buf_fmt(&b, "%s=%s\r\n", f[i].key, u); free(u); }
        else buf_fmt(&b, "%s=%d\r\n", f[i].key, *(int *)f[i].p != 0 && f[i].kind == 2 ? 1 : *(int *)f[i].p);
    }
    if (g_winRect.w > 0) buf_fmt(&b, "window_x=%d\r\nwindow_y=%d\r\nwindow_w=%d\r\nwindow_h=%d\r\n", g_winRect.x, g_winRect.y, g_winRect.w, g_winRect.h);
    buf_fmt(&b, "options_window=%d\r\ntheme_mode=%d\r\nsearch_bar=%d\r\nfilters_bar=%d\r\ntoolbar=%d\r\n", g_set.optWindow != 0, g_set.themeMode, !g_set.hideSearch, !g_set.hideFilters, !g_set.hideToolbar);
    if (g_optRect.w > 0) buf_fmt(&b, "optwin_x=%d\r\noptwin_y=%d\r\noptwin_w=%d\r\noptwin_h=%d\r\n", g_optRect.x, g_optRect.y, g_optRect.w, g_optRect.h);
    buf_fmt(&b, "favorites_only=%d\r\nsort_column=%d\r\nsort_ascending=%d\r\n", g_set.favOnly != 0, g_set.sortCol, g_set.sortAsc != 0);
    { char *u;
      u = w_to_u8(g_set.fRegion); buf_fmt(&b, "filter_region=%s\r\n", u); free(u);
      u = w_to_u8(g_set.fMaker); buf_fmt(&b, "filter_maker=%s\r\n", u); free(u);
      u = w_to_u8(g_set.fGenre); buf_fmt(&b, "filter_genre=%s\r\n", u); free(u); }
    add_ints(&b, "columns_games3", g_set.colsGame, 7);
    add_ints(&b, "columns_controls", g_set.colsCtl, 5);
    add_ints(&b, "columns_combos", g_set.colsCmb, 9);
    if (g_set.splitX > 0) buf_fmt(&b, "splitter=%d\r\n", g_set.splitX);
    buf_fmt(&b, "columns_hidden2=%d\r\n", g_set.colsHidden & 127);
    add_list(&b, "favorites", &g_set.fav);
    add_list(&b, "recent", &g_set.recent);
    ok = write_file(g_settingsFile, b.s, b.n);
    buf_free(&b);
    return ok;
}

void settings_sig(Buf *b) {
    IniField f[40];
    int nf = ini_fields(f), i;
    for (i = 0; i < nf; i++) {
        if (!strcmp(f[i].key, "dark_theme")) continue;   /* the theme is chosen in the View menu and saved at once */
        if (f[i].kind == 0) { char *u = w_to_u8((wchar_t *)f[i].p); buf_fmt(b, "%s;", u); free(u); }
        else buf_fmt(b, "%d;", *(int *)f[i].p);
    }
}

void set_favorite(int id, int on) {
    il_remove(&g_set.fav, id);
    if (on) il_add(&g_set.fav, id);
    settings_save();
}

/* ---------------------------------------------------------------- renderer.cfg */
void renderer_defaults(KVMap *m) {
    static const struct { const char *k; int v; } d[] = {
        {"XSize", 640}, {"YSize", 480},   /* (replaced by the desktop size below) */ {"FullScreen", 1}, {"ColorDepth", 32}, {"ScanLines", 0}, {"Filtering", 3},
        {"Blending", 1}, {"Dithering", 0}, {"ShowFPS", 0}, {"FrameLimitation", 1}, {"FrameSkipping", 0},
        {"FramerateDetection", 1}, {"FramerateManual", 60}, {"TextureType", 3}, {"TextureCaching", 2}, {"InternalScale", 0}, {"XBRZ", 2}, {"FXAA", 1}, {"TextureSmoothing", 1}, {"Overscan", 0}, {"KeepAspect", 1}, {"Dedither", 3}, {"VSync", 0}, {"Borderless", 1}};
    size_t i;
    memset(m, 0, sizeof *m);
    for (i = 0; i < sizeof d / sizeof d[0]; i++) kv_set(m, d[i].k, d[i].v);
    { int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN); if (sw >= 320 && sh >= 240) { kv_set(m, "XSize", sw); kv_set(m, "YSize", sh); } }   /* the resolution is the one of Windows' display settings */
}

/* "  Key   =  -12  ; comment": returns 1 and the spans of the key and of the number */
static int cfg_line(const char *l, int *k0, int *k1, int *v0, int *v1) {
    int i = 0;
    while (l[i] == ' ' || l[i] == '\t') i++;
    *k0 = i;
    while (isalnum((unsigned char)l[i]) || l[i] == '_') i++;
    *k1 = i;
    if (*k1 == *k0) return 0;
    while (l[i] == ' ' || l[i] == '\t') i++;
    if (l[i] != '=') return 0;
    i++;
    while (l[i] == ' ' || l[i] == '\t') i++;
    *v0 = i;
    if (l[i] == '-') i++;
    if (!isdigit((unsigned char)l[i])) return 0;
    while (isdigit((unsigned char)l[i])) i++;
    *v1 = i;
    return 1;
}

wchar_t g_bezelPath[520];

/* is this line "BezelImage = text"? then v0 is where the text starts */
static int bezel_line(const char *l, int *v0) {
    int i = 0;
    while (l[i] == ' ' || l[i] == '\t') i++;
    if (_strnicmp(l + i, "BezelImage", 10)) return 0;
    i += 10;
    while (l[i] == ' ' || l[i] == '\t') i++;
    if (l[i] != '=') return 0;
    i++;
    while (l[i] == ' ' || l[i] == '\t') i++;
    *v0 = i;
    return 1;
}

void renderer_read(KVMap *m) {
    char *txt, *line, *save = NULL;
    renderer_defaults(m);
    g_bezelPath[0] = 0;
    txt = read_file(g_rendererCfg, NULL);
    if (!txt) return;
    for (line = strtok_s(txt, "\n", &save); line; line = strtok_s(NULL, "\n", &save)) {
        int k0, k1, v0, v1;
        char key[40], *cr = strchr(line, '\r');
        if (cr) *cr = 0;
        if (bezel_line(line, &v0)) {
            char *semi = strchr(line + v0, ';'), *e;   /* a comment ends the path (the renderer does the same) */
            if (semi) *semi = 0;
            e = line + strlen(line);
            while (e > line + v0 && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
            MultiByteToWideChar(CP_UTF8, 0, line + v0, -1, g_bezelPath, 520);
            continue;
        }
        if (cfg_line(line, &k0, &k1, &v0, &v1) && k1 - k0 < 39) {
            memcpy(key, line + k0, k1 - k0);
            key[k1 - k0] = 0;
            kv_set(m, key, atoi(line + v0));
        }
    }
    free(txt);
}

static int cmp_kv(const void *a, const void *b) { return strcmp(((const KV *)a)->key, ((const KV *)b)->key); }

/* updates values in place, preserving comments and layout */
static const wchar_t *g_bezelOverride;   /* the bezel of one game, for the copy of renderer.cfg that is being written */
static void write_cfg_to(const wchar_t *dst, const KVMap *values) {
    char *txt = read_file(g_rendererCfg, NULL), *line;
    Buf out = {0};
    char seen[48] = {0};
    KV sorted[48];
    int i;
    if (txt) {
        char *cur = txt;
        size_t tl = strlen(txt);
        while (tl && (txt[tl - 1] == '\n' || txt[tl - 1] == '\r')) txt[--tl] = 0;   /* no trailing blank lines */
        while (cur) {
            int k0, k1, v0, v1, handled = 0;
            char *nl = strchr(cur, '\n'), *cr;
            if (nl) *nl = 0;
            cr = strchr(cur, '\r');
            if (cr) *cr = 0;
            line = cur;
            if (bezel_line(line, &v0)) { cur = nl ? nl + 1 : NULL; continue; }   /* written again below */
            if (cfg_line(line, &k0, &k1, &v0, &v1) && k1 - k0 < 39) {
                char key[40];
                memcpy(key, line + k0, k1 - k0);
                key[k1 - k0] = 0;
                for (i = 0; i < values->n; i++) {
                    if (!strcmp(values->kv[i].key, key)) {
                        seen[i] = 1;
                        buf_addn(&out, line, v0);
                        buf_fmt(&out, "%d%s\r\n", values->kv[i].val, line + v1);
                        handled = 1;
                        break;
                    }
                }
            }
            if (!handled) { buf_add(&out, line); buf_add(&out, "\r\n"); }
            cur = nl ? nl + 1 : NULL;
        }
        free(txt);
    }
    memcpy(sorted, values->kv, sizeof(KV) * values->n);
    qsort(sorted, values->n, sizeof(KV), cmp_kv);
    for (i = 0; i < values->n; i++) {
        int j, done = 0;
        for (j = 0; j < values->n; j++) if (!strcmp(values->kv[j].key, sorted[i].key) && seen[j]) done = 1;
        if (!done) {
            static const struct { const char *key, *note; } notes[] = {
                {"InternalScale", "Direct3D 11 only: internal resolution: 0=auto, 1=1x (console resolution) ... 4=4x"},
                {"XBRZ", "Direct3D 11 only: xBRZ filter: 0=off, 1=all graphics, 2=2D objects only"},
                {"TextureSmoothing", "Direct3D 11 only: smooths the 3D textures (bilinear; 2D sprites stay sharp): 0=off, 1=on"},
                {"FXAA", "Direct3D 11 only: FXAA post-process filter: 0=off, 1=on"},
                {"Overscan", "Direct3D 11 only: pixels of the console picture cut off at every edge: 0=none"},
                {"KeepAspect", "Direct3D 11 only: picture shape: 0=stretch to the window, 1=4:3, 2=16:9, 3=pixel perfect (whole-number size)"},
                {"VSync", "Direct3D 11 only: wait for the screen refresh before showing a frame (no tearing): 0=off, 1=on"},
                {"Borderless", "Direct3D 11 only: fullscreen as a borderless window over the desktop, the screen mode is not changed: 0=off, 1=on"},
                {"Dedither", "Direct3D 11 only: smooths the checkerboard dither pattern in the picture: 0=off, 1=low, 2=medium, 3=high"},
                {"Logging", "Direct3D 11 only: write zinc-d3d11.log: 0/1"},
            };
            size_t k;
            const char *note = NULL;
            for (k = 0; k < sizeof notes / sizeof notes[0]; k++) if (!strcmp(notes[k].key, sorted[i].key)) note = notes[k].note;
            if (note) buf_fmt(&out, "%s = %d\t; %s\r\n", sorted[i].key, sorted[i].val, note);
            else buf_fmt(&out, "%s = %d\r\n", sorted[i].key, sorted[i].val);
        }
    }
    if (g_bezelOverride ? g_bezelOverride[0] : g_bezelPath[0]) {
        char u[1600];
        if (WideCharToMultiByte(CP_UTF8, 0, g_bezelOverride ? g_bezelOverride : g_bezelPath, -1, u, sizeof u, NULL, NULL)) buf_fmt(&out, "BezelImage = %s\t; Direct3D 11 only: a PNG with a transparent window: the picture is shown in it\r\n", u);
    }
    write_file(dst, out.s ? out.s : "", out.n);
    buf_free(&out);
}
void renderer_write(const KVMap *values) { write_cfg_to(g_rendererCfg, values); }
/* the same renderer.cfg with some values changed, written to another file (renderer.cfg itself stays as it is) */
int renderer_write_copy(const wchar_t *dst, const KVMap *values) { write_cfg_to(dst, values); return file_exists(dst); }

/* ---------------------------------------------------------------- built-in plugins */
static void make_dirs(const wchar_t *dir) {
    wchar_t tmp[560];
    size_t i, n = wcslen(dir);
    wcsncpy(tmp, dir, 559);
    tmp[559] = 0;
    for (i = 3; i < n; i++)
        if (tmp[i] == L'\\' || tmp[i] == L'/') { wchar_t c = tmp[i]; tmp[i] = 0; CreateDirectoryW(tmp, NULL); tmp[i] = c; }
    CreateDirectoryW(tmp, NULL);
}

static int install_blob(const wchar_t *path, const unsigned char *data, size_t n) {
    size_t have = 0;
    char *old = read_file(path, &have);
    if (old && have == n && !memcmp(old, data, n)) { free(old); return 0; }
    free(old);
    return write_file(path, data, n) ? 0 : GetLastError() ? (int)GetLastError() : -1;
}

/* writes the renderers that are built into the launcher (renderers\Direct3D11) when they are missing or outdated */
void install_renderers(void) {
    wchar_t dir[560], p[560];
    size_t n;
    const unsigned char *d = embedded_d3d11(&n);
    if (!file_exists(g_rendererCfg)) {   /* no renderer.cfg yet: the documented default one */
        size_t cn;
        const unsigned char *c = embedded_renderer_cfg(&cn);
        write_file(g_rendererCfg, c, cn);
        { KVMap m; renderer_defaults(&m); renderer_write(&m); }   /* with the defaults of the launcher: the desktop resolution... */
    }
    path_join(dir, 560, g_root, L"renderers\\Direct3D11");
    path_join(p, 560, dir, L"renderer_d3d11.znc");
    make_dirs(dir);
    install_blob(p, d, n);
}

int install_input_plugin(wchar_t *outPath, size_t cap) {
    size_t n;
    const unsigned char *d = embedded_input(&n);
    path_join(outPath, cap, g_root, L"zinc-input.znc");
    return install_blob(outPath, d, n);
}

const wchar_t *renderer_file(const wchar_t *name, wchar_t *out, size_t cap) {
    wchar_t pat[600];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, 600, L"%ls\\renderers\\%ls\\*.znc", g_root, name);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    FindClose(h);
    _snwprintf(out, cap, L"renderers/%ls/%ls", name, fd.cFileName);
    return out;
}

int renderer_names(wchar_t names[][64], int max) {
    static const wchar_t *order[] = {L"Direct3D11", L"OpenGL", L"Direct3D"};
    wchar_t tmp[600], pat[600];
    int n = 0, i;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    for (i = 0; i < 3 && n < max; i++)
        if (renderer_file(order[i], tmp, 600)) { wcsncpy(names[n], order[i], 63); names[n++][63] = 0; }
    _snwprintf(pat, 600, L"%ls\\renderers\\*", g_root);
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            int dup = 0, j;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            for (j = 0; j < n; j++) if (!wcscmp(names[j], fd.cFileName)) dup = 1;
            if (!dup && n < max && renderer_file(fd.cFileName, tmp, 600)) { wcsncpy(names[n], fd.cFileName, 63); names[n++][63] = 0; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (n == 0) { wcscpy(names[0], L"OpenGL"); wcscpy(names[1], L"Direct3D"); n = 2; }
    return n;
}

/* ---------------------------------------------------------------- starting ZiNc */
void args_free(ArgList *l) { int i; for (i = 0; i < l->n; i++) free(l->a[i]); free(l->a); l->a = NULL; l->n = 0; }
static void arg_add(ArgList *l, const wchar_t *s) { l->a = (wchar_t **)realloc(l->a, (l->n + 1) * sizeof(wchar_t *)); l->a[l->n++] = wdup(s); }
static void arg_addf(ArgList *l, const wchar_t *fmt, ...) {
    wchar_t tmp[1200];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(tmp, 1200, fmt, ap);
    va_end(ap);
    tmp[1199] = 0;
    arg_add(l, tmp);
}

void zinc_args(int id, ArgList *a, wchar_t *warn, size_t warnCap) { zinc_args_ex(id, NULL, a, warn, warnCap); }

/* the folder for the generated per-game config files */
static void game_dir(wchar_t *out, size_t cap) {
    path_join(out, cap, g_root, L"game-settings");
    CreateDirectoryW(out, NULL);
}

/* the command line of a game: the main settings, changed by the settings of the game itself (zinc-games.cfg) and by the one-off
   options of this start (PlayOpts). Changed renderer.cfg values go into a copy of the file for this game. */
/* the bezel of a game: bezels\<set>.png, else bezels\<parent set>.png (next to ZiNc.exe); "" when there is none */
static int game_bezel(int id, wchar_t *out, size_t cap) {
    int i;
    wchar_t dir[560];
    out[0] = 0;
    path_join(dir, 560, g_root, L"bezels");
    for (i = 0; i < g_ngames; i++) {
        const Game *g = &g_games[i];
        const wchar_t *names[2];
        int k;
        if (g->id != id) continue;
        names[0] = g->set; names[1] = g->parent;
        for (k = 0; k < 2; k++) {
            wchar_t f[600];
            if (!names[k] || !names[k][0]) continue;
            _snwprintf(f, 600, L"%ls\\%ls.png", dir, names[k]); f[599] = 0;
            if (file_exists(f)) { wcsncpy(out, f, cap - 1); out[cap - 1] = 0; return 1; }
        }
        break;
    }
    return 0;
}

void zinc_args_ex(int id, const PlayOpts *po, ArgList *a, wchar_t *warn, size_t warnCap) {
    wchar_t tmp[600], rf[300], renderer[64], gd[560];
    const GameCfg *gc = gamecfg_find(id);
    int rotate = g_set.rotate, useSound = g_set.useSound, filter = g_set.soundFilter, surround = g_set.surround, exciter = g_set.exciter, slow = g_set.slowGeometry;
    KVMap ov;
    wchar_t gameBezel[600];
    int hasBezel = 0;
    memset(a, 0, sizeof *a);
    memset(&ov, 0, sizeof ov);
    if (warn) warn[0] = 0;
    wcscpy(renderer, g_set.renderer);
    if (gc) {
        if (gc->renderer[0]) wcscpy(renderer, gc->renderer);
        if (gc->rotate != -2) rotate = gc->rotate;
        if (gc->sound >= 0) useSound = gc->sound;
        if (gc->soundFilter >= 0) filter = gc->soundFilter;
        if (gc->surround >= 0) surround = gc->surround;
        if (gc->exciter >= 0) exciter = gc->exciter;
        if (gc->slowGeometry >= 0) slow = gc->slowGeometry;
        if (gc->fullscreen >= 0) kv_set(&ov, "FullScreen", gc->fullscreen);
        if (gc->xsize > 0) { kv_set(&ov, "XSize", gc->xsize); kv_set(&ov, "YSize", gc->ysize); }
    }
    if (po) {
        if (po->renderer[0]) wcscpy(renderer, po->renderer);
        if (po->fullscreen >= 0) kv_set(&ov, "FullScreen", po->fullscreen);
    }
    if (gc && !wcscmp(renderer, L"Direct3D11")) {   /* the other renderers do not know these keys */
        if (gc->scale >= 0) kv_set(&ov, "InternalScale", gc->scale);
        if (gc->xbrz >= 0) kv_set(&ov, "XBRZ", gc->xbrz);
        if (gc->borderless >= 0) kv_set(&ov, "Borderless", gc->borderless);
        if (gc->aspect >= 0) kv_set(&ov, "KeepAspect", gc->aspect);
        if (gc->fxaa >= 0) kv_set(&ov, "FXAA", gc->fxaa);
        if (gc->texsmooth >= 0) kv_set(&ov, "TextureSmoothing", gc->texsmooth);
        if (gc->dedither >= 0) kv_set(&ov, "Dedither", gc->dedither);
        if (gc->vsync >= 0) kv_set(&ov, "VSync", gc->vsync);
        if (gc->overscan >= 0) kv_set(&ov, "Overscan", gc->overscan);
    }
    if (!wcscmp(renderer, L"Direct3D11")) {   /* the bezel: the one of the game settings, else bezels\<set>.png, else the one of the settings */
        if (gc && !wcscmp(gc->bezel, L"-")) { hasBezel = 1; gameBezel[0] = 0; }
        else if (gc && gc->bezel[0]) {
            if (is_abs_path(gc->bezel)) { wcsncpy(gameBezel, gc->bezel, 599); gameBezel[599] = 0; }
            else { wchar_t dir[560]; path_join(dir, 560, g_root, L"bezels"); path_join(gameBezel, 600, dir, gc->bezel); }
            hasBezel = file_exists(gameBezel);
        }
        if (!hasBezel) hasBezel = game_bezel(id, gameBezel, 600);
    }
    arg_addf(a, L"%d", id);
    arg_addf(a, L"--roms-directory=%ls", g_set.roms);
    arg_addf(a, L"--use-sound=%ls", useSound ? L"yes" : L"no");
    /* config files by absolute path, so they are found whatever the working directory is (desktop shortcuts) */
    if (file_exists(g_rendererCfg)) {
        if (ov.n > 0 || hasBezel) {   /* a copy of renderer.cfg with this game's values */
            game_dir(gd, 560);
            _snwprintf(tmp, 600, L"%ls\\%d-renderer%ls.cfg", gd, id, po && po->tag ? po->tag : L"-once");
            tmp[599] = 0;
            g_bezelOverride = hasBezel ? gameBezel : NULL;
            { int okc = renderer_write_copy(tmp, &ov); g_bezelOverride = NULL;
              if (okc) arg_addf(a, L"--use-renderer-cfg-file=%ls", tmp);
              else arg_addf(a, L"--use-renderer-cfg-file=%ls", g_rendererCfg); }
        } else arg_addf(a, L"--use-renderer-cfg-file=%ls", g_rendererCfg);
    }
    if (renderer[0] && renderer_file(renderer, rf, 300)) arg_addf(a, L"--renderer=%ls", rf);
    if (rotate >= 0) arg_addf(a, L"--rotate=%d", rotate);
    if (filter) { arg_add(a, L"--sound-filter-enable=yes"); arg_addf(a, L"--sound-filter-cutoff=%d", g_set.cutoff); }
    if (surround) { arg_add(a, L"--sound-surround-lite-enable=yes"); arg_addf(a, L"--sound-surround-lite-multiplier=%d", g_set.surroundMul); }
    if (exciter) arg_add(a, L"--sound-stereo-exciter=yes");
    if (slow) arg_add(a, L"--use-slow-geometry=yes");
    if (g_set.enhanced) {
        wchar_t p[560];
        if (install_input_plugin(p, 560)) {
            if (warn) _snwprintf(warn, warnCap, L"Could not write zinc-input.znc.\nFalling back to the original keyboard plugin.");
        } else {
            arg_addf(a, L"--controller=%ls", p);
            path_join(tmp, 600, g_root, L"zinc-input.cfg");
            if (gc && gc->profile[0]) {   /* the game's controls profile: the current cfg with the controls of the profile */
                char *prof = profile_load(gc->profile);
                if (prof) {
                    Buf b = {0};
                    input_cfg_text_profile(&b, prof);
                    game_dir(gd, 560);
                    _snwprintf(tmp, 600, L"%ls\\%d-input.cfg", gd, id);
                    tmp[599] = 0;
                    write_file(tmp, b.s, b.n);
                    buf_free(&b);
                    free(prof);
                } else if (warn && !warn[0]) _snwprintf(warn, warnCap, L"The controls profile \"%ls\" of this game was not found: the current controls are used.", gc->profile);
            }
            if (file_exists(tmp)) arg_addf(a, L"--use-controller-cfg-file=%ls", tmp);
        }
    }
}

/* a command line the way the C runtime of ZiNc parses it: arguments with blanks are quoted as a whole */
static wchar_t *command_line(const wchar_t *exe, const ArgList *a) {
    size_t cap = wcslen(exe) + 8, i;
    wchar_t *cl;
    for (i = 0; (int)i < a->n; i++) cap += wcslen(a->a[i]) + 4;
    cl = (wchar_t *)malloc(cap * sizeof(wchar_t));
    _snwprintf(cl, cap, L"\"%ls\"", exe);
    for (i = 0; (int)i < a->n; i++) {
        int blank = wcschr(a->a[i], L' ') || wcschr(a->a[i], L'\t');
        wcscat(cl, L" ");
        if (blank) wcscat(cl, L"\"");
        wcscat(cl, a->a[i]);
        if (blank) wcscat(cl, L"\"");
    }
    return cl;
}

BOOL zinc_start(ArgList *a, PROCESS_INFORMATION *pi) {
    STARTUPINFOW si;
    wchar_t *cl = command_line(g_exePath, a);
    BOOL ok;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    ok = CreateProcessW(g_exePath, cl, NULL, NULL, FALSE, g_set.hideConsole ? CREATE_NO_WINDOW : 0, NULL, g_root, &si, pi);
    free(cl);
    return ok;
}

typedef struct { DWORD pid; HWND found; } FindCtx;
static BOOL CALLBACK find_proc(HWND h, LPARAM lp) {
    FindCtx *c = (FindCtx *)lp;
    DWORD p = 0;
    GetWindowThreadProcessId(h, &p);
    if (p == c->pid && IsWindowVisible(h)) { c->found = h; return FALSE; }
    return TRUE;
}

/* gives the keyboard focus to the first visible window of a process (a shortcut has no window of its own that could
   hand the focus over, so ZiNc may otherwise start in the background without receiving input) */
static DWORD WINAPI focus_thread(LPVOID pidp) {
    FindCtx c;
    int tries;
    c.pid = (DWORD)(size_t)pidp;
    for (tries = 0; tries < 60; tries++) {
        Sleep(250);
        c.found = NULL;
        EnumWindows(find_proc, (LPARAM)&c);
        if (c.found) {
            keybd_event(VK_MENU, 0, 0, 0);   /* a tap of Alt lifts the foreground lock */
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            SetForegroundWindow(c.found);
            return 0;
        }
    }
    return 0;
}

/* "ZiNc-EX.exe --game <set name or ROM file> [--fullscreen | --windowed]": for frontends (LaunchBox, Pegasus, RetroBat / EmulationStation, Playnite, Steam...).
 * Starts the game with the global and the game's own settings, without the launcher window, waits until the game is closed and then returns.
 * Exit codes: 0 played, 2 unknown game, 3 ROM files missing, 4 ZiNc.exe could not be started, 5 no game given. Problems are written to ZiNc-EX_launch.log. */
static void cli_log(const wchar_t *what, const wchar_t *arg) {
    wchar_t logp[560];
    Buf log = {0};
    char *w8 = w_to_u8(what), *a8 = w_to_u8(arg ? arg : L"");
    buf_fmt(&log, "%s: %s\r\n", w8, a8);
    path_join(logp, 560, g_root, L"ZiNc-EX_launch.log");
    write_file(logp, log.s, log.n);
    buf_free(&log);
    free(w8); free(a8);
}

static const Game *cli_find_game(const wchar_t *arg) {
    wchar_t name[300], *p, *dot;
    int i, pass;
    wcsncpy(name, arg, 299);
    name[299] = 0;
    trim_w(name);
    if (name[0] == L'"') { memmove(name, name + 1, wcslen(name) * sizeof(wchar_t)); }
    { size_t l = wcslen(name); if (l && name[l - 1] == L'"') name[l - 1] = 0; }
    p = wcsrchr(name, L'\\');
    if (wcsrchr(name, L'/') > p) p = wcsrchr(name, L'/');
    if (p) memmove(name, p + 1, (wcslen(p + 1) + 1) * sizeof(wchar_t));
    for (pass = 0; pass < 2; pass++) {   /* first the name as it is, then without the extension (.zip, .7z...) */
        if (pass == 1) { dot = wcsrchr(name, L'.'); if (!dot) break; *dot = 0; }
        for (i = 0; i < g_ngames; i++) if (g_games[i].set && !_wcsicmp(g_games[i].set, name)) return &g_games[i];
    }
    return NULL;
}

int run_game_cli(const wchar_t *game, int fullscreen) {   /* fullscreen: -1 as set, 0 window, 1 fullscreen */
    ArgList a;
    wchar_t warn[300];
    PROCESS_INFORMATION pi;
    PlayOpts po;
    const Game *g;
    if (!game || !game[0]) { cli_log(L"no game given", L""); return 5; }
    if (!file_exists(g_exePath)) { cli_log(L"ZiNc.exe not found beside ZiNc-EX.exe", g_exePath); return 4; }
    if (games_fetch(&g_games, &g_ngames) != 0 || g_ngames <= 0) { cli_log(L"could not read the game list of ZiNc.exe", g_exePath); return 4; }
    g = cli_find_game(game);
    if (!g) { cli_log(L"unknown game (use the ZiNc set name, e.g. sfex, or the ROM zip)", game); return 2; }
    rom_available_init(g_set.roms);
    if (!rom_available(g)) {
        wchar_t miss[300];
        rom_missing(g, miss, 300);
        cli_log(L"ROM files missing", miss);
        return 3;
    }
    memset(&po, 0, sizeof po);
    po.fullscreen = fullscreen;
    po.tag = L"-cli";
    memset(&a, 0, sizeof a);
    zinc_args_ex(g->id, &po, &a, warn, 300);
    AllowSetForegroundWindow((DWORD)-1);
    if (!zinc_start(&a, &pi)) { cli_log(L"ZiNc.exe could not be started", g_exePath); args_free(&a); return 4; }
    CreateThread(NULL, 0, focus_thread, (LPVOID)(size_t)pi.dwProcessId, 0, NULL);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    args_free(&a);
    return 0;
}

/* used by desktop icons of older builds: "ZiNc-EX.exe --play <id> [--fullscreen]" starts the game and exits with it */
void run_headless(int id, int fullscreen) {
    ArgList a;
    wchar_t warn[300], logp[560];
    PROCESS_INFORMATION pi;
    int restore = 0;
    KVMap m;
    Buf log = {0};
    if (fullscreen) {
        renderer_read(&m);
        if (kv_get(&m, "FullScreen") == 0) {
            KVMap w = {0};
            kv_set(&w, "FullScreen", 1);
            renderer_write(&w);
            restore = 1;
        }
    }
    zinc_args(id, &a, warn, 300);
    {
        char *w8 = w_to_u8(warn), *e8 = w_to_u8(g_exePath);
        int i;
        buf_fmt(&log, "ZiNc EX v" APP_VERSION "\r\n%s\r\n%s", w8, e8);
        for (i = 0; i < a.n; i++) { char *x = w_to_u8(a.a[i]); buf_fmt(&log, " %s", x); free(x); }
        buf_add(&log, "\r\n");
        free(w8); free(e8);
    }
    path_join(logp, 560, g_root, L"ZiNc-EX_launch.log");
    if (g_set.logs) write_file(logp, log.s, log.n);
    buf_free(&log);
    AllowSetForegroundWindow((DWORD)-1);
    if (zinc_start(&a, &pi)) {
        CreateThread(NULL, 0, focus_thread, (LPVOID)(size_t)pi.dwProcessId, 0, NULL);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    args_free(&a);
    if (restore) { KVMap w = {0}; kv_set(&w, "FullScreen", 0); renderer_write(&w); }
}
