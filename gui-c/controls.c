/* The "Controls" tab: key / XInput / DirectInput bindings, autofire, pad choice. */
#include "common.h"

CtlUI g_ctl;

static const wchar_t *padChoices[] = {L"Auto", L"None", L"Controller 1", L"Controller 2", L"Controller 3", L"Controller 4"};

static int pad_index(int p) { return p < 0 ? 1 : p == 0 ? 0 : p + 1; }
static int pad_value(int i) { return i == 0 ? 0 : i == 1 ? -1 : i - 1; }

static void profiles_fill(const wchar_t *select);

static void dash_text(const char *s, wchar_t *out, int cap) { if (!s[0]) wcscpy(out, L"-"); else wide_from_ascii(s, out, cap); }

void ctl_create(HWND page, HWND tip) {
    static const wchar_t *cols[] = {L"Action", L"Keyboard", L"XInput", L"DirectInput", L"Autofire"};
    static const int widths[] = {105, 70, 100, 100, 64};
    CtlUI *c = &g_ctl;
    int i;
    c->page = page;
    c->analog = 1;
#define T(h, key) set_tip(tip, page, h, tip_for(key))
    c->chEnh = mk(page, L"BUTTON", L"Use Enhanced Input (Remappable Keys, XInput, DirectInput)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_C_ENH);
    T(c->chEnh, L"Use Enhanced Input (Remappable Keys, XInput, DirectInput)");
    c->lP1 = mk(page, L"STATIC", L"Player 1 Pad", SS_LEFT, 0, 0); T(c->lP1, L"Player 1 Pad");
    c->cbP1 = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_C_P1); T(c->cbP1, L"Player 1 Pad");
    c->lP2 = mk(page, L"STATIC", L"Player 2 Pad", SS_LEFT, 0, 0); T(c->lP2, L"Player 2 Pad");
    c->cbP2 = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_C_P2); T(c->cbP2, L"Player 2 Pad");
    combo_fill(c->cbP1, padChoices, 6);
    combo_fill(c->cbP2, padChoices, 6);
    c->lDead = mk(page, L"STATIC", L"Stick Threshold %", SS_LEFT, 0, 0); T(c->lDead, L"Stick Threshold %");
    c->eDead = mk(page, L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_C_DEAD); T(c->eDead, L"Stick Threshold %");
    c->lProf = mk(page, L"STATIC", L"Profile", SS_LEFT, 0, 0); T(c->lProf, L"Profile");
    c->cbProf = mk(page, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, ID_C_PROF); T(c->cbProf, L"Profile");
    c->bPSave = mk(page, L"BUTTON", L"Save As…", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_PSAVE); T(c->bPSave, L"Save As…");
    c->bPDel = mk(page, L"BUTTON", L"Delete", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_PDEL); T(c->bPDel, L"Delete Profile");
    c->lv = lv_create(page, ID_C_LV, cols, widths, 5);
    lv_set_widths(c->lv, g_set.colsCtl, 5);
    for (i = 0; i < g_in.nrows; i++) {
        LVITEMW it;
        memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = g_in.rows[i].label;
        ListView_InsertItem(c->lv, &it);
    }
    c->bKey = mk(page, L"BUTTON", L"Key…", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_KEY); T(c->bKey, L"Key…");
    c->bX = mk(page, L"BUTTON", L"XInput…", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_XB); T(c->bX, L"XInput…");
    c->bJ = mk(page, L"BUTTON", L"DirectInput…", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_JB); T(c->bJ, L"DirectInput…");
    c->bClear = mk(page, L"BUTTON", L"Clear", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_CLEAR); T(c->bClear, L"Clear");
    c->bDef = mk(page, L"BUTTON", L"Defaults", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_DEF); T(c->bDef, L"Defaults");
    c->lPads = mk(page, L"STATIC", L"", SS_LEFT, 0, 0);
    c->bDetect = mk(page, L"BUTTON", L"Detect", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_C_DETECT); T(c->bDetect, L"Detect");
#undef T
}

static void place(HWND h, int x, int y, int w, int hh) { SetWindowPos(h, NULL, x, y, w, hh, SWP_NOZORDER); }

void ctl_layout(int w, int h) {
    CtlUI *c = &g_ctl;
    int m = S(8), x = m, y = m, W = w - 2 * m, rh = S(24), half = W / 2, lw, bw, by, i;
    int ch = S(22), bh = S(26);
    place(c->chEnh, x, y, W, ch); y += S(26);
    y += S(2);
    lw = text_width(c->page, L"Player 1 Pad") + S(10);
    place(c->lP1, x, y + S(4), lw, S(18));
    place(c->cbP1, x + lw, y, half - lw - S(8), S(200));
    place(c->lP2, x + half, y + S(4), lw, S(18));
    place(c->cbP2, x + half + lw, y, half - lw, S(200));
    y += S(28);
    lw = text_width(c->page, L"Stick Threshold %") + S(10);
    place(c->lDead, x, y + S(4), lw, S(18));
    place(c->eDead, x + lw, y, S(70), rh);
    y += S(32);
    {   /* profiles: pick one to load it, Save As keeps the current layout under a name, Delete removes the chosen one */
        int pl = text_width(c->page, L"Profile") + S(10), bw2 = S(84), dw = S(66);
        int cbw = W - pl - bw2 - dw - 2 * S(8);
        place(c->lProf, x, y + S(4), pl, S(18));
        place(c->cbProf, x + pl, y, cbw, S(220));
        place(c->bPSave, x + W - bw2 - S(8) - dw, y - 1, bw2, bh);
        place(c->bPDel, x + W - dw, y - 1, dw, bh);
    }
    y += S(32);
    by = h - m - S(44);                      /* pad detection text and Detect button */
    place(c->lPads, x, by, W - S(80), S(44));
    place(c->bDetect, x + W - S(72), by + S(10), S(72), bh);
    by -= S(32);                             /* buttons under the list */
    bw = S(86);
    place(c->bKey, x, by, S(64), bh);
    place(c->bX, x + S(70), by, S(76), bh);
    place(c->bJ, x + S(152), by, S(96), bh);
    place(c->bClear, x + S(254), by, S(60), bh);
    place(c->bDef, x + W - S(80), by, S(80), bh);
    place(c->lv, x, y, W, by - y - S(6));
    (void)i; (void)bw;
}

void ctl_refresh_rows(void) {
    CtlUI *c = &g_ctl;
    int i;
    for (i = 0; i < g_in.nrows; i++) {
        RoleRow *r = &g_in.rows[i];
        wchar_t t[40];
        dash_text(r->key, t, 40);
        ListView_SetItemText(c->lv, i, 1, t);
        if (r->dir && !r->x[0]) wcscpy(t, c->analog ? L"D-Pad / Stick" : L"-"); else dash_text(r->x[0] ? x_friendly(r->x) : "", t, 40);   /* directions: the D-pad / stick work by themselves while "Pad Directions" is on, a binding adds to it */
        ListView_SetItemText(c->lv, i, 2, t);
        if (r->dir && !r->j[0]) wcscpy(t, c->analog ? L"Hat / Axes" : L"-"); else dash_text(r->j[0] ? j_friendly(r->j) : "", t, 40);
        ListView_SetItemText(c->lv, i, 3, t);
        {
            size_t l = strlen(r->id);
            int af = l == 5 && r->id[0] == 'p' && r->id[3] == 'b' && r->id[4] >= '1' && r->id[4] <= '6';
            ListView_SetItemText(c->lv, i, 4, af ? (r->autofire ? L"[x]" : L"[ ]") : L"");
        }
    }
    InvalidateRect(c->lv, NULL, TRUE);
}

static void refresh_pads(void) {
    wchar_t t[600];
    detect_pads(t, 600);
    SetWindowTextW(g_ctl.lPads, t);
}

void ctl_load(void) {
    CtlUI *c = &g_ctl;
    input_load_cfg();
    ctl_set_checked(c->chEnh, g_set.enhanced);
    g_set.xinputOn = g_set.dinputOn = 1;   /* XInput and DirectInput are always on */
    g_set.analog = 1;   /* the D-pad, stick and hat always drive the directions */
    c->analog = 1;
    edit_set_int(c->eDead, g_set.deadzone);
    SendMessageW(c->cbP1, CB_SETCURSEL, pad_index(g_set.pad1), 0);
    SendMessageW(c->cbP2, CB_SETCURSEL, pad_index(g_set.pad2), 0);
    ctl_refresh_rows();
    refresh_pads();
    profiles_fill(NULL);
    cmb_load();
}

void ctl_collect(void) {
    CtlUI *c = &g_ctl;
    g_set.enhanced = ctl_checked(c->chEnh);
    g_set.xinputOn = g_set.dinputOn = 1;
    g_set.analog = 1;
    g_set.deadzone = edit_int(c->eDead, 5, 95);
    g_set.pad1 = pad_value((int)SendMessageW(c->cbP1, CB_GETCURSEL, 0, 0));
    g_set.pad2 = pad_value((int)SendMessageW(c->cbP2, CB_GETCURSEL, 0, 0));
    cmb_collect();
}

/* ---- profiles */
static wchar_t g_pnames[64][64];
static int g_np;

static void profiles_fill(const wchar_t *select) {
    CtlUI *c = &g_ctl;
    int i, sel = 0;
    g_np = profile_names(g_pnames, 64);
    SendMessageW(c->cbProf, CB_RESETCONTENT, 0, 0);
    SendMessageW(c->cbProf, CB_ADDSTRING, 0, (LPARAM)L"(Choose A Profile)");
    for (i = 0; i < g_np; i++) {
        SendMessageW(c->cbProf, CB_ADDSTRING, 0, (LPARAM)g_pnames[i]);
        if (select && !_wcsicmp(select, g_pnames[i])) sel = i + 1;
    }
    SendMessageW(c->cbProf, CB_SETCURSEL, sel, 0);
    EnableWindow(c->bPDel, sel > 0);
}

/* the pad choice and the threshold of a loaded profile */
static void pads_to_widgets(void) {
    CtlUI *c = &g_ctl;
    edit_set_int(c->eDead, g_set.deadzone);
    SendMessageW(c->cbP1, CB_SETCURSEL, pad_index(g_set.pad1), 0);
    SendMessageW(c->cbP2, CB_SETCURSEL, pad_index(g_set.pad2), 0);
}

static void profile_selected(void) {
    CtlUI *c = &g_ctl;
    int i = (int)SendMessageW(c->cbProf, CB_GETCURSEL, 0, 0);
    EnableWindow(c->bPDel, i > 0);
    if (i > 0 && i <= g_np) {
        char *text = profile_load(g_pnames[i - 1]);
        if (!text) { msg_box(g_main, L"Profile", L"The profile file could not be read.", MB_ICONWARNING, NULL); return; }
        input_profile_apply(text);
        free(text);
        pads_to_widgets();
        ctl_refresh_rows();
    }
}

static void profile_save_as(void) {
    CtlUI *c = &g_ctl;
    wchar_t name[64] = L"";
    int cur = (int)SendMessageW(c->cbProf, CB_GETCURSEL, 0, 0), i, exists = 0;
    Buf b = {0};
    if (cur > 0 && cur <= g_np) wcscpy(name, g_pnames[cur - 1]);
    if (!dlg_text(g_main, L"Save Controls Profile", L"Name of the profile (arcade stick, pad, keyboard ...):", name, 64)) return;
    profile_clean_name(name);
    if (!name[0]) return;
    for (i = 0; i < g_np; i++) if (!_wcsicmp(name, g_pnames[i])) exists = 1;
    if (exists) {
        int r = 0;
        msg_box(g_main, L"Save Controls Profile", L"A profile with this name exists. Replace it?", MB_YESNO | MB_ICONQUESTION, &r);
        if (r != IDYES) return;
    }
    ctl_collect();
    input_profile_text(&b);
    if (!profile_save(name, b.s)) msg_box(g_main, L"Save Controls Profile", L"The profile could not be saved (is the ZiNc folder writable?).", MB_ICONWARNING, NULL);
    buf_free(&b);
    profiles_fill(name);
}

static void profile_delete_ask(void) {
    CtlUI *c = &g_ctl;
    int cur = (int)SendMessageW(c->cbProf, CB_GETCURSEL, 0, 0), r = 0;
    wchar_t t[200];
    if (cur < 1 || cur > g_np) return;
    swprintf(t, 200, L"Delete the profile \"%ls\"?", g_pnames[cur - 1]);
    msg_box(g_main, L"Delete Controls Profile", t, MB_YESNO | MB_ICONQUESTION, &r);
    if (r != IDYES) return;
    profile_delete(g_pnames[cur - 1]);
    profiles_fill(NULL);
}

static RoleRow *selected_row(void) {
    int i = ListView_GetNextItem(g_ctl.lv, -1, LVNI_SELECTED);
    if (i < 0 || i >= g_in.nrows) {
        msg_box(g_main, L"Controls", L"Select an action in the list first.", MB_ICONINFORMATION, NULL);
        return NULL;
    }
    return &g_in.rows[i];
}

static void select_row(int i) {
    ListView_SetItemState(g_ctl.lv, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

/* which controller an action belongs to: Player 1, Coin 1, Test and Service use the first one, Player 2 and Coin 2 the second */
static int pad_group(const RoleRow *r) { return (!strncmp(r->id, "p2_", 3) || !strcmp(r->id, "coin2")) ? 2 : 1; }

/* a key or button belongs to one action only: the row that already has it (NULL when it is free).
   The keyboard is one device for all actions; the pad buttons of the two players are different devices, so only the actions of one player count. */
static const RoleRow *dup_owner(const RoleRow *self, const char *val, int col) {
    int i;
    if (!val[0]) return NULL;
    for (i = 0; i < g_in.nrows; i++) {
        const RoleRow *o = &g_in.rows[i];
        const char *v = col == 1 ? o->key : col == 2 ? o->x : o->j;
        if (o == self || _stricmp(v, val)) continue;
        if (col != 1 && pad_group(o) != pad_group(self)) continue;
        return o;
    }
    return NULL;
}

/* runs a capture dialog for one binding; one that another action has already is only kept when you agree to take it from there */
static void bind_checked(RoleRow *r, int col, wchar_t *title) {
    char *dst = col == 1 ? r->key : col == 2 ? r->x : r->j, old[24];
    const RoleRow *o;
    strncpy(old, dst, 23); old[23] = 0;
    if (col == 1) dlg_capture_key(g_main, title, dst, 24); else dlg_pick_pad(g_main, title, col == 2, dst, 24);
    if ((o = dup_owner(r, dst, col)) != NULL) {
        wchar_t t[400];
        int yes = 0;
        swprintf(t, 400, L"\"%hs\" is already used by \"%ls\".\n\nUse it for \"%ls\" instead? \"%ls\" will then have no %ls.", dst, o->label, r->label, o->label, col == 1 ? L"key" : L"button");
        msg_box(g_main, L"Controls", t, MB_YESNO | MB_ICONQUESTION, &yes);
        if (yes == IDYES) { char *od = col == 1 ? ((RoleRow *)o)->key : col == 2 ? ((RoleRow *)o)->x : ((RoleRow *)o)->j; od[0] = 0; }   /* taken from the other action */
        else strcpy(dst, old);   /* kept as it was */
    }
    ctl_refresh_rows();
}

static void set_key(RoleRow *r) { bind_checked(r, 1, r->label); }
static void set_x(RoleRow *r) {
    wchar_t t[120];
    swprintf(t, 120, L"XInput Button for %ls", r->label);
    bind_checked(r, 2, t);
}
static void set_j(RoleRow *r) {
    wchar_t t[120];
    swprintf(t, 120, L"DirectInput Control for %ls", r->label);
    bind_checked(r, 3, t);
}

void ctl_reset_ask(HWND owner) {
    int r = 0;
    msg_box(owner, L"Reset Controls", L"Restore the default keyboard and gamepad layout?", MB_YESNO | MB_ICONQUESTION, &r);
    if (r != IDYES) return;
    input_reset_rows();
    ctl_refresh_rows();
}

int ctl_command(int id, int code) {
    RoleRow *r;
    if (id == ID_C_PROF) { if (code == CBN_SELCHANGE) profile_selected(); return 1; }
    if (code != BN_CLICKED && id != ID_C_DEAD) return 0;
    switch (id) {
    case ID_C_KEY: if ((r = selected_row())) set_key(r); return 1;
    case ID_C_XB: if ((r = selected_row())) set_x(r); return 1;
    case ID_C_JB: if ((r = selected_row())) set_j(r); return 1;
    case ID_C_CLEAR: if ((r = selected_row())) { r->key[0] = r->x[0] = r->j[0] = 0; ctl_refresh_rows(); } return 1;
    case ID_C_DEF: ctl_reset_ask(g_main); return 1;
    case ID_C_PSAVE: profile_save_as(); return 1;
    case ID_C_PDEL: profile_delete_ask(); return 1;
    case ID_C_DETECT: refresh_pads(); return 1;
    }
    return 0;
}

/* a click on a cell opens the matching editor: Keyboard -> Key..., XInput -> XInput..., DirectInput -> DirectInput...;
   the Action column only selects the row and the Autofire column toggles autofire */
void ctl_cell(int row, int col) {
    RoleRow *r;
    if (row < 0 || row >= g_in.nrows) return;
    r = &g_in.rows[row];
    if (col == 4) {
        size_t l = strlen(r->id);
        if (l == 5 && r->id[0] == 'p' && r->id[3] == 'b' && r->id[4] >= '1' && r->id[4] <= '6') { r->autofire = !r->autofire; ctl_refresh_rows(); }
        return;
    }
    if (col == 1) set_key(r);
    else if (col == 2) set_x(r);
    else if (col == 3) set_j(r);
}

int ctl_notify(NMHDR *nh) {
    if (nh->idFrom != ID_C_LV) return 0;
    if (nh->code == NM_CLICK) {
        int row, col;
        if (lv_hit_cell(g_ctl.lv, (LPARAM)nh, &row, &col)) {
            select_row(row);
            if (col >= 1 && col <= 4) PostMessageW(g_main, WM_APP_CELL, 0, MAKELPARAM(row, col));
        }
        return 1;
    }
    if (nh->code == NM_RCLICK) {   /* a right click on a Keyboard / XInput / DirectInput cell: menu with Set and Clear */
        int row, col;
        if (lv_hit_cell(g_ctl.lv, (LPARAM)nh, &row, &col) && row >= 0 && row < g_in.nrows && col >= 1 && col <= 3) {
            RoleRow *r = &g_in.rows[row];
            int pick;
            select_row(row);
            pick = cell_menu(g_main, 1);
            if (pick == 2) { if (col == 1) r->key[0] = 0; else if (col == 2) r->x[0] = 0; else r->j[0] = 0; ctl_refresh_rows(); }
            else if (pick == 1) PostMessageW(g_main, WM_APP_CELL, 0, MAKELPARAM(row, col));
        }
        return 1;
    }
    if (nh->code == NM_CUSTOMDRAW) return -1;   /* handled by the caller (alternating row colours) */
    return 0;
}
