/* Input plugin settings: key / pad bindings, autofire and combo macros (zinc-input.cfg) */
#include "common.h"
#include <mmsystem.h>

InputState g_in;

static const struct { const char *id, *key, *x, *j; } defaults[] = {
    {"p1_up", "W", "DPAD_UP", "POV_UP"}, {"p1_down", "S", "DPAD_DOWN", "POV_DOWN"}, {"p1_left", "A", "DPAD_LEFT", "POV_LEFT"}, {"p1_right", "D", "DPAD_RIGHT", "POV_RIGHT"},
    {"p1_b1", "NUM4", "X", "B1"}, {"p1_b2", "NUM5", "Y", "B2"}, {"p1_b3", "NUM6", "RB", "B3"},
    {"p1_b4", "NUM1", "A", "B4"}, {"p1_b5", "NUM2", "B", "B5"}, {"p1_b6", "NUM3", "RT", "B6"},
    {"p1_start", "ENTER", "START", "B10"},
    {"p2_up", "UP", "DPAD_UP", "POV_UP"}, {"p2_down", "DOWN", "DPAD_DOWN", "POV_DOWN"}, {"p2_left", "LEFT", "DPAD_LEFT", "POV_LEFT"}, {"p2_right", "RIGHT", "DPAD_RIGHT", "POV_RIGHT"},
    {"p2_b1", "U", "X", "B1"}, {"p2_b2", "I", "Y", "B2"}, {"p2_b3", "O", "RB", "B3"},
    {"p2_b4", "J", "A", "B4"}, {"p2_b5", "K", "B", "B5"}, {"p2_b6", "L", "RT", "B6"},
    {"p2_start", "Y", "START", "B10"},
    {"coin1", "RSHIFT", "BACK", "B9"}, {"coin2", "H", "BACK", "B9"}, {"test", "F4", "", ""}, {"service", "F7", "", ""},
};

#define CFG_VERSION 3   /* (3: the directions have D-pad / hat bindings of their own) bump when the default layout changes so stale files from older builds are ignored */

static int has_autofire(const RoleRow *r) {
    return strlen(r->id) == 5 && r->id[0] == 'p' && r->id[2] == '_' && r->id[3] == 'b' && r->id[4] >= '1' && r->id[4] <= '6';
}

static void row_defaults_pad(RoleRow *r);

static void row_defaults(RoleRow *r) {
    size_t i;
    for (i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
        if (!strcmp(defaults[i].id, r->id)) {
            strcpy(r->key, defaults[i].key); strcpy(r->x, defaults[i].x); strcpy(r->j, defaults[i].j);
            return;
        }
    r->key[0] = r->x[0] = r->j[0] = 0;
}

/* ---------------------------------------------------------------- names */
typedef struct { char token[16]; char label[28]; } Item;
static const Item xItems[] = {
    {"A", "A"}, {"B", "B"}, {"X", "X"}, {"Y", "Y"}, {"LB", "Left Bumper"}, {"RB", "Right Bumper"},
    {"LT", "Left Trigger"}, {"RT", "Right Trigger"}, {"START", "Start"}, {"BACK", "Back"},
    {"LS", "Left Stick Click"}, {"RS", "Right Stick Click"},
    {"DPAD_UP", "D-Pad Up"}, {"DPAD_DOWN", "D-Pad Down"}, {"DPAD_LEFT", "D-Pad Left"}, {"DPAD_RIGHT", "D-Pad Right"},
    {"RS_UP", "Right Stick Up"}, {"RS_DOWN", "Right Stick Down"}, {"RS_LEFT", "Right Stick Left"}, {"RS_RIGHT", "Right Stick Right"},
};
#define NX ((int)(sizeof xItems / sizeof xItems[0]))
static Item jItems[32 + 4 + 12];
static int nj;

static void init_items(void) {
    static const char *dirs[] = {"UP", "DOWN", "LEFT", "RIGHT"}, *dirl[] = {"Up", "Down", "Left", "Right"};
    static const char *axes[] = {"X", "Y", "Z", "RX", "RY", "RZ"};
    int i;
    if (nj) return;
    for (i = 1; i <= 32; i++) { sprintf(jItems[nj].token, "B%d", i); sprintf(jItems[nj].label, "Button %d", i); nj++; }
    for (i = 0; i < 4; i++) { sprintf(jItems[nj].token, "POV_%s", dirs[i]); sprintf(jItems[nj].label, "Hat %s", dirl[i]); nj++; }
    for (i = 0; i < 6; i++) {
        sprintf(jItems[nj].token, "AXIS_%s-", axes[i]); sprintf(jItems[nj].label, "Axis %s -", axes[i]); nj++;
        sprintf(jItems[nj].token, "AXIS_%s+", axes[i]); sprintf(jItems[nj].label, "Axis %s +", axes[i]); nj++;
    }
}
const char *x_friendly(const char *t) { int i; for (i = 0; i < NX; i++) if (!strcmp(xItems[i].token, t)) return xItems[i].label; return t; }
const char *j_friendly(const char *t) { int i; init_items(); for (i = 0; i < nj; i++) if (!strcmp(jItems[i].token, t)) return jItems[i].label; return t; }
int pad_item_count(int isX) { init_items(); return isX ? NX : nj; }
const char *pad_item_token(int isX, int i) { return isX ? xItems[i].token : jItems[i].token; }
const char *pad_item_label(int isX, int i) { return isX ? xItems[i].label : jItems[i].label; }

const char *vk_name(int vk, char *buf) {
    static const struct { int vk; const char *n; } names[] = {
        {0x26, "UP"}, {0x28, "DOWN"}, {0x25, "LEFT"}, {0x27, "RIGHT"}, {0x20, "SPACE"}, {0x0D, "ENTER"}, {0x1B, "ESC"}, {0x09, "TAB"},
        {0x08, "BACKSPACE"}, {0xA0, "LSHIFT"}, {0xA1, "RSHIFT"}, {0xA2, "LCTRL"}, {0xA3, "RCTRL"}, {0xA4, "LALT"}, {0xA5, "RALT"},
        {0x2D, "INSERT"}, {0x2E, "DELETE"}, {0x24, "HOME"}, {0x23, "END"}, {0x21, "PAGEUP"}, {0x22, "PAGEDOWN"},
        {0x6B, "NUMADD"}, {0x6D, "NUMSUB"}, {0x6A, "NUMMUL"}, {0x6F, "NUMDIV"}, {0x6E, "NUMDEC"},
        {0xBC, "COMMA"}, {0xBE, "PERIOD"}, {0xBD, "MINUS"}, {0xBB, "EQUALS"}, {0xBF, "SLASH"}, {0xBA, "SEMICOLON"},
        {0xDE, "QUOTE"}, {0xDB, "LBRACKET"}, {0xDD, "RBRACKET"}, {0xDC, "BACKSLASH"}, {0xC0, "BACKTICK"},
    };
    size_t i;
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= 0x60 && vk <= 0x69) { sprintf(buf, "NUM%d", vk - 0x60); return buf; }
    if (vk >= 0x70 && vk <= 0x7B) { sprintf(buf, "F%d", vk - 0x6F); return buf; }
    for (i = 0; i < sizeof names / sizeof names[0]; i++) if (names[i].vk == vk) { strcpy(buf, names[i].n); return buf; }
    sprintf(buf, "VK%d", vk);
    return buf;
}

int key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

/* ---------------------------------------------------------------- combo moves */
static const ComboMove genericMoves[] = {
    {"Throw", "Throw (forward + MP)", "6+MP", "When close, f + MP"},
    {"Throw", "Throw (forward + MK)", "6+MK", "When close, f + MK"},
    {"Guard Break", "Guard Break (HP + HK)", "HP+HK", "same strength P + K (costs 1 level)"},
    {"Excel", "Excel activation (LP + MK)", "LP+MK", "different strength P + K (costs 1 level)"},
    {"Special", "Fireball (QCF + HP)", "236+HP", "qcf + P"},
    {"Special", "Dragon punch (DP + HP)", "623+HP", "f,d,df + P"},
    {"Special", "Hurricane kick (QCB + HK)", "214+HK", "qcb + K"},
    {"Super", "Super fireball (QCF x2 + HP)", "236236+HP", "qcf,qcf + P"},
    {"Super", "Super dragon punch (QCF, DP + HP)", "23623+HP", "qcf,d,df + P"},
    {"Super", "Super hurricane kick (QCB x2 + HK)", "214214+HK", "qcb,qcb + K"},
    {"Super", "Fireball > super cancel", "236+MP ~ ~ 236236+HP", "qcf + MP, then qcf,qcf + HP"},
    {"Meteor", "Meteor combo (QCF x2 + PPP)", "236236+LP+MP+HP", "qcf,qcf + PPP"},
    {"Meteor", "Meteor combo (QCB x2 + PPP)", "214214+LP+MP+HP", "qcb,qcb + PPP"},
    {"Meteor", "Meteor combo (QCF x2 + KKK)", "236236+LK+MK+HK", "qcf,qcf + KKK"},
    {"Meteor", "Meteor combo (QCB x2 + KKK)", "214214+LK+MK+HK", "qcb,qcb + KKK"},
    {"Meteor", "Meteor combo (720 + PPP)", "632147632147+LP+MP+HP", "rotate 720 + PPP"},
};

int combo_group_count(void) { return g_nComboChars + 1; }     /* group 0 is "Generic Moves" */
const char *combo_group(int i, const ComboMove **moves, int *n) {
    if (i == 0) { *moves = genericMoves; *n = (int)(sizeof genericMoves / sizeof genericMoves[0]); return "Generic Moves"; }
    *moves = g_comboChars[i - 1].moves;
    *n = g_comboChars[i - 1].nmoves;
    return g_comboChars[i - 1].name;
}

static int has_motion(const char *s) {   /* three or more digits in a row */
    int run = 0;
    for (; *s; s++) { if (isdigit((unsigned char)*s)) { if (++run >= 3) return 1; } else run = 0; }
    return 0;
}

/* every move of a character, most important first: Meteor Combos, Super Combos, motion specials, other special /
   command attacks, Guard Break, throws */
void combos_apply_loadout(const char *charName) {
    const ComboMove *tier[6][40];
    int nt[6] = {0}, c, i, t, k = 0;
    const ComboMove *ordered[NUM_COMBOS * 4];
    int no = 0;
    for (c = 0; c < g_nComboChars; c++) {
        if (strcmp(g_comboChars[c].name, charName)) continue;
        for (i = 0; i < g_comboChars[c].nmoves; i++) {
            const ComboMove *m = &g_comboChars[c].moves[i];
            int ti;
            if (!strcmp(m->kind, "Meteor")) ti = 0;
            else if (!strcmp(m->kind, "Super")) ti = 1;
            else if (!strcmp(m->kind, "Special")) ti = (strstr(m->seq, "*C") || has_motion(m->seq)) ? 2 : 3;
            else if (!strcmp(m->kind, "Guard Break")) ti = 4;
            else ti = 5;
            if (nt[ti] < 40) tier[ti][nt[ti]++] = m;
        }
    }
    for (t = 0; t < 6; t++) for (i = 0; i < nt[t]; i++) if (no < NUM_COMBOS * 4) ordered[no++] = tier[t][i];
    for (k = 0; k < NUM_COMBOS; k++) {
        ComboSlot *s = &g_in.slots[k];
        s->name[0] = s->kind[0] = s->seq[0] = 0;
        if (k < no) {
            _snprintf(s->name, sizeof s->name, "%s: %s", charName, ordered[k]->name);
            s->name[sizeof s->name - 1] = 0;
            strncpy(s->kind, ordered[k]->kind, sizeof s->kind - 1);
            strncpy(s->seq, ordered[k]->seq, sizeof s->seq - 1);
        }
    }
}

int combo_rows_shown(void) { return NUM_COMBOS; }   /* all 20 rows are always listed */

/* the Combos tab as it starts: default step and charge times, triggers 1-8 on the keyboard, the Ryu move list */
void combos_defaults(void) {
    int i;
    g_in.stepMs = 30;
    g_in.chargeMs = 600; g_in.chargeCredit = 0;
    for (i = 0; i < NUM_COMBOS; i++) {
        ComboSlot *s = &g_in.slots[i];
        memset(s, 0, sizeof *s);
        s->player = 1;
        if (i < 8) sprintf(s->key, "%d", i + 1);
    }
    combos_apply_loadout("Ryu");
}

void input_init(void) {
    int p, i, n = 0;
    static const struct { const char *id, *label; } r1[] = {{"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"},
        {"b1", "Button 1"}, {"b2", "Button 2"}, {"b3", "Button 3"}, {"b4", "Button 4"}, {"b5", "Button 5"}, {"b6", "Button 6"}, {"start", "Start"}};
    static const struct { const char *id, *label; } r2[] = {{"coin1", "Coin 1"}, {"coin2", "Coin 2"}, {"test", "Test Switch"}, {"service", "Service Switch"}};
    memset(&g_in, 0, sizeof g_in);
    for (p = 1; p <= 2; p++)
        for (i = 0; i < 11; i++) {
            RoleRow *r = &g_in.rows[n++];
            sprintf(r->id, "p%d_%s", p, r1[i].id);
            swprintf(r->label, 40, L"Player %d  %hs", p, r1[i].label);
        }
    for (i = 0; i < 4; i++) {
        RoleRow *r = &g_in.rows[n++];
        strcpy(r->id, r2[i].id);
        swprintf(r->label, 40, L"%hs", r2[i].label);
    }
    g_in.nrows = n;
    for (i = 0; i < n; i++) {
        RoleRow *r = &g_in.rows[i];
        size_t l = strlen(r->id);
        r->dir = (l > 3 && !strcmp(r->id + l - 3, "_up")) || (l > 5 && !strcmp(r->id + l - 5, "_down")) ||
                 (l > 5 && !strcmp(r->id + l - 5, "_left")) || (l > 6 && !strcmp(r->id + l - 6, "_right"));
        row_defaults(r);
    }
    combos_defaults();
    init_items();
}

/* only the pad (XInput / DirectInput) part of a row's defaults */
static void row_defaults_pad(RoleRow *r) {
    RoleRow t;
    memset(&t, 0, sizeof t);
    strcpy(t.id, r->id);
    row_defaults(&t);
    if (!r->x[0]) strcpy(r->x, t.x);
    if (!r->j[0]) strcpy(r->j, t.j);
}

void input_reset_rows(void) {
    int i;
    for (i = 0; i < g_in.nrows; i++) { row_defaults(&g_in.rows[i]); g_in.rows[i].autofire = 0; }
}

/* ---------------------------------------------------------------- zinc-input.cfg */
typedef struct { char *k, *v; } Pair;
typedef struct { Pair *p; int n; char *text; } Vals;
static const char *vget(const Vals *v, const char *k) { int i; for (i = 0; i < v->n; i++) if (!strcmp(v->p[i].k, k)) return v->p[i].v; return NULL; }

static void parse_binding(const char *v, char *key, char *x, char *j) {
    char *copy = _strdup(v), *tok, *save = NULL;
    for (tok = strtok_s(copy, ", ", &save); tok; tok = strtok_s(NULL, ", ", &save)) {
        char *dst = NULL;
        if (strlen(tok) < 3 || tok[1] != ':') continue;
        switch (toupper((unsigned char)tok[0])) { case 'K': dst = key; break; case 'X': dst = x; break; case 'J': dst = j; break; }
        if (dst && !dst[0]) { strncpy(dst, tok + 2, 23); dst[23] = 0; _strupr(dst); }
    }
    free(copy);
}

static void load_combos(const Vals *v) {
    const char *s;
    int i, layout;
    if (!vget(v, "combo_step_ms")) return;   /* no combo section yet: keep the SF EX2 Plus presets */
    { int n; if ((s = vget(v, "combo_step_ms")) && xatoi(s, &n) && n >= 16 && n <= 500) g_in.stepMs = n; }
    { int n; if ((s = vget(v, "combo_charge_credit")) && xatoi(s, &n)) g_in.chargeCredit = n != 0; }
    { int n; if ((s = vget(v, "combo_charge_ms")) && xatoi(s, &n) && n >= 100 && n <= 3000) g_in.chargeMs = n; }
    s = vget(v, "combo_layout");
    layout = s ? atoi(s) : 0;
    if (layout != 3 && layout != 4 && layout != 5) return;   /* saved by an older build: use the new default slots and triggers */
    if (layout == 3 && g_in.stepMs == 50) g_in.stepMs = 30;  /* 50 ms was the old default step length */
    for (i = 0; i < NUM_COMBOS; i++) {
        ComboSlot *c = &g_in.slots[i];
        char k[40];
        const char *t;
        memset(c, 0, sizeof *c);
        c->player = 1;
        sprintf(k, "combo%d_name", i + 1); if ((t = vget(v, k))) strncpy(c->name, t, sizeof c->name - 1);
        sprintf(k, "combo%d_kind", i + 1); if ((t = vget(v, k))) strncpy(c->kind, t, sizeof c->kind - 1);
        sprintf(k, "combo%d_seq", i + 1); if ((t = vget(v, k))) strncpy(c->seq, t, sizeof c->seq - 1);
        sprintf(k, "combo%d_player", i + 1); if ((t = vget(v, k)) && !strcmp(t, "2")) c->player = 2;
        sprintf(k, "combo%d_face", i + 1); if ((t = vget(v, k)) && toupper((unsigned char)t[0]) == 'L') c->faceLeft = 1;
        sprintf(k, "combo%d", i + 1); if ((t = vget(v, k))) parse_binding(t, c->key, c->x, c->j);
    }
    if (layout == 3 || layout == 4) {   /* defaults of the older layouts move to the current ones while untouched */
        static const char *old[] = {"1", "2", "3", "5", "6", "8", "9", "0"};
        for (i = 0; i < 8; i++) {
            char now[4];
            sprintf(now, "%d", i + 1);
            if (!strcmp(g_in.slots[i].key, old[i])) strcpy(g_in.slots[i].key, now);
        }
    }
}

/* "key=value" lines (comments after ";") into pairs that point into txt, which is changed */
static void vals_parse(Vals *v, char *txt) {
    char *cur;
    int cap = 0;
    for (cur = txt; cur; ) {
        char *nl = strchr(cur, '\n'), *semi, *eq, *line;
        if (nl) *nl = 0;
        semi = strchr(cur, ';');
        if (semi) *semi = 0;
        line = trim_a(cur);
        eq = strchr(line, '=');
        if (eq && *line != '[') {
            *eq = 0;
            if (v->n == cap) { cap = cap ? cap * 2 : 128; v->p = (Pair *)realloc(v->p, cap * sizeof(Pair)); }
            v->p[v->n].k = _strlwr(trim_a(line));
            v->p[v->n].v = trim_a(eq + 1);
            v->n++;
        }
        cur = nl ? nl + 1 : NULL;
    }
}

/* the controls part of a cfg: pads, stick threshold, the bindings and the autofire flags */
static void load_rows(const Vals *v, int ver) {
    int i;
    const char *s;
    if ((s = vget(v, "deadzone")) && atoi(s) >= 5 && atoi(s) <= 95) g_set.deadzone = atoi(s);
    for (i = 0; i < 2; i++) {
        char k[8];
        sprintf(k, "pad%d", i + 1);
        if ((s = vget(v, k))) { int n = !_stricmp(s, "none") ? -1 : atoi(s); if (i) g_set.pad2 = n; else g_set.pad1 = n; }
    }
    for (i = 0; i < g_in.nrows; i++) {
        RoleRow *r = &g_in.rows[i];
        char k[40];
        const char *b;
        sprintf(k, "%s_auto", r->id);
        b = vget(v, k);
        r->autofire = has_autofire(r) && b && !strcmp(b, "1");
        b = vget(v, r->id);
        if (!b) continue;
        r->key[0] = r->x[0] = r->j[0] = 0;
        parse_binding(b, r->key, r->x, r->j);
        if (ver == 2 && r->dir) row_defaults_pad(r);   /* files of version 2 could not hold these: the D-pad / hat bindings are new */
        /* builds before this one used the digits 4 and 7 for Test / Service (now combo triggers) */
        if (!strcmp(r->id, "test") && !strcmp(r->key, "4")) strcpy(r->key, "F4");
        if (!strcmp(r->id, "service") && !strcmp(r->key, "7")) strcpy(r->key, "F7");
    }
}

void input_load_cfg(void) {
    wchar_t path[560];
    char *txt;
    Vals v = {0};
    const char *ver;
    path_join(path, 560, g_root, L"zinc-input.cfg");
    txt = read_file(path, NULL);
    if (!txt) return;
    vals_parse(&v, txt);
    ver = vget(&v, "version");
    if (ver && (atoi(ver) == CFG_VERSION || atoi(ver) == 2)) {   /* anything else: written by an older build, use the current defaults */
        load_combos(&v);
        load_rows(&v, atoi(ver));
    }
    free(v.p);
    free(txt);
}

/* a controls profile: the pad choice, the stick threshold and every binding (no combos) */
void input_profile_apply(const char *text) {
    char *copy = _strdup(text);
    Vals v = {0};
    vals_parse(&v, copy);
    load_rows(&v, CFG_VERSION);
    free(v.p);
    free(copy);
}

static void join_binding(char *out, const char *key, const char *x, const char *j, int dir) {
    out[0] = 0;
    if (key[0]) sprintf(out + strlen(out), "%sK:%s", out[0] ? "," : "", key);
    if (x[0] && !dir) sprintf(out + strlen(out), "%sX:%s", out[0] ? "," : "", x);
    if (j[0] && !dir) sprintf(out + strlen(out), "%sJ:%s", out[0] ? "," : "", j);
}

/* renders zinc-input.cfg from the current rows and combo slots */
void input_cfg_text(Buf *b) {
    int i, p;
    buf_add(b, "; ZiNc input plugin settings (written by ZiNc-EX; safe to edit by hand)\r\n");
    buf_add(b, "; bindings: K:<key>  X:<XInput button>  J:<DirectInput button/hat/axis>, comma separated\r\n");
    buf_fmt(b, "version=%d\r\n", CFG_VERSION);
    buf_fmt(b, "deadzone=%d\r\nxinput=%d\r\ndirectinput=%d\r\nanalog=%d\r\nlogs=%d\r\n", g_set.deadzone, g_set.xinputOn != 0, g_set.dinputOn != 0, g_set.analog != 0, g_set.logs != 0);
    buf_add(b, "cursor_hide_ms=3000\r\n");   /* the mouse cursor is shown in the game and hidden after 3 s without movement (0 = never hide, -1 = leave it alone) */
    for (p = 0; p < 2; p++) {
        int v = p ? g_set.pad2 : g_set.pad1;
        if (v < 0) buf_fmt(b, "pad%d=none\r\n", p + 1); else buf_fmt(b, "pad%d=%d\r\n", p + 1, v);
    }
    for (i = 0; i < g_in.nrows; i++) {
        RoleRow *r = &g_in.rows[i];
        char j[100];
        join_binding(j, r->key, r->x, r->j, 0);   /* directions can have buttons / hat bindings too (besides the automatic D-pad / stick) */
        buf_fmt(b, "%s=%s\r\n", r->id, j);
        if (has_autofire(r)) buf_fmt(b, "%s_auto=%d\r\n", r->id, r->autofire != 0);
    }
    buf_fmt(b, "combo_layout=5\r\ncombo_step_ms=%d\r\ncombo_charge_ms=%d\r\ncombo_charge_credit=%d\r\n", g_in.stepMs, g_in.chargeMs, g_in.chargeCredit != 0);
    for (i = 0; i < NUM_COMBOS; i++) {
        ComboSlot *s = &g_in.slots[i];
        char seq[160], j[100], *t;
        strcpy(seq, s->seq);
        t = trim_a(seq);
        if (!*t) continue;
        join_binding(j, s->key, s->x, s->j, 0);
        buf_fmt(b, "combo%d=%s\r\ncombo%d_seq=%s\r\ncombo%d_player=%d\r\ncombo%d_face=%s\r\ncombo%d_name=%s\r\ncombo%d_kind=%s\r\n",
                i + 1, j, i + 1, t, i + 1, s->player, i + 1, s->faceLeft ? "L" : "R", i + 1, s->name, i + 1, s->kind);
    }
}

void input_profile_text(Buf *b) {
    int i, p;
    buf_add(b, "; ZiNc EX controls profile\r\n");
    buf_fmt(b, "deadzone=%d\r\n", g_set.deadzone);
    for (p = 0; p < 2; p++) {
        int v = p ? g_set.pad2 : g_set.pad1;
        if (v < 0) buf_fmt(b, "pad%d=none\r\n", p + 1); else buf_fmt(b, "pad%d=%d\r\n", p + 1, v);
    }
    for (i = 0; i < g_in.nrows; i++) {
        RoleRow *r = &g_in.rows[i];
        char j[100];
        join_binding(j, r->key, r->x, r->j, 0);
        buf_fmt(b, "%s=%s\r\n", r->id, j);
        if (has_autofire(r)) buf_fmt(b, "%s_auto=%d\r\n", r->id, r->autofire != 0);
    }
}

/* the cfg the plugin reads for one game that uses a profile: everything as it is, with the controls of the profile */
void input_cfg_text_profile(Buf *b, const char *profile) {
    InputState keep = g_in;
    int dz = g_set.deadzone, p1 = g_set.pad1, p2 = g_set.pad2;
    input_profile_apply(profile);
    input_cfg_text(b);
    g_in = keep;
    g_set.deadzone = dz; g_set.pad1 = p1; g_set.pad2 = p2;
}

void input_save_cfg(void) {
    Buf b = {0};
    wchar_t path[560];
    path_join(path, 560, g_root, L"zinc-input.cfg");
    input_cfg_text(&b);
    write_file(path, b.s, b.n);
    buf_free(&b);
}

/* ---------------------------------------------------------------- game controllers */
typedef DWORD (WINAPI *XGetState)(DWORD, void *);

void detect_pads(wchar_t *out, size_t cap) {
    wchar_t line[200];
    int n = 0, u;
    UINT nd, i;
    HMODULE xi = LoadLibraryW(L"xinput1_4.dll");
    out[0] = 0;
    if (!xi) xi = LoadLibraryW(L"xinput1_3.dll");
    if (xi) {
        XGetState get = (XGetState)GetProcAddress(xi, "XInputGetState");
        if (get)
            for (u = 0; u < 4; u++) {
                BYTE st[16];
                if (get(u, st) == 0) { swprintf(line, 200, L"XInput controller %d", u + 1); if (n++) wcsncat(out, L"\n", cap - wcslen(out) - 1); wcsncat(out, line, cap - wcslen(out) - 1); }
            }
        FreeLibrary(xi);
    }
    nd = joyGetNumDevs();
    for (i = 0; i < nd && i < 16; i++) {
        JOYINFOEX info;
        JOYCAPSW caps;
        wchar_t low[64];
        memset(&info, 0, sizeof info);
        info.dwSize = sizeof info;
        info.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(i, &info) != JOYERR_NOERROR) continue;
        memset(&caps, 0, sizeof caps);
        joyGetDevCapsW(i, &caps, sizeof caps);
        wcsncpy(low, caps.szPname, 63);
        low[63] = 0;
        _wcslwr(low);
        if (wcsstr(low, L"xbox") || wcsstr(low, L"xinput")) continue;   /* already listed through XInput */
        swprintf(line, 200, L"DirectInput: %ls", caps.szPname);
        if (n++) wcsncat(out, L"\n", cap - wcslen(out) - 1);
        wcsncat(out, line, cap - wcslen(out) - 1);
    }
    if (n == 0) wcsncpy(out, L"No game controllers detected (keyboard only).", cap - 1);
}
