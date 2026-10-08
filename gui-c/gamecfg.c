/* Per-game settings: what a game does differently from the main settings (renderer, rotation, window / resolution,
   sound, controls profile, trainer). Everything left on "Same As Global" follows the main settings.
   Stored in zinc-games.cfg; a game that has settings is started with its own copies of renderer.cfg / zinc-input.cfg. */
#include "common.h"

static GameCfg *g_gc;
static int g_ngc;

static void gc_init(GameCfg *c, int id) {
    memset(c, 0, sizeof *c);
    c->id = id;
    c->rotate = -2;
    c->fullscreen = c->sound = c->soundFilter = c->surround = c->exciter = c->slowGeometry = c->trainer = c->scale = c->xbrz = c->borderless = c->aspect = c->fxaa = c->texsmooth = c->dedither = c->vsync = c->overscan = -1;
}

int gamecfg_active(const GameCfg *c) {
    return c && (c->renderer[0] || c->rotate != -2 || c->fullscreen >= 0 || c->xsize > 0 || c->scale >= 0 || c->xbrz >= 0 || c->borderless >= 0 || c->aspect >= 0 || c->fxaa >= 0 || c->texsmooth >= 0 || c->dedither >= 0 || c->vsync >= 0 || c->overscan >= 0 || c->sound >= 0 ||
                 c->soundFilter >= 0 || c->surround >= 0 || c->exciter >= 0 || c->slowGeometry >= 0 || c->profile[0] || c->bezel[0] || c->trainer >= 0);
}

const GameCfg *gamecfg_find(int id) {
    int i;
    for (i = 0; i < g_ngc; i++) if (g_gc[i].id == id) return gamecfg_active(&g_gc[i]) ? &g_gc[i] : NULL;
    return NULL;
}

static GameCfg *gc_slot(int id) {
    int i;
    for (i = 0; i < g_ngc; i++) if (g_gc[i].id == id) return &g_gc[i];
    g_gc = (GameCfg *)realloc(g_gc, (size_t)(g_ngc + 1) * sizeof(GameCfg));
    gc_init(&g_gc[g_ngc], id);
    return &g_gc[g_ngc++];
}

void gamecfg_set(const GameCfg *c) { *gc_slot(c->id) = *c; }

int gamecfg_rename_profile(const wchar_t *from, const wchar_t *to) {
    int i, n = 0;
    for (i = 0; i < g_ngc; i++) if (g_gc[i].profile[0] && !_wcsicmp(g_gc[i].profile, from)) { wcsncpy(g_gc[i].profile, to, 63); g_gc[i].profile[63] = 0; n++; }
    if (n) gamecfg_save();
    return n;
}

void gamecfg_remove(int id) {
    int i;
    for (i = 0; i < g_ngc; i++) if (g_gc[i].id == id) { memmove(&g_gc[i], &g_gc[i + 1], (size_t)(g_ngc - i - 1) * sizeof(GameCfg)); g_ngc--; return; }
}

static void cfg_path(wchar_t *out, size_t cap) { path_join(out, cap, g_root, L"zinc-games.cfg"); }

void gamecfg_load(void) {
    wchar_t p[560];
    char *txt, *line, *save = NULL;
    GameCfg *cur = NULL;
    free(g_gc); g_gc = NULL; g_ngc = 0;
    cfg_path(p, 560);
    txt = read_file(p, NULL);
    if (!txt) return;
    for (line = strtok_s(txt, "\n", &save); line; line = strtok_s(NULL, "\n", &save)) {
        char *eq, *key, *val;
        line = trim_a(line);
        if (!*line || *line == ';' || *line == '#') continue;
        if (*line == '[') {
            int id;
            char *close = strchr(line, ']');
            cur = NULL;
            if (close) *close = 0;
            if (!strncmp(line, "[game ", 6) && xatoi(trim_a(line + 6), &id)) cur = gc_slot(id);
            continue;
        }
        if (!cur) continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        key = trim_a(line);
        val = trim_a(eq + 1);
        _strlwr(key);
        if (!strcmp(key, "renderer")) { wchar_t *w = u8_to_w(val, -1); wcsncpy(cur->renderer, w, 63); free(w); canonical_renderer(cur->renderer); }
        else if (!strcmp(key, "profile")) { wchar_t *w = u8_to_w(val, -1); wcsncpy(cur->profile, w, 63); free(w); }
        else if (!strcmp(key, "bezel")) { wchar_t *w = u8_to_w(val, -1); wcsncpy(cur->bezel, w, 259); free(w); }
        else if (!strcmp(key, "rotate")) { int x; if (xatoi(val, &x) && x >= -1 && x <= 3) cur->rotate = x; }
        else if (!strcmp(key, "fullscreen")) { int x; if (xatoi(val, &x)) cur->fullscreen = x ? 1 : 0; }
        else if (!strcmp(key, "size")) { int w, h; if (sscanf(val, "%dx%d", &w, &h) == 2 && w >= 160 && h >= 120 && w <= 7680 && h <= 4320) { cur->xsize = w; cur->ysize = h; } }
        else if (!strcmp(key, "scale")) { int x; if (xatoi(val, &x) && x >= 0 && x <= 4) cur->scale = x; }
        else if (!strcmp(key, "xbrz")) { int x; if (xatoi(val, &x) && x >= 0 && x <= 2) cur->xbrz = x; }
        else if (!strcmp(key, "borderless")) { int x; if (xatoi(val, &x)) cur->borderless = x ? 1 : 0; }
        else if (!strcmp(key, "aspect")) { int x; if (xatoi(val, &x) && x >= 0 && x <= 3) cur->aspect = x; }
        else if (!strcmp(key, "texture_smoothing")) { int x; if (xatoi(val, &x)) cur->texsmooth = x ? 1 : 0; }
        else if (!strcmp(key, "fxaa")) { int x; if (xatoi(val, &x)) cur->fxaa = x ? 1 : 0; }
        else if (!strcmp(key, "dither_smoothing")) { int x; if (xatoi(val, &x) && x >= 0 && x <= 3) cur->dedither = x; }
        else if (!strcmp(key, "vsync")) { int x; if (xatoi(val, &x)) cur->vsync = x ? 1 : 0; }
        else if (!strcmp(key, "overscan")) { int x; if (xatoi(val, &x) && x >= 0 && x <= 64) cur->overscan = x; }
        else if (!strcmp(key, "sound")) { int x; if (xatoi(val, &x)) cur->sound = x ? 1 : 0; }
        else if (!strcmp(key, "sound_filter")) { int x; if (xatoi(val, &x)) cur->soundFilter = x ? 1 : 0; }
        else if (!strcmp(key, "surround")) { int x; if (xatoi(val, &x)) cur->surround = x ? 1 : 0; }
        else if (!strcmp(key, "exciter")) { int x; if (xatoi(val, &x)) cur->exciter = x ? 1 : 0; }
        else if (!strcmp(key, "slow_geometry")) { int x; if (xatoi(val, &x)) cur->slowGeometry = x ? 1 : 0; }
        else if (!strcmp(key, "trainer")) { int x; if (xatoi(val, &x)) cur->trainer = x ? 1 : 0; }
    }
    free(txt);
}

int gamecfg_save(void) {
    wchar_t p[560];
    Buf b = {0};
    int i, n = 0, ok = 1;
    cfg_path(p, 560);
    buf_add(&b, "; ZiNc EX per-game settings (written by ZiNc EX; a missing line means: same as the main settings)\r\n");
    for (i = 0; i < g_ngc; i++) {
        const GameCfg *c = &g_gc[i];
        char *u;
        if (!gamecfg_active(c)) continue;
        n++;
        buf_fmt(&b, "\r\n[game %d]\r\n", c->id);
        if (c->renderer[0]) { u = w_to_u8(c->renderer); buf_fmt(&b, "renderer=%s\r\n", u); free(u); }
        if (c->rotate != -2) buf_fmt(&b, "rotate=%d\r\n", c->rotate);
        if (c->fullscreen >= 0) buf_fmt(&b, "fullscreen=%d\r\n", c->fullscreen);
        if (c->xsize > 0) buf_fmt(&b, "size=%dx%d\r\n", c->xsize, c->ysize);
        if (c->scale >= 0) buf_fmt(&b, "scale=%d\r\n", c->scale);
        if (c->xbrz >= 0) buf_fmt(&b, "xbrz=%d\r\n", c->xbrz);
        if (c->borderless >= 0) buf_fmt(&b, "borderless=%d\r\n", c->borderless);
        if (c->aspect >= 0) buf_fmt(&b, "aspect=%d\r\n", c->aspect);
        if (c->fxaa >= 0) buf_fmt(&b, "fxaa=%d\r\n", c->fxaa);
        if (c->texsmooth >= 0) buf_fmt(&b, "texture_smoothing=%d\r\n", c->texsmooth);
        if (c->dedither >= 0) buf_fmt(&b, "dither_smoothing=%d\r\n", c->dedither);
        if (c->vsync >= 0) buf_fmt(&b, "vsync=%d\r\n", c->vsync);
        if (c->overscan >= 0) buf_fmt(&b, "overscan=%d\r\n", c->overscan);
        if (c->sound >= 0) buf_fmt(&b, "sound=%d\r\n", c->sound);
        if (c->soundFilter >= 0) buf_fmt(&b, "sound_filter=%d\r\n", c->soundFilter);
        if (c->surround >= 0) buf_fmt(&b, "surround=%d\r\n", c->surround);
        if (c->exciter >= 0) buf_fmt(&b, "exciter=%d\r\n", c->exciter);
        if (c->slowGeometry >= 0) buf_fmt(&b, "slow_geometry=%d\r\n", c->slowGeometry);
        if (c->profile[0]) { u = w_to_u8(c->profile); buf_fmt(&b, "profile=%s\r\n", u); free(u); }
        if (c->bezel[0]) { u = w_to_u8(c->bezel); buf_fmt(&b, "bezel=%s\r\n", u); free(u); }
        if (c->trainer >= 0) buf_fmt(&b, "trainer=%d\r\n", c->trainer);
    }
    if (n == 0) { DeleteFileW(p); buf_free(&b); return 1; }
    ok = write_file(p, b.s, b.n);
    buf_free(&b);
    return ok;
}

/* ---------------------------------------------------------------- the dialog */
typedef struct {
    HWND cb[24], lbl[24];
    int tab[24];   /* the tab of every row */
    HWND tabs;
    GameCfg cfg;
    wchar_t rend[16][64];
    int nrend, nmodes;
    wchar_t pn[64][64];
    int np;
    wchar_t bz[64][260];   /* the PNG files of the bezels folder */
    int nbz;
    int reset;
} Dlg;

/* the rows, by tab: General, Video, Audio, Other */
enum { R_REND, R_ROT, R_WIN, R_RES, R_BEZEL,
       R_FSMODE, R_ASPECT, R_SCALE, R_XBRZ, R_TEXSM, R_FXAA, R_DEDITH, R_VSYNC, R_OVERSCAN,
       R_SOUND, R_FILT, R_SURR, R_EXC,
       R_SLOW, R_PROF, R_TRAIN, NROWS };
#define TOPY 84      /* the first row, below the tabs */
#define MAXROWS 10   /* the most rows of a tab */
static const int kOverscan[7] = {0, 4, 8, 12, 16, 24, 32};
#define ID_G_RESET 1001

static int tri(int v) { return v < 0 ? 0 : v ? 1 : 2; }          /* -1 / 1 / 0 -> combo index */
static int untri(int i) { return i == 0 ? -1 : i == 1 ? 1 : 0; }

static int dlg_ok(Modal *md) {
    Dlg *d = (Dlg *)md->user;
    GameCfg *c = &d->cfg;
    int i;
    if (d->reset) { gc_init(c, c->id); return 1; }
    i = (int)SendMessageW(d->cb[R_REND], CB_GETCURSEL, 0, 0);
    c->renderer[0] = 0;
    if (i > 0 && i <= d->nrend) wcscpy(c->renderer, d->rend[i - 1]);
    i = (int)SendMessageW(d->cb[R_ROT], CB_GETCURSEL, 0, 0);
    c->rotate = i <= 0 ? -2 : i - 2;                      /* 1 = game default (-1), 2.. = 0, 1, 2, 3 */
    i = (int)SendMessageW(d->cb[R_WIN], CB_GETCURSEL, 0, 0);
    c->fullscreen = i <= 0 ? -1 : i == 2 ? 1 : 0;
    i = (int)SendMessageW(d->cb[R_RES], CB_GETCURSEL, 0, 0);
    c->xsize = c->ysize = 0;
    if (i > 0 && i <= d->nmodes) ui_mode_get(i - 1, &c->xsize, &c->ysize);
    i = (int)SendMessageW(d->cb[R_SCALE], CB_GETCURSEL, 0, 0);
    c->scale = i <= 0 ? -1 : i - 1;
    i = (int)SendMessageW(d->cb[R_XBRZ], CB_GETCURSEL, 0, 0);
    c->xbrz = i <= 0 ? -1 : i - 1;
    i = (int)SendMessageW(d->cb[R_BEZEL], CB_GETCURSEL, 0, 0);
    c->bezel[0] = 0;
    if (i == 1) wcscpy(c->bezel, L"-");
    else if (i >= 2 && i - 2 < d->nbz) wcscpy(c->bezel, d->bz[i - 2]);
    i = (int)SendMessageW(d->cb[R_FSMODE], CB_GETCURSEL, 0, 0); c->borderless = i <= 0 ? -1 : i - 1;
    i = (int)SendMessageW(d->cb[R_ASPECT], CB_GETCURSEL, 0, 0); c->aspect = i <= 0 ? -1 : i - 1;
    c->texsmooth = untri((int)SendMessageW(d->cb[R_TEXSM], CB_GETCURSEL, 0, 0));
    c->fxaa = untri((int)SendMessageW(d->cb[R_FXAA], CB_GETCURSEL, 0, 0));
    i = (int)SendMessageW(d->cb[R_DEDITH], CB_GETCURSEL, 0, 0); c->dedither = i <= 0 ? -1 : i - 1;
    c->vsync = untri((int)SendMessageW(d->cb[R_VSYNC], CB_GETCURSEL, 0, 0));
    i = (int)SendMessageW(d->cb[R_OVERSCAN], CB_GETCURSEL, 0, 0); c->overscan = i <= 0 || i > 7 ? -1 : kOverscan[i - 1];
    c->sound = untri((int)SendMessageW(d->cb[R_SOUND], CB_GETCURSEL, 0, 0));
    c->soundFilter = untri((int)SendMessageW(d->cb[R_FILT], CB_GETCURSEL, 0, 0));
    c->surround = untri((int)SendMessageW(d->cb[R_SURR], CB_GETCURSEL, 0, 0));
    c->exciter = untri((int)SendMessageW(d->cb[R_EXC], CB_GETCURSEL, 0, 0));
    c->slowGeometry = untri((int)SendMessageW(d->cb[R_SLOW], CB_GETCURSEL, 0, 0));
    i = (int)SendMessageW(d->cb[R_PROF], CB_GETCURSEL, 0, 0);
    c->profile[0] = 0;
    if (i > 0 && i <= d->np) wcscpy(c->profile, d->pn[i - 1]);
    c->trainer = untri((int)SendMessageW(d->cb[R_TRAIN], CB_GETCURSEL, 0, 0));
    return 1;
}

static int dlg_cmd(Modal *md, int id) {
    Dlg *d = (Dlg *)md->user;
    if (id == ID_G_RESET) { d->reset = 1; SendMessageW(md->dlg, WM_COMMAND, IDOK, 0); return 1; }
    return 0;
}

static LRESULT dlg_notify(Modal *md, NMHDR *nh) {   /* another tab: its rows are shown, the rows of the others are hidden */
    Dlg *d = (Dlg *)md->user;
    if (nh->hwndFrom == d->tabs && nh->code == TCN_SELCHANGE) {
        int s = TabCtrl_GetCurSel(d->tabs), i;
        for (i = 0; i < NROWS; i++) { ShowWindow(d->cb[i], d->tab[i] == s ? SW_SHOW : SW_HIDE); ShowWindow(d->lbl[i], d->tab[i] == s ? SW_SHOW : SW_HIDE); }
    }
    return 0;
}

static HWND add_row(Modal *md, Dlg *d, int row, int tab, int pos, const wchar_t *label, const wchar_t **items, int n, int sel) {
    int y = TOPY + pos * 28, i;
    HWND cb;
    d->lbl[row] = mkat(md->dlg, L"STATIC", label, SS_LEFT, 12, y + 4, 150, 18, 0);
    d->tab[row] = tab;
    cb = mkat(md->dlg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 170, y, 290, 240, 0);
    for (i = 0; i < n; i++) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(cb, CB_SETCURSEL, sel < 0 || sel >= n ? 0 : sel, 0);
    d->cb[row] = cb;
    return cb;
}

/* Game > Game Settings: returns 1 when the settings of the game changed */
int dlg_game_settings(HWND owner, const Game *g) {
    static const wchar_t *same = L"Same As Global";
    static const wchar_t *triItems[] = {L"Same As Global", L"On", L"Off"};
    static const wchar_t *yesNo[] = {L"Same As Global", L"Yes", L"No"};
    static const wchar_t *rotItems[] = {L"Same As Global", L"Game Default", L"None", L"90°", L"180°", L"270°"};
    static const wchar_t *winItems[] = {L"Same As Global", L"Window", L"Fullscreen"};
    static const wchar_t *scaleItems[] = {L"Same As Global", L"Auto", L"1x (Console Resolution)", L"2x", L"3x", L"4x"};
    static const wchar_t *xbrzItems[] = {L"Same As Global", L"Off", L"All Graphics", L"2D Objects Only"};
    Modal md;
    Dlg d;
    const GameCfg *old = gamecfg_find(g->id);
    wchar_t head[600], **items;
    int i, n, changed;
    memset(&md, 0, sizeof md);
    memset(&d, 0, sizeof d);
    gc_init(&d.cfg, g->id);
    if (old) d.cfg = *old;
    md.kind = 6; md.owner = owner; md.onok = dlg_ok; md.oncmd = dlg_cmd; md.user = &d;
    wcscpy(md.title, L"Game Settings");
    make_modal(&md, 480, TOPY + MAXROWS * 28 + 56);
    swprintf(head, 600, L"%ls\n\"Same As Global\" follows the main settings.", g->title);
    mkat(md.dlg, L"STATIC", head, SS_LEFT | SS_NOPREFIX, 12, 8, 456, 34, 0);
    md.onnotify = dlg_notify;
    {   /* the tabs: the rows of a tab are shown when it is chosen */
        static const wchar_t *tn[] = {L"General", L"Video", L"Audio", L"Other"};
        TCITEMW ti;
        int k;
        d.tabs = mkat(md.dlg, WC_TABCONTROLW, L"", WS_TABSTOP, 12, 46, 456, 28, 0);
        th_tab(d.tabs);   /* dark in the dark theme */
        memset(&ti, 0, sizeof ti);
        for (k = 0; k < 4; k++) { ti.mask = TCIF_TEXT; ti.pszText = (LPWSTR)tn[k]; TabCtrl_InsertItem(d.tabs, k, &ti); }
    }
    d.nrend = renderer_names(d.rend, 16);
    items = (wchar_t **)calloc((size_t)(d.nrend + 1), sizeof(wchar_t *));
    items[0] = (wchar_t *)same;
    for (i = 0; i < d.nrend; i++) items[i + 1] = d.rend[i];
    for (n = 0, i = 0; i < d.nrend; i++) if (!_wcsicmp(d.rend[i], d.cfg.renderer)) n = i + 1;
    add_row(&md, &d, R_REND, 0, 0, L"Renderer", (const wchar_t **)items, d.nrend + 1, n);
    free(items);
    add_row(&md, &d, R_ROT, 0, 1, L"Rotation", rotItems, 6, d.cfg.rotate == -2 ? 0 : d.cfg.rotate + 2);
    add_row(&md, &d, R_WIN, 0, 2, L"Window", winItems, 3, d.cfg.fullscreen < 0 ? 0 : d.cfg.fullscreen ? 2 : 1);
    d.nmodes = ui_mode_count();
    items = (wchar_t **)calloc((size_t)(d.nmodes + 1), sizeof(wchar_t *));
    items[0] = (wchar_t *)same;
    for (n = 0, i = 0; i < d.nmodes; i++) {
        int w, h;
        wchar_t t[40];
        ui_mode_get(i, &w, &h);
        swprintf(t, 40, L"%d x %d%ls", w, h, w * 3 == h * 4 ? L" (4:3)" : L"");
        items[i + 1] = wdup(t);
        if (w == d.cfg.xsize && h == d.cfg.ysize) n = i + 1;
    }
    add_row(&md, &d, R_RES, 0, 3, L"Resolution", (const wchar_t **)items, d.nmodes + 1, n);
    for (i = 1; i <= d.nmodes; i++) free(items[i]);
    free(items);
    {   /* Video tab: the Direct3D 11 options */
        static const wchar_t *fsItems[] = {L"Same As Global", L"Fullscreen", L"Borderless"};
        static const wchar_t *aspItems[] = {L"Same As Global", L"Stretch To Window", L"4:3", L"16:9", L"Pixel Perfect"};
        static const wchar_t *ditItems[] = {L"Same As Global", L"Off", L"Low", L"Medium", L"High"};
        static const wchar_t *ovItems[] = {L"Same As Global", L"Off", L"4 Pixels", L"8 Pixels", L"12 Pixels", L"16 Pixels", L"24 Pixels", L"32 Pixels"};
        int ov = 0, k;
        for (k = 0; k < 7; k++) if (d.cfg.overscan == kOverscan[k]) ov = k + 1;
        add_row(&md, &d, R_FSMODE, 1, 0, L"Fullscreen Mode", fsItems, 3, d.cfg.borderless < 0 ? 0 : d.cfg.borderless + 1);
        add_row(&md, &d, R_ASPECT, 1, 1, L"Aspect Ratio", aspItems, 5, d.cfg.aspect < 0 ? 0 : d.cfg.aspect + 1);
        add_row(&md, &d, R_SCALE, 1, 2, L"Internal Resolution", scaleItems, 6, d.cfg.scale < 0 ? 0 : d.cfg.scale + 1);
        add_row(&md, &d, R_XBRZ, 1, 3, L"xBRZ Filter", xbrzItems, 4, d.cfg.xbrz < 0 ? 0 : d.cfg.xbrz + 1);
        add_row(&md, &d, R_TEXSM, 1, 4, L"3D Texture Smoothing", triItems, 3, tri(d.cfg.texsmooth));
        add_row(&md, &d, R_FXAA, 1, 5, L"FXAA Filter", triItems, 3, tri(d.cfg.fxaa));
        add_row(&md, &d, R_DEDITH, 1, 6, L"Dither Smoothing", ditItems, 5, d.cfg.dedither < 0 ? 0 : d.cfg.dedither + 1);
        add_row(&md, &d, R_VSYNC, 1, 7, L"V-Sync", triItems, 3, tri(d.cfg.vsync));
        add_row(&md, &d, R_OVERSCAN, 1, 8, L"Overscan Crop", ovItems, 8, ov);
        add_row(&md, &d, R_SLOW, 1, 9, L"Slow Geometry", triItems, 3, tri(d.cfg.slowGeometry));
    }
    {   /* the bezel: the settings' one (or bezels\<set>.png), none, or a PNG of the bezels folder */
        WIN32_FIND_DATAW fd;
        wchar_t pat[560], dir[560];
        HANDLE h;
        int sel = 0;
        path_join(dir, 560, g_root, L"bezels");
        path_join(pat, 560, dir, L"*.png");
        d.nbz = 0;
        h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && d.nbz < 64) { wcsncpy(d.bz[d.nbz], fd.cFileName, 259); d.bz[d.nbz][259] = 0; d.nbz++; } } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!wcscmp(d.cfg.bezel, L"-")) sel = 1;
        else if (d.cfg.bezel[0]) {
            for (i = 0; i < d.nbz; i++) if (!_wcsicmp(d.bz[i], d.cfg.bezel)) sel = i + 2;
            if (!sel && d.nbz < 64) { wcscpy(d.bz[d.nbz], d.cfg.bezel); sel = d.nbz++ + 2; }   /* a bezel that is not in the folder (any more) stays */
        }
        items = (wchar_t **)calloc((size_t)(d.nbz + 2), sizeof(wchar_t *));
        items[0] = (wchar_t *)L"Same As Global"; items[1] = (wchar_t *)L"None";
        for (i = 0; i < d.nbz; i++) items[i + 2] = d.bz[i];
        add_row(&md, &d, R_BEZEL, 0, 4, L"Bezel (D3D11)", (const wchar_t **)items, d.nbz + 2, sel);
        free(items);
    }
    add_row(&md, &d, R_SOUND, 2, 0, L"Sound", triItems, 3, tri(d.cfg.sound));
    add_row(&md, &d, R_FILT, 2, 1, L"Sound Filter", triItems, 3, tri(d.cfg.soundFilter));
    add_row(&md, &d, R_SURR, 2, 2, L"Lite Surround", triItems, 3, tri(d.cfg.surround));
    add_row(&md, &d, R_EXC, 2, 3, L"Stereo Exciter", triItems, 3, tri(d.cfg.exciter));
    d.np = profile_names(d.pn, 64);
    items = (wchar_t **)calloc((size_t)(d.np + 1), sizeof(wchar_t *));
    items[0] = L"Current Controls";
    for (n = 0, i = 0; i < d.np; i++) { items[i + 1] = d.pn[i]; if (!_wcsicmp(d.pn[i], d.cfg.profile)) n = i + 1; }
    add_row(&md, &d, R_PROF, 3, 0, L"Controls Profile", (const wchar_t **)items, d.np + 1, n);
    free(items);
    add_row(&md, &d, R_TRAIN, 3, 1, L"Start Trainer", yesNo, 3, tri(d.cfg.trainer));
    {   int k;   /* only the rows of the first tab are shown */
        for (k = 0; k < NROWS; k++) if (d.tab[k] != 0) { ShowWindow(d.cb[k], SW_HIDE); ShowWindow(d.lbl[k], SW_HIDE); }
    }
    mkat(md.dlg, L"BUTTON", L"Reset", BS_PUSHBUTTON | WS_TABSTOP, 12, TOPY + MAXROWS * 28 + 12, 90, 26, ID_G_RESET);
    mkat(md.dlg, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 290, TOPY + MAXROWS * 28 + 12, 80, 26, IDOK);
    mkat(md.dlg, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 380, TOPY + MAXROWS * 28 + 12, 80, 26, IDCANCEL);
    run_modal(&md);
    changed = md.changed;
    if (changed) {
        if (gamecfg_active(&d.cfg)) gamecfg_set(&d.cfg); else gamecfg_remove(g->id);
        gamecfg_save();
    }
    return changed;
}
