/* ZiNc EX - native Windows launcher, plain C + Win32. Shared declarations. */
#ifndef ZGUI_COMMON_H
#define ZGUI_COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0600
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>

/* ---------------------------------------------------------------- util.c */
typedef struct { int *v; int n, cap; } IntList;
void il_clear(IntList *l);
void il_add(IntList *l, int x);
int il_has(const IntList *l, int x);
void il_remove(IntList *l, int x);
void il_copy(IntList *dst, const IntList *src);

typedef struct { char *s; size_t n, cap; } Buf;   /* growable byte string (UTF-8 / ANSI text) */
void buf_add(Buf *b, const char *s);
void buf_addn(Buf *b, const char *s, size_t n);
void buf_fmt(Buf *b, const char *fmt, ...);
void buf_free(Buf *b);

wchar_t *wdup(const wchar_t *s);
wchar_t *u8_to_w(const char *s, int len);        /* malloc'ed */
char *w_to_u8(const wchar_t *s);                 /* malloc'ed */
char *read_file(const wchar_t *path, size_t *len);   /* malloc'ed, NUL terminated, NULL if missing */
int write_file(const wchar_t *path, const void *data, size_t len);
int file_exists(const wchar_t *path);
void path_join(wchar_t *out, size_t cap, const wchar_t *a, const wchar_t *b);
int is_abs_path(const wchar_t *p);
void trim_w(wchar_t *s);
char *trim_a(char *s);                            /* in place, returns start */
int xatoi(const char *s, int *out);               /* 1 when s holds an integer */

/* ---------------------------------------------------------------- tips.c */
const wchar_t *tip_for(const wchar_t *label);

/* ---------------------------------------------------------------- gamecfg.c: settings of single games */
typedef struct {
    int id;
    wchar_t renderer[64];        /* "" = same as global */
    int rotate;                  /* -2 = same as global, -1 = game default, 0..3 */
    int fullscreen;              /* -1 = same as global, 0 window, 1 fullscreen */
    int xsize, ysize;            /* 0 = same as global */
    int scale, xbrz;             /* Direct3D 11 only; -1 = same as global */
    int borderless, aspect, fxaa, dedither, vsync, overscan, texsmooth;   /* Direct3D 11 only; -1 = same as global (borderless 0/1, aspect 0..3, fxaa 0/1, texsmooth 0/1, dedither 0..3, vsync 0/1, overscan in pixels) */
    int sound, soundFilter, surround, exciter, slowGeometry, trainer;   /* -1 = same as global, 0 off, 1 on */
    wchar_t profile[64];         /* controls profile, "" = the current controls */
    wchar_t bezel[260];          /* Direct3D 11 bezel: "" = the bezel of the settings (or bezels\\<set>.png), "-" = none, else a file of the bezels folder (or a full path) */
} GameCfg;
typedef struct { int fullscreen; wchar_t renderer[64]; const wchar_t *tag; } PlayOpts;   /* one-off changes of a start; fullscreen -1 = unchanged */
void gamecfg_load(void);
int gamecfg_save(void);
const GameCfg *gamecfg_find(int id);            /* NULL when the game has no settings of its own */
int gamecfg_active(const GameCfg *c);
void gamecfg_set(const GameCfg *c);
void gamecfg_remove(int id);
int gamecfg_rename_profile(const wchar_t *from, const wchar_t *to);   /* the games that use the controls profile "from" use "to"; returns how many */
int ui_mode_count(void);                        /* main.c: the screen modes of the Video tab */
void ui_mode_get(int i, int *w, int *h);

/* ---------------------------------------------------------------- settings.c */
typedef struct {
    int hideConsole;
    int onStart;                /* when a game starts: 0 keep the launcher open, 1 minimize it (it comes back when the game ends), 2 close it */
    wchar_t roms[520];
    int useSound;
    wchar_t renderer[64];
    int rotate;                 /* -1 = game default */
    int soundFilter, cutoff, surround, surroundMul, exciter, slowGeometry;
    int enhanced, xinputOn, dinputOn, analog, deadzone, pad1, pad2;
    int onlyAvail, favOnly;
    wchar_t fRegion[8], fMaker[40], fGenre[24];   /* the list filters of the last session ("" = all) */
    int sortCol, sortAsc;       /* game list sorting: column 0..6, ascending flag */
    int logs;                   /* write log files (zinc-d3d11.log, ZiNc-EX_launch.log) */
    int dark;                   /* dark theme (applied at the next start) */
    wchar_t trainer[520];       /* a trainer program that is started with the games */
    int trainerOn;
    int themeMode;              /* 0 follow Windows, 1 light, 2 dark (applied at the next start); dark is what it comes to */
    int hideSearch, hideFilters, hideToolbar;   /* the search bar / the filters bar of the game list are hidden */
    int colsHidden;             /* game list columns that are hidden (bit 0 Favorite, 1 #, 2 Game, 3 Info, 4 Status, 5 Hardware, 6 Year); Info is hidden by default */
    int optWindow;              /* the options (Video, Audio ... tabs) are in a window of their own */
    int colsGame[7], colsCtl[5], colsCmb[9], splitX;   /* column widths of the lists and the splitter position (96 dpi pixels, 0 = default) */
    IntList fav;
    IntList recent;
} Settings;

typedef struct { char key[32]; int val; } KV;
typedef struct { KV kv[48]; int n; } KVMap;
int kv_get(const KVMap *m, const char *key);
void kv_set(KVMap *m, const char *key, int v);
int kv_has(const KVMap *m, const char *key);

typedef struct { int x, y, w, h; } WinRect;

extern Settings g_set;
extern WinRect g_winRect, g_optRect;   /* the main window and the options window of the last session */
extern wchar_t g_root[560], g_exePath[560], g_settingsFile[560], g_rendererCfg[560];

void settings_defaults(Settings *s, const Settings *keep);
void settings_load(void);
int settings_save(void);                           /* 0 when the file could not be written */
void settings_sig(Buf *b);                         /* what "Save settings" would write (from the fields) */
void set_favorite(int id, int on);

extern wchar_t g_bezelPath[520];   /* BezelImage of renderer.cfg (text, so not in the KVMap) */
void settings_resolve_theme(void);   /* g_set.dark from g_set.themeMode (and the Windows setting) */
void renderer_defaults(KVMap *m);
void renderer_read(KVMap *m);
void renderer_write(const KVMap *m);
int renderer_write_copy(const wchar_t *dst, const KVMap *m);

void install_renderers(void);
int install_input_plugin(wchar_t *outPath, size_t cap);   /* 0 on success */
const wchar_t *renderer_file(const wchar_t *name, wchar_t *out, size_t cap);  /* "renderers/<name>/x.znc" or NULL */
int renderer_names(wchar_t names[][64], int max);
void canonical_renderer(wchar_t *name);

typedef struct { wchar_t **a; int n; } ArgList;
void args_free(ArgList *l);
void zinc_args(int gameId, ArgList *out, wchar_t *warn, size_t warnCap);
void zinc_args_ex(int gameId, const PlayOpts *po, ArgList *out, wchar_t *warn, size_t warnCap);   /* with the game's own settings and one-off changes */
BOOL zinc_start(ArgList *args, PROCESS_INFORMATION *pi);
void run_headless(int id, int fullscreen);
int run_game_cli(const wchar_t *game, int fullscreen);   /* frontends: --game <set or ROM file>; returns the exit code */

/* ---------------------------------------------------------------- games.c */
typedef struct { int id; wchar_t *title, *info, *set, *parent, *bios; } Game;
const wchar_t *game_hardware(const Game *g);   /* gamedb.c: "Sony ZN-1 System", "Namco System 11"... ("" = unknown) */
int game_year(const Game *g);                  /* 0 = unknown */
extern Game *g_games;
extern int g_ngames;
int games_fetch(Game **out, int *n);              /* runs ZiNc --list-games; 0 on success */
void games_free(Game *g, int n);
void games_sort_default(Game *g, int n);
int rom_available_init(const wchar_t *dir);       /* builds the set of ROM names; returns handle count */
int rom_available(const Game *g);
int rom_missing(const Game *g, wchar_t *out, size_t cap);   /* "a.zip, b.zip" of the ROM sets that are not there; count */
void rom_available_done(void);
int create_desktop_icon(const Game *g, wchar_t *err, size_t errCap);
void short_title(const wchar_t *title, int keepVer, wchar_t *out, size_t cap);
void shortcut_name(const Game *g, wchar_t *out, size_t cap);

/* ---------------------------------------------------------------- controls.c / combos.c */
typedef struct {
    char id[16];           /* zinc-input.cfg key, e.g. p1_b1 */
    wchar_t label[40];
    int dir;                 /* directions: the pad part is automatic */
    char key[24], x[24], j[24];   /* binding names, empty = unbound */
    int autofire;
} RoleRow;

typedef struct { const char *kind, *name, *seq, *cmd; } ComboMove;
typedef struct { const char *name; const ComboMove *moves; int nmoves; } ComboChar;
extern const ComboChar g_comboChars[];
extern const int g_nComboChars;

#define NUM_COMBOS 20
typedef struct {
    char name[160], kind[40], seq[160];
    int player;              /* 1 or 2 */
    int faceLeft;
    char key[24], x[24], j[24];
} ComboSlot;

typedef struct {
    RoleRow rows[26];
    int nrows;
    ComboSlot slots[NUM_COMBOS];
    int stepMs, chargeMs, chargeCredit;   /* chargeCredit: the charge the player already holds counts */
} InputState;
extern InputState g_in;

void input_init(void);
void input_load_cfg(void);
void input_cfg_text(Buf *b);
void input_save_cfg(void);
void input_reset_rows(void);
void input_profile_text(Buf *b);                   /* the controls (pads, threshold, bindings) as a profile */
void input_profile_apply(const char *text);
void input_cfg_text_profile(Buf *b, const char *profile);

/* profiles.c: controls profiles (profiles\\<name>.cfg) */
void profile_clean_name(wchar_t *name);
int profile_names(wchar_t names[][64], int max);   /* sorted */
char *profile_load(const wchar_t *name);            /* malloc'ed text or NULL */
int profile_save(const wchar_t *name, const char *text);
int profile_rename(const wchar_t *from, const wchar_t *to);   /* 0 when it fails (the name is taken...) */
int profile_delete(const wchar_t *name);
void combos_apply_loadout(const char *charName);
void combos_defaults(void);
int combo_rows_shown(void);
const char *x_friendly(const char *t);
const char *j_friendly(const char *t);
const char *vk_name(int vk, char *buf);
int pad_item_count(int isX);
const char *pad_item_token(int isX, int i);
const char *pad_item_label(int isX, int i);
int combo_group_count(void);
const char *combo_group(int i, const ComboMove **moves, int *n);
void detect_pads(wchar_t *out, size_t cap);
int key_down(int vk);

/* ---------------------------------------------------------------- theme.c: the optional dark theme */
extern int g_dark;
COLORREF th_face(void); COLORREF th_page(void); COLORREF th_text(void); COLORREF th_gray(void); COLORREF th_alt(void);
HBRUSH th_face_brush(void); HBRUSH th_page_brush(void);
void th_init(int dark);
void th_window(HWND h);
void th_control(HWND h, const wchar_t *cls);
HBRUSH th_ctlcolor(UINT msg, HDC dc, HWND ctl, int onPage);   /* NULL in the light theme */
void th_tab(HWND tab);
void th_status(HWND sb);
LRESULT th_menu_msg(HWND h, UINT m, WPARAM w, LPARAM l, int *handled);   /* the menu bar */
void th_menu_line(HWND h);

/* ---------------------------------------------------------------- ui helpers (ui.c) */
extern HINSTANCE g_inst;
extern HFONT g_font;
extern int g_dpi;
extern HICON g_icon, g_iconSm;
void ui_init(HINSTANCE inst);
void wide_from_ascii(const char *s, wchar_t *out, int cap);
extern HWND g_main;
int S(int px);                                     /* scale 96-dpi pixels */
HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id);
void set_tip(HWND tipWnd, HWND parent, HWND ctl, const wchar_t *text);
int text_width(HWND h, const wchar_t *t);
int ctl_checked(HWND h);
void ctl_set_checked(HWND h, int on);
int edit_int(HWND h, int lo, int hi);              /* number in an edit box, clamped; lo when empty */
void edit_set_int(HWND h, int v);
void edit_set_text_utf8(HWND h, const char *s);
void edit_get_text_utf8(HWND h, char *out, int cap);
void combo_fill(HWND cb, const wchar_t **items, int n);
LRESULT lv_altrows(LPNMLVCUSTOMDRAW cd);
HWND lv_create(HWND parent, int id, const wchar_t **cols, const int *widths, int ncols);
void lv_get_widths(HWND lv, int *out, int n);          /* in 96 dpi pixels */
void lv_set_widths(HWND lv, const int *w, int n);      /* ignores zeros */
int lv_hit_cell(HWND lv, LPARAM nm, int *row, int *col);
void msg_box(HWND owner, const wchar_t *title, const wchar_t *text, UINT flags, int *result);

/* modal windows (ui.c): a dialog made of mkat() controls; onok runs when OK is pressed (return 0 to stay open), oncmd for buttons with ids from 1000 */
typedef struct Modal {
    int kind, done, changed;
    HWND owner, dlg, c1, c2;
    char *dst;
    int cap, isX, sel;
    BOOL was[256];
    wchar_t title[120];
    HWND link;
    HFONT linkFont;
    HICON logo;
    int (*onok)(struct Modal *);
    int (*oncmd)(struct Modal *, int id);
    LRESULT (*onnotify)(struct Modal *, NMHDR *);
    void *user;
} Modal;
HWND make_modal(Modal *md, int cw, int ch);
int lv_min_w(HWND lv, int col);   /* ui.c: the narrowest width of a column of a list (pixels) */
extern int g_lvProg;   /* ui.c: while > 0 the width of a list column may be set below the minimum (hidden columns) */
HWND ui_owner(HWND o);   /* main.c: the window a dialog should belong to (the settings window when it is open) */
HWND mkat(HWND p, const wchar_t *cls, const wchar_t *t, DWORD st, int x, int y, int w, int h, int id);   /* x, y, w, h in 96 dpi pixels */
void run_modal(Modal *md);
typedef struct { const wchar_t *a, *b; } TableRow;   /* b NULL: a title row */
void dlg_table(HWND owner, const wchar_t *title, const wchar_t *c0, const wchar_t *c1, const TableRow *rows, int n, int w0, int w1);   /* a window with a two-column table and an OK button (96 dpi widths) */
int dlg_text(HWND owner, const wchar_t *title, const wchar_t *prompt, wchar_t *text, int cap);   /* one line of text; 1 when OK */

/* modal pickers */
int dlg_capture_key(HWND owner, const wchar_t *label, char *dst, int cap);       /* 1 when changed */
int cell_menu(HWND owner, int withSet);   /* right click menu of a list cell: 1 = Set, 2 = Clear */
int dlg_pick_pad(HWND owner, const wchar_t *title, int isX, char *dst, int cap);
#define APP_VERSION "1.0.0"
#define APP_VERSION_W L"1.0.0"
void dlg_about(HWND owner);
int dlg_game_settings(HWND owner, const Game *g);   /* gamecfg.c */
void dlg_check_roms(HWND owner, const Game *g);     /* romcheck.c */
int backup_create(const wchar_t *zipPath);          /* backup.c: files written, 0 on failure */
int backup_restore(const wchar_t *zipPath);         /* files restored, -1 when the zip is not a readable backup */
void roms_dir_abs(wchar_t *out, size_t cap);        /* main.c: the ROMs folder as a full path */

/* pages of the right pane (controls.c / combos.c) */
typedef struct {
    HWND page;
    HWND lProf, cbProf, bPUpd, bPSave, bPRen, bPDel, chEnh, lP1, lP2, cbP1, cbP2, lDead, eDead, lv, bKey, bX, bJ, bClear, bDef, lPads, bDetect;
    int analog;
} CtlUI;
extern CtlUI g_ctl;
void ctl_create(HWND page, HWND tipWnd);
void ctl_layout(int w, int h);
void ctl_load(void);
void ctl_collect(void);
void ctl_refresh_rows(void);
int ctl_notify(NMHDR *nh);
int ctl_command(int id, int code);
void ctl_cell(int row, int col);
void ctl_reset_ask(HWND owner);

typedef struct { const char *name; int w, h; const unsigned char *bgra; } IconData;   /* icon_data.c, made by gen_icons.py from the PNG files in icons */
extern const IconData g_icons[];
extern const int g_nIcons;
#define CMB_NTOK 30   /* the buttons under the Sequence box */
typedef struct {
    HWND page, info, lStep, eStep, lChg, eChg, lLoad, cbLoad, lv, lChar, cbChar, lMove, cbMove, lSeq, eSeq, lPlay, cbPlay, lFace, cbFace, bClear, bLegend, chCredit, bDef, note, tok[CMB_NTOK];
    int syncing, loading;
} CmbUI;
extern CmbUI g_cmb;
void cmb_create(HWND page, HWND tipWnd);
void cmb_layout(int w, int h);
void cmb_load(void);
void cmb_collect(void);
int cmb_notify(NMHDR *nh);
int cmb_command(int id, int code);
void cmb_cell(int row, int col);
void cmb_refresh(void);

/* control ids */
enum {
    ID_SEARCH = 100, ID_CLEARSEARCH, ID_GAMELIST, ID_FAVONLY, ID_ICONBTN, ID_STOPBTN, ID_PLAYBTN, ID_TAB,
    ID_DEFAULTBTN, ID_SAVEBTN, ID_CANCELBTN, ID_OPTBTN, ID_TBFAV, ID_TBFS, ID_TBAVAIL, ID_TBAUDIO, ID_TBSET, ID_TBVIDEO, ID_TBCTL, ID_TBCMB,
    /* menu */
    ID_M_RESCAN = 200, ID_M_OPENZINC, ID_M_OPENROMS, ID_M_CLEARRECENT, ID_M_EXIT, ID_M_PLAY, ID_M_STOP, ID_M_TOGGLEFAV,
    ID_M_SAVE, ID_M_SETAUDIO, ID_M_RESETWIN, ID_M_LAY0, ID_M_LAY1, ID_M_FOCUSSEARCH, ID_M_KEYS, ID_M_ABOUT, ID_M_OPTWIN, ID_COL0, ID_COL1, ID_COL2, ID_COL3, ID_COL4, ID_COL5, ID_COL6, ID_M_THEME0, ID_M_THEME1, ID_M_THEME2, ID_M_FAVONLY, ID_M_AVAIL, ID_M_SHOWSEARCH, ID_M_SHOWFILTERS, ID_M_SHOWTOOLBAR,
    ID_M_GAMECFG = 250, ID_M_PLAYFS, ID_M_PLAYWIN, ID_M_CHECKROMS, ID_M_OPENSNAP, ID_M_BACKUP, ID_M_RESTORE, ID_M_PLAYTRAINER,
    ID_PLAYREND0 = 270,   /* 8 ids: play with the n-th renderer once */
    ID_RECENT0 = 600,   /* 8 ids: the recent games (clear of the other ids) */
    /* video page */
    ID_V_RENDERER = 300, ID_V_RES, ID_V_W, ID_V_H, ID_V_ROT, ID_V_SCALE, ID_V_XBRZ, ID_V_DEPTH, ID_V_SCAN, ID_V_FILTER,
    ID_V_TEXTYPE, ID_V_TEXCACHE, ID_V_BLEND, ID_V_FPS, ID_V_FULL, ID_V_DITHER, ID_V_SHOWFPS, ID_V_LIMIT, ID_V_SKIP, ID_V_AUTO, ID_V_FXAA, ID_V_OVERSCAN, ID_V_ASPECT, ID_V_FSMODE, ID_V_VSYNC, ID_V_DEDITHER, ID_V_TEXSMOOTH,
    /* audio */
    ID_A_SOUND = 340, ID_A_FILTER, ID_A_CUTOFF, ID_A_SURR, ID_A_SURRMUL, ID_A_EXC,
    /* system */
    ID_S_ROMS = 360, ID_S_BROWSE, ID_S_AVAIL, ID_S_SLOW, ID_S_HIDE, ID_S_LOGS, ID_S_TRAINER, ID_S_TRBROWSE, ID_S_TRON, ID_S_DARK, ID_S_ROMCLEAR, ID_S_TRCLEAR, ID_S_BEZEL, ID_S_BEZBROWSE, ID_S_BEZCLEAR, ID_S_LAYOUT, ID_S_ONSTART, ID_S_THEME,
    /* controls page */
    ID_C_ENH = 380, ID_C_X, ID_C_J, ID_C_AN, ID_C_P1, ID_C_P2, ID_C_DEAD, ID_C_LV, ID_C_KEY, ID_C_XB, ID_C_JB, ID_C_CLEAR,
    ID_C_DEF, ID_C_DETECT,
    /* combos page */
    ID_K_STEP = 410, ID_K_CHG, ID_K_LOADCHAR, ID_K_LV, ID_K_CHAR, ID_K_MOVE, ID_K_SEQ, ID_K_PLAY, ID_K_FACE, ID_K_CLEAR, ID_K_DEF,
    ID_C_PROF = 430, ID_C_PSAVE, ID_C_PDEL, ID_C_PUPD, ID_C_PREN,
    ID_F_MAKER = 440, ID_F_GENRE, ID_F_REGION, ID_RESETSORT,
    ID_K_TOK = 470,   /* ... + CMB_NTOK - 1: the sequence buttons */
    ID_K_LEGEND = 510, ID_K_CREDIT
};

#define WM_APP_GAMES (WM_APP + 1)
#define WM_APP_GAMEEXIT (WM_APP + 2)
#define WM_APP_REFILTER (WM_APP + 3)
#define WM_APP_LAYOUT (WM_APP + 9)    /* wParam: 1 the options in a window of their own, 0 in the main window */
#define WM_APP_CELL (WM_APP + 4)      /* wParam: 0 controls, 1 combos; lParam: MAKELONG(row, column) */

/* ---------------------------------------------------------------- toolbar.c */
enum { TI_REFRESH, TI_FAV, TI_PLAY, TI_STOP, TI_FULLSCREEN, TI_SETTINGS, TI_VIDEO, TI_CONTROLS, TI_COMBOS, TI_OK, TI_MISSING, TI_AVAIL, TI_AUDIO, TI_N };
HWND tb_button(HWND parent, int id, int icon);        /* a flat button that shows an icon (owner drawn, theme colours) */
HWND tb_separator(HWND parent);
void tb_set_checked(HWND b, int on);                  /* the pressed look of a switch (Favorites Only) */
int tb_draw(const DRAWITEMSTRUCT *di);                /* WM_DRAWITEM: 1 when it was a toolbar item */
unsigned char *tb_icon_mask(int icon, int variant, int sz);
HIMAGELIST tb_star_images(int sz, COLORREF off, COLORREF on);   /* the favorite stars of the game list */
HIMAGELIST tb_status_images(int sz);   /* the icons of the Status column: 0 = ROM set there (green), 1 = missing (red) */

#endif
