/* The "Combos" tab: triggers (key / XInput / DirectInput) that make the input plugin play a move sequence.
   The sequences are shown and typed in words ("Quarter-circle Forward + Heavy Punch", "Down, Down-Right, Right + Heavy Punch");
   zinc-input.cfg keeps the short numpad notation the plugin reads (236+HP: directions 1-9 as on a numpad, buttons LP MP HP LK MK HK,
   "+" together, "~" a pause, "4*C" hold for the charge time). Typing the short notation also still works. */
#include "common.h"

CmbUI g_cmb;

static const wchar_t *INFO =
    L"HOW COMBOS WORK\n"
    L"Give a row a trigger (click its Keyboard, XInput or DirectInput cell) and the move of that row is played for you whenever you press the trigger in a game. Double-click a name to rename a combo.\n\n"
    L"FILL A ROW\n"
    L"\u2022 Load Character puts every move of a character into the rows. Or pick a Character and a Move below to set the selected row.\n"
    L"\u2022 Or write your own Sequence: type it or use the buttons under the Sequence box. Steps are separated by commas, + presses buttons together.\n"
    L"    Example: D, DR, R + HP   (the same as QCF + HP)\n"
    L"\u2022 The short names (QCF, HP, DR, BN1 ...) are explained by the Legend button. The long names (Quarter-circle Forward, Heavy Punch) work as well. ~ is a short wait (one step, 30 ms by default).\n\n"
    L"CHARGE MOVES (Guile, Blanka, Balrog, Vega, Cracker Jack)\n"
    L"\"Hold L\" holds that direction for the Charge time (default 600 ms). If a charge move does not come out, raise the Charge time a little: the game needs its own minimum of charge.\n\n"
    L"GOOD TO KNOW\n"
    L"\u2022 Player Side: Left (default) = your character is on the left of the screen and looks right. Right = it is on the right and looks left; Left / Right then swap in the sequence.\n"
    L"\u2022 Supers and Meteor Combos need super gauge.\n"
    L"\u2022 The short number notation (236+HP) still works.\n\n"
    L"Note: the move lists (Load Character) are from Street Fighter EX2 Plus. Combos are made for the Street Fighter games; other games can use them too, but the characters, moves and the button names are Street Fighter's.";

/* the Legend button: what the short names stand for, in a small table */
static void dlg_legend(void) {
    static const struct { const wchar_t *a, *b; } ROWS[] = {
        {L"DIRECTIONS", NULL}, {L"", L"for Player Side Left (the character looks right); Left and Right swap when Player Side is Right"},
        {L"U", L"Up"}, {L"D", L"Down"}, {L"L", L"Left"}, {L"R", L"Right"},
        {L"UL", L"Up-Left"}, {L"UR", L"Up-Right"}, {L"DL", L"Down-Left"}, {L"DR", L"Down-Right"},
        {L"F", L"Forward (towards the opponent)"}, {L"B", L"Back (away from the opponent)"}, {L"N", L"Neutral"},
        {L"BUTTONS", NULL},
        {L"LP", L"Light Punch (Button 1)"}, {L"MP", L"Medium Punch (Button 2)"}, {L"HP", L"Heavy Punch (Button 3)"},
        {L"LK", L"Light Kick (Button 4)"}, {L"MK", L"Medium Kick (Button 5)"}, {L"HK", L"Heavy Kick (Button 6)"},
        {L"BN1 ... BN6", L"Button 1 ... 6 of the Controls tab (the same buttons as LP ... HK)"}, {L"START", L"The Start button"},
        {L"MOTION PIECES", NULL},
        {L"Q", L"Quarter"}, {L"H", L"Half"}, {L"C", L"Circle"}, {L"D (in front)", L"Double (twice)"},
        {L"CF", L"Circle Forward"}, {L"CB", L"Circle Back"}, {L"R (in front)", L"Reverse"},
        {L"DP", L"Dragon Punch motion"}, {L"FCF / FCB", L"Full Circle Forward / Back"},
        {L"MOTIONS", NULL},
        {L"QCF", L"Quarter-circle Forward  (D, DR, R)"}, {L"QCB", L"Quarter-circle Back  (D, DL, L)"},
        {L"HCF", L"Half-circle Forward  (L, DL, D, DR, R)"}, {L"HCB", L"Half-circle Back  (R, DR, D, DL, L)"},
        {L"DP  or  FDP", L"Dragon-punch Motion, forward  (R, D, DR)"}, {L"RDP  or  BDP", L"Reverse Dragon-punch Motion, back  (L, D, DL)"},
        {L"FCF", L"Full Circle Forward  (R, DR, D, DL, L, UL)"}, {L"FCB", L"Full Circle Back  (L, DL, D, DR, R, UR)"},
        {L"DFC", L"Double Full Circle  (two turns, forward)"},
        {L"DQCF", L"Double Quarter-circle Forward  (QCF twice)"}, {L"DQCB", L"Double Quarter-circle Back  (QCB twice)"},
        {L"SIGNS", NULL},
        {L"+", L"Pressed together (QCF + HP)"}, {L",", L"The next step"},
        {L"~", L"A short wait: one neutral step as long as Step (ms), 30 ms by default (~ ~ = 60 ms)"},
        {L"Hold L", L"Hold that direction for the Charge time (default 600 ms), for charge moves"}};
    dlg_table(g_main, L"Legend", L"Short Name", L"Meaning", (const TableRow *)ROWS, (int)(sizeof ROWS / sizeof ROWS[0]), 100, 520);
}

/* the buttons under the Sequence box: the label is what they write (+ , and Del are the exceptions) */
static const char *TOKS[CMB_NTOK] = {   /* 3 rows of 10 cells, NULL = empty cell */
    "UL", "U", "UR", "LP", "MP", "HP", "HCB", "HCF", "~", "Del",
    "L", NULL, "R", "LK", "MK", "HK", "QCB", "QCF", "+", "START",
    "DL", "D", "DR", NULL, NULL, NULL, "FCB", "FCF", ",", "Hold"};

/* a button picture from the PNG files in icons (via icon_data.c), scaled to the screen DPI; NULL when the token has no picture */
static HICON token_icon(const char *name) {
    int k, w, h, nw, nh, x, y;
    const IconData *ic = NULL;
    BITMAPINFO bi;
    void *bits = NULL;
    HBITMAP color, mask;
    ICONINFO ii;
    HICON ico;
    unsigned char *dst;
    for (k = 0; k < g_nIcons; k++) if (!strcmp(g_icons[k].name, name)) ic = &g_icons[k];
    if (!ic) return NULL;
    w = ic->w; h = ic->h;
    nw = S(w); nh = S(h);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = nw; bi.bmiHeader.biHeight = -nh; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color) return NULL;
    dst = (unsigned char *)bits;
    for (y = 0; y < nh; y++) for (x = 0; x < nw; x++) {
        /* bilinear on premultiplied colours, then back to straight alpha (the format of icons) */
        double fx = nw > 1 ? (double)x * (w - 1) / (nw - 1) : 0, fy = nh > 1 ? (double)y * (h - 1) / (nh - 1) : 0, acc[4] = {0, 0, 0, 0};
        int x0 = (int)fx, y0 = (int)fy, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0, c;
        double tx = fx - x0, ty = fy - y0, wgt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
        int px[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}, q;
        for (q = 0; q < 4; q++) {
            const unsigned char *p = ic->bgra + ((size_t)px[q][1] * w + px[q][0]) * 4;
            double a = p[3] / 255.0;
            acc[0] += wgt[q] * p[0] * a; acc[1] += wgt[q] * p[1] * a; acc[2] += wgt[q] * p[2] * a; acc[3] += wgt[q] * a;
        }
        for (c = 0; c < 3; c++) dst[((size_t)y * nw + x) * 4 + c] = acc[3] > 0.0001 ? (unsigned char)(acc[c] / acc[3] + 0.5) : 0;
        dst[((size_t)y * nw + x) * 4 + 3] = (unsigned char)(acc[3] * 255 + 0.5);
    }
    mask = CreateBitmap(nw, nh, 1, 1, NULL);
    memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
    ico = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    return ico;
}

static int g_rows;   /* rows shown in the list */

/* ---------------------------------------------------------------- sequences in words */
static const char *DIRW[10] = {"", "DL", "D", "DR", "L", "N", "R", "UL", "U", "UR"};   /* shown: D = Down, DR = Down-Right, ... */
static const char *DIRFULL[10] = {"", "Down-Left", "Down", "Down-Right", "Left", "Neutral", "Right", "Up-Left", "Up", "Up-Right"};   /* also accepted when typing */
static const char *BTNW[6][2] = {{"LP", "Light Punch"}, {"MP", "Medium Punch"}, {"HP", "Heavy Punch"}, {"LK", "Light Kick"}, {"MK", "Medium Kick"}, {"HK", "Heavy Kick"}};
static const char *MOTION[][3] = {   /* numpad digits, full name, short name */
    {"236", "Quarter-circle Forward", "QCF"}, {"214", "Quarter-circle Back", "QCB"}, {"623", "Dragon-punch Motion", "DP"}, {"421", "Reverse Dragon-punch Motion", "RDP"},
    {"41236", "Half-circle Forward", "HCF"}, {"63214", "Half-circle Back", "HCB"}, {"632147", "Full Circle Forward", "FCF"}, {"412369", "Full Circle Back", "FCB"}, {"632147632147", "Double Full Circle", "DFC"},
    {"236236", "Double Quarter-circle Forward", "DQCF"}, {"214214", "Double Quarter-circle Back", "DQCB"}};

static int mirror(int d) { return d == 1 ? 3 : d == 3 ? 1 : d == 4 ? 6 : d == 6 ? 4 : d == 7 ? 9 : d == 9 ? 7 : d; }
static void app(char *o, int cap, const char *s) { size_t l = strlen(o); if ((int)l < cap - 1) snprintf(o + l, (size_t)cap - l, "%s", s); }

/* is this the short notation the plugin reads (and not text that is still being typed)? */
static int is_notation(const char *seq) {
    char buf[512], *tok, *ctx = 0;
    strncpy(buf, seq, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (tok = strtok_s(buf, ", \t", &ctx); tok; tok = strtok_s(NULL, ", \t", &ctx)) {
        const char *p = tok;
        if (!strcmp(tok, "~")) continue;
        while (*p >= '1' && *p <= '9') p++;
        if (p > tok && *p == '*') { p++; if (*p == 'C' || *p == 'c') p++; else while (*p >= '0' && *p <= '9') p++; }
        while (*p) {
            const char *e;
            char nm[8];
            size_t n;
            int ok = 0, k;
            static const char *NAMES[] = {"LP", "MP", "HP", "LK", "MK", "HK", "ST", "START", "B1", "B2", "B3", "B4", "B5", "B6", "BN1", "BN2", "BN3", "BN4", "BN5", "BN6", NULL};
            if (*p == '+') p++;
            e = p; while (*e && *e != '+') e++;
            n = (size_t)(e - p);
            if (n == 0 || n > 5) return 0;
            memcpy(nm, p, n); nm[n] = 0;
            for (k = 0; NAMES[k]; k++) if (!_stricmp(NAMES[k], nm)) ok = 1;
            if (!ok) return 0;
            p = e;
        }
    }
    return 1;
}

/* numpad notation -> words ("236+HP" -> "Quarter-circle Forward (Down, Down-Right, Right) + Heavy Punch") */
static void seq_to_words(const char *seq, int left, char *out, int cap) {
    char buf[512], *tok, *ctx = 0;
    out[0] = 0;
    if (!is_notation(seq)) { strncpy(out, seq, (size_t)cap - 1); out[cap - 1] = 0; return; }   /* text that is still being typed: shown as typed */
    strncpy(buf, seq, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    for (tok = strtok_s(buf, ", \t", &ctx); tok; tok = strtok_s(NULL, ", \t", &ctx)) {
        char step[300] = "", digits[40];
        const char *p = tok;
        int nd = 0, i, held = 0, ms = 0;
        if (out[0]) app(out, cap, ", ");
        if (!strcmp(tok, "~")) { app(out, cap, "~"); continue; }
        while (*p >= '1' && *p <= '9' && nd < 39) digits[nd++] = *p++;
        digits[nd] = 0;
        if (nd > 0 && *p == '*') {
            p++;
            if (*p == 'C' || *p == 'c') { held = 1; ms = -1; p++; }
            else { ms = atoi(p); held = 1; while (*p >= '0' && *p <= '9') p++; }
        }
        if (nd > 0) {
            int motion = -1;
            if (!held) for (i = 0; i < (int)(sizeof MOTION / sizeof MOTION[0]); i++) if (!strcmp(MOTION[i][0], digits)) motion = i;
            if (motion >= 0) app(step, sizeof step, MOTION[motion][2]), app(step, sizeof step, " (");
            for (i = 0; i < nd - (held ? 1 : 0); i++) { if (i) app(step, sizeof step, ", "); app(step, sizeof step, DIRW[left ? mirror(digits[i] - '0') : digits[i] - '0']); }
            if (held) {
                char h[60];
                if (nd > 1) app(step, sizeof step, ", ");
                snprintf(h, sizeof h, ms < 0 ? "Hold %s (charge)" : "Hold %s (%d ms)", DIRW[left ? mirror(digits[nd - 1] - '0') : digits[nd - 1] - '0'], ms);
                app(step, sizeof step, h);
            }
            if (motion >= 0) app(step, sizeof step, ")");
        }
        while (*p) {
            const char *e;
            char nm[12];
            size_t n;
            int k, found = -1;
            if (*p == '+') p++;
            e = p; while (*e && *e != '+') e++;
            n = (size_t)(e - p);
            if (n == 0) break;
            if (n > 11) n = 11;
            memcpy(nm, p, n); nm[n] = 0;
            for (k = 0; k < 6; k++) if (!_stricmp(BTNW[k][0], nm)) found = k;
            if (found < 0 && (nm[0] == 'B' || nm[0] == 'b') && nm[1] >= '1' && nm[1] <= '6' && !nm[2]) found = nm[1] - '1';
            if (step[0]) app(step, sizeof step, " + ");
            if (found >= 0) app(step, sizeof step, BTNW[found][0]);
            else if (!_stricmp(nm, "ST") || !_stricmp(nm, "START")) app(step, sizeof step, "START");
            else { char up[12]; int q; for (q = 0; nm[q]; q++) up[q] = (char)toupper((unsigned char)nm[q]); up[q] = 0; app(step, sizeof step, up); }
            p = e;
        }
        app(out, cap, step);
    }
}

static void lower_norm(const char *s, char *o, int cap) {   /* lower case, blanks and underscores become "-" */
    int n = 0;
    while (*s && n < cap - 1) { char ch = *s++; o[n++] = (ch == ' ' || ch == '_') ? '-' : (char)tolower((unsigned char)ch); }
    o[n] = 0;
}

static char *trim_dash(char *s) {
    char *e;
    while (*s == '-' || *s == ' ') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == '-' || e[-1] == ' ')) *--e = 0;
    return s;
}

/* one part of a step in words -> numpad notation; returns 0 when the words are not understood */
static int part_to_notation(char *part, int left, char *dig, char *btn, char *ms) {
    int k;
    char *at;
    part = trim_dash(part);
    if (!*part) return 1;
    if (!strcmp(part, "pause") || !strcmp(part, "~")) { strcat(dig, "~"); return 1; }
    if ((at = strstr(part, "@"))) { strncpy(ms, at + 1, 8); *at = 0; part = trim_dash(part); }
    if (!strncmp(part, "hold-", 5)) { part = trim_dash(part + 5); if (!*ms) strcpy(ms, "C"); }
    for (k = 0; k < (int)(sizeof MOTION / sizeof MOTION[0]); k++) {
        char m[60], ab[16];
        lower_norm(MOTION[k][1], m, sizeof m);
        lower_norm(MOTION[k][2], ab, sizeof ab);
        if (!strcmp(m, part) || !strcmp(ab, part)) { strcat(dig, MOTION[k][0]); return 1; }
    }
    if (!strcmp(part, "fc") || !strcmp(part, "full-circle")) { strcat(dig, "632147"); return 1; }   /* the older name of FCF */
    if (!strcmp(part, "fdp")) { strcat(dig, "623"); return 1; }   /* forward / back dragon punch: the same motions as DP / RDP */
    if (!strcmp(part, "bdp")) { strcat(dig, "421"); return 1; }
    for (k = 1; k <= 9; k++) {
        char d[20], a[8];
        lower_norm(DIRFULL[k], d, sizeof d);
        lower_norm(DIRW[k], a, sizeof a);
        if (!strcmp(d, part) || !strcmp(a, part)) { char c1[2] = {(char)('0' + (left ? mirror(k) : k)), 0}; strcat(dig, c1); return 1; }
    }
    {   /* facing-relative names */
        static const struct { const char *n, *a; char d; } REL[] = {{"forward", "f", '6'}, {"back", "b", '4'}, {"down-forward", "df", '3'}, {"down-back", "db", '1'}, {"up-forward", "uf", '9'}, {"up-back", "ub", '7'}};
        for (k = 0; k < 6; k++) if (!strcmp(REL[k].n, part) || !strcmp(REL[k].a, part)) { char c1[2] = {REL[k].d, 0}; strcat(dig, c1); return 1; }
    }
    if (part[0] == 'b' && part[1] == 'n' && part[2] >= '1' && part[2] <= '6' && !part[3]) {   /* BN1..BN6: the game buttons of the Controls tab */
        char t[4] = {'B', 'N', part[2], 0};
        if (btn[0]) strcat(btn, "+");
        strcat(btn, t);
        return 1;
    }
    if (!strcmp(part, "st") || !strcmp(part, "start")) { if (btn[0]) strcat(btn, "+"); strcat(btn, "ST"); return 1; }
    for (k = 0; k < 6; k++) {
        char b[20];
        lower_norm(BTNW[k][1], b, sizeof b);
        if (!strcmp(b, part) || !_stricmp(BTNW[k][0], part)) { if (btn[0]) strcat(btn, "+"); strcat(btn, BTNW[k][0]); return 1; }
    }
    return 0;
}

/* words -> numpad notation. Returns 0 when the text is not in words (it is then the short notation, or not understood) */
static int words_to_seq(const char *text, int left, char *out, int cap) {
    char t[600], flat[640], *step, *ctx1 = 0;
    int n = 0, depth = 0, i;
    static const char *KEY[] = {"down", "up", "left", "right", "neutral", "punch", "kick", "hold", "pause", "circle", "dragon", "forward", "back"};
    char low[600];
    int human = 0;
    for (i = 0; text[i] && i < 590; i++) low[i] = (char)tolower((unsigned char)text[i]);
    low[i] = 0;
    for (i = 0; i < (int)(sizeof KEY / sizeof KEY[0]); i++) if (strstr(low, KEY[i])) human = 1;
    {   /* the short direction names (d, dr, r, ...) on their own */
        static const char *ABBR[] = {"d", "dr", "dl", "r", "l", "u", "ur", "ul", "n", "f", "b", "df", "db", "uf", "ub", "qcf", "qcb", "dp", "rdp", "fdp", "bdp", "hcf", "hcb", "fc", "fcf", "fcb", "dfc", "dqcf", "dqcb"};
        char tk[600], *t1, *c1 = 0;
        strncpy(tk, low, sizeof tk - 1); tk[sizeof tk - 1] = 0;
        for (t1 = strtok_s(tk, ", +\t", &c1); t1; t1 = strtok_s(NULL, ", +\t", &c1)) {
            int a;
            for (a = 0; a < (int)(sizeof ABBR / sizeof ABBR[0]); a++) if (!strcmp(t1, ABBR[a])) human = 1;
        }
    }
    if (!human) return 0;
    /* parentheses are explanations, except "(2000 ms)" which becomes "@2000" */
    for (i = 0; low[i] && n < 590; i++) {
        if (low[i] == '(') {
            int v = atoi(low + i + 1);
            const char *close = strchr(low + i, ')');
            if (v > 0 && close && strstr(low + i, "ms") && strstr(low + i, "ms") < close) n += snprintf(flat + n, sizeof flat - (size_t)n, "@%d", v);
            depth = 1;
            continue;
        }
        if (low[i] == ')') { depth = 0; continue; }
        if (!depth) flat[n++] = low[i];
    }
    flat[n] = 0;
    { char *th; while ((th = strstr(flat, " then "))) memcpy(th, ",     ", 6); }
    out[0] = 0;
    strncpy(t, flat, sizeof t - 1); t[sizeof t - 1] = 0;
    for (step = strtok_s(t, ",", &ctx1); step; step = strtok_s(NULL, ",", &ctx1)) {
        char dig[700] = "", btn[700] = "", ms[10] = "", tmp[300], *part, *ctx2 = 0, sn[1500];
        strncpy(tmp, step, sizeof tmp - 1); tmp[sizeof tmp - 1] = 0;
        { char norm[300]; lower_norm(tmp, norm, sizeof norm); strcpy(tmp, norm); }
        for (part = strtok_s(tmp, "+", &ctx2); part; part = strtok_s(NULL, "+", &ctx2))
            if (!part_to_notation(part, left, dig, btn, ms)) return 0;
        sn[0] = 0;
        if (!strcmp(dig, "~")) strcpy(sn, "~");
        else {
            strcat(sn, dig);
            if (ms[0] && dig[0]) { strcat(sn, "*"); strcat(sn, ms); }
            if (btn[0]) { if (dig[0]) strcat(sn, "+"); strcat(sn, btn); }
        }
        if (!sn[0]) continue;
        /* directions on their own, several in a row: one step each */
        if (!btn[0] && !ms[0] && strlen(dig) > 1 && strcmp(dig, "~")) { char sp[1500] = ""; for (i = 0; dig[i]; i++) { char c1[3] = {dig[i], ' ', 0}; strcat(sp, c1); } strcpy(sn, sp); }
        if (out[0]) app(out, cap, " ");
        app(out, cap, sn);
    }
    { size_t l = strlen(out); while (l && out[l - 1] == ' ') out[--l] = 0; }
    return 1;
}

static void txt8(const char *s, wchar_t *out, int cap) {
    wchar_t *w = u8_to_w(s, -1);
    wcsncpy(out, w, cap - 1);
    out[cap - 1] = 0;
    free(w);
}
static void dash8(const char *s, wchar_t *out, int cap) { if (!s[0]) wcscpy(out, L"-"); else txt8(s, out, cap); }

/* ---------------------------------------------------------------- the explanation: a read-only text view
   with the window's own scroll bar, themed like the one of the game list (DarkMode_Explorer in the dark theme) */
typedef struct { int pos, total, view; HFONT font; wchar_t *text; } TextView;

static int tv_height(HDC dc, const wchar_t *t, int width) {
    RECT r;
    r.left = r.top = 0; r.right = width > 10 ? width : 10; r.bottom = 0;
    DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    return r.bottom;
}

static void tv_sync_bar(HWND h, TextView *v) {
    SCROLLINFO si;
    int range = v->total - v->view;
    if (range < 0) range = 0;
    if (v->pos > range) v->pos = range;
    if (v->pos < 0) v->pos = 0;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = v->total > 0 ? v->total - 1 : 0; si.nPage = (UINT)(v->view > 0 ? v->view : 1); si.nPos = v->pos;
    SetScrollInfo(h, SB_VERT, &si, TRUE);
}

static void tv_measure(HWND h, TextView *v) {
    RECT rc;
    HDC dc = GetDC(h);
    HFONT old = (HFONT)SelectObject(dc, v->font ? v->font : g_font);
    int pad = S(6);
    GetClientRect(h, &rc);
    v->view = rc.bottom - 2 * pad;
    v->total = tv_height(dc, v->text ? v->text : L"", rc.right - 2 * pad) + 2 * pad;
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    tv_sync_bar(h, v);
}

static LRESULT CALLBACK tv_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    TextView *v = (TextView *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE: {
        const CREATESTRUCTW *cs = (const CREATESTRUCTW *)l;
        v = (TextView *)calloc(1, sizeof *v);
        v->text = wdup(cs->lpszName ? cs->lpszName : L"");
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)v);
        return TRUE;
    }
    case WM_NCDESTROY:
        if (v) { free(v->text); free(v); }
        break;
    case WM_SETFONT: if (v) { v->font = (HFONT)w; tv_measure(h, v); InvalidateRect(h, NULL, TRUE); } return 0;
    case WM_SIZE: if (v) { tv_measure(h, v); InvalidateRect(h, NULL, FALSE); } return 0;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_STATIC;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps), mem;
        HBITMAP bmp, oldb;
        HFONT oldf;
        HBRUSH br;
        RECT rc, tr;
        int pad = S(6);
        GetClientRect(h, &rc);
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        oldb = (HBITMAP)SelectObject(mem, bmp);
        br = CreateSolidBrush(th_page());
        FillRect(mem, &rc, br);
        DeleteObject(br);
        oldf = (HFONT)SelectObject(mem, v && v->font ? v->font : g_font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, th_text());
        tr.left = pad; tr.right = rc.right - pad;
        tr.top = pad - (v ? v->pos : 0); tr.bottom = tr.top + (v ? v->total : 0);
        DrawTextW(mem, v && v->text ? v->text : L"", -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(mem, oldf);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldb);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCPAINT:
        DefWindowProcW(h, m, w, l);   /* the scroll bar (and the sunken edge of the light theme) */
        if (g_dark) {   /* the thin line of the lists */
            HDC dc = GetWindowDC(h);
            RECT r;
            HBRUSH br = CreateSolidBrush(RGB(100, 100, 100));
            GetWindowRect(h, &r);
            OffsetRect(&r, -r.left, -r.top);
            FrameRect(dc, &r, br);
            DeleteObject(br);
            ReleaseDC(h, dc);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (v) { v->pos -= (short)HIWORD(w) / WHEEL_DELTA * S(48); tv_sync_bar(h, v); InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_VSCROLL:
        if (v) {
            SCROLLINFO si;
            int np = v->pos;
            memset(&si, 0, sizeof si);
            si.cbSize = sizeof si; si.fMask = SIF_ALL;
            GetScrollInfo(h, SB_VERT, &si);
            switch (LOWORD(w)) {
            case SB_LINEUP: np -= S(16); break;
            case SB_LINEDOWN: np += S(16); break;
            case SB_PAGEUP: np -= v->view; break;
            case SB_PAGEDOWN: np += v->view; break;
            case SB_TOP: np = 0; break;
            case SB_BOTTOM: np = v->total; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: np = si.nTrackPos; break;
            }
            v->pos = np;
            tv_sync_bar(h, v);
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void cmb_create(HWND page, HWND tip) {
    static const wchar_t *cols[] = {L"#", L"Name", L"Type", L"Sequence", L"Player", L"Side", L"Keyboard", L"XInput", L"DirectInput"};
    static const int widths[] = {40, 120, 64, 270, 64, 64, 64, 64, 70};
    static const wchar_t *plays[] = {L"Player 1", L"Player 2"}, *faces[] = {L"Left", L"Right"};   /* the side of the screen: on the left the character looks right */
    CmbUI *u = &g_cmb;
    int i;
    u->page = page;
#define T(h, key) set_tip(tip, page, h, tip_for(key))
    {   /* the explanation: a read-only text view with its own scroll bar */
        WNDCLASSW wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = tv_proc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = L"ZTextView";
        RegisterClassW(&wc);
        u->info = mk(page, L"ZTextView", INFO, WS_VSCROLL, g_dark ? 0 : WS_EX_CLIENTEDGE, 0);
        th_control(u->info, L"SCROLLBAR");   /* the same dark scroll bar theme as the lists */
    }
    u->lStep = mk(page, L"STATIC", L"Step (ms)", SS_LEFT, 0, 0); T(u->lStep, L"Step (ms)");
    u->eStep = mk(page, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_K_STEP); T(u->eStep, L"Step (ms)");
    u->lChg = mk(page, L"STATIC", L"Charge (ms)", SS_LEFT, 0, 0); T(u->lChg, L"Charge (ms)");
    u->eChg = mk(page, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_K_CHG); T(u->eChg, L"Charge (ms)");
    u->chCredit = mk(page, L"BUTTON", L"Count The Charge You Already Hold", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_K_CREDIT);
    set_tip(tip, page, u->chCredit, L"When you already hold the charge direction (down-back) as you press a combo's trigger, that time counts, so a charge move comes out sooner. Leave it off if charge moves stop working.");
    u->lLoad = mk(page, L"STATIC", L"Load Character", SS_LEFT, 0, 0); T(u->lLoad, L"Load Character");
    u->cbLoad = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_K_LOADCHAR); T(u->cbLoad, L"Load Character");
    for (i = 0; i < g_nComboChars; i++) { wchar_t t[80]; txt8(g_comboChars[i].name, t, 80); SendMessageW(u->cbLoad, CB_ADDSTRING, 0, (LPARAM)t); }
    SendMessageW(u->cbLoad, CB_SETCURSEL, (WPARAM)-1, 0);
    u->lv = lv_create(page, ID_K_LV, cols, widths, 9);
    lv_set_widths(u->lv, g_set.colsCmb, 9);
    if (ListView_GetColumnWidth(u->lv, 0) < S(30)) ListView_SetColumnWidth(u->lv, 0, S(30));   /* "10", "11" ... must fit (they showed as "1.") */
    u->lChar = mk(page, L"STATIC", L"Character", SS_LEFT, 0, 0); T(u->lChar, L"Character");
    u->cbChar = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_K_CHAR); T(u->cbChar, L"Character");
    for (i = 0; i < combo_group_count(); i++) {
        const ComboMove *mv; int n; wchar_t t[80];
        txt8(combo_group(i, &mv, &n), t, 80);
        SendMessageW(u->cbChar, CB_ADDSTRING, 0, (LPARAM)t);
    }
    SendMessageW(u->cbChar, CB_SETCURSEL, 0, 0);
    u->lMove = mk(page, L"STATIC", L"Move", SS_LEFT, 0, 0); T(u->lMove, L"Move");
    u->cbMove = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_K_MOVE); T(u->cbMove, L"Move");
    u->lSeq = mk(page, L"STATIC", L"Sequence", SS_LEFT, 0, 0); T(u->lSeq, L"Sequence");
    u->eSeq = mk(page, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_K_SEQ); T(u->eSeq, L"Sequence");
    for (i = 0; i < CMB_NTOK; i++) {
        wchar_t t[12];
        if (!TOKS[i]) { u->tok[i] = NULL; continue; }
        HICON ico = token_icon(TOKS[i]);
        txt8(TOKS[i], t, 12);
        u->tok[i] = mk(page, L"BUTTON", ico ? L"" : t, BS_PUSHBUTTON | WS_TABSTOP | (ico ? BS_ICON : 0), 0, ID_K_TOK + i);
        if (ico) SendMessageW(u->tok[i], BM_SETIMAGE, IMAGE_ICON, (LPARAM)ico);
    }
    for (i = 0; i < CMB_NTOK; i++) {   /* what each button stands for */
        static const struct { const char *k, *t; } TT[] = {
            {"U", "Up"}, {"D", "Down"}, {"L", "Left"}, {"R", "Right"}, {"UL", "Up-Left"}, {"UR", "Up-Right"}, {"DL", "Down-Left"}, {"DR", "Down-Right"},
            {"F", "Forward (towards the opponent)"}, {"B", "Back (away from the opponent)"},
            {"LP", "Light Punch (Button 1)"}, {"MP", "Medium Punch (Button 2)"}, {"HP", "Heavy Punch (Button 3)"},
            {"LK", "Light Kick (Button 4)"}, {"MK", "Medium Kick (Button 5)"}, {"HK", "Heavy Kick (Button 6)"}, {"START", "Start button"},
            {"+", "Pressed together: written between buttons / directions of one step (QCF + HP)"}, {",", "Starts the next step"}, {"~", "A short wait between two steps: one neutral step as long as Step (ms), 30 ms by default"},
            {"Hold", "Hold the next direction for the Charge time (charge moves): Hold, then a direction, e.g. Hold DL, R + HP"},
            {"QCF", "Quarter-circle Forward (D, DR, R)"}, {"QCB", "Quarter-circle Back (D, DL, L)"}, {"DP", "Dragon-punch Motion (R, D, DR)"},
            {"RDP", "Reverse Dragon-punch Motion (L, D, DL)"}, {"HCF", "Half-circle Forward"}, {"HCB", "Half-circle Back"},
            {"FCF", "Full Circle Forward (R, DR, D, DL, L, UL)"}, {"FCB", "Full Circle Back (L, DL, D, DR, R, UR)"},
            {"DQCF", "Double Quarter-circle Forward"}, {"DQCB", "Double Quarter-circle Back"}, {"Del", "Removes the last step, button or direction of the Sequence"}};
        int k;
        wchar_t tx[120];
        for (k = 0; TOKS[i] && k < (int)(sizeof TT / sizeof TT[0]); k++) if (!strcmp(TT[k].k, TOKS[i])) { txt8(TT[k].t, tx, 120); set_tip(tip, page, u->tok[i], tx); }
    }
    u->lPlay = mk(page, L"STATIC", L"Plays For", SS_LEFT, 0, 0); T(u->lPlay, L"Plays For");
    u->cbPlay = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, ID_K_PLAY); T(u->cbPlay, L"Plays For");
    combo_fill(u->cbPlay, plays, 2);
    SendMessageW(u->cbPlay, CB_SETCURSEL, 0, 0);
    u->lFace = mk(page, L"STATIC", L"Player Side", SS_LEFT, 0, 0); T(u->lFace, L"Player Side");
    u->cbFace = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, ID_K_FACE); T(u->cbFace, L"Player Side");
    combo_fill(u->cbFace, faces, 2);
    SendMessageW(u->cbFace, CB_SETCURSEL, 0, 0);
    u->bClear = mk(page, L"BUTTON", L"Clear Slot", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_K_CLEAR);
    u->bLegend = mk(page, L"BUTTON", L"Legend", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_K_LEGEND);
    set_tip(tip, page, u->bLegend, L"What the short names (QCF, HP, DR, BN1 ...) stand for.");
    u->bDef = mk(page, L"BUTTON", L"Defaults", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_K_DEF); T(u->bDef, L"Combo Defaults");
    u->note = mk(page, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0);   /* the move's note from the move list (how it is done in the game) */
#undef T
    g_rows = 0;
}

static void place(HWND h, int x, int y, int w, int hh) { SetWindowPos(h, NULL, x, y, w, hh, SWP_NOZORDER); }

void cmb_layout(int w, int h) {
    CmbUI *u = &g_cmb;
    int m = S(8), x = m, y = m, W = w - 2 * m, bh = S(26), rh = S(24), lw, form, fy, half;
    int infoH = S(164);
    place(u->info, x, y, W, infoH);
    y += infoH + S(4);
    place(u->bLegend, x + W - S(90), y - 1, S(90), S(26));   /* under the text, at the right */
    lw = text_width(u->page, L"Step (ms)") + S(10);
    place(u->lStep, x, y + S(4), lw, S(18));
    place(u->eStep, x + lw, y, S(60), rh);
    place(u->lChg, x + lw + S(80), y + S(4), text_width(u->page, L"Charge (ms)") + S(10), S(18));
    place(u->eChg, x + lw + S(80) + text_width(u->page, L"Charge (ms)") + S(10), y, S(60), rh);
    {   /* the check box to the right of Charge, as far as the Legend button leaves room */
        int cx = x + lw + S(80) + text_width(u->page, L"Charge (ms)") + S(10) + S(60) + S(14), cwid = x + W - S(96) - cx;
        place(u->chCredit, cx, y + S(2), cwid > S(40) ? cwid : S(40), S(22));
    }
    y += S(30);
    lw = text_width(u->page, L"Load Character") + S(10);
    place(u->lLoad, x, y + S(4), lw, S(18));
    place(u->cbLoad, x + lw, y, S(260), S(260));
    y += S(32);
    form = 4 * S(28) + 3 * S(32) + S(30) + S(22);           /* four form rows, three rows of sequence buttons, the button row and the note line */
    fy = h - m - form;
    place(u->lv, x, y, W, fy - y - S(6));
    lw = text_width(u->page, L"Player Side") + S(10);
    half = W / 2;
    place(u->lChar, x, fy + S(4), lw, S(18));  place(u->cbChar, x + lw, fy, W - lw, S(300));
    fy += S(28);
    place(u->lMove, x, fy + S(4), lw, S(18));  place(u->cbMove, x + lw, fy, W - lw, S(300));
    fy += S(28);
    place(u->lSeq, x, fy + S(4), lw, S(18));   place(u->eSeq, x + lw, fy, W - lw, rh);
    fy += S(28);
    {   /* three rows of ten buttons under the Sequence box */
        int k, gap = S(3), bw = (W - lw - 9 * gap) / 10, row;
        for (row = 0; row < 3; row++) {
            for (k = 0; k < 10; k++) if (u->tok[row * 10 + k]) place(u->tok[row * 10 + k], x + lw + k * (bw + gap), fy, bw, S(30));
            fy += S(32);
        }
    }
    place(u->lPlay, x, fy + S(4), text_width(u->page, L"Plays For") + S(10), S(18));
    place(u->cbPlay, x + text_width(u->page, L"Plays For") + S(10), fy, half - text_width(u->page, L"Plays For") - S(24), S(120));
    place(u->lFace, x + half, fy + S(4), lw, S(18));
    place(u->cbFace, x + half + lw, fy, W - half - lw, S(120));
    fy += S(30);
    place(u->bClear, x, fy, S(90), bh);
    place(u->bDef, x + W - S(90), fy, S(90), bh);
    place(u->note, x, fy + bh + S(4), W, S(18));
}

/* ---------------------------------------------------------------- list */
static void row_texts(int i) {
    ComboSlot *s = &g_in.slots[i];
    wchar_t t[180];
    swprintf(t, 180, L"%d", i + 1); ListView_SetItemText(g_cmb.lv, i, 0, t);
    txt8(s->name, t, 180); ListView_SetItemText(g_cmb.lv, i, 1, t);
    txt8(s->kind, t, 180); ListView_SetItemText(g_cmb.lv, i, 2, t);
    { char w[400]; wchar_t tw[400]; seq_to_words(s->seq, s->faceLeft, w, sizeof w); txt8(w, tw, 400); ListView_SetItemText(g_cmb.lv, i, 3, tw); }
    swprintf(t, 180, L"Player %d", s->player); ListView_SetItemText(g_cmb.lv, i, 4, t);
    ListView_SetItemText(g_cmb.lv, i, 5, s->faceLeft ? L"Right" : L"Left");   /* the side of the screen */
    dash8(s->key, t, 180); ListView_SetItemText(g_cmb.lv, i, 6, t);
    dash8(s->x[0] ? x_friendly(s->x) : "", t, 180); ListView_SetItemText(g_cmb.lv, i, 7, t);
    dash8(s->j[0] ? j_friendly(s->j) : "", t, 180); ListView_SetItemText(g_cmb.lv, i, 8, t);
}

static int cur_row(void) { return ListView_GetNextItem(g_cmb.lv, -1, LVNI_SELECTED); }
static ComboSlot *cur_slot(void) { int i = cur_row(); return (i >= 0 && i < g_rows) ? &g_in.slots[i] : NULL; }

static void fill_form(void);

void cmb_refresh(void) {
    CmbUI *u = &g_cmb;
    int n = combo_rows_shown(), i, cur = cur_row();
    if (n != g_rows) {
        SendMessageW(u->lv, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(u->lv);
        for (i = 0; i < n; i++) { LVITEMW it; memset(&it, 0, sizeof it); it.mask = LVIF_TEXT; it.iItem = i; it.pszText = L""; ListView_InsertItem(u->lv, &it); }
        g_rows = n;
        if (cur >= 0 && cur < n) ListView_SetItemState(u->lv, cur, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        SendMessageW(u->lv, WM_SETREDRAW, TRUE, 0);
    }
    for (i = 0; i < g_rows; i++) row_texts(i);
    InvalidateRect(u->lv, NULL, TRUE);
}

/* ---------------------------------------------------------------- pickers */
static void set_seq_edit(const ComboSlot *s) {
    char w[400];
    seq_to_words(s->seq, s->faceLeft, w, sizeof w);
    edit_set_text_utf8(g_cmb.eSeq, w);
}

static void set_note(const char *cmd) {
    wchar_t t[300];
    txt8(cmd ? cmd : "", t, 300);
    CharUpperW(t);   /* QCF,QCF + KKK */
    SetWindowTextW(g_cmb.note, t);
}

static void show_char(int ci) {
    CmbUI *u = &g_cmb;
    const ComboMove *mv;
    int n, i;
    if (ci < 0 || ci >= combo_group_count()) return;
    combo_group(ci, &mv, &n);
    SendMessageW(u->cbMove, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < n; i++) {
        char a[500];
        wchar_t *w;
        char ws[300];
        ComboSlot *cs = cur_slot();
        seq_to_words(mv[i].seq, cs ? cs->faceLeft : 0, ws, sizeof ws);
        _snprintf(a, sizeof a, "[%s] %s   (%s)", mv[i].kind, mv[i].name, ws);
        a[sizeof a - 1] = 0;
        w = u8_to_w(a, -1);
        SendMessageW(u->cbMove, CB_ADDSTRING, 0, (LPARAM)w);
        free(w);
    }
    SendMessageW(u->cbMove, CB_SETCURSEL, (WPARAM)-1, 0);
}

static void fill_form(void) {
    CmbUI *u = &g_cmb;
    ComboSlot *s = cur_slot();
    int ci, mi = -1, k;
    char *colon;
    u->syncing = 1;
    if (!s) { SetWindowTextW(u->eSeq, L""); set_note(NULL); u->syncing = 0; return; }
    set_seq_edit(s);
    SendMessageW(u->cbPlay, CB_SETCURSEL, s->player - 1, 0);
    SendMessageW(u->cbFace, CB_SETCURSEL, s->faceLeft ? 1 : 0, 0);
    ci = (int)SendMessageW(u->cbChar, CB_GETCURSEL, 0, 0);
    /* "Character: Move" names select the matching entries in the two pickers */
    colon = strstr(s->name, ": ");
    if (colon && colon > s->name) {
        char grp[160];
        int g;
        size_t gl = colon - s->name;
        memcpy(grp, s->name, gl);
        grp[gl] = 0;
        for (g = 0; g < combo_group_count(); g++) {
            const ComboMove *mv; int n;
            const char *gn = combo_group(g, &mv, &n);
            if (_stricmp(gn, grp)) continue;
            ci = g;
            show_char(ci);
            for (k = 0; k < n; k++) if (!strcmp(mv[k].name, colon + 2)) mi = k;
        }
    }
    if (ci < 0) ci = 0;
    SendMessageW(u->cbChar, CB_SETCURSEL, ci, 0);
    SendMessageW(u->cbMove, CB_SETCURSEL, mi, 0);
    {
        const ComboMove *mv; int n;
        if (mi >= 0 && ci >= 0 && ci < combo_group_count()) { combo_group(ci, &mv, &n); set_note(mi < n ? mv[mi].cmd : NULL); } else set_note(NULL);
    }
    u->syncing = 0;
}

void cmb_load(void) {
    CmbUI *u = &g_cmb;
    if (g_in.stepMs < 16) g_in.stepMs = 30;
    if (g_in.chargeMs < 100) g_in.chargeMs = 600;
    edit_set_int(u->eChg, g_in.chargeMs);
    SendMessageW(u->chCredit, BM_SETCHECK, g_in.chargeCredit ? BST_CHECKED : BST_UNCHECKED, 0);
    edit_set_int(u->eStep, g_in.stepMs);
    cmb_refresh();
    fill_form();
}

void cmb_collect(void) {
    int v = edit_int(g_cmb.eStep, 0, 500);
    if (v >= 16) g_in.stepMs = v;
    v = edit_int(g_cmb.eChg, 0, 3000);
    if (v >= 100) g_in.chargeMs = v;
}

/* a click on a trigger cell */
void cmb_cell(int row, int col) {
    ComboSlot *s;
    wchar_t label[60];
    if (row < 0 || row >= g_rows || col < 6 || col > 8) return;
    s = &g_in.slots[row];
    swprintf(label, 60, L"Combo %d Trigger", row + 1);
    if (col == 6) dlg_capture_key(g_main, label, s->key, 24);
    else {
        wchar_t t[100];
        swprintf(t, 100, L"%ls for %ls", col == 7 ? L"XInput Button" : L"DirectInput Control", label);
        dlg_pick_pad(g_main, t, col == 7, col == 7 ? s->x : s->j, 24);
    }
    cmb_refresh();
}

static void load_character(void) {
    CmbUI *u = &g_cmb;
    int i = (int)SendMessageW(u->cbLoad, CB_GETCURSEL, 0, 0);
    if (u->loading || i < 0 || i >= g_nComboChars) return;
    u->loading = 1;
    combos_apply_loadout(g_comboChars[i].name);
    cmb_refresh();
    ListView_SetItemState(u->lv, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    fill_form();
    SendMessageW(u->cbLoad, CB_SETCURSEL, (WPARAM)-1, 0);   /* cleared, so the same character can be picked again */
    u->loading = 0;
}

/* a button under the Sequence box: its text goes to the end of the Sequence, with the separator that fits */
static int is_button_word(const wchar_t *w) {
    static const wchar_t *B[] = {L"LP", L"MP", L"HP", L"LK", L"MK", L"HK", L"START", L"ST", NULL};
    int k;
    for (k = 0; B[k]; k++) if (!_wcsicmp(B[k], w)) return 1;
    return (w[0] == L'B' || w[0] == L'b') && (w[1] == L'N' || w[1] == L'n') && w[2] >= L'1' && w[2] <= L'6' && !w[3];
}

static void wtrim_end(wchar_t *t, size_t *n) { while (*n && t[*n - 1] == L' ') t[--*n] = 0; }

/* a button under the Sequence box: its text goes into the Sequence at the caret (or over the selection), with the separator that fits */
static void token_click(int i) {
    CmbUI *u = &g_cmb;
    wchar_t t[400], before[400], after[400], tok[16], res[800];
    DWORD s0 = 0, s1 = 0;
    size_t n, nb, caret;
    const wchar_t *a;
    if (!cur_slot()) { ListView_SetItemState(u->lv, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); if (!cur_slot()) return; }
    GetWindowTextW(u->eSeq, t, 400);
    n = wcslen(t);
    SendMessageW(u->eSeq, EM_GETSEL, (WPARAM)&s0, (LPARAM)&s1);
    if (s0 > n) s0 = (DWORD)n;
    if (s1 > n || s1 < s0) s1 = (DWORD)n;
    wcsncpy(before, t, s0); before[s0] = 0;
    wcscpy(after, t + s1);
    nb = wcslen(before);
    wtrim_end(before, &nb);
    for (a = after; *a == L' '; a++) ;
    memmove(after, a, (wcslen(a) + 1) * sizeof(wchar_t));
    txt8(TOKS[i], tok, 16);
    if (!strcmp(TOKS[i], "Del")) {   /* the token before the caret, a motion's (...) with it */
        if (nb && (before[nb - 1] == L',' || before[nb - 1] == L'+')) before[--nb] = 0;
        wtrim_end(before, &nb);
        if (nb && before[nb - 1] == L')') { while (nb && before[nb - 1] != L'(') nb--; if (nb) nb--; while (nb && before[nb - 1] == L' ') nb--; }
        while (nb && before[nb - 1] != L',' && before[nb - 1] != L'+' && before[nb - 1] != L' ' && before[nb - 1] != L'(') nb--;
        while (nb && (before[nb - 1] == L' ' || before[nb - 1] == L',' || before[nb - 1] == L'+')) nb--;
        before[nb] = 0;
        caret = nb;
        if (!after[0]) _snwprintf(res, 800, L"%ls", before);
        else if (!nb) { const wchar_t *r = after; while (*r == L',' || *r == L'+' || *r == L' ') r++; _snwprintf(res, 800, L"%ls", r); }
        else _snwprintf(res, 800, L"%ls%ls%ls", before, after[0] == L',' ? L"" : after[0] == L'+' ? L" " : L", ", after);
    } else {
        wchar_t sepB[8] = L"", sepA[8] = L"";
        int endsSep = !nb || before[nb - 1] == L',' || before[nb - 1] == L'+';
        if (!wcscmp(tok, L"+")) {
            if (endsSep) tok[0] = 0;   /* nothing to join yet, or already joined */
            else { wcscpy(sepB, L" "); if (after[0] && after[0] != L'+') wcscpy(sepA, L" "); }
        } else if (!wcscmp(tok, L",")) {
            if (endsSep) tok[0] = 0;
            else if (after[0] && after[0] != L',') wcscpy(sepA, L" ");
        } else {
            if (endsSep) { if (nb) wcscpy(sepB, L" "); }
            else {   /* a button after a direction or a motion is pressed with it, anything else is the next step */
                size_t e = nb, b0;
                wchar_t last[24];
                while (e && before[e - 1] != L',' && before[e - 1] != L'+' && before[e - 1] != L' ' && before[e - 1] != L')') e--;
                b0 = e;
                last[0] = 0;
                if (before[nb - 1] != L')') { size_t l = nb - b0; if (l > 0 && l < 24) { wmemcpy(last, before + b0, l); last[l] = 0; } }
                if (!_wcsicmp(last, L"Hold")) wcscpy(sepB, L" ");   /* Hold, then the direction that is held */
                else wcscpy(sepB, (is_button_word(tok) && (before[nb - 1] == L')' || (last[0] && !is_button_word(last) && wcscmp(last, L"~")))) ? L" + " : L", ");
            }
            if (after[0]) wcscpy(sepA, !_wcsicmp(tok, L"Hold") ? L" " : after[0] == L',' ? L"" : after[0] == L'+' ? L" " : L", ");
        }
        caret = nb + wcslen(sepB) + wcslen(tok);
        _snwprintf(res, 800, L"%ls%ls%ls%ls%ls", before, sepB, tok, sepA, after);
    }
    res[399] = 0;
    SetWindowTextW(u->eSeq, res);   /* EN_CHANGE reads it */
    SetFocus(u->eSeq);
    SendMessageW(u->eSeq, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
}

int cmb_command(int id, int code) {
    CmbUI *u = &g_cmb;
    ComboSlot *s;
    if (id == ID_K_LEGEND) { if (code == BN_CLICKED) dlg_legend(); return 1; }
    if (id == ID_K_CREDIT) { if (code == BN_CLICKED) g_in.chargeCredit = SendMessageW(u->chCredit, BM_GETCHECK, 0, 0) == BST_CHECKED; return 1; }
    if (id >= ID_K_TOK && id < ID_K_TOK + CMB_NTOK) { if (code == BN_CLICKED) token_click(id - ID_K_TOK); return 1; }
    if (id == ID_K_STEP || id == ID_K_CHG) { if (code == EN_CHANGE) cmb_collect(); return 1; }
    if (id == ID_K_LOADCHAR && code == CBN_SELCHANGE) { load_character(); return 1; }
    if (id == ID_K_CHAR && code == CBN_SELCHANGE) {
        if (!u->syncing && SendMessageW(u->cbChar, CB_GETCURSEL, 0, 0) >= 0) show_char((int)SendMessageW(u->cbChar, CB_GETCURSEL, 0, 0));
        return 1;
    }
    if (id == ID_K_MOVE && code == CBN_SELCHANGE) {
        int ci = (int)SendMessageW(u->cbChar, CB_GETCURSEL, 0, 0), mi = (int)SendMessageW(u->cbMove, CB_GETCURSEL, 0, 0);
        const ComboMove *mv; int n;
        const char *gn;
        if (u->syncing || ci < 0 || mi < 0 || ci >= combo_group_count()) return 1;
        gn = combo_group(ci, &mv, &n);
        if (mi >= n || !(s = cur_slot())) return 1;
        _snprintf(s->name, sizeof s->name, "%s: %s", gn, mv[mi].name);
        s->name[sizeof s->name - 1] = 0;
        strncpy(s->kind, mv[mi].kind, sizeof s->kind - 1);
        strncpy(s->seq, mv[mi].seq, sizeof s->seq - 1);
        u->syncing = 1;
        set_seq_edit(s);
        set_note(mv[mi].cmd);
        u->syncing = 0;
        cmb_refresh();
        return 1;
    }
    if (id == ID_K_SEQ && code == EN_CHANGE) {
        if (u->syncing || !(s = cur_slot())) return 1;
        {   /* words become the short notation the plugin reads; anything else is kept as typed */
            char raw[400], seq[400];
            edit_get_text_utf8(u->eSeq, raw, sizeof raw);
            strncpy(s->seq, words_to_seq(raw, s->faceLeft, seq, sizeof seq) ? seq : raw, sizeof s->seq - 1);
            s->seq[sizeof s->seq - 1] = 0;
        }
        set_note(NULL);
        strcpy(s->name, "Custom");
        strcpy(s->kind, "Custom");
        u->syncing = 1;
        SendMessageW(u->cbMove, CB_SETCURSEL, (WPARAM)-1, 0);
        u->syncing = 0;
        cmb_refresh();
        return 1;
    }
    if (id == ID_K_PLAY && code == CBN_SELCHANGE) {
        int i = (int)SendMessageW(u->cbPlay, CB_GETCURSEL, 0, 0);
        if ((s = cur_slot()) && !u->syncing && i >= 0) { s->player = i + 1; cmb_refresh(); }
        return 1;
    }
    if (id == ID_K_FACE && code == CBN_SELCHANGE) {
        int i = (int)SendMessageW(u->cbFace, CB_GETCURSEL, 0, 0);
        if ((s = cur_slot()) && !u->syncing && i >= 0) {
            int ci = (int)SendMessageW(u->cbChar, CB_GETCURSEL, 0, 0), mi = (int)SendMessageW(u->cbMove, CB_GETCURSEL, 0, 0);
            s->faceLeft = i == 1;
            u->syncing = 1;   /* Left / Right in the words swap with the player side */
            set_seq_edit(s);
            if (ci >= 0) { show_char(ci); SendMessageW(u->cbMove, CB_SETCURSEL, mi, 0); }
            u->syncing = 0;
            cmb_refresh();
        }
        return 1;
    }
    if (id == ID_K_DEF && code == BN_CLICKED) {
        int r = 0;
        msg_box(g_main, L"Combo Defaults", L"Restore the combos to their defaults?\n\nAll rows, triggers and the step / charge times of the Combos tab go back to the start values. Other tabs are not touched.", MB_YESNO | MB_ICONQUESTION, &r);
        if (r == IDYES) { combos_defaults(); cmb_load(); ListView_SetItemState(u->lv, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); fill_form(); }
        return 1;
    }
    if (id == ID_K_CLEAR && code == BN_CLICKED) {
        if ((s = cur_slot())) { memset(s, 0, sizeof *s); s->player = 1; cmb_refresh(); fill_form(); }
        return 1;
    }
    return 0;
}

int cmb_notify(NMHDR *nh) {
    if (nh->idFrom != ID_K_LV) return 0;
    if (nh->code == NM_CLICK) {
        int row, col;
        if (lv_hit_cell(g_cmb.lv, (LPARAM)nh, &row, &col)) {
            if (row < g_rows) ListView_SetItemState(g_cmb.lv, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            if (col >= 6 && col <= 8) PostMessageW(g_main, WM_APP_CELL, 1, MAKELPARAM(row, col));
        }
        return 1;
    }
    if (nh->code == NM_DBLCLK) {   /* a double click on the name renames the combo */
        int row, col;
        if (lv_hit_cell(g_cmb.lv, (LPARAM)nh, &row, &col) && row < g_rows && col == 1) {
            ComboSlot *s = &g_in.slots[row];
            wchar_t t[160];
            ListView_SetItemState(g_cmb.lv, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            txt8(s->name, t, 160);
            if (dlg_text(g_main, L"Rename Combo", L"Name of this combo:", t, 100)) {
                char *u = w_to_u8(t);
                strncpy(s->name, u ? u : "", sizeof s->name - 1);
                s->name[sizeof s->name - 1] = 0;
                free(u);
                cmb_refresh();
            }
        }
        return 1;
    }
    if (nh->code == NM_RCLICK) {   /* a right click on a trigger cell: menu with Set and Clear */
        int row, col;
        if (lv_hit_cell(g_cmb.lv, (LPARAM)nh, &row, &col) && row < g_rows && col >= 6 && col <= 8) {
            ComboSlot *s = &g_in.slots[row];
            int pick;
            ListView_SetItemState(g_cmb.lv, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            pick = cell_menu(g_main, 1);
            if (pick == 2) { if (col == 6) s->key[0] = 0; else if (col == 7) s->x[0] = 0; else s->j[0] = 0; cmb_refresh(); }
            else if (pick == 1) PostMessageW(g_main, WM_APP_CELL, 1, MAKELPARAM(row, col));
        }
        return 1;
    }
    if (nh->code == LVN_ITEMCHANGED) {
        NMLISTVIEW *lv = (NMLISTVIEW *)nh;
        if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED)) fill_form();
        return 1;
    }
    if (nh->code == NM_CUSTOMDRAW) return -1;
    return 0;
}
