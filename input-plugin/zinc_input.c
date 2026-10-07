/*
 * ZiNc input plugin: keyboard (remappable) + XInput + DirectInput game controllers.
 * Drop-in replacement for controller.znc (same ZN_Jamma* exports and bit layouts,
 * extracted from the original plugin). Settings live in zinc-input.cfg next to the DLL.
 *
 * Build (32-bit, ZiNc is a 32-bit program):
 *   python -m ziglang cc -target x86-windows-gnu -O2 -shared -o zinc-input.znc zinc_input.c zinc_input.def
 */
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <commctrl.h>
#include <stdarg.h>
#include <initguid.h>
#include <dinput.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tables.h"
#define COBJMACROS
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#ifdef ZN_TEST
static unsigned char test_keys[256];
static int raw_key(int vk) { return test_keys[vk & 255] != 0; }
#else
static int raw_key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
#endif
/* Alt+Enter is the fullscreen hotkey: it must not also press Start */
#define KEYDOWN(vk) (raw_key(vk) && !((vk) == VK_RETURN && raw_key(VK_MENU)))
#ifdef ZN_TEST
static DWORD test_time;
#define NOW() test_time
#else
#define NOW() GetTickCount()
#endif
#define NROLES 26
#define NBIND 4
DEFINE_GUID(ZIID_DI8A, 0xBF798030, 0x483A, 0x4DA2, 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00);

/* ---------- binding model ---------- */
typedef struct { char src; int code; } Bind; /* src: 'K' keyboard, 'X' XInput, 'J' DirectInput */
static Bind binds[NROLES][NBIND];
static int nbinds[NROLES];

static const char *ROLE_NAME[NROLES] = {
    "p1_up", "p1_down", "p1_left", "p1_right", "p1_b1", "p1_b2", "p1_b3", "p1_b4", "p1_b5", "p1_b6", "p1_start",
    "p2_up", "p2_down", "p2_left", "p2_right", "p2_b1", "p2_b2", "p2_b3", "p2_b4", "p2_b5", "p2_b6", "p2_start",
    "coin1", "coin2", "test", "service"};

static const char *DEFAULTS[NROLES] = {
    "K:W", "K:S", "K:A", "K:D",
    "K:NUM4,X:X,J:B1", "K:NUM5,X:Y,J:B2", "K:NUM6,X:RB,J:B3",
    "K:NUM1,X:A,J:B4", "K:NUM2,X:B,J:B5", "K:NUM3,X:RT,J:B6", "K:ENTER,X:START,J:B10",
    "K:UP", "K:DOWN", "K:LEFT", "K:RIGHT",
    "K:U,X:X,J:B1", "K:I,X:Y,J:B2", "K:O,X:RB,J:B3",
    "K:J,X:A,J:B4", "K:K,X:B,J:B5", "K:L,X:RT,J:B6", "K:Y,X:START,J:B10",
    "K:RSHIFT,X:BACK,J:B9", "K:H,X:BACK,J:B9", "K:F4", "K:F7"};

/* ---------- combo macros ---------- */
#define NCOMBO 20
#define MAXSTEPS 160
typedef struct {
    Bind trig[NBIND];
    int ntrig;
    unsigned short steps[MAXSTEPS]; /* bit0 up,1 down,2 left,3 right,4..9 button 1..6, 10 start */
    unsigned short dur[MAXSTEPS];   /* hold time in ms, 0 = default step length */
    int nsteps;
    int player;   /* 0 or 1 */
    int face_left;
    int was_down;
} Combo;
static Combo combos[NCOMBO];
static int cfg_auto[26];       /* autofire per role (buttons only) */
static int cfg_auto_ms = 33;   /* half period of the autofire pulse: on for this long, then off */
static DWORD af_t0[26];
static int af_was[26];
static int cfg_step_ms = 30;
static int cfg_charge_ms = 600; /* hold time of "4*C" charge steps */
static int cfg_charge_credit = 0; /* combo_charge_credit=1: the time the player already holds the charge direction counts */
static struct { int active; int idx; int seen; DWORD t0; DWORD start; DWORD credit; const Combo *c; } macro[2];   /* credit: ms of the first (charge) step the player has already held for real */

static int parse_button(const char *t, int len) { /* returns button index 0..5 (Button 1..6), 6 = Start, or -1 */
    static const char *N[] = {"LP", "MP", "HP", "LK", "MK", "HK", 0};
    char b[8]; int i;
    if (len < 2 || len > 5) return -1;
    memcpy(b, t, len); b[len] = 0;
    for (i = 0; N[i]; i++) if (!_stricmp(N[i], b)) return i;
    if (!_stricmp(b, "ST") || !_stricmp(b, "START")) return 6;
    if ((b[0] == 'B' || b[0] == 'b') && (b[1] == 'N' || b[1] == 'n') && b[2] >= '1' && b[2] <= '6' && !b[3]) return b[2] - '1';   /* BN1..BN6: the game buttons of the Controls tab */
    if ((b[0] == 'B' || b[0] == 'b') && b[1] >= '1' && b[1] <= '6' && !b[2]) return b[1] - '1';
    return -1;
}

static unsigned short dir_mask(int d, int face_left) {
    unsigned short m = 0;
    static const char up[10] = {0,0,0,0,0,0,0,1,1,1}, down[10] = {0,1,1,1,0,0,0,0,0,0};
    static const char back[10] = {0,1,0,0,1,0,0,1,0,0}, fwd[10] = {0,0,0,1,0,0,1,0,0,1};
    if (up[d]) m |= 1;
    if (down[d]) m |= 2;
    if (back[d]) m |= face_left ? 8 : 4;
    if (fwd[d]) m |= face_left ? 4 : 8;
    return m;
}

static void add_step(Combo *c, unsigned short mask, unsigned short hold) {
    /* two presses of the same button in a row need a release in between */
    if (c->nsteps > 0 && c->nsteps < MAXSTEPS - 1 && (mask & c->steps[c->nsteps - 1] & 0x7F0)) {
        c->steps[c->nsteps] = 0; c->dur[c->nsteps] = 0; c->nsteps++;
    }
    if (c->nsteps < MAXSTEPS) { c->steps[c->nsteps] = mask; c->dur[c->nsteps] = hold; c->nsteps++; }
}

/* Notation: tokens separated by space/comma.
   "236+HP"  = down, down-fwd, fwd+HP (numpad directions, facing right)
   Buttons: LP MP HP LK MK HK (= BN1..BN6, the Button 1..6 of the Controls tab; also B1..B6) and ST / START.
   "4*2000"  = hold back for 2000 ms; "4*C" = hold for the configured charge time; "HP+HK" = buttons only; "~" = one neutral step.
   Repeated identical buttons ("LP LP") get a release step in between. Each step lasts cfg_step_ms. */
static void parse_seq(Combo *c, const char *text) {
    char buf[512], *tok, *ctx = 0;
    c->nsteps = 0;
    strncpy(buf, text, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (tok = strtok_s(buf, ", \t", &ctx); tok; tok = strtok_s(0, ", \t", &ctx)) {
        const char *p = tok;
        unsigned short dirs[40], btn = 0, hold = 0;
        int nd = 0, k;
        if (!strcmp(tok, "~")) { add_step(c, 0, 0); continue; }
        while (*p >= '1' && *p <= '9' && nd < 40) dirs[nd++] = dir_mask(*p++ - '0', c->face_left);
        if (nd > 0 && *p == '*') {
            int ms;
            p++;
            if (*p == 'C' || *p == 'c') { ms = cfg_charge_ms; p++; }
            else { ms = atoi(p); while (*p >= '0' && *p <= '9') p++; }
            hold = (unsigned short)(ms < 0 ? 0 : ms > 5000 ? 5000 : ms);
        }
        while (*p) {
            const char *e;
            int b;
            if (*p == '+') p++;
            e = p; while (*e && *e != '+') e++;
            b = parse_button(p, (int)(e - p));
            if (b >= 0) btn |= (unsigned short)(1u << (4 + b));
            p = e;
        }
        if (nd == 0) { if (btn) add_step(c, btn, 0); continue; }
        for (k = 0; k < nd - 1; k++) add_step(c, dirs[k], 0);
        add_step(c, (unsigned short)(dirs[nd - 1] | btn), hold);
    }
}

static int cfg_cursor_ms = 3000;   /* cursor_hide_ms in zinc-input.cfg: hide the mouse cursor after this long without movement; 0 = keep it visible; -1 = leave the cursor alone */
static int cfg_deadzone = 50;   /* analog stick press threshold, percent */
static int cfg_xinput = 1, cfg_dinput = 1, cfg_analog = 1;
static char combo_seq[NCOMBO][256];
static int cfg_pad[2] = {0, 0}; /* 0 = auto (n-th detected pad), -1 = none, n>0 = n-th detected pad */

/* ---------- names ---------- */
static const struct { const char *n; int vk; } KEYS[] = {
    {"UP", VK_UP}, {"DOWN", VK_DOWN}, {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"SPACE", VK_SPACE},
    {"ENTER", VK_RETURN}, {"ESC", VK_ESCAPE}, {"TAB", VK_TAB}, {"BACKSPACE", VK_BACK},
    {"LSHIFT", VK_LSHIFT}, {"RSHIFT", VK_RSHIFT}, {"LCTRL", VK_LCONTROL}, {"RCTRL", VK_RCONTROL},
    {"LALT", VK_LMENU}, {"RALT", VK_RMENU}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
    {"HOME", VK_HOME}, {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
    {"NUMADD", VK_ADD}, {"NUMSUB", VK_SUBTRACT}, {"NUMMUL", VK_MULTIPLY}, {"NUMDIV", VK_DIVIDE},
    {"NUMDEC", VK_DECIMAL}, {"COMMA", VK_OEM_COMMA}, {"PERIOD", VK_OEM_PERIOD}, {"MINUS", VK_OEM_MINUS},
    {"EQUALS", VK_OEM_PLUS}, {"SLASH", VK_OEM_2}, {"SEMICOLON", VK_OEM_1}, {"QUOTE", VK_OEM_7},
    {"LBRACKET", VK_OEM_4}, {"RBRACKET", VK_OEM_6}, {"BACKSLASH", VK_OEM_5}, {"BACKTICK", VK_OEM_3},
    {0, 0}};
static const char *XNAMES[] = {"A", "B", "X", "Y", "LB", "RB", "BACK", "START", "LS", "RS",
                               "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", 0};
static const WORD XMASK[14] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010,
                               0x0040, 0x0080, 0x0001, 0x0002, 0x0004, 0x0008};
static const char *DIRN[] = {"UP", "DOWN", "LEFT", "RIGHT", 0};
static const char *AXN[] = {"X", "Y", "Z", "RX", "RY", "RZ", 0};

static int find(const char **list, const char *s) {
    int i;
    for (i = 0; list[i]; i++) if (!_stricmp(list[i], s)) return i;
    return -1;
}

static int parse_key(const char *s) {
    int i, n;
    if (strlen(s) == 1 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || (s[0] >= '0' && s[0] <= '9')))
        return toupper((unsigned char)s[0]);
    if (!_strnicmp(s, "NUM", 3) && s[3] >= '0' && s[3] <= '9' && !s[4]) return VK_NUMPAD0 + s[3] - '0';
    if ((s[0] == 'F' || s[0] == 'f') && (n = atoi(s + 1)) >= 1 && n <= 12) return VK_F1 + n - 1;
    for (i = 0; KEYS[i].n; i++) if (!_stricmp(KEYS[i].n, s)) return KEYS[i].vk;
    if (!_strnicmp(s, "VK", 2)) return atoi(s + 2);
    return -1;
}

/* XInput codes: 0..13 buttons, 20 LT, 21 RT, 30..33 left stick U/D/L/R, 34..37 right stick U/D/L/R */
static int parse_x(const char *s) {
    int i = find(XNAMES, s);
    if (i >= 0) return i;
    if (!_stricmp(s, "LT")) return 20;
    if (!_stricmp(s, "RT")) return 21;
    if (!_strnicmp(s, "LS_", 3) && (i = find(DIRN, s + 3)) >= 0) return 30 + i;
    if (!_strnicmp(s, "RS_", 3) && (i = find(DIRN, s + 3)) >= 0) return 34 + i;
    return -1;
}

/* DirectInput codes: 0..31 buttons, 40..43 POV U/D/L/R, 50+2a (+1 for positive) axis a */
static int parse_j(const char *s) {
    int i, n;
    if ((s[0] == 'B' || s[0] == 'b') && (n = atoi(s + 1)) >= 1 && n <= 32) return n - 1;
    if (!_strnicmp(s, "POV_", 4) && (i = find(DIRN, s + 4)) >= 0) return 40 + i;
    if (!_strnicmp(s, "AXIS_", 5)) {
        char nm[8]; size_t l = strlen(s + 5);
        if (l >= 2 && l < 8) {
            strcpy(nm, s + 5); nm[l - 1] = 0;
            if ((i = find(AXN, nm)) >= 0 && (s[5 + l - 1] == '-' || s[5 + l - 1] == '+'))
                return 50 + 2 * i + (s[5 + l - 1] == '+');
        }
    }
    return -1;
}

static void set_binds(int role, const char *text) {
    char buf[256], *tok, *ctx = 0;
    nbinds[role] = 0;
    strncpy(buf, text, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (tok = strtok_s(buf, ", \t", &ctx); tok && nbinds[role] < NBIND; tok = strtok_s(0, ", \t", &ctx)) {
        Bind b; b.src = (char)toupper((unsigned char)tok[0]);
        if (tok[1] != ':') continue;
        b.code = b.src == 'K' ? parse_key(tok + 2) : b.src == 'X' ? parse_x(tok + 2) : b.src == 'J' ? parse_j(tok + 2) : -1;
        if (b.code >= 0) binds[role][nbinds[role]++] = b;
    }
}

static void set_combo_trigger(int n, const char *text) {
    int saved_n = nbinds[0];
    Bind saved[NBIND];
    int i;
    memcpy(saved, binds[0], sizeof saved);
    set_binds(0, text);                       /* reuse the binding parser via slot 0 ... */
    memcpy(combos[n].trig, binds[0], sizeof combos[n].trig);
    combos[n].ntrig = nbinds[0];
    memcpy(binds[0], saved, sizeof saved);    /* ... then restore it */
    nbinds[0] = saved_n;
    (void)i;
}

static HINSTANCE g_inst;
static int cfg_log;   /* logs=1 in zinc-input.cfg: write zinc-input.log (off by default) */
static char g_logpath[MAX_PATH];
static void clog(const char *fmt, ...) {   /* one line appended to zinc-input.log (only with logs=1) */
#ifndef ZN_TEST
    FILE *lf;
    va_list ap;
    if (!cfg_log || !g_logpath[0] || !(lf = fopen(g_logpath, "a"))) return;
    fprintf(lf, "[%lu] ", (unsigned long)GetTickCount());
    va_start(ap, fmt); vfprintf(lf, fmt, ap); va_end(ap);
    fputc('\n', lf); fclose(lf);
#else
    (void)fmt;
#endif
}
static void write_log(const char *dir, const char *cfgstate) {
#ifndef ZN_TEST
    char lp[MAX_PATH], me[MAX_PATH];
    FILE *lf;
    snprintf(lp, sizeof lp, "%s\\zinc-input.log", dir);
    strcpy(g_logpath, lp);
    GetModuleFileNameA(g_inst, me, sizeof me);
    if ((lf = fopen(lp, "w"))) { fprintf(lf, "plugin: %s\nconfig: %s\n", me, cfgstate); fclose(lf); }
#else
    (void)dir; (void)cfgstate;
#endif
}

static void load_config(void) {
    char path[MAX_PATH], line[512], dlldir[MAX_PATH];
    FILE *f;
    int i;
    for (i = 0; i < NROLES; i++) set_binds(i, DEFAULTS[i]);
    memset(combos, 0, sizeof combos); memset(combo_seq, 0, sizeof combo_seq); memset(macro, 0, sizeof macro);
    cfg_step_ms = 30; cfg_charge_ms = 600; cfg_charge_credit = 0;
    memset(cfg_auto, 0, sizeof cfg_auto); memset(af_was, 0, sizeof af_was); cfg_auto_ms = 33;
    cfg_deadzone = 50; cfg_xinput = cfg_dinput = cfg_analog = 1; cfg_pad[0] = cfg_pad[1] = 0; cfg_log = 0; cfg_cursor_ms = 3000;
    GetModuleFileNameA(g_inst, path, sizeof path);
    { char *s = strrchr(path, '\\'); if (s) *s = 0; }
    strcpy(dlldir, path);
    strncat(path, "\\zinc-input.cfg", sizeof path - strlen(path) - 1);
    f = fopen(path, "r");
    if (!f) { /* not beside the plugin: try beside ZiNc.exe, then the working directory */
        char alt[MAX_PATH], *s2;
        if (GetModuleFileNameA(NULL, alt, sizeof alt) && (s2 = strrchr(alt, '\\'))) {
            *(s2 + 1) = 0; strncat(alt, "zinc-input.cfg", sizeof alt - strlen(alt) - 1);
            if ((f = fopen(alt, "r"))) strcpy(path, alt);
        }
        if (!f && (f = fopen("zinc-input.cfg", "r"))) strcpy(path, "zinc-input.cfg");
    }
    if (!f) return;   /* no configuration, no log */
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='), *val, *semi;
        if (!eq || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        *eq = 0; val = eq + 1;
        if ((semi = strchr(val, ';'))) *semi = 0;
        { char *e = val + strlen(val); while (e > val && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0; }
        { char *k = line; while (*k == ' ' || *k == '\t') k++; { char *e = k + strlen(k); while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0; }
          if (!_stricmp(k, "deadzone")) cfg_deadzone = atoi(val);
          else if (!_stricmp(k, "xinput")) cfg_xinput = atoi(val);
          else if (!_stricmp(k, "directinput")) cfg_dinput = atoi(val);
          else if (!_stricmp(k, "analog")) cfg_analog = atoi(val);
          else if (!_stricmp(k, "logs")) cfg_log = atoi(val) != 0;
          else if (!_stricmp(k, "cursor_hide_ms")) cfg_cursor_ms = atoi(val);
          else if (!_stricmp(k, "autofire_ms")) cfg_auto_ms = atoi(val);
          else if (!_stricmp(k, "combo_step_ms")) cfg_step_ms = atoi(val);
          else if (!_stricmp(k, "combo_charge_ms")) cfg_charge_ms = atoi(val);
          else if (!_stricmp(k, "combo_charge_credit")) cfg_charge_credit = atoi(val) != 0;
          else if (!_strnicmp(k, "combo", 5) && k[5] >= '0' && k[5] <= '9') {
              int n = atoi(k + 5) - 1;
              const char *rest = k + 5;
              while (*rest >= '0' && *rest <= '9') rest++;
              if (n >= 0 && n < NCOMBO) {
                  if (!*rest) set_combo_trigger(n, val);
                  else if (!_stricmp(rest, "_seq")) { strncpy(combo_seq[n], val, 255); combo_seq[n][255] = 0; }
                  else if (!_stricmp(rest, "_player")) combos[n].player = atoi(val) == 2;
                  else if (!_stricmp(rest, "_face")) combos[n].face_left = (val[0] == 'L' || val[0] == 'l');
              }
          }
          else if (!_stricmp(k, "pad1")) cfg_pad[0] = !_stricmp(val, "none") ? -1 : atoi(val);
          else if (!_stricmp(k, "pad2")) cfg_pad[1] = !_stricmp(val, "none") ? -1 : atoi(val);
          else {
              size_t kl = strlen(k);
              if (kl > 5 && !_stricmp(k + kl - 5, "_auto")) {
                  for (i = 0; i < NROLES; i++) if (!_strnicmp(k, ROLE_NAME[i], kl - 5) && strlen(ROLE_NAME[i]) == kl - 5) cfg_auto[i] = atoi(val) != 0;
              } else for (i = 0; i < NROLES; i++) if (!_stricmp(k, ROLE_NAME[i])) set_binds(i, val);
          } }
    }
    fclose(f);
    if (cfg_log) write_log(dlldir, path);
    for (i = 0; i < NCOMBO; i++) parse_seq(&combos[i], combo_seq[i]);
    if (cfg_auto_ms < 16) cfg_auto_ms = 16;
    if (cfg_auto_ms > 500) cfg_auto_ms = 500;
    if (cfg_charge_ms < 100) cfg_charge_ms = 100;
    if (cfg_charge_ms > 3000) cfg_charge_ms = 3000;
    if (cfg_step_ms < 16) cfg_step_ms = 16;
    if (cfg_step_ms > 500) cfg_step_ms = 500;
    if (cfg_deadzone < 5) cfg_deadzone = 5;
    if (cfg_deadzone > 95) cfg_deadzone = 95;
}

/* ---------- pads ---------- */
typedef DWORD (WINAPI *XGetState_t)(DWORD, void *);
typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } XPad; /* XINPUT_GAMEPAD layout */
typedef struct { DWORD packet; XPad pad; } XState;
typedef struct { LONG axis[6]; DWORD pov[4]; BYTE btn[32]; } DJS;

typedef struct {
    int kind; /* 1 XInput, 2 DirectInput */
    int xindex;
    XPad x;
    IDirectInputDevice8A *dev;
    GUID inst;
    DJS js;
} Pad;

static Pad pads[8];
static int npads;
static HMODULE hxi;
static XGetState_t xget;
static IDirectInput8A *di;
static HWND g_hwnd;
static DWORD last_scan;
static DWORD sig_x;
static GUID sig_di[16];
static int sig_ndi;

static void release_pads(void) {
    int i;
    for (i = 0; i < npads; i++)
        if (pads[i].dev) { IDirectInputDevice8_Unacquire(pads[i].dev); IDirectInputDevice8_Release(pads[i].dev); }
    memset(pads, 0, sizeof pads);
    npads = 0;
}

/* Is this DirectInput device actually an XInput pad? (it would then show up twice) */
static int is_xinput_device(const GUID *prod) {
    UINT n = 0, i;
    RAWINPUTDEVICELIST *l;
    int found = 0;
    if (GetRawInputDeviceList(0, &n, sizeof *l) != 0 || !n) return 0;
    l = (RAWINPUTDEVICELIST *)malloc(n * sizeof *l);
    if (!l) return 0;
    if (GetRawInputDeviceList(l, &n, sizeof *l) != (UINT)-1) {
        for (i = 0; i < n && !found; i++) {
            char name[256]; UINT sz = sizeof name; unsigned vid, pid;
            RID_DEVICE_INFO info; UINT isz = sizeof info; (void)isz;
            if (l[i].dwType != RIM_TYPEHID) continue;
            if (GetRawInputDeviceInfoA(l[i].hDevice, RIDI_DEVICENAME, name, &sz) == (UINT)-1) continue;
            if (!strstr(name, "IG_")) continue;
            { char *v = strstr(name, "VID_"), *p = strstr(name, "PID_");
              if (!v || !p) continue;
              vid = (unsigned)strtoul(v + 4, 0, 16); pid = (unsigned)strtoul(p + 4, 0, 16);
              if (prod->Data1 == ((pid << 16) | vid)) found = 1; }
        }
    }
    free(l);
    return found;
}

typedef struct { GUID g[16]; int n; GUID prod[16]; } EnumCtx;
static BOOL CALLBACK enum_cb(const DIDEVICEINSTANCEA *inst, void *ctx) {
    EnumCtx *c = (EnumCtx *)ctx;
    if (c->n < 16 && !is_xinput_device(&inst->guidProduct)) { c->g[c->n] = inst->guidInstance; c->prod[c->n] = inst->guidProduct; c->n++; }
    return DIENUM_CONTINUE;
}

static DIOBJECTDATAFORMAT fmt_obj[6 + 4 + 32];
static DIDATAFORMAT fmt;
static void build_format(void) {
    int i, k = 0;
    const GUID *ax[6] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis};
    for (i = 0; i < 6; i++, k++) {
        fmt_obj[k].pguid = ax[i]; fmt_obj[k].dwOfs = i * 4;
        fmt_obj[k].dwType = DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; fmt_obj[k].dwFlags = 0;
    }
    for (i = 0; i < 4; i++, k++) {
        fmt_obj[k].pguid = &GUID_POV; fmt_obj[k].dwOfs = 24 + i * 4;
        fmt_obj[k].dwType = DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; fmt_obj[k].dwFlags = 0;
    }
    for (i = 0; i < 32; i++, k++) {
        fmt_obj[k].pguid = 0; fmt_obj[k].dwOfs = 40 + i;
        fmt_obj[k].dwType = DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL; fmt_obj[k].dwFlags = 0;
    }
    fmt.dwSize = sizeof fmt; fmt.dwObjSize = sizeof(DIOBJECTDATAFORMAT); fmt.dwFlags = DIDF_ABSAXIS;
    fmt.dwDataSize = sizeof(DJS); fmt.dwNumObjs = k; fmt.rgodf = fmt_obj;
}

/* Looking for pads is slow: XInputGetState on an empty slot and the DirectInput enumeration can each take hundreds of
   milliseconds, which showed up as a stall of the whole game every two seconds. So the periodic check runs on its own
   thread and the game thread only compares the result. */
static HANDLE scan_thread, scan_stop;
static CRITICAL_SECTION scan_cs;
static DWORD snap_x;
static EnumCtx snap_ctx;
static volatile LONG snap_new;

static void probe_pads(DWORD *xmask, EnumCtx *ctx) {
    int u;
    *xmask = 0;
    memset(ctx, 0, sizeof *ctx);
    if (cfg_xinput && xget)
        for (u = 0; u < 4; u++) { XState st; if (xget(u, &st) == 0) *xmask |= 1u << u; }
    if (cfg_dinput && di) IDirectInput8_EnumDevices(di, DI8DEVCLASS_GAMECTRL, enum_cb, ctx, DIEDFL_ATTACHEDONLY);
}

static DWORD WINAPI scan_proc(LPVOID arg) {
    (void)arg;
    while (WaitForSingleObject(scan_stop, 2000) == WAIT_TIMEOUT) {
        DWORD x;
        EnumCtx c;
        probe_pads(&x, &c);
        EnterCriticalSection(&scan_cs);
        snap_x = x; snap_ctx = c; snap_new = 1;
        LeaveCriticalSection(&scan_cs);
    }
    return 0;
}

static void scan_start(void) {
#ifndef ZN_TEST
    if (scan_thread) return;
    InitializeCriticalSection(&scan_cs);
    snap_new = 0;
    scan_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    scan_thread = scan_stop ? CreateThread(NULL, 0, scan_proc, NULL, 0, NULL) : NULL;
#endif
}

static void scan_end(void) {
    if (!scan_thread) return;
    SetEvent(scan_stop);
    WaitForSingleObject(scan_thread, 3000);
    CloseHandle(scan_thread); CloseHandle(scan_stop);
    DeleteCriticalSection(&scan_cs);
    scan_thread = scan_stop = NULL;
}

static void scan_pads(int force) {
#ifdef ZN_TEST
    (void)force; return;
#endif
    DWORD xmask = 0;
    EnumCtx ctx;
    int u, i, changed;
    if (force || !scan_thread) {
        if (!force && GetTickCount() - last_scan < 2000) return;
        last_scan = GetTickCount();
        probe_pads(&xmask, &ctx);
    } else {
        if (!snap_new) return;
        EnterCriticalSection(&scan_cs);
        xmask = snap_x; ctx = snap_ctx; snap_new = 0;
        LeaveCriticalSection(&scan_cs);
    }
    changed = force || xmask != sig_x || ctx.n != sig_ndi;
    for (i = 0; !changed && i < ctx.n; i++) if (memcmp(&ctx.g[i], &sig_di[i], sizeof(GUID))) changed = 1;
    if (!changed) return;
    release_pads();
    sig_x = xmask; sig_ndi = ctx.n;
    memcpy(sig_di, ctx.g, sizeof sig_di);
    for (u = 0; u < 4; u++) if ((xmask >> u) & 1) { pads[npads].kind = 1; pads[npads].xindex = u; npads++; }
    for (i = 0; i < ctx.n && npads < 8; i++) {
        IDirectInputDevice8A *dev = 0;
        int a;
        if (FAILED(IDirectInput8_CreateDevice(di, &ctx.g[i], &dev, 0)) || !dev) continue;
        if (FAILED(IDirectInputDevice8_SetDataFormat(dev, &fmt))) { IDirectInputDevice8_Release(dev); continue; }
        IDirectInputDevice8_SetCooperativeLevel(dev, g_hwnd ? g_hwnd : GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
        for (a = 0; a < 6; a++) {
            DIPROPRANGE r;
            r.diph.dwSize = sizeof r; r.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            r.diph.dwHow = DIPH_BYOFFSET; r.diph.dwObj = a * 4; r.lMin = -1000; r.lMax = 1000;
            IDirectInputDevice8_SetProperty(dev, DIPROP_RANGE, &r.diph);
        }
        IDirectInputDevice8_Acquire(dev);
        pads[npads].kind = 2; pads[npads].dev = dev; pads[npads].inst = ctx.g[i];
        npads++;
    }
}

static void poll_pads(void) {
    int i;
#ifdef ZN_TEST
    return;
#endif
    for (i = 0; i < npads; i++) {
        Pad *p = &pads[i];
        if (p->kind == 1) {
            XState st;
            if (xget && xget(p->xindex, &st) == 0) p->x = st.pad; else memset(&p->x, 0, sizeof p->x);
        } else if (p->dev) {
            HRESULT hr;
            IDirectInputDevice8_Poll(p->dev);
            hr = IDirectInputDevice8_GetDeviceState(p->dev, sizeof p->js, &p->js);
            if (FAILED(hr)) {
                IDirectInputDevice8_Acquire(p->dev);
                hr = IDirectInputDevice8_GetDeviceState(p->dev, sizeof p->js, &p->js);
                if (FAILED(hr)) memset(&p->js, 0, sizeof p->js);
            }
        }
    }
}

static Pad *pad_of(int player) {
    int want = cfg_pad[player];
    if (want < 0) return 0;
    if (want == 0) want = player + 1;
    return want <= npads ? &pads[want - 1] : 0;
}

/* ---------- evaluation ---------- */
static int thr(void) { return 32767 * cfg_deadzone / 100; }

static int x_down(const XPad *x, int code) {
    int t = thr();
    if (code < 14) return (x->buttons & XMASK[code]) != 0;
    switch (code) {
    case 20: return x->lt > 255 * cfg_deadzone / 100;
    case 21: return x->rt > 255 * cfg_deadzone / 100;
    case 30: return x->ly > t;  case 31: return x->ly < -t; case 32: return x->lx < -t; case 33: return x->lx > t;
    case 34: return x->ry > t;  case 35: return x->ry < -t; case 36: return x->rx < -t; case 37: return x->rx > t;
    }
    return 0;
}

static int pov_dir(DWORD pov, int dir) { /* dir 0 up,1 down,2 left,3 right; pov in 1/100 degree, 0xFFFF = centred */
    if ((pov & 0xFFFF) == 0xFFFF) return 0;
    pov %= 36000;
    switch (dir) {
    case 0: return pov >= 31500 || pov <= 4500;
    case 1: return pov >= 13500 && pov <= 22500;
    case 2: return pov >= 22500 && pov <= 31500;
    default: return pov >= 4500 && pov <= 13500;
    }
}

static int j_down(const DJS *j, int code) {
    int t = 1000 * cfg_deadzone / 100;
    if (code < 32) return (j->btn[code] & 0x80) != 0;
    if (code >= 40 && code < 44) return pov_dir(j->pov[0], code - 40);
    if (code >= 50 && code < 62) {
        int a = (code - 50) / 2, positive = (code - 50) & 1;
        return positive ? j->axis[a] > t : j->axis[a] < -t;
    }
    return 0;
}

static int bind_down(const Bind *b, const Pad *p) {
    switch (b->src) {
    case 'K': return KEYDOWN(b->code);
    case 'X': return p && p->kind == 1 && x_down(&p->x, b->code);
    case 'J': return p && p->kind == 2 && j_down(&p->js, b->code);
    }
    return 0;
}

/* built-in direction support: D-pad / left stick (XInput), hat / X,Y axes (DirectInput) */
static int dir_extra(const Pad *p, int dir) { /* dir: 0 up, 1 down, 2 left, 3 right */
    static const int axis_code[4] = {52, 53, 50, 51}; /* Y-, Y+, X-, X+ */
    if (!cfg_analog || !p) return 0;
    if (p->kind == 1) return x_down(&p->x, 10 + dir) || x_down(&p->x, 30 + dir);
    return j_down(&p->js, 40 + dir) || j_down(&p->js, axis_code[dir]);
}

static unsigned macro_mask[2];

/* how long each real direction (up, down, left, right) of a player has been held (for combo_charge_credit) */
static DWORD dir_t0[2][4];
static int real_dir(int pl, int r) {
    const Pad *p = pad_of(pl);
    int role = pl * 11 + r, i, down = 0;
    for (i = 0; i < nbinds[role] && !down; i++) down = bind_down(&binds[role][i], p);
    return down || dir_extra(p, r);
}

/* the length of the current step; with combo_charge_credit the first step of a charge move is shortened by what the player has already charged */
static DWORD step_len(int pl) {
    const Combo *c = macro[pl].c;
    int i = macro[pl].idx;
    DWORD d = c->dur[i] ? c->dur[i] : (DWORD)cfg_step_ms;
    if (i == 0 && c->dur[0] && macro[pl].credit) { d = d > macro[pl].credit ? d - macro[pl].credit : 0; if (d < (DWORD)cfg_step_ms) d = (DWORD)cfg_step_ms; }
    return d;
}

/* A step is held for at least cfg_step_ms AND until the game has polled it at least once, and the macro moves on
   by one step per poll at most - so a slow or irregular poll rate can never make the game miss a step. */
static void update_macros(void) {
    int i, pl, b;
    DWORD now = NOW();
    if (cfg_charge_credit) {
        for (pl = 0; pl < 2; pl++) {
            for (b = 0; b < 4; b++) {
                if (real_dir(pl, b)) { if (!dir_t0[pl][b]) dir_t0[pl][b] = now ? now : 1; }
                else dir_t0[pl][b] = 0;
            }
        }
    }
    for (pl = 0; pl < 2; pl++) {
        macro_mask[pl] = 0;
        if (!macro[pl].active) continue;
        if (macro[pl].seen && now - macro[pl].t0 >= step_len(pl)) {
            macro[pl].idx++; macro[pl].t0 = now; macro[pl].seen = 0;
            if (macro[pl].idx >= macro[pl].c->nsteps) { macro[pl].active = 0; clog("combo %d done after %lu ms", (int)(macro[pl].c - combos) + 1, (unsigned long)(now - macro[pl].start)); continue; }
        }
        macro_mask[pl] = macro[pl].c->steps[macro[pl].idx];
        macro[pl].seen = 1;
    }
    for (i = 0; i < NCOMBO; i++) {
        Combo *c = &combos[i];
        const Pad *pad;
        int down = 0;
        if (!c->ntrig || !c->nsteps) continue;
        pad = pad_of(c->player);
        for (b = 0; b < c->ntrig && !down; b++) down = bind_down(&c->trig[b], pad);
        if (down && !c->was_down && !macro[c->player].active) {
            macro[c->player].active = 1; macro[c->player].idx = 0; macro[c->player].seen = 1;
            macro[c->player].t0 = now; macro[c->player].c = c;
            macro[c->player].start = now;
            macro[c->player].credit = 0;
            if (cfg_charge_credit && c->dur[0] && (c->steps[0] & 15)) {   /* the charge direction was already held: count that time */
                DWORD cr = 0xFFFFFFFFu;
                for (b = 0; b < 4; b++) if (c->steps[0] & (1u << b)) { DWORD held = dir_t0[c->player][b] ? now - dir_t0[c->player][b] : 0; if (held < cr) cr = held; }
                macro[c->player].credit = cr == 0xFFFFFFFFu ? 0 : cr;
            }
            clog("combo %d started (player %d, %d steps, charge %d ms, step %d ms, credit %lu ms)", i + 1, c->player + 1, c->nsteps, cfg_charge_ms, cfg_step_ms, (unsigned long)macro[c->player].credit);
            macro_mask[c->player] = c->steps[0];
        }
        c->was_down = down;
    }
}

static int role_down(int role) {
    int i;
    const Pad *p;
    if (role >= 24) { /* test / service: keyboard or any pad */
        int k;
        for (i = 0; i < nbinds[role]; i++) {
            if (binds[role][i].src == 'K') { if (bind_down(&binds[role][i], 0)) return 1; }
            else for (k = 0; k < npads; k++) if (bind_down(&binds[role][i], &pads[k])) return 1;
        }
        return 0;
    }
    if (role >= 22) { /* coin 1 follows pad 1, coin 2 follows pad 2 */
        p = pad_of(role - 22);
        for (i = 0; i < nbinds[role]; i++) if (bind_down(&binds[role][i], p)) return 1;
        return 0;
    }
    p = pad_of(role / 11);
    { int r = role % 11; if ((macro_mask[role / 11] >> r) & 1) return 1; }
    {
        int r = role % 11, down = 0;
        for (i = 0; i < nbinds[role] && !down; i++) down = bind_down(&binds[role][i], p);
        if (r >= 4 && r <= 9 && cfg_auto[role]) { /* autofire: pulse while the button is held */
            if (!down) { af_was[role] = 0; return 0; }
            if (!af_was[role]) { af_was[role] = 1; af_t0[role] = NOW(); }
            return ((((NOW() - af_t0[role]) / (DWORD)cfg_auto_ms)) & 1) == 0;
        }
        if (down) return 1;
        if (r < 4 && dir_extra(p, r)) return 1;
    }
    return 0;
}

/* extra buttons that only some boards use (Konami GV, ...) stay on their letter key unless it is taken */
static int vk_taken(int vk) {
    int r, i;
    for (r = 0; r < NROLES; r++)
        for (i = 0; i < nbinds[r]; i++) if (binds[r][i].src == 'K' && binds[r][i].code == vk) return 1;
    return 0;
}

static const Ent *g_ents;
static int g_nents;
static char extra_ok[64];


/* ---------- hotkeys: Pause (pause / resume) and Alt+Enter (fullscreen), only while the game window has the focus ---------- */
static int hk_prev_pause, hk_prev_ae, hk_fs, hk_have;
static RECT hk_rc;
static LONG hk_style, hk_ex;
static HMENU hk_menu;

static int game_focused(void) {
    HWND f = GetForegroundWindow();
    DWORD pid = 0;
    if (!f) return 0;
    GetWindowThreadProcessId(f, &pid);
    return pid == GetCurrentProcessId();
}

/* Mutes (or un-mutes) all sound of this process through its Windows audio session, so a paused game is silent
   whatever sound system ZiNc uses. Without a sound device (or before Windows Vista) nothing happens. */
static void set_audio_mute(int mute) {
    HRESULT hr, init;
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioSessionManager *mgr = NULL;
    ISimpleAudioVolume *vol = NULL;
    init = CoInitializeEx(NULL, COINIT_MULTITHREADED);   /* RPC_E_CHANGED_MODE: COM is already up in another mode, still usable */
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&en);
    if (SUCCEEDED(hr)) hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (SUCCEEDED(hr)) hr = IMMDevice_Activate(dev, &IID_IAudioSessionManager, CLSCTX_ALL, NULL, (void **)&mgr);
    if (SUCCEEDED(hr)) hr = IAudioSessionManager_GetSimpleAudioVolume(mgr, NULL, FALSE, &vol);   /* the process's own session */
    if (SUCCEEDED(hr)) ISimpleAudioVolume_SetMute(vol, mute ? TRUE : FALSE, NULL);
    if (vol) ISimpleAudioVolume_Release(vol);
    if (mgr) IAudioSessionManager_Release(mgr);
    if (dev) IMMDevice_Release(dev);
    if (en) IMMDeviceEnumerator_Release(en);
    if (init == S_OK || init == S_FALSE) CoUninitialize();
}

/* the emulation waits in here, the window keeps answering Windows; Pause again, Esc or closing the window ends it */
/* ---------- mouse cursor: visible in the game window, hidden after a while without mouse movement ----------
   ZiNc's own window procedure answers WM_SETCURSOR with SetCursor(NULL), which hides the cursor over the game window; so the window is
   subclassed here (it then answers with the arrow, or with NULL after the idle time), and a renderer's ShowCursor(FALSE) is undone. */
static HCURSOR cur_arrow;
static int cur_hidden, cur_extra, cur_active, cur_attached, cur_logn;
static POINT cur_pt;
static DWORD cur_t, cur_check, cur_wtid;
#define CUR_SUB_ID 0xC0DE0001u
typedef BOOL (WINAPI *SetSub_t)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR);   /* comctl32 is looked up at run time: without it the cursor still works (less reliably) */
typedef BOOL (WINAPI *RemSub_t)(HWND, SUBCLASSPROC, UINT_PTR);
typedef LRESULT (WINAPI *DefSub_t)(HWND, UINT, WPARAM, LPARAM);
static SetSub_t pSetSub;
static RemSub_t pRemSub;
static DefSub_t pDefSub;

static int cur_counter_ok(void) { return g_hwnd && (cur_wtid == GetCurrentThreadId() || cur_attached); }

static int cur_over_window(const POINT *pt) {
    RECT rc;
    POINT p = *pt;
    HWND w;
    if (!ScreenToClient(g_hwnd, &p)) return 0;
    GetClientRect(g_hwnd, &rc);
    if (!PtInRect(&rc, p)) return 0;
    w = WindowFromPoint(*pt);
    return w == g_hwnd || (w && IsChild(g_hwnd, w));
}

static LRESULT CALLBACK cur_sub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (m == WM_SETCURSOR && cur_active && LOWORD(l) == HTCLIENT) { SetCursor(cur_hidden ? NULL : cur_arrow); return TRUE; }
    /* title bar, borders, menu: ZiNc's own procedure would hide the cursor there too; the system's default gives the arrow / resize cursors */
    if (m == WM_SETCURSOR && cur_active && LOWORD(l) != HTCLIENT) return DefWindowProcA(h, m, w, l);
    if (m == WM_NCDESTROY && pRemSub) pRemSub(h, cur_sub, id);
    return pDefSub(h, m, w, l);
}

/* the system asks the window thread for its cursor again right now (not only at the next mouse move) */
static void cur_refresh(void) {
    DWORD_PTR res;
    POINT pt;
    if (!GetCursorPos(&pt) || !cur_over_window(&pt)) return;
    SendMessageTimeoutA(g_hwnd, WM_SETCURSOR, (WPARAM)g_hwnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE), SMTO_ABORTIFHUNG, 50, &res);
}

/* a renderer may have hidden the cursor with ShowCursor(FALSE) (that counts up and down): bring the count back */
static void cur_fix_counter(void) {
    CURSORINFO ci;
    int k;
    if (!cur_counter_ok()) return;
    for (k = 0; k < 20; k++) {
        memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
        if (!GetCursorInfo(&ci) || (ci.flags & CURSOR_SHOWING)) break;
        ShowCursor(TRUE); cur_extra++;
    }
}

static void cursor_show(int on) {
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    if (on) {
        SetClassLongPtrA(g_hwnd, GCLP_HCURSOR, (LONG_PTR)cur_arrow);
        cur_fix_counter();
        cur_hidden = 0;
    } else {
        SetClassLongPtrA(g_hwnd, GCLP_HCURSOR, 0);
        if (cur_counter_ok()) while (cur_extra > 0) { ShowCursor(FALSE); cur_extra--; }
        cur_hidden = 1;
    }
    SetCursor(on ? cur_arrow : NULL);
    cur_refresh();
    clog("cursor: %s", on ? "shown" : "hidden");
}

static void cursor_init(void) {
    DWORD ct = GetCurrentThreadId();
    char cls[80] = "";
    cur_active = 0; cur_attached = 0; cur_logn = 0;
    if (cfg_cursor_ms < 0 || !g_hwnd || !IsWindow(g_hwnd)) { clog("cursor: off (hide time %d, window %p)", cfg_cursor_ms, (void *)g_hwnd); return; }
    cur_wtid = GetWindowThreadProcessId(g_hwnd, NULL);
    if (cur_wtid && cur_wtid != ct) cur_attached = AttachThreadInput(ct, cur_wtid, TRUE) ? 1 : 0;
    GetClassNameA(g_hwnd, cls, sizeof cls);
    cur_arrow = LoadCursor(NULL, IDC_ARROW);
    cur_extra = 0; cur_hidden = 1;
    GetCursorPos(&cur_pt);
    cur_t = NOW(); cur_check = 0;
    cur_active = 1;
    {
        HMODULE cc = LoadLibraryA("comctl32.dll");
        pSetSub = cc ? (SetSub_t)GetProcAddress(cc, "SetWindowSubclass") : 0;
        pRemSub = cc ? (RemSub_t)GetProcAddress(cc, "RemoveWindowSubclass") : 0;
        pDefSub = cc ? (DefSub_t)GetProcAddress(cc, "DefSubclassProc") : 0;
        if (pSetSub && pRemSub && pDefSub) pSetSub(g_hwnd, cur_sub, CUR_SUB_ID, 0); else { pSetSub = 0; clog("cursor: no window subclassing available"); }
    }
    clog("cursor: window %p class %s, window thread %lu, plugin thread %lu, attached %d, hide after %d ms", (void *)g_hwnd, cls, (unsigned long)cur_wtid, (unsigned long)ct, cur_attached, cfg_cursor_ms);
    cursor_show(1);
}

/* every frame: show the cursor when the mouse moves, hide it after cfg_cursor_ms without movement */
static void cursor_poll(void) {
    POINT pt;
    int moved, over;
    if (!cur_active || !g_hwnd || !IsWindow(g_hwnd)) return;
    if (!GetCursorPos(&pt)) return;
    over = cur_over_window(&pt);
    moved = pt.x != cur_pt.x || pt.y != cur_pt.y || ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000);
    if (moved) { cur_pt = pt; cur_t = NOW(); if (cur_hidden) cursor_show(1); }
    else if (!cur_hidden && cfg_cursor_ms > 0 && over && NOW() - cur_t >= (DWORD)cfg_cursor_ms) cursor_show(0);
    if (over) {
        if (!cur_hidden) {
            SetCursor(cur_arrow);
            if (NOW() - cur_check > 300) { cur_check = NOW(); cur_fix_counter(); }
        } else SetCursor(NULL);
    }
    if (cur_logn < 8 && over) {   /* what the system says, for the log */
        CURSORINFO ci;
        memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
        GetCursorInfo(&ci);
        clog("cursor: over the window, %s, system cursor flags %lu handle %p (arrow %p)", cur_hidden ? "hidden by us" : "visible", (unsigned long)ci.flags, (void *)ci.hCursor, (void *)cur_arrow);
        cur_logn++;
    }
}

static void cursor_done(void) {
    if (!cur_active) return;
    cur_active = 0;
    if (g_hwnd && IsWindow(g_hwnd)) { if (pSetSub && pRemSub) pRemSub(g_hwnd, cur_sub, CUR_SUB_ID); SetClassLongPtrA(g_hwnd, GCLP_HCURSOR, (LONG_PTR)cur_arrow); }
    if (cur_counter_ok()) while (cur_extra > 0) { ShowCursor(FALSE); cur_extra--; }
    if (cur_attached) { AttachThreadInput(GetCurrentThreadId(), cur_wtid, FALSE); cur_attached = 0; }
}

static void do_pause(void) {
    char title[256], paused[300];
    int prev = 1, own = g_hwnd && GetWindowThreadProcessId(g_hwnd, NULL) == GetCurrentThreadId();
    title[0] = 0;
    set_audio_mute(1);
    if (g_hwnd) {
        GetWindowTextA(g_hwnd, title, sizeof title);
        _snprintf(paused, sizeof paused, "%s  [Paused - press Pause to continue]", title);
        paused[sizeof paused - 1] = 0;
        SetWindowTextA(g_hwnd, paused);
    }
    for (;;) {
        int p;
        if (own) {
            MSG m;
            while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); goto done; }
                TranslateMessage(&m);
                DispatchMessageA(&m);
            }
        }
        if (g_hwnd && !IsWindow(g_hwnd)) goto done;
        cursor_poll();
        Sleep(15);
        p = raw_key(VK_PAUSE);
        if (p && !prev) break;
        /* ZiNc reads Esc (quit) itself, in its own loop, which is stopped in here: hand over to it right away */
        if (raw_key(VK_ESCAPE) && game_focused()) break;
        prev = p;
    }
done:
    set_audio_mute(0);
    if (g_hwnd && IsWindow(g_hwnd)) SetWindowTextA(g_hwnd, title);
}

/* Fullscreen for renderers that cannot resize their picture (OpenGL, Direct3D): the screen is switched to the resolution of
   the game window when the display supports it (the picture then fills the screen exactly, 4:3 modes included), otherwise
   the borderless window just covers the monitor. Back: the old mode and the window as it was. */
static int hk_modechanged;
static HWND hk_backdrop;   /* black window behind the game window (Direct3D 6 renderer) */

static void toggle_fullscreen(void) {
    MONITORINFO mi;
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    if (!hk_fs) {
        LONG st = GetWindowLongA(g_hwnd, GWL_STYLE);
        RECT cr;
        DEVMODEA dm;
        if (!(st & WS_CAPTION)) return;   /* already fullscreen (set up by the renderer) */
        hk_style = st;
        hk_ex = GetWindowLongA(g_hwnd, GWL_EXSTYLE);
        hk_menu = GetMenu(g_hwnd);
        GetWindowRect(g_hwnd, &hk_rc);
        GetClientRect(g_hwnd, &cr);
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        if (!GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        if (GetModuleHandleA("ddraw.dll")) {
            /* The Direct3D 6 (DirectDraw) renderer loses its surfaces when the screen mode changes and always draws its picture
               at the window's own size: no mode switch, the window keeps its size, goes borderless and is centered on a black screen. */
            int cw = cr.right, ch = cr.bottom, mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
            WNDCLASSA wc;
            memset(&wc, 0, sizeof wc);
            wc.lpfnWndProc = DefWindowProcA;
            wc.hInstance = g_inst;
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.lpszClassName = "ZincBackdrop";
            RegisterClassA(&wc);   /* fails harmlessly when it already exists */
            hk_backdrop = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "ZincBackdrop", "", WS_POPUP | WS_VISIBLE,
                                          mi.rcMonitor.left, mi.rcMonitor.top, mw, mh, NULL, NULL, g_inst, NULL);
            SetMenu(g_hwnd, NULL);
            SetWindowLongA(g_hwnd, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
            SetWindowLongA(g_hwnd, GWL_EXSTYLE, hk_ex & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
            SetWindowPos(g_hwnd, HWND_TOPMOST, mi.rcMonitor.left + (mw - cw) / 2, mi.rcMonitor.top + (mh - ch) / 2, cw, ch, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            hk_fs = 1;
            hk_have = 1;
            return;
        }
        /* the primary display only: the mode of another monitor cannot be changed this way */
        if ((mi.dwFlags & MONITORINFOF_PRIMARY) && cr.right >= 320 && cr.bottom >= 240 &&
            (cr.right != mi.rcMonitor.right - mi.rcMonitor.left || cr.bottom != mi.rcMonitor.bottom - mi.rcMonitor.top)) {
            memset(&dm, 0, sizeof dm);
            dm.dmSize = sizeof dm;
            if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
                dm.dmPelsWidth = (DWORD)cr.right; dm.dmPelsHeight = (DWORD)cr.bottom;
                dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
                if (ChangeDisplaySettingsA(&dm, CDS_TEST) == DISP_CHANGE_SUCCESSFUL &&
                    ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL) {
                    hk_modechanged = 1;
                    GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
                }
            }
        }
        SetMenu(g_hwnd, NULL);
        SetWindowLongA(g_hwnd, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
        SetWindowLongA(g_hwnd, GWL_EXSTYLE, hk_ex & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
        SetWindowPos(g_hwnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        hk_fs = 1;
        hk_have = 1;
    } else {
        if (hk_modechanged) { ChangeDisplaySettingsA(NULL, 0); hk_modechanged = 0; }
        if (hk_backdrop) { DestroyWindow(hk_backdrop); hk_backdrop = NULL; }
        SetWindowLongA(g_hwnd, GWL_STYLE, hk_style);
        SetWindowLongA(g_hwnd, GWL_EXSTYLE, hk_ex);
        if (hk_menu) SetMenu(g_hwnd, hk_menu);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, hk_rc.left, hk_rc.top, hk_rc.right - hk_rc.left, hk_rc.bottom - hk_rc.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        hk_fs = 0;
    }
}

/* Alt+Tab: a borderless fullscreen game window is topmost, so the program switched to would stay hidden behind it. While another program
   has the focus the window (and the black backdrop of the Direct3D 6 mode) is not topmost; it is again when the focus comes back. */
static int hk_dropped;
static void focus_guard(void) {
    LONG ex, st;
    int fg;
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    fg = game_focused();
    ex = GetWindowLongA(g_hwnd, GWL_EXSTYLE);
    st = GetWindowLongA(g_hwnd, GWL_STYLE);
    if (!fg) {
        if (!hk_dropped && (ex & WS_EX_TOPMOST) && (st & WS_POPUP) && !(st & WS_CAPTION)) {
            SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (hk_backdrop) SetWindowPos(hk_backdrop, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            hk_dropped = 1;
        }
    } else if (hk_dropped) {
        if (hk_backdrop) SetWindowPos(hk_backdrop, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        hk_dropped = 0;
    }
}

static void hotkeys(void) {
    focus_guard();
    int pause = raw_key(VK_PAUSE), ae = raw_key(VK_MENU) && raw_key(VK_RETURN);
    if (game_focused()) {
        /* a renderer that does its own Alt+Enter marks the window, then it is left alone here */
        if (ae && !hk_prev_ae && !(g_hwnd && GetPropA(g_hwnd, "ZincFullscreenHandled"))) toggle_fullscreen();
        if (pause && !hk_prev_pause) { do_pause(); pause = raw_key(VK_PAUSE); }
    }
    hk_prev_ae = ae;
    hk_prev_pause = pause;
}

/* ---------- ZiNc plugin exports ---------- */
int __stdcall ZN_JammaOpen(HWND hwnd, int type, int unused1, int unused2) {
    int idx, i;
    HRESULT hr;
    (void)unused1; (void)unused2;
    g_hwnd = hwnd;
    idx = (type >= 1 && type <= 16) ? TYPEMAP[type - 1] : 4;
    g_ents = TABLES[idx].e; g_nents = TABLES[idx].n;
    load_config();
    for (i = 0; i < g_nents && i < 64; i++) extra_ok[i] = g_ents[i].role < 0 && !vk_taken(g_ents[i].vk);
    build_format();
    hxi = 0; xget = 0;
    if (cfg_xinput) {
        const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", 0};
        for (i = 0; dlls[i] && !hxi; i++) hxi = LoadLibraryA(dlls[i]);
        if (hxi) xget = (XGetState_t)GetProcAddress(hxi, "XInputGetState");
    }
    di = 0;
    if (cfg_dinput) {
        HMODULE hd = LoadLibraryA("dinput8.dll");
        typedef HRESULT (WINAPI *Create_t)(HINSTANCE, DWORD, REFIID, void **, void *);
        Create_t create = hd ? (Create_t)GetProcAddress(hd, "DirectInput8Create") : 0;
        hr = create ? create(g_inst, 0x0800, &ZIID_DI8A, (void **)&di, 0) : E_FAIL;
        if (FAILED(hr)) di = 0;
    }
    sig_x = 0; sig_ndi = -1;
    scan_pads(1);
    scan_start();
    cursor_init();
    return 0;
}

int __stdcall ZN_JammaClose(void) {
    scan_end();
    cursor_done();
    if (hk_modechanged) { ChangeDisplaySettingsA(NULL, 0); hk_modechanged = 0; }
    if (hk_backdrop) { DestroyWindow(hk_backdrop); hk_backdrop = NULL; }
    release_pads();
    if (di) { IDirectInput8_Release(di); di = 0; }
    if (hxi) { FreeLibrary(hxi); hxi = 0; xget = 0; }
    return 0;
}

/* for the launcher's Test tab: which actions (the roles of zinc-input.cfg, bit n = role n) are pressed right now, without game bits, hotkeys or
   combo macros. Returns the number of pads found. */
int __stdcall ZN_InputPoll(unsigned long *mask) {
    int i;
    unsigned long m = 0;
    scan_pads(0);
    poll_pads();
    for (i = 0; i < NROLES; i++) if (role_down(i)) m |= 1ul << i;
    if (mask) *mask = m;
    return npads;
}

int __stdcall ZN_JammaRead(unsigned long *out) {
    int i;
    unsigned char role_state[NROLES];
    hotkeys();
    cursor_poll();
    scan_pads(0);
    poll_pads();
    update_macros();
    for (i = 0; i < NROLES; i++) role_state[i] = (unsigned char)role_down(i);
    for (i = 0; i < 5; i++) out[i] = 0;
    for (i = 0; i < 4; i++) if (role_state[22 + i]) out[0] |= SYSMASK[i];
    for (i = 0; i < g_nents; i++) {
        const Ent *e = &g_ents[i];
        int down = e->role >= 0 ? role_state[e->role] : (i < 64 && extra_ok[i] && KEYDOWN(e->vk));
        if (down) out[e->out] |= 1ul << e->bit;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) g_inst = h;
    return TRUE;
}

#ifdef ZN_TEST
void __cdecl ZT_Time(unsigned t) { test_time = t; }
void __cdecl ZT_Key(int vk, int down) { test_keys[vk & 255] = (unsigned char)down; }
void __cdecl ZT_Reset(void) { memset(test_keys, 0, sizeof test_keys); memset(pads, 0, sizeof pads); npads = 0; }
void __cdecl ZT_AddXPad(void) { pads[npads].kind = 1; npads++; }
void __cdecl ZT_AddJPad(void) { int i; pads[npads].kind = 2; for (i = 0; i < 4; i++) pads[npads].js.pov[i] = 0xFFFFFFFF; npads++; }
void __cdecl ZT_X(int slot, int buttons, int lx, int ly, int rt) { pads[slot].x.buttons = (WORD)buttons; pads[slot].x.lx = (SHORT)lx; pads[slot].x.ly = (SHORT)ly; pads[slot].x.rt = (BYTE)rt; }
void __cdecl ZT_JBtn(int slot, int b, int down) { pads[slot].js.btn[b] = down ? 0x80 : 0; }
void __cdecl ZT_JAxis(int slot, int a, int v) { pads[slot].js.axis[a] = v; }
void __cdecl ZT_JPov(int slot, int deg100) { pads[slot].js.pov[0] = (DWORD)deg100; }
#endif
