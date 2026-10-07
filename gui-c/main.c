/* ZiNc EX - native Windows launcher (plain C + Win32). Put ZiNc-EX.exe next to ZiNc.exe. */
#include "common.h"
#include <uxtheme.h>
#include <shlobj.h>
#include <commdlg.h>
#include "classify.h"

static const wchar_t *REPO = L"https://github.com/hazem-abdelghani/ZiNc-EX";

enum { PG_SYSTEM, PG_VIDEO, PG_AUDIO, PG_CONTROLS, PG_COMBOS, NPAGES };   /* the tabs of the right pane, in order */

typedef struct { HWND lbl, ctl, btn, clr; int kind; } PRow;   /* kind 0: label + combo, 1: label + edit (btn: Browse, clr: X, to the right of it), 2: wide check box, 3: wide button */

/* ---------------------------------------------------------------- state */
static struct {
    HWND status, tip, tab, page[NPAGES];
    HWND cbRegion, cbMaker, cbGenre;
    HWND lblSearch, search, clearBtn, sortBtn, lv, favOnly, iconBtn, stopBtn, playBtn, saveBtn, cancelBtn, defBtn;
    /* video */
    HWND cbRenderer, cbRes, cbRot, cbScale, cbXbrz, cbDepth, lblDepth, lblFPS, tbRefresh, tbFav, tbAvail, tbFs, tbSet, tbVideo, tbAudio, tbCtl, tbCmb, tbSep1, tbSep2, lblFilter, lblTexType, lblTexCache, lblBlend, cbScan, cbFilter, cbTexType, cbTexCache, cbBlend, eFPS;
    HWND chFull, chDither, chFPS, chLimit, chSkip, chAuto, lblScale, lblXbrz, cbFxaa, cbOnStart, lblFxaa, cbTexSm, lblTexSm, cbDedith, lblDedith, cbAspect, lblAspect, cbOverscan, lblOverscan, chClassic, cbTheme, eBezel, bBezBrowse, bBezClear, cbFsMode, lblFsMode, cbVsync, lblVsync;
    /* audio */
    HWND chSound, chFilt, eCutoff, chSurr, eSurround, chExc;
    /* system */
    HWND lblRoms, eRoms, bRomClear, bTrClear, bBrowse, chAvail, chSlow, chHide, chLogs, eTrainer, bTrBrowse, chTrainer;
    PRow vrows[40], arows[8], srows[16];
    int nv, na, ns;
} W;

typedef struct { int w, h; } Mode;
static Mode *g_modes;
static int g_nmodes;

static int g_applying, g_syncing, g_rebuilding;
static char *g_savedSig;
static PROCESS_INFORMATION g_proc;
static void sync_main_enable(void);
static void hide_opt(int activateMain);
static int g_running, g_stopped, g_minimized, g_leaveTrainer;   /* g_minimized: the launcher was minimized for the game; g_leaveTrainer: the launcher closes for the game, the trainer stays */
static DWORD g_startTick;
static int g_splitX, g_dragging;
static HWND g_opt;            /* the options window (the tabs in a window of their own) */
static HMENU g_optsMenu;      /* the Options menu of the menu bar */
static HMENU g_viewMenu, g_themeMenu, g_colsMenu;
static int g_searchOv = -1;   /* for this session only: 1 the search bar was shown by Ctrl+F, 0 it was closed with Close; -1 as in the View menu (remembered) */
#define SEARCH_SHOWN() (g_searchOv >= 0 ? g_searchOv : !g_set.hideSearch)
static int g_optWin;          /* 1: the tabs are in the options window, 0: in the main window (the classic layout) */
#define g_sortCol (g_set.sortCol)
#define NCOLS 7   /* the columns of the game list: Favorite, #, Game, Info, Status, Hardware, Year */

#define g_sortAsc (g_set.sortAsc)
static int *g_view, g_nview;
static HMENU g_recentMenu, g_playWithMenu;
static wchar_t g_note[200];
static wchar_t g_rendNames[16][64];
static int g_nrend;
static int g_columnsSized;
static wchar_t g_regions[8][8], g_makers[32][40], g_genres[16][24];   /* the makers and genres of the listed games, for the filters */
static int g_nreg, g_nmak, g_ngen;

/* ---------------------------------------------------------------- helpers */
static void set_status(const wchar_t *s) { SendMessageW(W.status, SB_SETTEXTW, 0, (LPARAM)s); }
static void get_status(wchar_t *s, int cap) {   /* at most cap - 1 characters, whatever the length of the text */
    int n = LOWORD(SendMessageW(W.status, SB_GETTEXTLENGTHW, 0, 0));
    wchar_t *tmp = (wchar_t *)calloc(n + 2, sizeof(wchar_t));
    SendMessageW(W.status, SB_GETTEXTW, 0, (LPARAM)tmp);
    wcsncpy(s, tmp, cap - 1);
    s[cap - 1] = 0;
    free(tmp);
}
static void set_note(const wchar_t *s) {
    wcsncpy(g_note, s, 199);
    set_status(s);
    SetTimer(g_main, 2, 5000, NULL);
}
static int playable(const Game *g);
static void selection_status(void);
static void start_trainer(const Game *g);
static void stop_trainer(void);
static void fill_filters(void);
/* "71 games supported, 12 with ROMs": how many of the games have their ROM sets in the ROMs folder */
static void games_status(void) {
    wchar_t t[120];
    int i, have = 0;
    for (i = 0; i < g_ngames; i++) if (rom_available(&g_games[i])) have++;
    swprintf(t, 120, L"%d games supported, %d with ROMs", g_ngames, have);
    set_status(t);
}

/* enabling / disabling a control inside a page: the page background must be painted again under the label text */
static void enable_ctl(HWND c, BOOL on) {
    RECT rc;
    wchar_t cls[16];
    if (GetClassNameW(c, cls, 16) && !_wcsicmp(cls, L"Static")) SetPropW(c, L"dim", (HANDLE)(INT_PTR)(!on));   /* a disabled label would get Windows' white emboss: it is only drawn grey */
    else EnableWindow(c, on);
    GetWindowRect(c, &rc);
    MapWindowPoints(NULL, GetParent(c), (POINT *)&rc, 2);
    InvalidateRect(GetParent(c), &rc, TRUE);
    InvalidateRect(c, NULL, TRUE);   /* a label is not disabled, only drawn grey: it has to repaint itself */
}

static int selected_game_index(void) {
    int i = ListView_GetNextItem(W.lv, -1, LVNI_SELECTED);
    LVITEMW it;
    if (i < 0) return -1;
    memset(&it, 0, sizeof it);
    it.mask = LVIF_PARAM;
    it.iItem = i;
    ListView_GetItem(W.lv, &it);
    return (int)it.lParam;
}
static const Game *selected_game(void) { int i = selected_game_index(); return i >= 0 && i < g_ngames ? &g_games[i] : NULL; }

/* ---------------------------------------------------------------- display modes */
static int cmp_mode(const void *a, const void *b) { const Mode *x = a, *y = b; return x->w != y->w ? x->w - y->w : x->h - y->h; }
static void load_modes(void) {
    DEVMODEW dm;
    DWORD i;
    int cap = 0, k;
    for (i = 0;; i++) {
        memset(&dm, 0, sizeof dm);
        dm.dmSize = sizeof dm;
        if (!EnumDisplaySettingsW(NULL, i, &dm)) break;
        if (dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480 && dm.dmBitsPerPel >= 16) {
            for (k = 0; k < g_nmodes; k++) if (g_modes[k].w == (int)dm.dmPelsWidth && g_modes[k].h == (int)dm.dmPelsHeight) break;
            if (k < g_nmodes) continue;
            if (g_nmodes == cap) { cap = cap ? cap * 2 : 32; g_modes = (Mode *)realloc(g_modes, cap * sizeof(Mode)); }
            g_modes[g_nmodes].w = dm.dmPelsWidth; g_modes[g_nmodes].h = dm.dmPelsHeight; g_nmodes++;
        }
    }
    if (!g_nmodes) {
        static const Mode fb[] = {{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 720}, {1280, 960}, {1280, 1024}, {1366, 768}, {1600, 900}, {1600, 1200}, {1920, 1080}};
        g_modes = (Mode *)malloc(sizeof fb);
        memcpy(g_modes, fb, sizeof fb);
        g_nmodes = (int)(sizeof fb / sizeof fb[0]);
    }
    qsort(g_modes, g_nmodes, sizeof(Mode), cmp_mode);
}

int ui_mode_count(void) { return g_nmodes; }
void ui_mode_get(int i, int *w, int *h) { *w = g_modes[i].w; *h = g_modes[i].h; }

/* ---------------------------------------------------------------- game list */
static int cmp_view(const void *a, const void *b) {
    const Game *x = &g_games[*(const int *)a], *y = &g_games[*(const int *)b];
    int c = 0, d = g_sortAsc ? 1 : -1;
    if (g_sortCol == 1) c = x->id - y->id;
    else if (g_sortCol == 4) { c = rom_available(x) - rom_available(y); if (!c) c = _wcsicmp(x->title, y->title); }   /* Status: the missing ones first */
    else if (g_sortCol == 5) { c = _wcsicmp(game_hardware(x), game_hardware(y)); if (!c) c = _wcsicmp(x->title, y->title); }
    else if (g_sortCol == 6) { c = game_year(x) - game_year(y); if (!c) c = _wcsicmp(x->title, y->title); }
    else {
        if (g_sortCol == 0) {   /* favorites first (ascending), then by name */
            int fa = il_has(&g_set.fav, x->id), fb = il_has(&g_set.fav, y->id);
            if (fa != fb) return (fa ? -1 : 1) * d;
        }
        c = _wcsicmp(x->title, y->title);
        if (!c) c = _wcsicmp(x->info, y->info);
    }
    if (!c) c = x->id - y->id;
    return c * d;
}

static void sort_arrow(void) {
    HWND hdr = ListView_GetHeader(W.lv);
    int i;
    for (i = 0; i < NCOLS; i++) {
        HDITEMW h;
        memset(&h, 0, sizeof h);
        h.mask = HDI_FORMAT;
        Header_GetItem(hdr, i, &h);
        h.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g_sortCol) h.fmt |= g_sortAsc ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(hdr, i, &h);
    }
}

/* ---- game list columns: hidden ones have width 0 (their width is kept in g_colW to show them again) */
static int g_colW[NCOLS];
static const int kColOrder[NCOLS] = {1, 4, 0, 2, 5, 6, 3};   /* the order the columns are shown in: #, Status, Favorite, Game, Hardware, Year, Info */
static const wchar_t *const kColNames[NCOLS] = {L"Favorite", L"#", L"Game", L"Info", L"Status", L"Hardware", L"Year"};   /* by column number */

static void refilter(void);
static void apply_columns(void) {
    int k, n, i;
    g_set.colsHidden &= ~4;   /* the Game column always stays */
    g_lvProg++;
    for (k = 0; k < NCOLS; k++) {
        int w = ListView_GetColumnWidth(W.lv, k);
        if ((g_set.colsHidden >> k) & 1) { if (w > 0) g_colW[k] = w; if (w != 0) ListView_SetColumnWidth(W.lv, k, 0); }
        else if (w == 0) ListView_SetColumnWidth(W.lv, k, g_colW[k] > 0 ? g_colW[k] : S(k == 0 ? 64 : k == 1 ? 40 : k == 2 ? 260 : k == 3 ? 140 : k == 4 ? 64 : k == 5 ? 190 : 64));
    }
    g_lvProg--;
    n = ListView_GetItemCount(W.lv);   /* the Status icons follow the column: not shown when it is hidden */
    for (i = 0; i < n; i++) {
        LVITEMW li;
        LVITEMW si;
        memset(&li, 0, sizeof li);
        li.mask = LVIF_PARAM; li.iItem = i;
        if (!ListView_GetItem(W.lv, &li) || li.lParam < 0 || li.lParam >= g_ngames) continue;
        memset(&si, 0, sizeof si);
        si.mask = LVIF_IMAGE; si.iItem = i; si.iSubItem = 4;
        si.iImage = ((g_set.colsHidden >> 4) & 1) ? I_IMAGENONE : rom_available(&g_games[li.lParam]) ? 0 : 1;
        ListView_SetItem(W.lv, &si);
    }
}

/* right-click on the title of the list: which columns are shown */
static LRESULT CALLBACK colhdr_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (m == WM_RBUTTONUP) {
        HMENU pm = CreatePopupMenu();
        POINT pt = {(short)LOWORD(l), (short)HIWORD(l)};
        int k;
        for (k = 0; k < NCOLS; k++) { int c = kColOrder[k]; AppendMenuW(pm, MF_STRING | (c == 2 ? MF_GRAYED : 0) | (((g_set.colsHidden >> c) & 1) ? 0 : MF_CHECKED), ID_COL0 + c, kColNames[c]); }   /* Game always stays */
        ClientToScreen(h, &pt);
        TrackPopupMenu(pm, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, NULL);
        DestroyMenu(pm);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}

/* the title notifies the list: a hidden column cannot be dragged open */
static LRESULT CALLBACK lvhdr_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (m == WM_NOTIFY) {
        NMHEADERW *nh = (NMHEADERW *)l;
        if ((nh->hdr.code == HDN_BEGINTRACKW || nh->hdr.code == HDN_BEGINTRACKA || nh->hdr.code == HDN_DIVIDERDBLCLICKW || nh->hdr.code == HDN_DIVIDERDBLCLICKA) &&
            nh->iItem >= 0 && nh->iItem < NCOLS && ((g_set.colsHidden >> nh->iItem) & 1)) return TRUE;
    }
    return DefSubclassProc(h, m, w, l);
}

static void size_columns(void) {
    HDC dc;
    TEXTMETRICW tm;
    HGDIOBJ old;
    int ch, k;
    static const int chars[] = {6, 4, 53, 34, 8, 27, 5};
    if (g_columnsSized) return;
    dc = GetDC(W.lv);
    old = SelectObject(dc, g_font);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(W.lv, dc);
    ch = tm.tmAveCharWidth;
    if (ch <= 0) return;
    if (g_set.colsGame[1] > 0) { lv_set_widths(W.lv, g_set.colsGame, NCOLS); g_columnsSized = 1; apply_columns(); return; }   /* the widths of the last session */
    for (k = 0; k < NCOLS; k++) ListView_SetColumnWidth(W.lv, k, max(chars[k] * ch + 12 + (k == 0 ? S(24) : 0), lv_min_w(W.lv, k)));   /* a little room for the cell margins (and the check box) */
    g_columnsSized = 1;
    apply_columns();
}

static int contains_ci(const wchar_t *hay, const wchar_t *needleLower) {
    wchar_t *l = wdup(hay);
    int r;
    _wcslwr(l);
    r = wcsstr(l, needleLower) != NULL;
    free(l);
    return r;
}

/* the "All Makers" / "All Genres" lists: what the games of the list really have (guessed from the title and BIOS) */
static int cmp_w(const void *a, const void *b) { return _wcsicmp((const wchar_t *)a, (const wchar_t *)b); }
static void fill_filters(void) {
    wchar_t keepR[8] = L"", keepM[40] = L"", keepG[24] = L"";
    int i, j, sel;
    int ir = (int)SendMessageW(W.cbRegion, CB_GETCURSEL, 0, 0);
    if (ir > 0 && ir <= g_nreg) wcscpy(keepR, g_regions[ir - 1]);
    int im = (int)SendMessageW(W.cbMaker, CB_GETCURSEL, 0, 0), ig = (int)SendMessageW(W.cbGenre, CB_GETCURSEL, 0, 0);
    if (im > 0 && im <= g_nmak) wcscpy(keepM, g_makers[im - 1]);
    if (ig > 0 && ig <= g_ngen) wcscpy(keepG, g_genres[ig - 1]);
    if (!g_nreg && !g_nmak && !g_ngen) { wcsncpy(keepR, g_set.fRegion, 7); wcsncpy(keepM, g_set.fMaker, 39); wcsncpy(keepG, g_set.fGenre, 23); }   /* the first fill: the filters of the last session */
    g_nreg = g_nmak = g_ngen = 0;
    for (i = 0; i < g_ngames; i++) {
        const wchar_t *m = classify_maker(g_games[i].title, g_games[i].bios), *g = classify_genre(g_games[i].title);
        const wchar_t *rg = classify_region(g_games[i].title);
        for (j = 0; j < g_nreg; j++) if (!wcscmp(g_regions[j], rg)) break;
        if (j == g_nreg && g_nreg < 8) wcsncpy(g_regions[g_nreg++], rg, 7);
        for (j = 0; j < g_nmak; j++) if (!wcscmp(g_makers[j], m)) break;
        if (j == g_nmak && g_nmak < 32) wcsncpy(g_makers[g_nmak++], m, 39);
        for (j = 0; j < g_ngen; j++) if (!wcscmp(g_genres[j], g)) break;
        if (j == g_ngen && g_ngen < 16) wcsncpy(g_genres[g_ngen++], g, 23);
    }
    qsort(g_regions, (size_t)g_nreg, sizeof g_regions[0], cmp_w);
    qsort(g_makers, (size_t)g_nmak, sizeof g_makers[0], cmp_w);
    qsort(g_genres, (size_t)g_ngen, sizeof g_genres[0], cmp_w);
    SendMessageW(W.cbRegion, CB_RESETCONTENT, 0, 0);
    SendMessageW(W.cbRegion, CB_ADDSTRING, 0, (LPARAM)L"All Regions");
    for (i = 0, sel = 0; i < g_nreg; i++) { SendMessageW(W.cbRegion, CB_ADDSTRING, 0, (LPARAM)g_regions[i]); if (!wcscmp(keepR, g_regions[i])) sel = i + 1; }
    SendMessageW(W.cbRegion, CB_SETCURSEL, sel, 0);
    SendMessageW(W.cbMaker, CB_RESETCONTENT, 0, 0);
    SendMessageW(W.cbMaker, CB_ADDSTRING, 0, (LPARAM)L"All Makers");
    for (i = 0, sel = 0; i < g_nmak; i++) { SendMessageW(W.cbMaker, CB_ADDSTRING, 0, (LPARAM)g_makers[i]); if (!wcscmp(keepM, g_makers[i])) sel = i + 1; }
    SendMessageW(W.cbMaker, CB_SETCURSEL, sel, 0);
    SendMessageW(W.cbGenre, CB_RESETCONTENT, 0, 0);
    SendMessageW(W.cbGenre, CB_ADDSTRING, 0, (LPARAM)L"All Genres");
    for (i = 0, sel = 0; i < g_ngen; i++) { SendMessageW(W.cbGenre, CB_ADDSTRING, 0, (LPARAM)g_genres[i]); if (!wcscmp(keepG, g_genres[i])) sel = i + 1; }
    SendMessageW(W.cbGenre, CB_SETCURSEL, sel, 0);
}

static void refilter(void) {
    wchar_t q[200], rom[520], selRegion[8] = L"", selMaker[40] = L"", selGenre[24] = L"";
    int favOnly = ctl_checked(W.favOnly), avail, i, selId = -1;
    const Game *sg = selected_game();
    if (sg) selId = sg->id;
    {
        int im = (int)SendMessageW(W.cbMaker, CB_GETCURSEL, 0, 0), ig = (int)SendMessageW(W.cbGenre, CB_GETCURSEL, 0, 0);
        int irg = (int)SendMessageW(W.cbRegion, CB_GETCURSEL, 0, 0);
        if (irg > 0 && irg <= g_nreg) wcscpy(selRegion, g_regions[irg - 1]);
        if (im > 0 && im <= g_nmak) wcscpy(selMaker, g_makers[im - 1]);
        if (ig > 0 && ig <= g_ngen) wcscpy(selGenre, g_genres[ig - 1]);
    }
    if (g_nreg || g_nmak || g_ngen) { wcscpy(g_set.fRegion, selRegion); wcscpy(g_set.fMaker, selMaker); wcscpy(g_set.fGenre, selGenre); }   /* not before the first fill: the saved filters are still waiting */
    GetWindowTextW(W.search, q, 200);
    trim_w(q);
    _wcslwr(q);
    GetWindowTextW(W.eRoms, rom, 520);
    avail = ctl_checked(W.chAvail);
    tb_set_checked(W.tbAvail, avail);
    if (!rom[0]) wcscpy(rom, g_set.roms);
    rom_available_init(rom);   /* kept until the next refresh: the Play button needs it too */
    free(g_view);
    g_view = (int *)malloc((g_ngames + 1) * sizeof(int));
    g_nview = 0;
    for (i = 0; i < g_ngames; i++) {
        const Game *g = &g_games[i];
        if (q[0]) {
            wchar_t idt[16];
            swprintf(idt, 16, L"%d", g->id);
            if (!contains_ci(g->title, q) && !contains_ci(g->info, q) && wcscmp(idt, q)) continue;
        }
        if (favOnly && !il_has(&g_set.fav, g->id)) continue;
        if (avail && !rom_available(g)) continue;
        if (selRegion[0] && wcscmp(selRegion, classify_region(g->title))) continue;
        if (selMaker[0] && wcscmp(selMaker, classify_maker(g->title, g->bios))) continue;
        if (selGenre[0] && wcscmp(selGenre, classify_genre(g->title))) continue;
        g_view[g_nview++] = i;
    }
    qsort(g_view, g_nview, sizeof(int), cmp_view);

    g_rebuilding = 1;
    SendMessageW(W.lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(W.lv);
    for (i = 0; i < g_nview; i++) {
        const Game *g = &g_games[g_view[i]];
        wchar_t id[16];
        LVITEMW it;
        swprintf(id, 16, L"%d", g->id);
        memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        it.iImage = I_IMAGENONE;
        it.iItem = i;
        it.pszText = L"";
        it.lParam = g_view[i];
        ListView_InsertItem(W.lv, &it);
        ListView_SetItemText(W.lv, i, 1, id);
        ListView_SetItemText(W.lv, i, 2, g->title);
        ListView_SetItemText(W.lv, i, 3, g->info);
        {   /* Status: an icon, a tick when the ROM set is there, a cross when it is not (none when the column is hidden: an icon is not cut off with the width) */
            LVITEMW si;
            memset(&si, 0, sizeof si);
            si.mask = LVIF_IMAGE; si.iItem = i; si.iSubItem = 4; si.iImage = ((g_set.colsHidden >> 4) & 1) ? I_IMAGENONE : rom_available(g) ? 0 : 1;
            ListView_SetItem(W.lv, &si);
        }
        ListView_SetItemText(W.lv, i, 5, (wchar_t *)game_hardware(g));
        if (game_year(g)) { wchar_t yr[8]; swprintf(yr, 8, L"%d", game_year(g)); ListView_SetItemText(W.lv, i, 6, yr); }
        ListView_SetCheckState(W.lv, i, il_has(&g_set.fav, g->id));
        if (g->id == selId) ListView_SetItemState(W.lv, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SendMessageW(W.lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(W.lv, NULL, TRUE);
    g_rebuilding = 0;
    size_columns();
    sort_arrow();
}

/* ---------------------------------------------------------------- settings <-> widgets */
static void renderer_index_fill(void) {
    int i;
    wchar_t sel[64] = L"";
    if (W.cbRenderer && SendMessageW(W.cbRenderer, CB_GETCURSEL, 0, 0) >= 0) wcscpy(sel, g_rendNames[SendMessageW(W.cbRenderer, CB_GETCURSEL, 0, 0)]);
    g_nrend = renderer_names(g_rendNames, 16);
    SendMessageW(W.cbRenderer, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_nrend; i++) SendMessageW(W.cbRenderer, CB_ADDSTRING, 0, (LPARAM)g_rendNames[i]);
}

static int current_renderer_is_d3d11(void) {
    int i = (int)SendMessageW(W.cbRenderer, CB_GETCURSEL, 0, 0);
    return i >= 0 && i < g_nrend && !wcscmp(g_rendNames[i], L"Direct3D11");
}

static const int kOverscan[7] = {0, 4, 8, 12, 16, 24, 32};
static int val_index(const int *t, int n, int v) {   /* the item with the value nearest to v */
    int i, best = 0;
    for (i = 1; i < n; i++) if (abs(t[i] - v) < abs(t[best] - v)) best = i;
    return best;
}

static void update_renderer(void) {
    BOOL on = current_renderer_is_d3d11();
    enable_ctl(W.chDither, !on);   /* the Direct3D 11 renderer does not dither: it has Dither Smoothing instead */
    enable_ctl(W.cbScale, on); enable_ctl(W.cbXbrz, on); enable_ctl(W.lblScale, on); enable_ctl(W.lblXbrz, on);
    enable_ctl(W.cbFxaa, on); enable_ctl(W.lblFxaa, on);
    enable_ctl(W.cbTexSm, on); enable_ctl(W.lblTexSm, on);
    enable_ctl(W.cbAspect, on); enable_ctl(W.lblAspect, on);
    enable_ctl(W.cbOverscan, on); enable_ctl(W.lblOverscan, on);
    enable_ctl(W.cbFsMode, on); enable_ctl(W.lblFsMode, on); enable_ctl(W.cbVsync, on); enable_ctl(W.lblVsync, on);
    enable_ctl(W.cbDedith, on); enable_ctl(W.lblDedith, on);
    enable_ctl(W.eFPS, !ctl_checked(W.chAuto)); enable_ctl(W.lblFPS, !ctl_checked(W.chAuto));   /* the manual framerate is only used when the framerate is not detected */
    /* these belong to the other renderers: the Direct3D 11 renderer does not read them */
    enable_ctl(W.cbDepth, !on); enable_ctl(W.lblDepth, !on);
    enable_ctl(W.cbFilter, !on); enable_ctl(W.lblFilter, !on);
    enable_ctl(W.cbTexType, !on); enable_ctl(W.lblTexType, !on);
    enable_ctl(W.cbTexCache, !on); enable_ctl(W.lblTexCache, !on);
    enable_ctl(W.cbBlend, !on); enable_ctl(W.lblBlend, !on);
}

static void set_combo(HWND cb, int v) {
    int n = (int)SendMessageW(cb, CB_GETCOUNT, 0, 0);
    if (v >= 0 && v < n) SendMessageW(cb, CB_SETCURSEL, v, 0);
}

static void update_buttons(void);
static char *ui_sig(void);

/* the ROM sets of the games are looked up again (the ROMs folder may have changed) */
static void roms_refresh(void) {
    wchar_t rom[520];
    GetWindowTextW(W.eRoms, rom, 520);
    if (!rom[0]) wcscpy(rom, g_set.roms);
    rom_available_init(rom);
}
static int playable(const Game *g) { return g && !g_running && rom_available(g); }
static void gather(KVMap *m);

static void apply_to_ui(const KVMap *forced) {
    KVMap r;
    int i, found = 0;
    g_applying = 1;
    if (forced) r = *forced; else renderer_read(&r);
    edit_set_int(W.eFPS, kv_get(&r, "FramerateManual"));
    {   /* the size is one of the list; a size of the file that the list does not have is added to it */
        int x = kv_get(&r, "XSize"), y = kv_get(&r, "YSize"), sel = -1;
        for (i = 0; i < g_nmodes; i++) if (g_modes[i].w == x && g_modes[i].h == y) sel = i;
        if (sel < 0 && x > 0 && y > 0) {
            wchar_t t[40];
            g_modes = (Mode *)realloc(g_modes, (g_nmodes + 1) * sizeof(Mode));
            g_modes[g_nmodes].w = x; g_modes[g_nmodes].h = y; sel = g_nmodes++;
            swprintf(t, 40, L"%d x %d%ls", x, y, x * 3 == y * 4 ? L" (4:3)" : L"");
            SendMessageW(W.cbRes, CB_ADDSTRING, 0, (LPARAM)t);
        }
        SendMessageW(W.cbRes, CB_SETCURSEL, sel < 0 ? 0 : sel, 0);
    }
    ctl_set_checked(W.chFull, kv_get(&r, "FullScreen") != 0);
    tb_set_checked(W.tbFs, kv_get(&r, "FullScreen") != 0);
    ctl_set_checked(W.chDither, kv_get(&r, "Dithering") != 0);
    ctl_set_checked(W.chFPS, kv_get(&r, "ShowFPS") != 0);
    ctl_set_checked(W.chLimit, kv_get(&r, "FrameLimitation") != 0);
    ctl_set_checked(W.chSkip, kv_get(&r, "FrameSkipping") != 0);
    ctl_set_checked(W.chAuto, kv_get(&r, "FramerateDetection") != 0);
    set_combo(W.cbDepth, kv_get(&r, "ColorDepth") == 32);
    set_combo(W.cbScan, kv_get(&r, "ScanLines"));
    set_combo(W.cbFilter, kv_get(&r, "Filtering"));
    set_combo(W.cbTexType, kv_get(&r, "TextureType"));
    set_combo(W.cbScale, kv_get(&r, "InternalScale"));
    set_combo(W.cbXbrz, kv_get(&r, "XBRZ"));
    set_combo(W.cbFxaa, kv_get(&r, "FXAA") ? 1 : 0);
    set_combo(W.cbTexSm, kv_get(&r, "TextureSmoothing") ? 1 : 0);
    set_combo(W.cbOverscan, val_index(kOverscan, 7, kv_get(&r, "Overscan")));
    { int a = kv_get(&r, "KeepAspect"); set_combo(W.cbAspect, a < 0 || a > 3 ? (a ? 1 : 0) : a); }
    set_combo(W.cbFsMode, kv_get(&r, "Borderless") ? 1 : 0);
    set_combo(W.cbVsync, kv_get(&r, "VSync") ? 1 : 0);
    set_combo(W.cbDedith, kv_get(&r, "Dedither"));
    set_combo(W.cbTexCache, kv_get(&r, "TextureCaching"));
    set_combo(W.cbBlend, kv_get(&r, "Blending"));
    renderer_index_fill();
    for (i = 0; i < g_nrend; i++) if (!wcscmp(g_rendNames[i], g_set.renderer)) { SendMessageW(W.cbRenderer, CB_SETCURSEL, i, 0); found = 1; }
    if (!found)   /* the saved renderer is gone (e.g. a removed plugin): fall back to Direct3D 11 */
        for (i = 0; i < g_nrend; i++) if (!wcscmp(g_rendNames[i], L"Direct3D11")) { SendMessageW(W.cbRenderer, CB_SETCURSEL, i, 0); found = 1; }
    if (!found) SendMessageW(W.cbRenderer, CB_SETCURSEL, 0, 0);
    set_combo(W.cbRot, g_set.rotate + 1);
    ctl_set_checked(W.chSound, g_set.useSound);
    ctl_set_checked(W.chFilt, g_set.soundFilter);
    ctl_set_checked(W.chSurr, g_set.surround);
    ctl_set_checked(W.chExc, g_set.exciter);
    ctl_set_checked(W.chSlow, g_set.slowGeometry);
    ctl_set_checked(W.chHide, g_set.hideConsole);
    set_combo(W.cbOnStart, g_set.onStart);
    set_combo(W.cbTheme, g_set.themeMode);
    ctl_set_checked(W.chLogs, g_set.logs);
    edit_set_int(W.eCutoff, g_set.cutoff);
    edit_set_int(W.eSurround, g_set.surroundMul);
    SetWindowTextW(W.eRoms, g_set.roms);
    ctl_set_checked(W.chAvail, g_set.onlyAvail);
    SetWindowTextW(W.eTrainer, g_set.trainer);
    SetWindowTextW(W.eBezel, g_bezelPath);
    ctl_set_checked(W.chClassic, g_set.optWindow == 0);
    ctl_set_checked(W.chTrainer, g_set.trainerOn);
    ctl_set_checked(W.favOnly, g_set.favOnly);
    tb_set_checked(W.tbFav, g_set.favOnly);
    ctl_load();
    update_renderer();
    g_applying = 0;
    free(g_savedSig);
    g_savedSig = ui_sig();
    update_buttons();
}

/* copies the widget values into the settings and returns the renderer.cfg values */
static void gather(KVMap *m) {
    int i = (int)SendMessageW(W.cbRenderer, CB_GETCURSEL, 0, 0);
    wchar_t t[520];
    if (i >= 0 && i < g_nrend) wcscpy(g_set.renderer, g_rendNames[i]);
    g_set.rotate = (int)SendMessageW(W.cbRot, CB_GETCURSEL, 0, 0) - 1;
    g_set.useSound = ctl_checked(W.chSound);
    g_set.soundFilter = ctl_checked(W.chFilt);
    g_set.surround = ctl_checked(W.chSurr);
    g_set.exciter = ctl_checked(W.chExc);
    g_set.slowGeometry = ctl_checked(W.chSlow);
    g_set.hideConsole = ctl_checked(W.chHide);
    g_set.onStart = (int)SendMessageW(W.cbOnStart, CB_GETCURSEL, 0, 0);
    g_set.logs = ctl_checked(W.chLogs);
    g_set.cutoff = edit_int(W.eCutoff, 1000, 48000);
    g_set.surroundMul = edit_int(W.eSurround, 0, 200);
    GetWindowTextW(W.eRoms, t, 520);
    wcsncpy(g_set.roms, t, 519);
    g_set.onlyAvail = ctl_checked(W.chAvail);
    GetWindowTextW(W.eTrainer, t, 520);
    wcsncpy(g_set.trainer, t, 519); g_set.trainer[519] = 0; trim_w(g_set.trainer);
    g_set.trainerOn = ctl_checked(W.chTrainer);
    GetWindowTextW(W.eBezel, g_bezelPath, 520); trim_w(g_bezelPath);
    ctl_collect();
    memset(m, 0, sizeof *m);
    {   int sel = (int)SendMessageW(W.cbRes, CB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= g_nmodes) sel = 0;
        kv_set(m, "XSize", g_nmodes ? g_modes[sel].w : 640);
        kv_set(m, "YSize", g_nmodes ? g_modes[sel].h : 480);
    }
    kv_set(m, "FramerateManual", edit_int(W.eFPS, 0, 1000));
    kv_set(m, "FullScreen", ctl_checked(W.chFull));
    kv_set(m, "Dithering", ctl_checked(W.chDither));
    kv_set(m, "ShowFPS", ctl_checked(W.chFPS));
    kv_set(m, "FrameLimitation", ctl_checked(W.chLimit));
    kv_set(m, "FrameSkipping", ctl_checked(W.chSkip));
    kv_set(m, "FramerateDetection", ctl_checked(W.chAuto));
    kv_set(m, "ColorDepth", SendMessageW(W.cbDepth, CB_GETCURSEL, 0, 0) == 1 ? 32 : 16);
    kv_set(m, "ScanLines", (int)SendMessageW(W.cbScan, CB_GETCURSEL, 0, 0));
    kv_set(m, "Filtering", (int)SendMessageW(W.cbFilter, CB_GETCURSEL, 0, 0));
    kv_set(m, "TextureType", (int)SendMessageW(W.cbTexType, CB_GETCURSEL, 0, 0));
    kv_set(m, "TextureCaching", (int)SendMessageW(W.cbTexCache, CB_GETCURSEL, 0, 0));
    kv_set(m, "Blending", (int)SendMessageW(W.cbBlend, CB_GETCURSEL, 0, 0));
    if (!wcscmp(g_set.renderer, L"Direct3D11")) {   /* the other renderers do not know these keys */
        kv_set(m, "InternalScale", (int)SendMessageW(W.cbScale, CB_GETCURSEL, 0, 0));
        kv_set(m, "XBRZ", (int)SendMessageW(W.cbXbrz, CB_GETCURSEL, 0, 0));
        kv_set(m, "FXAA", (int)SendMessageW(W.cbFxaa, CB_GETCURSEL, 0, 0) == 1);
        kv_set(m, "TextureSmoothing", (int)SendMessageW(W.cbTexSm, CB_GETCURSEL, 0, 0) == 1);
        { int i = (int)SendMessageW(W.cbOverscan, CB_GETCURSEL, 0, 0); kv_set(m, "Overscan", kOverscan[i >= 0 && i < 7 ? i : 0]); }
        kv_set(m, "KeepAspect", (int)SendMessageW(W.cbAspect, CB_GETCURSEL, 0, 0));
        kv_set(m, "Borderless", (int)SendMessageW(W.cbFsMode, CB_GETCURSEL, 0, 0) == 1);
        kv_set(m, "VSync", (int)SendMessageW(W.cbVsync, CB_GETCURSEL, 0, 0) == 1);
        kv_set(m, "Dedither", (int)SendMessageW(W.cbDedith, CB_GETCURSEL, 0, 0));
        kv_set(m, "Logging", g_set.logs);
    }
}

/* describes everything "Save Settings" would write, to tell whether anything changed */
static char *ui_sig(void) {
    KVMap m;
    Buf b = {0};
    int i;
    gather(&m);
    for (i = 0; i < m.n; i++) buf_fmt(&b, "%s=%d;", m.kv[i].key, m.kv[i].val);
    settings_sig(&b);
    buf_fmt(&b, "classic=%d;", ctl_checked(W.chClassic));
    buf_fmt(&b, "bezel=%ls;", g_bezelPath);
    input_cfg_text(&b);
    return b.s;
}

static void update_buttons(void) {
    char *s;
    if (g_applying) return;
    enable_ctl(W.iconBtn, selected_game() != NULL);
    EnableWindow(W.playBtn, playable(selected_game()));
    s = ui_sig();
    free(s);
}

static void set_optmode(int on, int show);

static int do_save(void) {
    KVMap r;
    int ok, want;
    gather(&r);
    want = !ctl_checked(W.chClassic);   /* the options layout is applied with the other settings */
    g_set.optWindow = want;
    input_save_cfg();
    ok = settings_save();
    if (!ok) set_note(L"Could not save the settings: the ZiNc folder is not writable");
    renderer_write(&r);
    if (want != g_optWin) PostMessageW(g_main, WM_APP_LAYOUT, (WPARAM)want, 0);   /* after this message: the Save button itself moves */
    free(g_savedSig);
    g_savedSig = ui_sig();
    update_buttons();
    return ok;
}

/* ---------------------------------------------------------------- playing */
static void rebuild_recent(void);

static DWORD WINAPI wait_thread(LPVOID h) {
    WaitForSingleObject((HANDLE)h, INFINITE);
    PostMessageW(g_main, WM_APP_GAMEEXIT, 0, 0);
    return 0;
}

static void launch_game_opts(const Game *g, const PlayOpts *po) {
    ArgList a;
    wchar_t warn[300];
    int i, n;
    IntList rec = {0};
    if (!g) { msg_box(g_main, L"Play", L"Select a game first.", MB_ICONINFORMATION, NULL); return; }
    if (g_running) { msg_box(g_main, L"Play", L"A game is already running.", MB_ICONINFORMATION, NULL); return; }
    if (!rom_available(g)) { msg_box(g_main, L"Play", L"The ROM files of this game were not found in the ROMs folder.", MB_ICONINFORMATION, NULL); return; }
    do_save();
    zinc_args_ex(g->id, po, &a, warn, 300);
    if (warn[0]) msg_box(g_main, L"Input Plugin", warn, MB_ICONWARNING, NULL);
    if (!zinc_start(&a, &g_proc)) {
        wchar_t t[200];
        swprintf(t, 200, L"Windows error %lu", GetLastError());
        msg_box(g_main, L"Could Not Start ZiNc", t, MB_ICONERROR, NULL);
        args_free(&a);
        return;
    }
    args_free(&a);
    g_running = 1;
    g_stopped = 0;
    g_startTick = GetTickCount();
    {
        wchar_t t[400];
        swprintf(t, 400, L"Playing: %ls", g->title);
        set_status(t);
    }
    EnableWindow(W.stopBtn, TRUE);
    update_buttons();
    il_add(&rec, g->id);
    for (i = 0, n = 1; i < g_set.recent.n; i++) if (g_set.recent.v[i] != g->id && n < 8) { il_add(&rec, g_set.recent.v[i]); n++; }
    il_copy(&g_set.recent, &rec);
    il_clear(&rec);
    settings_save();
    rebuild_recent();
    CloseHandle(CreateThread(NULL, 0, wait_thread, g_proc.hProcess, 0, NULL));
    start_trainer(g);
    if (g_set.onStart == 1) {   /* Minimize The Launcher: it comes back when the game ends */
        if (g_opt) hide_opt(0);
        ShowWindow(g_main, SW_MINIMIZE);
        g_minimized = 1;
    } else if (g_set.onStart == 2) {   /* Close The Launcher: the game (and its trainer) go on without it */
        g_leaveTrainer = 1;
        PostMessageW(g_main, WM_CLOSE, 0, 0);
    }
}

static void launch_game(const Game *g) { launch_game_opts(g, NULL); }

static void launch_with_trainer(const Game *g);

static void launch_once(const Game *g, int fullscreen, const wchar_t *renderer) {
    PlayOpts po;
    memset(&po, 0, sizeof po);
    po.fullscreen = fullscreen;
    if (renderer) wcsncpy(po.renderer, renderer, 63);
    launch_game_opts(g, &po);
}

static void rebuild_recent(void) {
    int i, n = 0, k;
    HMENU file = GetSubMenu(GetMenu(g_main), 0);
    while (GetMenuItemCount(g_recentMenu) > 0) DeleteMenu(g_recentMenu, 0, MF_BYPOSITION);
    for (i = 0; i < g_set.recent.n; i++) {
        for (k = 0; k < g_ngames; k++) {
            wchar_t label[600], *p;
            const Game *g = &g_games[k];
            size_t j;
            if (g->id != g_set.recent.v[i]) continue;
            n++;
            swprintf(label, 600, L"&%d  ", n);
            p = label + wcslen(label);
            for (j = 0; g->title[j] && p < label + 500; j++) { *p++ = g->title[j]; if (g->title[j] == L'&') *p++ = L'&'; }
            if (g->info[0]) {
                *p++ = L' '; *p++ = L' '; *p++ = L'[';
                for (j = 0; g->info[j] && p < label + 590; j++) { *p++ = g->info[j]; if (g->info[j] == L'&') *p++ = L'&'; }
                *p++ = L']';
            }
            *p = 0;
            AppendMenuW(g_recentMenu, MF_STRING, ID_RECENT0 + n - 1, label);
            break;
        }
    }
    if (!n) AppendMenuW(g_recentMenu, MF_STRING | MF_GRAYED, 0, L"(No Recent Games)");
    EnableMenuItem(file, ID_M_CLEARRECENT, MF_BYCOMMAND | (n ? MF_ENABLED : MF_GRAYED));
}

static void launch_recent(int slot) {
    int i, n = 0, k;
    for (i = 0; i < g_set.recent.n; i++)
        for (k = 0; k < g_ngames; k++)
            if (g_games[k].id == g_set.recent.v[i]) { if (n++ == slot) { launch_game(&g_games[k]); return; } break; }
}

typedef struct { Game *list; int n; int err; } FetchResult;
static DWORD WINAPI fetch_thread(LPVOID unused) {
    FetchResult *r = (FetchResult *)calloc(1, sizeof *r);
    r->err = games_fetch(&r->list, &r->n);
    PostMessageW(g_main, WM_APP_GAMES, 0, (LPARAM)r);
    return 0;
}
static void rescan(void) {
    set_status(L"Scanning games…");
    CloseHandle(CreateThread(NULL, 0, fetch_thread, NULL, 0, NULL));
}

static void open_folder(const wchar_t *dir) { ShellExecuteW(g_main, L"open", dir, NULL, NULL, SW_SHOWNORMAL); }
void roms_dir_abs(wchar_t *out, size_t cap) {
    if (is_abs_path(g_set.roms)) { wcsncpy(out, g_set.roms, cap - 1); out[cap - 1] = 0; } else path_join(out, cap, g_root, g_set.roms);
}

static wchar_t g_browseStart[560];
static int CALLBACK browse_cb(HWND h, UINT m, LPARAM l, LPARAM d) {   /* the folder dialog opens in g_browseStart */
    (void)l; (void)d;
    if (m == BFFM_INITIALIZED && g_browseStart[0]) SendMessageW(h, BFFM_SETSELECTIONW, TRUE, (LPARAM)g_browseStart);
    return 0;
}

static void browse_roms(void) {
    BROWSEINFOW bi;
    LPITEMIDLIST pidl;
    wchar_t path[MAX_PATH];
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = ui_owner(g_main);
    bi.lpszTitle = L"Select ROMs Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    {   /* it starts in the ROMs folder of the settings (./roms by default) */
        wchar_t cur[560];
        GetWindowTextW(W.eRoms, cur, 560); trim_w(cur);
        if (!cur[0]) wcscpy(cur, L"roms");
        if (is_abs_path(cur)) wcsncpy(g_browseStart, cur, 559); else path_join(g_browseStart, 560, g_root, cur);
        g_browseStart[559] = 0;
        if (GetFileAttributesW(g_browseStart) == INVALID_FILE_ATTRIBUTES) path_join(g_browseStart, 560, g_root, L"roms");
    }
    bi.lpfn = browse_cb;
    CoInitialize(NULL);
    pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) { SetWindowTextW(W.eRoms, path); refilter(); }
        CoTaskMemFree(pidl);
    }
}

static void desktop_icon(void) {
    const Game *g = selected_game();
    wchar_t err[400];
    if (!g) { msg_box(g_main, L"Desktop Icon", L"Select a game first.", MB_ICONINFORMATION, NULL); return; }
    do_save();
    if (!create_desktop_icon(g, err, 400)) {
        wchar_t t[500];
        swprintf(t, 500, L"Could not create the shortcut:\n%ls", err);
        msg_box(g_main, L"Desktop Icon", t, MB_ICONWARNING, NULL);
        return;
    }
    {
        wchar_t t[400];
        swprintf(t, 400, L"Desktop icon created: %ls", g->title);
        set_status(t);
    }
}

static void toggle_fav(void) {
    const Game *g = selected_game();
    if (!g) return;
    set_favorite(g->id, !il_has(&g_set.fav, g->id));
    refilter();
}

/* ---- backup, restore, trainer */
static int pick_zip(int save, wchar_t *file, int cap) {
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = ui_owner(g_main);
    ofn.lpstrFilter = L"Zip files (*.zip)\0*.zip\0All files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)cap;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

static void do_backup(void) {
    wchar_t file[560];
    SYSTEMTIME st;
    int n;
    do_save();
    GetLocalTime(&st);
    swprintf(file, 560, L"ZiNc-EX-backup-%04d-%02d-%02d.zip", st.wYear, st.wMonth, st.wDay);
    if (!pick_zip(1, file, 560)) return;
    n = backup_create(file);
    if (n > 0) { wchar_t t[700]; swprintf(t, 700, L"Backup saved: %d files in %ls", n, file); set_note(t); }
    else msg_box(g_main, L"Backup Settings", L"The backup could not be written.", MB_ICONWARNING, NULL);
}

static void do_restore(void) {
    wchar_t file[560] = L"", keep[560];
    int r = 0, n;
    if (g_running) { msg_box(g_main, L"Restore Settings", L"Stop the running game first.", MB_ICONINFORMATION, NULL); return; }
    if (!pick_zip(0, file, 560)) return;
    msg_box(g_main, L"Restore Settings",
            L"Replace the current settings, controls, profiles, per-game settings and saves with the ones in this backup?\n\nThe current ones are saved first as ZiNc-EX-before-restore.zip in the ZiNc folder.",
            MB_YESNO | MB_ICONQUESTION, &r);
    if (r != IDYES) return;
    do_save();
    path_join(keep, 560, g_root, L"ZiNc-EX-before-restore.zip");
    backup_create(keep);
    n = backup_restore(file);
    if (n < 0) { msg_box(g_main, L"Restore Settings", L"This file is not a readable backup of ZiNc EX (damaged or not a zip).", MB_ICONWARNING, NULL); return; }
    settings_defaults(&g_set, NULL);   /* then everything that the backup holds */
    settings_load();
    gamecfg_load();
    input_load_cfg();
    apply_to_ui(NULL);
    fill_filters();
    refilter();
    { wchar_t t[200]; swprintf(t, 200, L"Backup restored: %d files", n); set_note(t); }
}

static void browse_trainer(void) {
    OPENFILENAMEW ofn;
    wchar_t file[560], trn[560];
    GetWindowTextW(W.eTrainer, file, 560); trim_w(file);
    memset(&ofn, 0, sizeof ofn);
    if (!file[0]) { path_join(trn, 560, g_root, L"trainer"); if (GetFileAttributesW(trn) == INVALID_FILE_ATTRIBUTES) CreateDirectoryW(trn, NULL); ofn.lpstrInitialDir = trn; }   /* ./trainer when no trainer is set */
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = ui_owner(g_main);
    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 560;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(W.eTrainer, file);
}

static void browse_bezel(void) {
    OPENFILENAMEW ofn;
    wchar_t file[560], bez[560];
    GetWindowTextW(W.eBezel, file, 560); trim_w(file);
    memset(&ofn, 0, sizeof ofn);
    if (!file[0]) { path_join(bez, 560, g_root, L"bezels"); CreateDirectoryW(bez, NULL); ofn.lpstrInitialDir = bez; }   /* ./bezels when no bezel is set */
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = ui_owner(g_main);
    ofn.lpstrFilter = L"Images with transparency (*.png)\0*.png\0All files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 560;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(W.eBezel, file);
}

/* the trainer program starts with the game (when switched on, for all games or for this one) and is closed when the game ends */
static HANDLE g_trainer;
static BOOL CALLBACK close_trainer_win(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == (DWORD)lp) PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}
static void stop_trainer(void) {
    if (!g_trainer) return;
    if (WaitForSingleObject(g_trainer, 0) == WAIT_TIMEOUT) EnumWindows(close_trainer_win, (LPARAM)GetProcessId(g_trainer));
    CloseHandle(g_trainer);
    g_trainer = NULL;
}
static int g_forceTrainer;   /* "Play with Trainer": start the trainer for this start whatever the settings say */
static void start_trainer(const Game *g) {
    const GameCfg *gc = gamecfg_find(g->id);
    int on = g_forceTrainer ? 1 : gc && gc->trainer >= 0 ? gc->trainer : g_set.trainerOn;
    wchar_t path[700], dir[700], *slash;
    SHELLEXECUTEINFOW sei;
    if (!on || !g_set.trainer[0]) return;
    if (is_abs_path(g_set.trainer)) wcsncpy(path, g_set.trainer, 699); else path_join(path, 700, g_root, g_set.trainer);
    path[699] = 0;
    if (!file_exists(path)) { set_note(L"The trainer program was not found (General tab)"); return; }
    if (g_trainer && WaitForSingleObject(g_trainer, 0) == WAIT_TIMEOUT) return;   /* already running */
    if (g_trainer) { CloseHandle(g_trainer); g_trainer = NULL; }
    wcscpy(dir, path);
    slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    memset(&sei, 0, sizeof sei);
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = g_main;
    sei.lpFile = path;
    sei.lpDirectory = dir;
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) g_trainer = sei.hProcess;
}

static void launch_with_trainer(const Game *g) {
    if (!g) { msg_box(g_main, L"Play With Trainer", L"Select a game first.", MB_ICONINFORMATION, NULL); return; }
    if (!g_set.trainer[0]) { msg_box(g_main, L"Play With Trainer", L"Choose the trainer program first: General tab > Trainer Program.", MB_ICONINFORMATION, NULL); return; }
    g_forceTrainer = 1;
    launch_game(g);
    g_forceTrainer = 0;
}

/* Game > Game Settings: the renderer, window, sound, controls profile ... of the selected game only */
static void game_settings(void) {
    const Game *g = selected_game();
    if (!g) { msg_box(g_main, L"Game Settings", L"Select a game first.", MB_ICONINFORMATION, NULL); return; }
    if (dlg_game_settings(g_main, g)) {
        set_note(gamecfg_find(g->id) ? L"This game now has its own settings" : L"This game follows the main settings again");
        selection_status();
    }
}

static void default_settings(void) {
    KVMap forced;
    int r = 0;
    msg_box(g_main, L"Default Settings",
            L"Restore the video, audio and system settings to their defaults?\n\nThe Controls and Combos tabs (key bindings, pads, combos), the ROM folder and favorites are kept. The defaults are saved right away.",
            MB_YESNO | MB_ICONQUESTION, &r);
    if (r != IDYES) return;
    input_save_cfg(); /* keep unsaved Controls edits; ctl_refresh_rows reloads from disk */
    settings_defaults(&g_set, &g_set);
    renderer_defaults(&forced);
    apply_to_ui(&forced);
    ctl_refresh_rows();
    if (do_save()) set_note(L"Default settings restored and saved");
}

/* ---------------------------------------------------------------- layout */
static void place(HWND h, int x, int y, int w, int hh) { SetWindowPos(h, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE); }

static void layout_rows(PRow *r, int n, int w, int h) {
    int x = S(10), y = S(10), lw = 0, i, W2 = w - 2 * S(10), step = S(23);
    if (n > 20 && h > 0) {   /* the long video page: the rows are spread evenly over the height of the page */
        int rows = 0, lines = 0, hdrs = 0;
        for (i = 0; i < n; i++) {
            if (r[i].kind == 2) { lines++; if (i + 1 < n && r[i + 1].kind == 2) i++; }
            else if (r[i].kind == 5) hdrs++;
            else rows++;
        }
        if (rows) {
            step = (h - 2 * S(10) - lines * S(24) - hdrs * S(32) - S(8)) / rows;
            if (step < S(23)) step = S(23);
            if (step > S(32)) step = S(32);
        }
    }
    for (i = 0; i < n; i++)
        if (r[i].kind < 2) { wchar_t b[120]; int t; GetWindowTextW(r[i].lbl, b, 120); t = text_width(g_main, b); if (t > lw) lw = t; }
    lw += S(14);
    if (lw > W2 / 2) lw = W2 / 2;
    for (i = 0; i < n; i++) {
        if (r[i].kind == 2) {
            if (n > 20 && i > 0 && r[i - 1].kind != 2 && r[i - 1].kind != 5) y += S(8);   /* a little gap before the check boxes */
            if (n > 20 && i + 1 < n && r[i + 1].kind == 2) {   /* the long video page: check boxes side by side */
                place(r[i].ctl, x, y, W2 / 2, S(21)); place(r[i + 1].ctl, x + W2 / 2, y, W2 / 2, S(21));
                i++; y += S(24); continue;
            }
            place(r[i].ctl, x, y, W2, S(21)); y += S(24); continue;
        }
        if (r[i].kind == 3) { place(r[i].ctl, x, y, W2, S(26)); y += S(32); continue; }
        if (r[i].kind == 5) {   /* a group title: the text, then a line */
            wchar_t b[120]; int tw;
            GetWindowTextW(r[i].lbl, b, 120); tw = text_width(g_main, b) + S(10);
            y += S(8);
            place(r[i].lbl, x, y, tw, S(18));
            place(r[i].btn, x + tw, y + S(9), W2 - tw, S(1) > 1 ? S(1) : 1);
            y += S(24); continue;
        }
        place(r[i].lbl, x, y + S(4), lw, S(18));
        if (i + 1 < n && r[i + 1].kind == 4) {   /* an edit with a second label + edit (Width, Height) on the same row */
            wchar_t b[120]; int hl, g = S(8), ew;
            GetWindowTextW(r[i + 1].lbl, b, 120); hl = text_width(g_main, b) + S(14);
            ew = (W2 - lw - hl - g) / 2;
            if (ew < S(50)) ew = S(50);
            place(r[i].ctl, x + lw, y, ew, S(n > 20 ? 22 : 23));
            place(r[i + 1].lbl, x + lw + ew + g, y + S(4), hl, S(18));
            place(r[i + 1].ctl, x + lw + ew + g + hl, y, W2 - lw - ew - g - hl, S(n > 20 ? 22 : 23));
            i++; y += n > 20 ? step : S(27); continue;
        }
        if (r[i].btn) {   /* an edit with Browse and a small X on the same row */
            int bw = S(80), xw = S(26), g = S(4), ew = W2 - lw - bw - xw - 2 * g;
            if (ew < S(60)) ew = S(60);
            place(r[i].ctl, x + lw, y, ew, S(23));
            place(r[i].btn, x + lw + ew + g, y - 1, bw, S(25));
            place(r[i].clr, x + lw + ew + g + bw + g, y - 1, xw, S(25));
            y += S(27);
            continue;
        }
        place(r[i].ctl, x + lw, y, W2 - lw, r[i].kind == 0 ? S(220) : n > 20 ? S(22) : S(23));
        y += n > 20 ? step : S(27);
    }
}

static void size_columns(void);

/* back to the default window: size, centered position, splitter and column widths */
static void reset_window(int keepColumns) {
    RECT wa;
    int w = g_optWin ? S(860) : S(1200), h = g_optWin ? S(720) : S(800), x, y, ow = S(560), oh = S(800), gap = S(8);
    if (IsZoomed(g_main) || IsIconic(g_main)) ShowWindow(g_main, SW_RESTORE);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (w > wa.right - wa.left) w = wa.right - wa.left;
    if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
    if (oh > wa.bottom - wa.top) oh = wa.bottom - wa.top;
    x = wa.left + (wa.right - wa.left - w) / 2;
    y = wa.top + (wa.bottom - wa.top - h) / 2;
    if (g_optWin) {   /* the options are in a window of their own: the main window is narrower, and the options window stands next to it when both fit */
        if (x + w + gap + ow > wa.right) ow = wa.right - x - w - gap > S(460) ? wa.right - x - w - gap : ow;   /* the main window stays centered; the settings window is beside it */
        g_optRect.w = ow; g_optRect.h = oh;
        g_optRect.x = x + w + gap; g_optRect.y = wa.top + (wa.bottom - wa.top - oh) / 2;
        if (g_optRect.x + g_optRect.w > wa.right) g_optRect.x = wa.right - g_optRect.w;
        if (g_opt) SetWindowPos(g_opt, NULL, g_optRect.x, g_optRect.y, g_optRect.w, g_optRect.h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (!keepColumns) {
        g_splitX = 0;
        g_columnsSized = 0;
        memset(g_set.colsGame, 0, sizeof g_set.colsGame); g_set.splitX = 0;   /* "Reset Window Size": the default columns and splitter as well */
    }
    SetWindowPos(g_main, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    size_columns();
    RedrawWindow(g_main, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    g_winRect.x = x; g_winRect.y = y; g_winRect.w = w; g_winRect.h = h;
}

/* the status bar tells which ROM files of the selected game are missing */
static void selection_status(void) {
    const Game *g;
    wchar_t miss[300], t[400];
    if (g_running) return;
    g = selected_game();
    if (g && !rom_available(g) && rom_missing(g, miss, 300)) {
        swprintf(t, 400, L"Missing in the ROMs folder: %ls", miss);
        set_status(t);
    } else {
        games_status();
        if (g && gamecfg_find(g->id)) { get_status(t, 400); wcscat(t, L"   |   This game has its own settings (Game > Game Settings)"); set_status(t); }
    }
}

/* View: the search bar and the filters bar of the game list can be hidden */
static void apply_bars(void) {
    int s = SEARCH_SHOWN() ? SW_SHOW : SW_HIDE, f = g_set.hideFilters ? SW_HIDE : SW_SHOW;
    {   /* the toolbar */
        int t = g_set.hideToolbar ? SW_HIDE : SW_SHOW;
        HWND tb[] = {W.tbRefresh, W.tbFav, W.tbAvail, W.tbSep1, W.playBtn, W.stopBtn, W.tbFs, W.tbSep2, W.tbSet, W.tbVideo, W.tbAudio, W.tbCtl, W.tbCmb};
        int k;
        for (k = 0; k < (int)(sizeof tb / sizeof tb[0]); k++) ShowWindow(tb[k], t);
    }
    ShowWindow(W.lblSearch, s); ShowWindow(W.search, s); ShowWindow(W.clearBtn, s);
    ShowWindow(W.cbRegion, f); ShowWindow(W.cbMaker, f); ShowWindow(W.cbGenre, f); ShowWindow(W.sortBtn, f);
}

/* the tab control with its pages and the save button, wherever they are (main window or options window): tab rectangle tx ty tw th, save button at sx sy */
static void layout_tabs(int tx, int ty, int tw, int th, int sx, int sy) {
    RECT tr;
    int i, w, h;
    place(W.tab, tx, ty, tw, th);
    GetClientRect(W.tab, &tr);
    TabCtrl_AdjustRect(W.tab, FALSE, &tr);
    tr.left += tx; tr.right += tx; tr.top += ty; tr.bottom += ty;
    w = tr.right - tr.left; h = tr.bottom - tr.top;
    for (i = 0; i < NPAGES; i++) place(W.page[i], tr.left, tr.top, w, h);
    layout_rows(W.vrows, W.nv, w, h);
    layout_rows(W.arows, W.na, w, 0);
    layout_rows(W.srows, W.ns, w, 0);
    ctl_layout(w, h);
    cmb_layout(w, h);
    place(W.defBtn, tx, sy, S(120), S(26));   /* Restore Defaults: opposite of OK */
    place(W.cancelBtn, sx + S(120) - S(80), sy, S(80), S(26));   /* OK, Cancel: at the right bottom */
    place(W.saveBtn, sx + S(120) - 2 * S(80) - S(8), sy, S(80), S(26));
}

static void layout_opt(void);

static void do_layout(void) {
    RECT rc, tr;
    int cw, ch, sbh, m = S(8), gap = S(8), x, y, w, h, leftW, rightX, rightW, i, bh = S(26), by, ly;
    GetClientRect(g_main, &rc);
    SendMessageW(W.status, WM_SIZE, 0, 0);
    GetWindowRect(W.status, &tr);
    sbh = tr.bottom - tr.top;
    cw = rc.right; ch = rc.bottom - sbh;
    if (g_splitX <= 0 && g_set.splitX > 0) g_splitX = S(g_set.splitX);   /* the splitter of the last session */
    if (g_splitX <= 0) g_splitX = (cw - 2 * m - gap) * 3 / 5;
    leftW = g_splitX;
    if (leftW < S(600)) leftW = S(600);   /* the toolbar is over the list */
    if (leftW > cw - 2 * m - gap - S(300)) leftW = cw - 2 * m - gap - S(300);
    g_splitX = leftW;
    if (g_optWin) leftW = cw - 2 * m;   /* the tabs are in the options window: the list has the whole width */
    {   /* the toolbar: Refresh, Favorites Only | Play, Stop, Fullscreen | Settings, Video, Controls, Combos */
        int bx = m, ty = m, bw = S(38), bh2 = S(32), g2 = S(2), sp = S(16);
        place(W.tbRefresh, bx, ty, bw, bh2); bx += bw + g2;
        place(W.tbFav, bx, ty, bw, bh2); bx += bw + g2;
        place(W.tbAvail, bx, ty, bw, bh2); bx += bw;
        place(W.tbSep1, bx, ty, sp, bh2); bx += sp;
        place(W.playBtn, bx, ty, bw, bh2); bx += bw + g2;
        place(W.stopBtn, bx, ty, bw, bh2); bx += bw + g2;
        place(W.tbFs, bx, ty, bw, bh2); bx += bw;
        place(W.tbSep2, bx, ty, sp, bh2); bx += sp;
        place(W.tbSet, bx, ty, bw, bh2); bx += bw + g2;
        place(W.tbVideo, bx, ty, bw, bh2); bx += bw + g2;
        place(W.tbAudio, bx, ty, bw, bh2); bx += bw + g2;
        place(W.tbCtl, bx, ty, bw, bh2); bx += bw + g2;
        place(W.tbCmb, bx, ty, bw, bh2);
    }
    x = m; y = m + (g_set.hideToolbar ? 0 : S(40));
    /* left: search row, list, buttons */
    ly = y;   /* top of the list: below the search bar and the filters bar when they are shown */
    if (SEARCH_SHOWN()) {
        place(W.lblSearch, x, ly + S(4), S(52), S(18));
        place(W.search, x + S(54), ly, leftW - S(54) - S(96), S(24));
        place(W.clearBtn, x + leftW - S(90), ly - 1, S(90), S(26));
        ly += S(32);
    }
    if (!g_set.hideFilters) {
        int cw = (leftW - S(96) - 2 * S(6)) / 3;
        place(W.cbRegion, x, ly, cw, S(220));
        place(W.cbMaker, x + cw + S(6), ly, cw, S(220));
        place(W.cbGenre, x + 2 * (cw + S(6)), ly, cw, S(220));
        place(W.sortBtn, x + leftW - S(90), ly - S(1), S(90), S(26));   /* Reset Filters */
        ly += S(30);
    }
    place(W.lv, x, ly, leftW, ch - m - ly);
    /* right: tabs, default / save buttons (in the options window instead: layout_opt) */
    if (!g_optWin) {
        rightX = x + leftW + gap;
        rightW = cw - m - rightX;
        /* the toolbar is over the game list only: the tabs have the whole height, Save Settings is at the bottom */
        layout_tabs(rightX, m, rightW, ch - 2 * m - bh - S(6), rightX + rightW - S(120), ch - m - bh);
    }
}

/* the options window: the tabs fill it */
static void layout_opt(void) {
    RECT rc;
    int m = S(8), bh = S(26);
    if (!g_opt) return;
    GetClientRect(g_opt, &rc);
    layout_tabs(m, m, rc.right - 2 * m, rc.bottom - 2 * m - bh - S(6), rc.right - m - S(120), rc.bottom - m - bh);
}

/* ---------------------------------------------------------------- creating the window contents */
#define TIP(h, key) set_tip(W.tip, GetParent(h), h, tip_for(key))

static HWND add_label(HWND p, const wchar_t *t) { HWND l = mk(p, L"STATIC", t, SS_LEFT, 0, 0); TIP(l, t); return l; }

static HWND add_combo(HWND p, PRow *rows, int *n, const wchar_t *label, int id, const wchar_t **items, int ni) {
    HWND l = add_label(p, label), c = mk(p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, id);
    if (items) { combo_fill(c, items, ni); SendMessageW(c, CB_SETCURSEL, 0, 0); }
    TIP(c, label);
    rows[*n].lbl = l; rows[*n].ctl = c; rows[*n].kind = 0; (*n)++;
    return c;
}
static HWND add_edit(HWND p, PRow *rows, int *n, const wchar_t *label, int id, int numeric) {
    HWND l = add_label(p, label), e = mk(p, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP | (numeric ? ES_NUMBER : 0), WS_EX_CLIENTEDGE, id);
    TIP(e, label);
    rows[*n].lbl = l; rows[*n].ctl = e; rows[*n].kind = 1; (*n)++;
    return e;
}
static HWND add_check(HWND p, PRow *rows, int *n, const wchar_t *label, int id) {
    HWND c = mk(p, L"BUTTON", label, BS_AUTOCHECKBOX | WS_TABSTOP, 0, id);
    TIP(c, label);
    rows[*n].lbl = NULL; rows[*n].ctl = c; rows[*n].kind = 2; (*n)++;
    return c;
}

/* a title of a group of settings, with a line to the right of it */
static void add_header(HWND p, PRow *rows, int *n, const wchar_t *text) {
    HWND l = mk(p, L"STATIC", text, SS_LEFT, 0, 0), ln = mk(p, L"STATIC", L"", SS_LEFT, 0, 0);
    SetPropW(ln, L"hline", (HANDLE)1);   /* a thin line: painted in a grey of the theme (see page_proc) */
    rows[*n].lbl = l; rows[*n].ctl = NULL; rows[*n].btn = ln; rows[*n].clr = NULL; rows[*n].kind = 5; (*n)++;
}

static LRESULT CALLBACK page_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: return SendMessageW(g_main, WM_COMMAND, w, l);
    case WM_NOTIFY: return SendMessageW(g_main, WM_NOTIFY, w, l);
    /* the tab body is plain window colour (white) in current Windows versions: pages and their labels use the same colour */
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, th_page_brush());
        return 1;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (g_dark) return (LRESULT)th_ctlcolor(m, (HDC)w, (HWND)l, 1);
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (m == WM_CTLCOLORSTATIC && GetPropW((HWND)l, L"hline")) {   /* the line of a group title */
            static HBRUSH bd, bl;
            if (!bd) { bd = CreateSolidBrush(RGB(80, 80, 80)); bl = CreateSolidBrush(RGB(200, 200, 200)); }
            return (LRESULT)(g_dark ? bd : bl);
        }
        if (g_dark) return (LRESULT)th_ctlcolor(m, (HDC)w, (HWND)l, 1);
        if (m == WM_CTLCOLORSTATIC && (GetPropW((HWND)l, L"dim") || !IsWindowEnabled((HWND)l))) SetTextColor((HDC)w, GetSysColor(COLOR_GRAYTEXT));   /* a label of a control that does not apply */
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    return DefDlgProcW(h, m, w, l);
}

static int on_screen(int x, int y, int w, int h);
static void select_tab(void);

/* ---------------------------------------------------------------- the options window */
static void remember_opt_rect(void) {
    RECT r;
    if (g_opt && IsWindowVisible(g_opt) && !IsIconic(g_opt) && GetWindowRect(g_opt, &r) && r.right - r.left > 200 && r.bottom - r.top > 200) {
        g_optRect.x = r.left; g_optRect.y = r.top; g_optRect.w = r.right - r.left; g_optRect.h = r.bottom - r.top;
    }
}

static LRESULT CALLBACK opt_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: return SendMessageW(g_main, WM_COMMAND, w, l);
    case WM_NOTIFY: return SendMessageW(g_main, WM_NOTIFY, w, l);
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) { layout_opt(); RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN); }
        return 0;
    case WM_EXITSIZEMOVE: remember_opt_rect(); return 0;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, th_face_brush());
        return 1;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN: case WM_CTLCOLORLISTBOX:
        if (g_dark) return (LRESULT)th_ctlcolor(m, (HDC)w, (HWND)l, 0);
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mi = (MINMAXINFO *)l;
        mi->ptMinTrackSize.x = S(460);
        mi->ptMinTrackSize.y = S(480);
        return 0;
    }
    case WM_CLOSE:   /* the window is only hidden: the next "Options" shows it again, with the same state */
        hide_opt(1);
        return 0;
    }
    return DefDlgProcW(h, m, w, l);
}

static void update_opt_menu(void) {   /* the Settings Layout box of the General tab shows the layout; Options > Settings… needs the separate window */
    if (g_optsMenu) EnableMenuItem(g_optsMenu, ID_M_OPTWIN, MF_BYCOMMAND | (g_optWin ? MF_ENABLED : MF_GRAYED));
    if (W.chClassic && ctl_checked(W.chClassic) == (g_optWin != 0)) ctl_set_checked(W.chClassic, !g_optWin);
}

/* the layout of the options: tabs in the main window (classic) or in a window of their own. The controls move, their state stays. */
/* the settings window is shown in the middle of the screen (when it was not open) */
/* the settings window of its own is modal: while it is open the main window cannot be clicked */
static void sync_main_enable(void) {
    int open = g_optWin && g_opt && IsWindowVisible(g_opt) && !IsIconic(g_opt);
    if ((IsWindowEnabled(g_main) != 0) == !open) return;
    EnableWindow(g_main, !open);
}

/* hide the settings window: the main window is enabled first, so Windows hands the activation to it (and not to another program) */
static void hide_opt(int activateMain) {
    if (!g_opt) return;
    remember_opt_rect();
    EnableWindow(g_main, TRUE);
    ShowWindow(g_opt, SW_HIDE);
    if (activateMain && !IsIconic(g_main)) SetForegroundWindow(g_main);
}

HWND ui_owner(HWND o) { return (o == g_main && g_optWin && g_opt && IsWindowVisible(g_opt)) ? g_opt : o; }   /* dialogs opened from the settings window belong to it */

static void show_opt(void) {
    if (!IsWindowVisible(g_opt)) {
        RECT wa, wr;
        int w, h;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        GetWindowRect(g_opt, &wr);
        w = wr.right - wr.left; h = wr.bottom - wr.top;
        if (!IsIconic(g_opt)) SetWindowPos(g_opt, NULL, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(g_opt, IsIconic(g_opt) ? SW_RESTORE : SW_SHOW);
    sync_main_enable();
    SetForegroundWindow(g_opt);
}

static void set_optmode(int on, int show) {
    HWND par;
    int i;
    on = on != 0;
    if (on && !g_opt) {
        RECT wa;
        int w = S(560), h = S(800), x, y;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
        if (w > wa.right - wa.left) w = wa.right - wa.left;
        x = wa.left + (wa.right - wa.left - w) / 2; y = wa.top + (wa.bottom - wa.top - h) / 2;
        if (g_optRect.w > 0 && on_screen(g_optRect.x, g_optRect.y, g_optRect.w, g_optRect.h)) { x = g_optRect.x; y = g_optRect.y; w = g_optRect.w; h = g_optRect.h; }
        g_opt = CreateWindowExW(0, L"ZOptions", L"Settings", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, g_main, NULL, g_inst, NULL);
        if (!g_opt) return;
        th_window(g_opt);
    }
    g_optWin = on;
    g_set.optWindow = on;
    par = on ? g_opt : g_main;
    SetParent(W.tab, par);
    for (i = 0; i < NPAGES; i++) SetParent(W.page[i], par);   /* the pages after the tab: they are on top of it */
    SetParent(W.saveBtn, par);
    SetParent(W.cancelBtn, par);
    SetParent(W.defBtn, par);
    if (on) { layout_opt(); if (show) show_opt(); }
    else if (g_opt) hide_opt(0);
    do_layout();
    select_tab();
    RedrawWindow(g_main, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    if (on) RedrawWindow(g_opt, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    update_opt_menu();
}

static void create_pages(void) {
    static const wchar_t *rot[] = {L"Game Default", L"None", L"90°", L"180°", L"270°"};
    static const wchar_t *scale[] = {L"Auto", L"1x (Console Resolution)", L"2x", L"3x", L"4x"};
    static const wchar_t *offon[] = {L"Off", L"On"};
    static const wchar_t *overscan[] = {L"Off", L"4 Pixels", L"8 Pixels", L"12 Pixels", L"16 Pixels", L"24 Pixels", L"32 Pixels"};
    static const wchar_t *aspect[] = {L"Stretch To Window", L"4:3", L"16:9", L"Pixel Perfect"};
    static const wchar_t *fsmode[] = {L"Fullscreen", L"Borderless"};
    static const wchar_t *dedith[] = {L"Off", L"Low", L"Medium", L"High"};
    static const wchar_t *xbrz[] = {L"Off", L"All Graphics", L"2D Objects Only"};
    static const wchar_t *depth[] = {L"16-Bit", L"32-Bit"};
    static const wchar_t *scan[] = {L"Off", L"Black", L"Bright"};
    static const wchar_t *filt[] = {L"0 - None (Sharp)", L"1 - Standard", L"2 - Extended", L"3 - Standard, No Sprite Smoothing"};
    static const wchar_t *tex[] = {L"Default (Driver)", L"4-Bit (Fastest, Lowest Quality)", L"5-Bit (16-Bit Colour)", L"8-Bit (Best Quality)"};
    static const wchar_t *cache[] = {L"0", L"1", L"2"};
    static const wchar_t *blend[] = {L"0", L"1", L"2 (D3D)"};
    HWND p;
    wchar_t **resl;
    int i, n;
    for (i = 0; i < NPAGES; i++) {
        W.page[i] = CreateWindowExW(0, L"ZPage", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (i == 0 ? WS_VISIBLE : 0), 0, 0, 10, 10, g_main, NULL, g_inst, NULL);
        BringWindowToTop(W.page[i]);
    }
    /* Video */
    p = W.page[PG_VIDEO];
    W.cbRenderer = add_combo(p, W.vrows, &W.nv, L"Renderer", ID_V_RENDERER, NULL, 0);
    resl = (wchar_t **)malloc((g_nmodes + 1) * sizeof(wchar_t *));
    for (i = 0; i < g_nmodes; i++) {
        wchar_t t[40];
        swprintf(t, 40, L"%d x %d%ls", g_modes[i].w, g_modes[i].h, g_modes[i].w * 3 == g_modes[i].h * 4 ? L" (4:3)" : L"");   /* the 4:3 modes show the picture undistorted */
        resl[i] = wdup(t);
    }
    W.cbRes = add_combo(p, W.vrows, &W.nv, L"Resolution", ID_V_RES, (const wchar_t **)resl, g_nmodes);
    W.cbRot = add_combo(p, W.vrows, &W.nv, L"Rotation", ID_V_ROT, rot, 5);
    W.cbScan = add_combo(p, W.vrows, &W.nv, L"Scanlines", ID_V_SCAN, scan, 3);
    W.eFPS = add_edit(p, W.vrows, &W.nv, L"Manual Framerate", ID_V_FPS, 1);
    W.lblFPS = W.vrows[W.nv - 1].lbl;
    W.chFull = add_check(p, W.vrows, &W.nv, L"Fullscreen", ID_V_FULL);
    W.chFPS = add_check(p, W.vrows, &W.nv, L"Show FPS", ID_V_SHOWFPS);
    W.chLimit = add_check(p, W.vrows, &W.nv, L"Frame Limiter", ID_V_LIMIT);
    W.chSkip = add_check(p, W.vrows, &W.nv, L"Frame Skipping", ID_V_SKIP);
    W.chAuto = add_check(p, W.vrows, &W.nv, L"Auto-Detect Framerate", ID_V_AUTO);
    W.chSlow = add_check(p, W.vrows, &W.nv, L"Slow Geometry", ID_S_SLOW);
    add_header(p, W.vrows, &W.nv, L"Direct3D 11");
    W.cbFsMode = add_combo(p, W.vrows, &W.nv, L"Fullscreen Mode", ID_V_FSMODE, fsmode, 2);
    W.lblFsMode = W.vrows[W.nv - 1].lbl;
    W.cbAspect = add_combo(p, W.vrows, &W.nv, L"Aspect Ratio", ID_V_ASPECT, aspect, 4);
    W.lblAspect = W.vrows[W.nv - 1].lbl;
    W.cbScale = add_combo(p, W.vrows, &W.nv, L"Internal Resolution", ID_V_SCALE, scale, 5);
    W.lblScale = W.vrows[W.nv - 1].lbl;
    W.cbXbrz = add_combo(p, W.vrows, &W.nv, L"xBRZ Filter", ID_V_XBRZ, xbrz, 3);
    W.lblXbrz = W.vrows[W.nv - 1].lbl;
    W.cbTexSm = add_combo(p, W.vrows, &W.nv, L"3D Texture Smoothing", ID_V_TEXSMOOTH, offon, 2);
    W.lblTexSm = W.vrows[W.nv - 1].lbl;
    W.cbFxaa = add_combo(p, W.vrows, &W.nv, L"FXAA Filter", ID_V_FXAA, offon, 2);
    W.lblFxaa = W.vrows[W.nv - 1].lbl;
    W.cbDedith = add_combo(p, W.vrows, &W.nv, L"Dither Smoothing", ID_V_DEDITHER, dedith, 4);
    W.lblDedith = W.vrows[W.nv - 1].lbl;
    W.cbVsync = add_combo(p, W.vrows, &W.nv, L"V-Sync", ID_V_VSYNC, offon, 2);
    W.lblVsync = W.vrows[W.nv - 1].lbl;
    W.cbOverscan = add_combo(p, W.vrows, &W.nv, L"Overscan Crop", ID_V_OVERSCAN, overscan, 7);
    W.lblOverscan = W.vrows[W.nv - 1].lbl;
    add_header(p, W.vrows, &W.nv, L"OpenGL / Direct3D 6");
    W.cbDepth = add_combo(p, W.vrows, &W.nv, L"Color Depth", ID_V_DEPTH, depth, 2);
    W.lblDepth = W.vrows[W.nv - 1].lbl;
    W.cbFilter = add_combo(p, W.vrows, &W.nv, L"Texture Filtering", ID_V_FILTER, filt, 4);
    W.lblFilter = W.vrows[W.nv - 1].lbl;
    W.cbTexType = add_combo(p, W.vrows, &W.nv, L"Texture Quality", ID_V_TEXTYPE, tex, 4);
    W.lblTexType = W.vrows[W.nv - 1].lbl;
    W.cbTexCache = add_combo(p, W.vrows, &W.nv, L"Texture Caching", ID_V_TEXCACHE, cache, 3);
    W.lblTexCache = W.vrows[W.nv - 1].lbl;
    W.cbBlend = add_combo(p, W.vrows, &W.nv, L"Color Blending", ID_V_BLEND, blend, 3);
    W.lblBlend = W.vrows[W.nv - 1].lbl;
    W.chDither = add_check(p, W.vrows, &W.nv, L"Dithering", ID_V_DITHER);
    for (i = 0; i < g_nmodes; i++) free(resl[i]);
    free(resl);
    /* Audio */
    p = W.page[PG_AUDIO];
    W.chSound = add_check(p, W.arows, &W.na, L"Enable Sound", ID_A_SOUND);
    W.chFilt = add_check(p, W.arows, &W.na, L"Sound Filter", ID_A_FILTER);
    W.eCutoff = add_edit(p, W.arows, &W.na, L"Filter Cutoff (Hz)", ID_A_CUTOFF, 1);
    W.chSurr = add_check(p, W.arows, &W.na, L"Lite Surround", ID_A_SURR);
    W.eSurround = add_edit(p, W.arows, &W.na, L"Surround Strength", ID_A_SURRMUL, 1);
    W.chExc = add_check(p, W.arows, &W.na, L"Stereo Exciter", ID_A_EXC);
    /* Controls, Combos */
    ctl_create(W.page[PG_CONTROLS], W.tip);
    cmb_create(W.page[PG_COMBOS], W.tip);
    /* General */
    p = W.page[PG_SYSTEM];
    W.eRoms = add_edit(p, W.srows, &W.ns, L"ROMs Folder", ID_S_ROMS, 0);
    W.lblRoms = W.srows[0].lbl;
    W.bBrowse = mk(p, L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_S_BROWSE);
    TIP(W.bBrowse, L"Browse…");
    W.bRomClear = mk(p, L"BUTTON", L"X", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_S_ROMCLEAR);
    TIP(W.bRomClear, L"Clear ROMs Folder");
    W.srows[0].btn = W.bBrowse; W.srows[0].clr = W.bRomClear;
    W.chAvail = add_check(p, W.srows, &W.ns, L"List Only Available ROMs", ID_S_AVAIL);
    W.eTrainer = add_edit(p, W.srows, &W.ns, L"Trainer Program", ID_S_TRAINER, 0);
    W.bTrBrowse = mk(p, L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_S_TRBROWSE);
    TIP(W.bTrBrowse, L"Browse Trainer");
    W.bTrClear = mk(p, L"BUTTON", L"X", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_S_TRCLEAR);
    TIP(W.bTrClear, L"Clear Trainer Program");
    W.srows[W.ns - 1].btn = W.bTrBrowse; W.srows[W.ns - 1].clr = W.bTrClear;
    W.chTrainer = add_check(p, W.srows, &W.ns, L"Start Trainer With Games", ID_S_TRON);
    W.eBezel = add_edit(p, W.srows, &W.ns, L"Bezel Image (D3D11)", ID_S_BEZEL, 0);
    W.bBezBrowse = mk(p, L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_S_BEZBROWSE);
    TIP(W.bBezBrowse, L"Browse Bezel Image");
    W.bBezClear = mk(p, L"BUTTON", L"X", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_S_BEZCLEAR);
    TIP(W.bBezClear, L"Clear Bezel Image");
    W.srows[W.ns - 1].btn = W.bBezBrowse; W.srows[W.ns - 1].clr = W.bBezClear;
    {
        static const wchar_t *themes[] = {L"System", L"Light", L"Dark"};
        W.cbTheme = add_combo(p, W.srows, &W.ns, L"Theme", ID_S_THEME, themes, 3);
    }
    {
        static const wchar_t *onstart[] = {L"Keep The Launcher Open", L"Minimize The Launcher", L"Close The Launcher"};
        W.cbOnStart = add_combo(p, W.srows, &W.ns, L"When A Game Starts", ID_S_ONSTART, onstart, 3);
    }
    W.chClassic = add_check(p, W.srows, &W.ns, L"Classic Layout", ID_S_LAYOUT);
    W.chHide = add_check(p, W.srows, &W.ns, L"Hide Console", ID_S_HIDE);
    W.chLogs = add_check(p, W.srows, &W.ns, L"Enable Logs", ID_S_LOGS);
    (void)n;
}

static void create_ui(void) {
    static const wchar_t *tabs[] = {L"General", L"Video", L"Audio", L"Controls", L"Combos"};
    static const wchar_t *cols[] = {L"Favorite", L"#", L"Game", L"Info", L"Status", L"Hardware", L"Year"};
    static const int widths[] = {64, 40, 260, 140, 40, 170, 40};
    TCITEMW ti;
    int i;
    W.tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, g_main, NULL, g_inst, NULL);
    SendMessageW(W.tip, TTM_SETMAXTIPWIDTH, 0, S(420));
    SendMessageW(W.tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    W.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"Ready", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_main, (HMENU)1, g_inst, NULL);
    th_status(W.status);
    SendMessageW(W.status, WM_SETFONT, (WPARAM)g_font, TRUE);
    W.lblSearch = mk(g_main, L"STATIC", L"Search:", SS_LEFT, 0, 0);
    W.search = mk(g_main, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_SEARCH);
    W.sortBtn = mk(g_main, L"BUTTON", L"Reset Filters", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_RESETSORT);
    W.clearBtn = mk(g_main, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_CLEARSEARCH);
    W.cbRegion = mk(g_main, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_F_REGION);
    W.cbMaker = mk(g_main, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_F_MAKER);
    W.cbGenre = mk(g_main, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_F_GENRE);
    SendMessageW(W.cbRegion, CB_ADDSTRING, 0, (LPARAM)L"All Regions"); SendMessageW(W.cbRegion, CB_SETCURSEL, 0, 0);
    SendMessageW(W.cbMaker, CB_ADDSTRING, 0, (LPARAM)L"All Makers"); SendMessageW(W.cbMaker, CB_SETCURSEL, 0, 0);
    SendMessageW(W.cbGenre, CB_ADDSTRING, 0, (LPARAM)L"All Genres"); SendMessageW(W.cbGenre, CB_SETCURSEL, 0, 0);
    set_tip(W.tip, g_main, W.sortBtn, tip_for(L"Reset Filters"));
    set_tip(W.tip, g_main, W.defBtn, tip_for(L"Restore Defaults"));
    set_tip(W.tip, g_main, W.cbRegion, tip_for(L"Region Filter"));
    set_tip(W.tip, g_main, W.cbMaker, tip_for(L"Maker Filter"));
    set_tip(W.tip, g_main, W.cbGenre, tip_for(L"Genre Filter"));
    W.lv = lv_create(g_main, ID_GAMELIST, cols, widths, NCOLS);
    SetWindowSubclass(ListView_GetHeader(W.lv), colhdr_proc, 77, 0);
    SetWindowSubclass(W.lv, lvhdr_proc, 78, 0);
    ListView_SetExtendedListViewStyle(W.lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES | LVS_EX_SUBITEMIMAGES);
    ListView_SetColumnOrderArray(W.lv, NCOLS, (int *)kColOrder);
    ListView_SetImageList(W.lv, tb_status_images(S(16)), LVSIL_SMALL);
    ListView_SetImageList(W.lv, tb_star_images(S(16), th_gray(), th_text()), LVSIL_STATE);   /* the check box of a favorite is a star */
    /* the state holders of Favorites Only and Create Desktop Icon stay (hidden); the toolbar has the visible buttons */
    W.favOnly = mk(g_main, L"BUTTON", L"Favorites Only", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_FAVONLY);
    W.iconBtn = mk(g_main, L"BUTTON", L"Create Desktop Icon", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, 0, ID_ICONBTN);
    ShowWindow(W.favOnly, SW_HIDE); ShowWindow(W.iconBtn, SW_HIDE);
    W.tbRefresh = tb_button(g_main, ID_M_RESCAN, TI_REFRESH);
    W.tbFav = tb_button(g_main, ID_TBFAV, TI_FAV);
    W.tbAvail = tb_button(g_main, ID_TBAVAIL, TI_AVAIL);
    W.tbSep1 = tb_separator(g_main);
    W.playBtn = tb_button(g_main, ID_PLAYBTN, TI_PLAY);
    W.stopBtn = tb_button(g_main, ID_STOPBTN, TI_STOP);
    EnableWindow(W.stopBtn, FALSE);
    W.tbFs = tb_button(g_main, ID_TBFS, TI_FULLSCREEN);
    W.tbSep2 = tb_separator(g_main);
    W.tbSet = tb_button(g_main, ID_TBSET, TI_SETTINGS);
    W.tbVideo = tb_button(g_main, ID_TBVIDEO, TI_VIDEO);
    W.tbAudio = tb_button(g_main, ID_TBAUDIO, TI_AUDIO);
    W.tbCtl = tb_button(g_main, ID_TBCTL, TI_CONTROLS);
    W.tbCmb = tb_button(g_main, ID_TBCMB, TI_COMBOS);
    set_tip(W.tip, g_main, W.tbRefresh, L"Refresh (F5)");
    set_tip(W.tip, g_main, W.tbFav, L"Favorites Only");
    set_tip(W.tip, g_main, W.tbAvail, L"List Only Available ROMs");
    set_tip(W.tip, g_main, W.playBtn, L"Play");
    set_tip(W.tip, g_main, W.stopBtn, L"Stop");
    set_tip(W.tip, g_main, W.tbFs, L"Fullscreen (on/off for the games you start)");
    set_tip(W.tip, g_main, W.tbSet, L"Settings");
    set_tip(W.tip, g_main, W.tbVideo, L"Video Settings");
    set_tip(W.tip, g_main, W.tbAudio, L"Audio Settings");
    set_tip(W.tip, g_main, W.tbCtl, L"Controls Settings");
    set_tip(W.tip, g_main, W.tbCmb, L"Combos Settings");
    W.tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 10, 10, g_main, (HMENU)ID_TAB, g_inst, NULL);
    SendMessageW(W.tab, WM_SETFONT, (WPARAM)g_font, TRUE);
    th_tab(W.tab);
    memset(&ti, 0, sizeof ti);
    for (i = 0; i < NPAGES; i++) { ti.mask = TCIF_TEXT; ti.pszText = (LPWSTR)tabs[i]; TabCtrl_InsertItem(W.tab, i, &ti); }
    W.saveBtn = mk(g_main, L"BUTTON", L"OK", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_SAVEBTN);
    W.cancelBtn = mk(g_main, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_CANCELBTN);
    W.defBtn = mk(g_main, L"BUTTON", L"Restore Defaults", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_DEFAULTBTN);
    create_pages();
}

static HMENU build_menu(void) {
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), game = CreatePopupMenu(), set = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_M_RESCAN, L"&Rescan Games\tF5");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, ID_M_OPENZINC, L"Open &ZiNc Folder");
    AppendMenuW(file, MF_STRING, ID_M_OPENROMS, L"Open &ROMs Folder");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    g_recentMenu = CreatePopupMenu();
    AppendMenuW(file, MF_POPUP, (UINT_PTR)g_recentMenu, L"Recent &Files");
    AppendMenuW(file, MF_STRING, ID_M_CLEARRECENT, L"&Clear Recent Files");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, ID_M_OPENSNAP, L"Open &Screenshots Folder");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, ID_M_BACKUP, L"&Backup Settings…");
    AppendMenuW(file, MF_STRING, ID_M_RESTORE, L"Res&tore Settings…");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, ID_M_EXIT, L"E&xit");
    AppendMenuW(game, MF_STRING, ID_M_PLAY, L"&Play\tEnter");
    AppendMenuW(game, MF_STRING, ID_M_STOP, L"&Stop");
    AppendMenuW(game, MF_STRING, ID_M_PLAYTRAINER, L"Play With &Trainer");
    AppendMenuW(game, MF_STRING, ID_M_PLAYFS, L"Play &Fullscreen");
    AppendMenuW(game, MF_STRING, ID_M_PLAYWIN, L"Play &Windowed");
    g_playWithMenu = CreatePopupMenu();
    AppendMenuW(game, MF_POPUP, (UINT_PTR)g_playWithMenu, L"Play &With");
    AppendMenuW(game, MF_SEPARATOR, 0, NULL);
    AppendMenuW(game, MF_STRING, ID_M_GAMECFG, L"Game S&ettings…");
    AppendMenuW(game, MF_STRING, ID_M_CHECKROMS, L"Check &ROM Set…");
    AppendMenuW(game, MF_STRING, ID_M_TOGGLEFAV, L"Toggle &Favorite\tCtrl+D");
    AppendMenuW(game, MF_STRING, ID_ICONBTN, L"Create Desktop &Icon");
    AppendMenuW(set, MF_STRING, ID_M_OPTWIN, L"Se&ttings…\tF9");   /* the settings window: when the tabs are in it */
    AppendMenuW(set, MF_SEPARATOR, 0, NULL);
    AppendMenuW(set, MF_STRING, ID_TBVIDEO, L"&Video Settings");
    AppendMenuW(set, MF_STRING, ID_M_SETAUDIO, L"&Audio Settings");
    AppendMenuW(set, MF_STRING, ID_TBCTL, L"&Controls Settings");
    AppendMenuW(set, MF_STRING, ID_TBCMB, L"C&ombos Settings");
    AppendMenuW(help, MF_STRING, ID_M_KEYS, L"&Keyboard Shortcuts");
    AppendMenuW(help, MF_SEPARATOR, 0, NULL);
    AppendMenuW(help, MF_STRING, ID_M_ABOUT, L"&About");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)game, L"&Game");
    g_optsMenu = set;
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)set, L"&Options");
    {   /* View: theme, what the game list shows */
        HMENU view = CreatePopupMenu(), theme = CreatePopupMenu();
        AppendMenuW(theme, MF_STRING, ID_M_THEME0, L"&System");
        AppendMenuW(theme, MF_STRING, ID_M_THEME1, L"&Light");
        AppendMenuW(theme, MF_STRING, ID_M_THEME2, L"&Dark");
        AppendMenuW(view, MF_POPUP, (UINT_PTR)theme, L"&Theme");
        AppendMenuW(view, MF_SEPARATOR, 0, NULL);
        AppendMenuW(view, MF_STRING, ID_M_FAVONLY, L"&Favorites Only");
        AppendMenuW(view, MF_STRING, ID_M_AVAIL, L"List Only &Available ROMs");
        AppendMenuW(view, MF_SEPARATOR, 0, NULL);
        {   /* the columns of the game list */
            HMENU cols = CreatePopupMenu();
            static const wchar_t *const cn[NCOLS] = {L"&Favorite", L"&#", L"&Game", L"&Info", L"&Status", L"&Hardware", L"&Year"};
            int k;
            for (k = 0; k < NCOLS; k++) AppendMenuW(cols, MF_STRING | (kColOrder[k] == 2 ? MF_GRAYED : 0), ID_COL0 + kColOrder[k], cn[kColOrder[k]]);
            AppendMenuW(view, MF_POPUP, (UINT_PTR)cols, L"List &Columns");
            g_colsMenu = cols;
        }
        AppendMenuW(view, MF_SEPARATOR, 0, NULL);
        AppendMenuW(view, MF_STRING, ID_M_SHOWTOOLBAR, L"Show Tool&bar");
        AppendMenuW(view, MF_STRING, ID_M_SHOWSEARCH, L"Show &Search Bar");
        AppendMenuW(view, MF_STRING, ID_M_SHOWFILTERS, L"Show Filter Ba&r");
        AppendMenuW(view, MF_SEPARATOR, 0, NULL);
        AppendMenuW(view, MF_STRING, ID_M_LAY0, L"Classic &Layout");   /* checked: the settings are in the main window; unchecked: in a window of their own */
        AppendMenuW(view, MF_STRING, ID_M_RESETWIN, L"&Reset Window Size");
        g_viewMenu = view; g_themeMenu = theme;
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    }
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

/* ---------------------------------------------------------------- window procedure */
static void select_tab(void) {
    int i, s = TabCtrl_GetCurSel(W.tab);
    for (i = 0; i < NPAGES; i++) ShowWindow(W.page[i], i == s ? SW_SHOW : SW_HIDE);
}

/* the toolbar: a tab of the settings (the settings window comes to the front when the tabs are in it) */
static void show_settings(int page) {
    TabCtrl_SetCurSel(W.tab, page);
    select_tab();
    if (g_optWin && g_opt) show_opt();
}

/* a new theme (View > Theme, or the Theme box of the General tab with OK): saved, applied at the next start */
static void apply_theme_mode(int mode) {
    if (mode == g_set.themeMode) return;
    g_set.themeMode = mode;
    set_combo(W.cbTheme, mode);
    settings_resolve_theme();
    settings_save();
    if (g_set.dark != g_dark) {
        int r = 0;
        msg_box(g_main, L"Theme", L"The theme changes after a restart.\n\nRestart ZiNc EX now?", MB_YESNO | MB_ICONQUESTION, &r);
        if (r == IDYES) {
            wchar_t self[MAX_PATH];
            if (do_save() && GetModuleFileNameW(NULL, self, MAX_PATH)) {
                stop_trainer();
                if ((INT_PTR)ShellExecuteW(g_main, NULL, self, NULL, NULL, SW_SHOWNORMAL) > 32) SendMessageW(g_main, WM_CLOSE, 0, 0);
                else set_note(L"Could not restart: start ZiNc EX again yourself");
            }
        } else set_note(L"The theme changes at the next start");
    } else set_note(L"Theme saved");
}

static void on_command(int id, int code, HWND src) {
    if (id >= ID_RECENT0 && id < ID_RECENT0 + 8) { launch_recent(id - ID_RECENT0); return; }
    if (id >= ID_PLAYREND0 && id < ID_PLAYREND0 + 8) { if (id - ID_PLAYREND0 < g_nrend) launch_once(selected_game(), -1, g_rendNames[id - ID_PLAYREND0]); return; }
    switch (id) {
    case ID_M_RESCAN: rescan(); return;
    case ID_M_OPENZINC: open_folder(g_root); return;
    case ID_M_OPENROMS: { wchar_t d[600]; roms_dir_abs(d, 600); CreateDirectoryW(d, NULL); open_folder(d); return; }
    case ID_M_CLEARRECENT: il_clear(&g_set.recent); settings_save(); rebuild_recent(); return;
    case ID_M_EXIT: SendMessageW(g_main, WM_CLOSE, 0, 0); return;
    case ID_M_PLAY: case ID_PLAYBTN: if (code == BN_CLICKED || code == 1 || id == ID_M_PLAY) launch_game(selected_game()); return;
    case ID_M_STOP: case ID_STOPBTN: if (g_running) { g_stopped = 1; TerminateProcess(g_proc.hProcess, 0); } return;
    case ID_M_TOGGLEFAV: toggle_fav(); return;
    case ID_M_PLAYTRAINER: launch_with_trainer(selected_game()); return;
    case ID_M_PLAYFS: launch_once(selected_game(), 1, NULL); return;
    case ID_M_PLAYWIN: launch_once(selected_game(), 0, NULL); return;
    case ID_M_GAMECFG: game_settings(); return;
    case ID_M_OPENSNAP: { wchar_t d[600]; path_join(d, 600, g_root, L"snap"); CreateDirectoryW(d, NULL); open_folder(d); return; }
    case ID_M_BACKUP: do_backup(); return;
    case ID_M_RESTORE: do_restore(); return;
    case ID_S_TRBROWSE: browse_trainer(); return;
    case ID_S_BEZBROWSE: browse_bezel(); return;
    case ID_S_BEZCLEAR: SetWindowTextW(W.eBezel, L""); return;
    case ID_S_ROMCLEAR: case ID_S_TRCLEAR: {
        int rom = id == ID_S_ROMCLEAR, r = 0;
        wchar_t cur[560];
        GetWindowTextW(rom ? W.eRoms : W.eTrainer, cur, 560);
        if (!cur[0]) return;
        msg_box(g_main, rom ? L"Clear ROMs Folder" : L"Clear Trainer Program", rom ? L"Clear the ROMs folder?\n\nNo game will be listed as available until you choose a folder again." : L"Clear the trainer program?\n\nGames start without a trainer until you choose one again.", MB_YESNO | MB_ICONQUESTION, &r);
        if (r != IDYES) return;
        SetWindowTextW(rom ? W.eRoms : W.eTrainer, L"");
        if (rom) { g_set.roms[0] = 0; refilter(); }
        return;
    }
    case ID_M_CHECKROMS: { const Game *g = selected_game(); if (g) dlg_check_roms(g_main, g); else msg_box(g_main, L"Check ROM Set", L"Select a game first.", MB_ICONINFORMATION, NULL); return; }
    case ID_M_THEME0: case ID_M_THEME1: case ID_M_THEME2: apply_theme_mode(id - ID_M_THEME0); return;   /* View > Theme */
    case ID_M_FAVONLY: SendMessageW(W.favOnly, BM_CLICK, 0, 0); return;
    case ID_M_SHOWTOOLBAR:
        g_set.hideToolbar = !g_set.hideToolbar; apply_bars(); do_layout(); settings_save();
        RedrawWindow(g_main, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);   /* the Save Settings button moved */
        return;
    case ID_M_SHOWSEARCH: g_set.hideSearch = SEARCH_SHOWN(); g_searchOv = -1; apply_bars(); do_layout(); settings_save(); return;   /* shown -> hidden, hidden -> shown (and remembered) */
    case ID_M_SHOWFILTERS: g_set.hideFilters = !g_set.hideFilters; apply_bars(); do_layout(); settings_save(); return;
    case ID_COL2: return;   /* the Game column always stays */
    case ID_COL0: case ID_COL1: case ID_COL3: case ID_COL4: case ID_COL5: case ID_COL6:   /* show / hide a column of the game list */
        g_set.colsHidden ^= 1 << (id - ID_COL0);
        apply_columns();
        settings_save();
        return;
    case ID_M_OPTWIN: case ID_OPTBTN:   /* F9 / Options > Settings… / the Settings button: show the settings window (only when the options are in it) */
        if (g_optWin) { set_optmode(1, 0); show_settings(PG_SYSTEM); }   /* always the General tab */
        return;
    case ID_SAVEBTN:   /* OK: save; a settings window of its own closes */
        if (do_save()) set_note(L"Settings saved");
        if (g_optWin && g_opt) hide_opt(1);
        { int mode = (int)SendMessageW(W.cbTheme, CB_GETCURSEL, 0, 0); if (mode >= 0 && mode != g_set.themeMode) apply_theme_mode(mode); }   /* the Theme box */
        return;
    case ID_CANCELBTN:   /* Cancel: the changes are dropped (the saved settings are shown again); a settings window of its own closes */
        apply_to_ui(NULL);
        if (g_optWin && g_opt) hide_opt(1);
        return;
    case ID_M_LAY0: {   /* Classic Layout on / off, from the View menu */
        int want = g_optWin ? 0 : 1;   /* in a window of their own now: back to classic; else: to a window of their own */
        ctl_set_checked(W.chClassic, !want);
        g_set.optWindow = want;
        settings_save();
        PostMessageW(g_main, WM_APP_LAYOUT, (WPARAM)want, 0);
        return;
    }
    case ID_M_RESETWIN: reset_window(0); set_note(L"Window size reset"); return;
    case ID_M_FOCUSSEARCH: if (!SEARCH_SHOWN()) { g_searchOv = 1; apply_bars(); do_layout(); }   /* only for now: the bar stays hidden at the next start */
         SetFocus(W.search); SendMessageW(W.search, EM_SETSEL, 0, -1); return;
    case ID_DEFAULTBTN: default_settings(); return;
    case ID_M_KEYS: {
        static const TableRow KEYS[] = {
            {L"IN THE LAUNCHER", NULL},
            {L"Enter / Ctrl+Enter", L"Play the selected game"}, {L"Double click", L"Play the game"},
            {L"Right click", L"Menu of the game (play options, favorite, desktop icon)"},
            {L"Ctrl+D", L"Toggle favorite"}, {L"Ctrl+F", L"Search"}, {L"F5", L"Rescan games"},
            {L"", L""},
            {L"IN A GAME", NULL}, {L"", L"the keys can be changed in the Controls tab"},
            {L"W  A  S  D", L"Player 1 Up, Left, Down, Right"},
            {L"Numpad 4", L"Player 1 Button 1"}, {L"Numpad 5", L"Player 1 Button 2"}, {L"Numpad 6", L"Player 1 Button 3"},
            {L"Numpad 1", L"Player 1 Button 4"}, {L"Numpad 2", L"Player 1 Button 5"}, {L"Numpad 3", L"Player 1 Button 6"},
            {L"Enter", L"Player 1 Start"}, {L"Right Shift", L"Player 1 Coin"},
            {L"", L""},
            {L"Arrow keys", L"Player 2 Up, Down, Left, Right"},
            {L"U", L"Player 2 Button 1"}, {L"I", L"Player 2 Button 2"}, {L"O", L"Player 2 Button 3"},
            {L"J", L"Player 2 Button 4"}, {L"K", L"Player 2 Button 5"}, {L"L", L"Player 2 Button 6"},
            {L"Y", L"Player 2 Start"}, {L"H", L"Player 2 Coin"},
            {L"", L""},
            {L"F4", L"Test switch"}, {L"F7", L"Service switch"},
            {L"Pause", L"Pause / resume"}, {L"Alt+Enter", L"Fullscreen / window"},
            {L"F5", L"Screenshot (handled by ZiNc)"}, {L"Esc", L"Quit the emulator"},
            {L"", L""},
            {L"NOTE", NULL}, {L"", L"Numpad keys need NumLock on."}};
        dlg_table(g_main, L"Keyboard Shortcuts", L"Key", L"Action", KEYS, (int)(sizeof KEYS / sizeof KEYS[0]), 140, 400);
        return;
    }
    case ID_M_ABOUT: dlg_about(g_main); return;
    case ID_SEARCH: if (code == EN_CHANGE && !g_applying && g_games) refilter(); return;
    case ID_CLEARSEARCH:   /* Close: the search is cleared and the search bar hidden for this session (Ctrl+F shows it again) */
        SetWindowTextW(W.search, L"");
        g_searchOv = 0;   /* hidden for now: the saved View > Search Bar choice is not touched */
        apply_bars(); do_layout();
        SetFocus(W.lv);
        return;
    case ID_FAVONLY: if (code == BN_CLICKED) { g_set.favOnly = ctl_checked(W.favOnly); tb_set_checked(W.tbFav, g_set.favOnly); refilter(); } return;
    case ID_TBAVAIL: case ID_M_AVAIL:   /* List Only Available ROMs: toggled directly (a click message does not reach the check box when its window is hidden) */
        ctl_set_checked(W.chAvail, !ctl_checked(W.chAvail));
        g_set.onlyAvail = ctl_checked(W.chAvail);
        refilter();
        settings_save();
        return;
    case ID_TBAUDIO: show_settings(PG_AUDIO); return;
    case ID_TBFS: {   /* the toolbar switch: the Fullscreen setting (the check box of the Video tab) on / off, saved at once */
        KVMap w = {0};
        ctl_set_checked(W.chFull, !ctl_checked(W.chFull));
        tb_set_checked(W.tbFs, ctl_checked(W.chFull));
        kv_set(&w, "FullScreen", ctl_checked(W.chFull));
        renderer_write(&w);
        set_note(ctl_checked(W.chFull) ? L"Fullscreen is on" : L"Fullscreen is off (window)");
        return;
    }
    case ID_V_FULL: if (code == BN_CLICKED) tb_set_checked(W.tbFs, ctl_checked(W.chFull)); return;
    case ID_TBFAV: SendMessageW(W.favOnly, BM_CLICK, 0, 0); return;   /* the toolbar switch clicks the hidden check box: one place for the logic */
    case ID_TBSET: show_settings(PG_SYSTEM); return;
    case ID_TBVIDEO: show_settings(PG_VIDEO); return;
    case ID_M_SETAUDIO: show_settings(PG_AUDIO); return;
    case ID_TBCTL: show_settings(PG_CONTROLS); return;
    case ID_TBCMB: show_settings(PG_COMBOS); return;
    case ID_ICONBTN: desktop_icon(); return;
    case ID_RESETSORT:   /* all filters back to "All" and the list back to its normal order */
        SendMessageW(W.cbRegion, CB_SETCURSEL, 0, 0); SendMessageW(W.cbMaker, CB_SETCURSEL, 0, 0); SendMessageW(W.cbGenre, CB_SETCURSEL, 0, 0);
        g_sortCol = 1; g_sortAsc = 1; refilter(); settings_save(); return;
    case ID_F_REGION: case ID_F_MAKER: case ID_F_GENRE: if (code == CBN_SELCHANGE) { refilter(); settings_save(); } return;
    case ID_V_RENDERER: if (code == CBN_SELCHANGE) update_renderer(); return;
    case ID_V_AUTO: if (code == BN_CLICKED) update_renderer(); return;
    case ID_S_ROMS: if (code == EN_KILLFOCUS) refilter(); return;
    case ID_S_BROWSE: browse_roms(); return;
    case ID_S_AVAIL: if (code == BN_CLICKED) refilter(); return;
    }
    if (ctl_command(id, code)) return;
    cmb_command(id, code);
    (void)src;
}

static LRESULT on_notify(NMHDR *nh) {
    if (nh->idFrom == ID_TAB && nh->code == TCN_SELCHANGE) { select_tab(); return 0; }
    if (nh->code == NM_CUSTOMDRAW && (nh->idFrom == ID_GAMELIST || nh->idFrom == ID_C_LV || nh->idFrom == ID_K_LV))
        return lv_altrows((LPNMLVCUSTOMDRAW)nh);
    if (nh->idFrom == ID_GAMELIST) {
        switch (nh->code) {
        case LVN_ITEMCHANGED: {
            NMLISTVIEW *n = (NMLISTVIEW *)nh;
            if (g_rebuilding || !(n->uChanged & LVIF_STATE)) return 0;
            if ((n->uNewState ^ n->uOldState) & LVIS_STATEIMAGEMASK) {
                int on = ((n->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
                if (n->iItem >= 0 && n->iItem < g_nview) {
                    set_favorite(g_games[g_view[n->iItem]].id, on);
                    if (ctl_checked(W.favOnly)) PostMessageW(g_main, WM_APP_REFILTER, 0, 0);   /* the list is rebuilt once this click is done */
                }
            }
            if ((n->uNewState ^ n->uOldState) & LVIS_SELECTED) { update_buttons(); selection_status(); }
            return 0;
        }
        case LVN_COLUMNCLICK: {
            int c = ((NMLISTVIEW *)nh)->iSubItem;
            if (c == 3) return 0;   /* the Info column is not sorted */
            if (c == g_sortCol) g_sortAsc = !g_sortAsc; else { g_sortCol = c; g_sortAsc = 1; }
            refilter();
            settings_save();   /* the sort order is remembered at once, whatever way the program is left */
            return 0;
        }
        case NM_DBLCLK: {   /* double-click on a game plays it (not on its check box) */
            LVHITTESTINFO ht;
            POINT pt;
            memset(&ht, 0, sizeof ht);
            GetCursorPos(&pt);
            ScreenToClient(W.lv, &pt);
            ht.pt = pt;
            ListView_HitTest(W.lv, &ht);
            if (ht.iItem >= 0 && !((ht.flags & LVHT_ONITEMSTATEICON) && !(ht.flags & LVHT_ONITEMLABEL))) launch_game(selected_game());
            return 0;
        }
        case NM_RCLICK: {   /* right-click menu of the game under the mouse */
            LVHITTESTINFO ht;
            LPNMITEMACTIVATE ia = (LPNMITEMACTIVATE)nh;
            const Game *g;
            HMENU pm;
            POINT pt;
            memset(&ht, 0, sizeof ht);
            ht.pt = ia->ptAction;
            ListView_HitTest(W.lv, &ht);
            if (ht.iItem < 0) return 0;
            ListView_SetItemState(W.lv, ht.iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            g = selected_game();
            if (!g) return 0;
            pm = CreatePopupMenu();
            AppendMenuW(pm, MF_STRING | (playable(g) ? 0 : MF_GRAYED), ID_M_PLAY, L"Play");
            AppendMenuW(pm, MF_STRING | (playable(g) ? 0 : MF_GRAYED), ID_M_PLAYTRAINER, L"Play With Trainer");
            AppendMenuW(pm, MF_STRING | (playable(g) ? 0 : MF_GRAYED), ID_M_PLAYFS, L"Play Fullscreen");
            AppendMenuW(pm, MF_STRING | (playable(g) ? 0 : MF_GRAYED), ID_M_PLAYWIN, L"Play Windowed");
            {
                HMENU sub = CreatePopupMenu();
                int ri;
                for (ri = 0; ri < g_nrend && ri < 8; ri++) AppendMenuW(sub, MF_STRING, ID_PLAYREND0 + ri, g_rendNames[ri]);
                AppendMenuW(pm, MF_POPUP | (playable(g) ? 0 : MF_GRAYED), (UINT_PTR)sub, L"Play With");   /* the submenu goes away with its parent */
            }
            AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
            AppendMenuW(pm, MF_STRING, ID_M_GAMECFG, gamecfg_find(g->id) ? L"Game Settings… (Own Settings Set)" : L"Game Settings…");
            AppendMenuW(pm, MF_STRING, ID_M_CHECKROMS, L"Check ROM Set…");
            AppendMenuW(pm, MF_STRING, ID_ICONBTN, L"Create Desktop Icon");
            AppendMenuW(pm, MF_STRING, ID_M_TOGGLEFAV, il_has(&g_set.fav, g->id) ? L"Remove From Favorite" : L"Add To Favorite");
            GetCursorPos(&pt);
            TrackPopupMenu(pm, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, NULL);
            DestroyMenu(pm);
            return 0;
        }
        case LVN_KEYDOWN:
            if (((NMLVKEYDOWN *)nh)->wVKey == VK_RETURN) launch_game(selected_game());
            return 0;
        }
        return 0;
    }
    if (ctl_notify(nh)) return 0;
    if (cmb_notify(nh)) return 0;
    return 0;
}

static void remember_opt_rect(void);

static void remember_window(void) {
    WINDOWPLACEMENT wp;
    remember_opt_rect();
    lv_get_widths(W.lv, g_set.colsGame, NCOLS);
    { int k; for (k = 0; k < NCOLS; k++) if (((g_set.colsHidden >> k) & 1) && g_colW[k] > 0) g_set.colsGame[k] = MulDiv(g_colW[k], 96, g_dpi); }   /* a hidden column keeps the width it will come back with */
    lv_get_widths(g_ctl.lv, g_set.colsCtl, 5);
    lv_get_widths(g_cmb.lv, g_set.colsCmb, 9);
    g_set.splitX = MulDiv(g_splitX, 96, g_dpi);
    wp.length = sizeof wp;
    if (GetWindowPlacement(g_main, &wp) && wp.showCmd == SW_SHOWNORMAL) {
        RECT r;
        GetWindowRect(g_main, &r);
        if (r.right - r.left > 200 && r.bottom - r.top > 200) { g_winRect.x = r.left; g_winRect.y = r.top; g_winRect.w = r.right - r.left; g_winRect.h = r.bottom - r.top; }
    }
}

static LRESULT CALLBACK main_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_main = h;
        create_ui();
        return 0;
    case WM_SIZE:
        if (W.status && w != SIZE_MINIMIZED) {
            do_layout();
            /* after maximizing, buttons next to the moved controls could stay unpainted (white) or leave stray lines: repaint everything */
            RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            SetTimer(h, 3, 150, NULL);   /* and once more when the maximize / restore animation is over */
        }
        return 0;
    case WM_DRAWITEM: if (tb_draw((const DRAWITEMSTRUCT *)l)) return TRUE; break;   /* the toolbar buttons */
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN: case WM_CTLCOLORLISTBOX:   /* the controls on the window itself (search row, filters) */
        if (g_dark) return (LRESULT)th_ctlcolor(m, (HDC)w, (HWND)l, 0);
        break;
    case 0x91: case 0x92:   /* the menu bar of the dark theme */
        if (g_dark) { int hnd; LRESULT r = th_menu_msg(h, m, w, l, &hnd); if (hnd) return r; }
        break;
    case WM_NCPAINT: case WM_NCACTIVATE:
        if (g_dark) { LRESULT r = DefWindowProcW(h, m, w, l); th_menu_line(h); return r; }
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mi = (MINMAXINFO *)l;
        { int minw = g_set.optWindow ? S(640) : S(1000);   /* without the tabs the main window can be narrower */
          mi->ptMinTrackSize.x = minw > GetSystemMetrics(SM_CXSCREEN) ? GetSystemMetrics(SM_CXSCREEN) : minw; }
        mi->ptMinTrackSize.y = S(700) > GetSystemMetrics(SM_CYSCREEN) ? GetSystemMetrics(SM_CYSCREEN) : S(700);
        return 0;
    }
    case WM_COMMAND: on_command(LOWORD(w), HIWORD(w), (HWND)l); return 0;
    case WM_INITMENUPOPUP: {   /* Play in the Game menu follows the selected game */
        HMENU mn = (HMENU)w;
        if (mn == g_viewMenu) CheckMenuItem(g_viewMenu, ID_M_LAY0, MF_BYCOMMAND | (g_optWin ? MF_UNCHECKED : MF_CHECKED));
        if (mn == g_colsMenu) { int k; for (k = 0; k < NCOLS; k++) CheckMenuItem(g_colsMenu, ID_COL0 + k, MF_BYCOMMAND | (((g_set.colsHidden >> k) & 1) ? MF_UNCHECKED : MF_CHECKED)); }
        if (mn == g_viewMenu || mn == g_themeMenu) {   /* the check marks of the View menu */
            CheckMenuRadioItem(g_themeMenu, ID_M_THEME0, ID_M_THEME2, ID_M_THEME0 + g_set.themeMode, MF_BYCOMMAND);
            CheckMenuItem(g_viewMenu, ID_M_FAVONLY, MF_BYCOMMAND | (ctl_checked(W.favOnly) ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(g_viewMenu, ID_M_AVAIL, MF_BYCOMMAND | (ctl_checked(W.chAvail) ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(g_viewMenu, ID_M_SHOWSEARCH, MF_BYCOMMAND | (SEARCH_SHOWN() ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(g_viewMenu, ID_M_SHOWTOOLBAR, MF_BYCOMMAND | (g_set.hideToolbar ? MF_UNCHECKED : MF_CHECKED));
            CheckMenuItem(g_viewMenu, ID_M_SHOWFILTERS, MF_BYCOMMAND | (g_set.hideFilters ? MF_UNCHECKED : MF_CHECKED));
        }
        if (mn == g_playWithMenu) {   /* the renderers, to try one once */
            int i;
            while (GetMenuItemCount(mn) > 0) DeleteMenu(mn, 0, MF_BYPOSITION);
            for (i = 0; i < g_nrend && i < 8; i++) AppendMenuW(mn, MF_STRING, ID_PLAYREND0 + i, g_rendNames[i]);
            return 0;
        }
        if (GetMenuState(mn, ID_M_PLAY, MF_BYCOMMAND) != (UINT)-1) {
            int ok = playable(selected_game()) ? MF_ENABLED : MF_GRAYED;
            EnableMenuItem(mn, ID_M_PLAYFS, MF_BYCOMMAND | ok);
            EnableMenuItem(mn, ID_M_PLAYTRAINER, MF_BYCOMMAND | ok);
            EnableMenuItem(mn, ID_M_PLAYWIN, MF_BYCOMMAND | ok);
            { int k, nk = GetMenuItemCount(mn); for (k = 0; k < nk; k++) if (GetSubMenu(mn, k) == g_playWithMenu) EnableMenuItem(mn, (UINT)k, MF_BYPOSITION | ok); }   /* Play With is a submenu: found by position */
            EnableMenuItem(mn, ID_M_GAMECFG, MF_BYCOMMAND | (selected_game() ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(mn, ID_M_CHECKROMS, MF_BYCOMMAND | (selected_game() ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(mn, ID_ICONBTN, MF_BYCOMMAND | (selected_game() ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(mn, ID_M_PLAY, MF_BYCOMMAND | ok);
            EnableMenuItem(mn, ID_M_STOP, MF_BYCOMMAND | (g_running ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(mn, ID_M_TOGGLEFAV, MF_BYCOMMAND | (selected_game() ? MF_ENABLED : MF_GRAYED));
        }
        return 0;
    }
    case WM_ACTIVATE:   /* ROMs may have been added while the window was in the background */
        if (LOWORD(w) != WA_INACTIVE && W.eRoms && g_games) { roms_refresh(); update_buttons(); }
        return 0;
    case WM_NOTIFY: return on_notify((NMHDR *)l);
    case WM_APP_LAYOUT: set_optmode((int)w == 1, 0); reset_window(1); return 0;   /* a new layout: the window sizes go back to the defaults of it */
    case WM_APP_CELL: if (w == 0) ctl_cell(LOWORD(l), HIWORD(l)); else cmb_cell(LOWORD(l), HIWORD(l)); update_buttons(); return 0;
    case WM_APP_REFILTER: refilter(); return 0;
    case WM_APP_GAMES: {
        FetchResult *r = (FetchResult *)l;
        if (r->err) {
            static const wchar_t *why[] = {L"", L"could not create a pipe", L"ZiNc.exe could not be started", L"ZiNc.exe did not answer --list-games",
                                           L"ZiNc.exe printed nothing", L"ZiNc.exe printed no game list"};
            wchar_t t[200];
            games_free(g_games, g_ngames);
            g_games = NULL; g_ngames = 0;
            swprintf(t, 200, L"Could not list games: %ls", why[r->err > 5 ? 4 : r->err]);
            set_status(t);
            if (!file_exists(g_exePath)) {
                wchar_t b[700];
                swprintf(b, 700, L"ZiNc.exe was not found next to ZiNc-EX.exe:\n%ls", g_root);
                msg_box(g_main, L"ZiNc Not Found", b, MB_ICONWARNING, NULL);
            }
        } else {
            games_free(g_games, g_ngames);
            g_games = r->list; g_ngames = r->n;
            games_sort_default(g_games, g_ngames);
            roms_refresh();
            games_status();
        }
        free(r);
        fill_filters();
        refilter();
        rebuild_recent();
        return 0;
    }
    case WM_APP_GAMEEXIT:
        {
            DWORD code = 0, secs = (GetTickCount() - g_startTick) / 1000;
            int hint = 0;
            if (g_running) {
                GetExitCodeProcess(g_proc.hProcess, &code);
                hint = !g_stopped && (code != 0 || secs < 5);   /* a game that ends at once or with an error has a problem to find */
                CloseHandle(g_proc.hProcess); CloseHandle(g_proc.hThread); g_running = 0;
            }
            stop_trainer();
            if (g_minimized) {   /* the launcher comes back */
                g_minimized = 0;
                if (IsIconic(g_main)) ShowWindow(g_main, SW_RESTORE);
                SetForegroundWindow(g_main);
            }
            EnableWindow(W.stopBtn, FALSE);
            update_buttons();
            games_status();
            if (hint) {
                wchar_t t[300];
                swprintf(t, 300, L"ZiNc closed after %lu s (exit code %lu): check the ROMs, the renderer and the Enable Logs option", (unsigned long)secs, (unsigned long)code);
                set_note(t);
            }
        }
        return 0;
    case WM_TIMER:
        if (w == 1) {
            static int tick;
            sync_main_enable();   /* the main window is off while the settings window is open */
            if (++tick % 8 == 0 && g_games && !g_applying) roms_refresh();   /* now and then: ROMs may be added or removed while the launcher is open */
            update_buttons();
        }
        else if (w == 3) {
            KillTimer(h, 3);
            RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        else if (w == 2) {
            wchar_t cur[200] = L"";
            KillTimer(h, 2);
            get_status(cur, 200);
            if (!wcscmp(cur, g_note)) set_status(L"");
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(l), lx = S(8) + g_splitX;
        if (!g_optWin && x >= lx && x < lx + S(8)) { g_dragging = 1; SetCapture(h); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_dragging) { g_splitX = (short)LOWORD(l) - S(8) - S(4); do_layout(); RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW); }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) { g_dragging = 0; ReleaseCapture(); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            if (!g_optWin && pt.x >= S(8) + g_splitX && pt.x < S(16) + g_splitX) { SetCursor(LoadCursorW(NULL, IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_CLOSE:
        if (!g_leaveTrainer) stop_trainer();
        remember_window();
        do_save();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ---------------------------------------------------------------- start */
static int on_screen(int x, int y, int w, int h) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return x + w > vx + 100 && x < vx + vw - 100 && y + 60 > vy && y < vy + vh - 100;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show) {
    wchar_t *slash;
    WNDCLASSEXW wc;
    HACCEL acc;
    ACCEL ac[6];
    MSG msg;
    int argc, W0, H0, x, y;
    wchar_t **argv;
    HWND hw;
    (void)prev; (void)cmd; (void)show;
    GetModuleFileNameW(NULL, g_exePath, 560);
    wcscpy(g_root, g_exePath);
    slash = wcsrchr(g_root, L'\\');
    if (slash) *slash = 0;
    path_join(g_exePath, 560, g_root, L"ZiNc.exe");
    path_join(g_settingsFile, 560, g_root, L"zinc-settings.cfg");
    path_join(g_rendererCfg, 560, g_root, L"renderer.cfg");
    install_renderers();
    settings_defaults(&g_set, NULL);
    settings_load();
    gamecfg_load();
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {   /* frontends: ZiNc-EX.exe --game <set or ROM file> [--fullscreen | --windowed]  (or just the ROM file) */
        const wchar_t *game = NULL;
        int fs = -1, i;
        for (i = 1; i < argc; i++) {
            if (!_wcsicmp(argv[i], L"--fullscreen")) fs = 1;
            else if (!_wcsicmp(argv[i], L"--windowed")) fs = 0;
            else if (!_wcsicmp(argv[i], L"--game") && i + 1 < argc) game = argv[++i];
            else if (!_wcsnicmp(argv[i], L"--game=", 7)) game = argv[i] + 7;
            else if (!_wcsicmp(argv[i], L"--play")) { game = NULL; break; }   /* the older desktop icons: below */
            else if (argv[i][0] != L'-' && !game) game = argv[i];
        }
        if (game) { int rc = run_game_cli(game, fs); LocalFree(argv); return rc; }
    }
    if (argv && argc > 2 && !wcscmp(argv[1], L"--play")) {   /* desktop icon of an older build: start the game and leave */
        run_headless(_wtoi(argv[2]), argc > 3 && !wcscmp(argv[3], L"--fullscreen"));
        return 0;
    }
    th_init(g_set.dark);
    ui_init(inst);
    input_init();
    load_modes();

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = th_face_brush();
    wc.lpszClassName = L"ZincExMain";
    wc.hIcon = g_icon;
    wc.hIconSm = g_iconSm;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = page_proc;
    wc.cbWndExtra = DLGWINDOWEXTRA;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"ZPage";
    wc.hIcon = wc.hIconSm = NULL;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = opt_proc;   /* the options window */
    wc.hbrBackground = th_face_brush();
    wc.lpszClassName = L"ZOptions";
    wc.hIcon = g_icon;
    wc.hIconSm = g_iconSm;
    RegisterClassExW(&wc);

    W0 = g_set.optWindow ? S(860) : S(1200); H0 = g_set.optWindow ? S(720) : S(800);   /* without the tabs the main window is narrower */
    {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        if (W0 > wa.right - wa.left) W0 = wa.right - wa.left;
        if (H0 > wa.bottom - wa.top) H0 = wa.bottom - wa.top;
        x = wa.left + (wa.right - wa.left - W0) / 2;
        y = wa.top + (wa.bottom - wa.top - H0) / 2;
    }
    if (g_winRect.w > 0 && on_screen(g_winRect.x, g_winRect.y, g_winRect.w, g_winRect.h)) { x = g_winRect.x; y = g_winRect.y; W0 = g_winRect.w; H0 = g_winRect.h; }
    hw = CreateWindowExW(0, L"ZincExMain", L"ZiNc EX", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, W0, H0, NULL, build_menu(), inst, NULL);
    if (!hw) return 1;
    th_window(hw);
    ac[0].fVirt = FVIRTKEY; ac[0].key = VK_F5; ac[0].cmd = ID_M_RESCAN;
    ac[1].fVirt = FVIRTKEY | FCONTROL; ac[1].key = VK_RETURN; ac[1].cmd = ID_M_PLAY;
    ac[2].fVirt = FVIRTKEY | FCONTROL; ac[2].key = 'D'; ac[2].cmd = ID_M_TOGGLEFAV;
    ac[3].fVirt = FVIRTKEY | FCONTROL; ac[3].key = 'F'; ac[3].cmd = ID_M_FOCUSSEARCH;
    ac[4].fVirt = FVIRTKEY; ac[4].key = VK_F9; ac[4].cmd = ID_M_OPTWIN;
    acc = CreateAcceleratorTableW(ac, 5);
    ShowWindow(hw, SW_SHOW);
    do_layout();
    select_tab();
    apply_to_ui(NULL);
    update_opt_menu();
    apply_bars();
    do_layout();
    if (g_set.optWindow) set_optmode(1, 0);   /* the options window stays closed until it is asked for */
    rebuild_recent();
    SetTimer(hw, 1, 400, NULL);   /* widgets have many change events: simply compare with the saved state a few times a second */
    rescan();
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        HWND dlg = NULL, p;
        if (TranslateAcceleratorW(hw, acc, &msg)) continue;
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && (msg.hwnd == W.lv || msg.hwnd == W.search || msg.hwnd == W.favOnly)) {   /* Enter plays the selected game (the dialog handling would swallow it) */
            if (!(msg.lParam & (1 << 30)) && selected_game()) launch_game(selected_game());
            continue;
        }
        for (p = msg.hwnd; p; p = GetParent(p)) {
            wchar_t cls[16];
            GetClassNameW(p, cls, 16);
            if (!wcscmp(cls, L"ZPage") || !wcscmp(cls, L"ZincExMain") || !wcscmp(cls, L"ZOptions")) { dlg = p; break; }
        }
        if (dlg && IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
