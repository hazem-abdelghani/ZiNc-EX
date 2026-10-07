/* Small helpers around Win32 controls, and the modal dialogs (key capture, pad button picker, About). */
#include "common.h"

HINSTANCE g_inst;
HFONT g_font;
int g_dpi = 96;
HICON g_icon, g_iconSm;
HWND g_main;

static const wchar_t *REPO_URL = L"https://github.com/hazem-abdelghani/ZiNc-EX";

int S(int px) { return MulDiv(px, g_dpi, 96); }

void wide_from_ascii(const char *s, wchar_t *out, int cap) {
    int i = 0;
    while (s[i] && i < cap - 1) { out[i] = (unsigned char)s[i]; i++; }
    out[i] = 0;
}

HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id) {
    HWND h = CreateWindowExW(ex, cls, text, style | WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_inst, NULL);
    if (h) { SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); th_control(h, cls); }
    return h;
}

void set_tip(HWND tipWnd, HWND parent, HWND ctl, const wchar_t *text) {
    TOOLINFOW ti;
    if (!text || !ctl) return;
    memset(&ti, 0, sizeof ti);
    ti.cbSize = sizeof ti;
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = (LPWSTR)text;
    SendMessageW(tipWnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

int text_width(HWND h, const wchar_t *t) {
    HDC dc = GetDC(h);
    HGDIOBJ old = SelectObject(dc, g_font);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &sz);
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    return sz.cx;
}

int ctl_checked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void ctl_set_checked(HWND h, int on) { SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }

int edit_int(HWND h, int lo, int hi) {
    wchar_t t[32];
    int v;
    GetWindowTextW(h, t, 32);
    if (!t[0]) return lo;
    v = _wtoi(t);
    return v < lo ? lo : v > hi ? hi : v;
}
void edit_set_int(HWND h, int v) { wchar_t t[32]; swprintf(t, 32, L"%d", v); SetWindowTextW(h, t); }
void edit_set_text_utf8(HWND h, const char *s) { wchar_t *w = u8_to_w(s, -1); SetWindowTextW(h, w); free(w); }
void edit_get_text_utf8(HWND h, char *out, int cap) {
    wchar_t w[512];
    char *u;
    GetWindowTextW(h, w, 512);
    u = w_to_u8(w);
    strncpy(out, u, cap - 1);
    out[cap - 1] = 0;
    free(u);
}
void combo_fill(HWND cb, const wchar_t **items, int n) {
    int i;
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < n; i++) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)items[i]);
}
void msg_box(HWND owner, const wchar_t *title, const wchar_t *text, UINT flags, int *result) {
    int r = MessageBoxW(owner, text, title, flags);
    if (result) *result = r;
}

/* ---------------------------------------------------------------- list views */
int g_lvProg;   /* > 0 while the program itself sets column widths (a hidden column has the width 0) */

#define MIN_COL_W 64   /* the narrowest a list column can be made (96 dpi pixels): it would vanish */
#define MIN_COL_W_NARROW 32   /* ... except the Favorite, # Status and Year columns (they hold a star, a number, an icon or a year) */

/* the narrowest width of one column of a list, in pixels */
int lv_min_w(HWND lv, int col) {
    int id = GetDlgCtrlID(lv);
    if ((id == ID_GAMELIST && (col == 0 || col == 1 || col == 4 || col == 6)) || (id == ID_K_LV && col == 0)) return S(MIN_COL_W_NARROW);
    return S(MIN_COL_W);
}
/* a column cannot be dragged narrower than this */
static LRESULT CALLBACK lvmin_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (m == WM_HSCROLL) { LRESULT r = DefSubclassProc(h, m, w, l); InvalidateRect(h, NULL, TRUE); return r; }   /* the lines are drawn again at their new places */
    if (m == WM_NOTIFY) {   /* a double-click on the divider of a column: its width fits the header and the content, but not below the minimum (a column of icons, a star ... has no text to fit) */
        NMHEADERW *nh = (NMHEADERW *)l;
        if ((nh->hdr.code == HDN_DIVIDERDBLCLICKW || nh->hdr.code == HDN_DIVIDERDBLCLICKA) && nh->iItem >= 0 && !g_lvProg) {
            int w, mn = lv_min_w(h, nh->iItem);
            g_lvProg++;
            SendMessageW(h, WM_SETREDRAW, FALSE, 0);   /* one repaint at the end: no flash of the other columns */
            ListView_SetColumnWidth(h, nh->iItem, LVSCW_AUTOSIZE_USEHEADER);
            w = ListView_GetColumnWidth(h, nh->iItem);
            if (w < mn) ListView_SetColumnWidth(h, nh->iItem, mn);
            SendMessageW(h, WM_SETREDRAW, TRUE, 0);
            g_lvProg--;
            RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return TRUE;
        }
    }
    if (m == WM_NOTIFY && !g_lvProg) {
        NMHEADERW *nh = (NMHEADERW *)l;
        if ((nh->hdr.code == HDN_ITEMCHANGINGW || nh->hdr.code == HDN_ITEMCHANGINGA) && nh->pitem && (nh->pitem->mask & HDI_WIDTH) && nh->pitem->cxy < lv_min_w(h, nh->iItem) && nh->pitem->cxy < ListView_GetColumnWidth(h, nh->iItem)) return TRUE;   /* a column does not get narrower than the minimum (it may still grow from below it) */
    }
    return DefSubclassProc(h, m, w, l);
}

HWND lv_create(HWND parent, int id, const wchar_t **cols, const int *widths, int ncols) {
    HWND lv = mk(parent, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP | WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE, id);
    LVCOLUMNW c;
    int i;
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(lv, lvmin_proc, 91, 0);
    memset(&c, 0, sizeof c);
    for (i = 0; i < ncols; i++) {
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)cols[i];
        c.cx = S(widths[i]);
        ListView_InsertColumn(lv, i, &c);
    }
    return lv;
}

void lv_get_widths(HWND lv, int *out, int n) {
    int i;
    for (i = 0; i < n; i++) { int w = ListView_GetColumnWidth(lv, i); out[i] = w > 0 ? MulDiv(w, 96, g_dpi) : 0; }
}
void lv_set_widths(HWND lv, const int *w, int n) {
    int i;
    for (i = 0; i < n; i++) if (w[i] >= 8 && w[i] < 4000) ListView_SetColumnWidth(lv, i, S(w[i]) < lv_min_w(lv, i) ? lv_min_w(lv, i) : S(w[i]));
}

/* every second row gets a slightly darker background */
LRESULT lv_altrows(LPNMLVCUSTOMDRAW cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPREPAINT:
        if (g_dark) cd->clrText = RGB(232, 232, 232);
        if ((cd->nmcd.dwItemSpec & 1) && !(cd->nmcd.uItemState & CDIS_SELECTED)) cd->clrTextBk = th_alt();
        return CDRF_NEWFONT;
    case CDDS_POSTPAINT: {   /* a thin line at the right edge of every column, over the whole height of the list (both themes) */
        HWND lv = cd->nmcd.hdr.hwndFrom, hdr = ListView_GetHeader(lv);
        RECT cr, hr;
        HBRUSH b = CreateSolidBrush(g_dark ? RGB(64, 64, 64) : RGB(224, 224, 224));
        int i, n = hdr ? Header_GetItemCount(hdr) : 0, top = 0, x = 0, order[32];
        GetClientRect(lv, &cr);
        if (hdr) { GetWindowRect(hdr, &hr); top = hr.bottom - hr.top; }
        if (n > 32) n = 32;
        for (i = 0; i < n; i++) order[i] = i;
        ListView_GetColumnOrderArray(lv, n, order);
        x = -GetScrollPos(lv, SB_HORZ);   /* the list scrolls sideways by this much; the columns are added up from the left in the order they are shown (the header itself is not asked: it follows a little later) */
        for (i = 0; i < n; i++) {
            int w = ListView_GetColumnWidth(lv, order[i]);
            x += w;
            if (w < 2 || x - 1 < 0 || x - 1 >= cr.right) continue;
            { RECT l = {x - 1, top, x, cr.bottom}; FillRect(cd->nmcd.hdc, &l, b); }
        }
        DeleteObject(b);
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

/* the row / column under the mouse of an NM_CLICK notification; 1 when a cell was hit */
int lv_hit_cell(HWND lv, LPARAM nm, int *row, int *col) {
    LPNMITEMACTIVATE ia = (LPNMITEMACTIVATE)nm;
    LVHITTESTINFO ht;
    memset(&ht, 0, sizeof ht);
    ht.pt = ia->ptAction;
    ListView_SubItemHitTest(lv, &ht);
    if (ht.iItem < 0) return 0;
    *row = ht.iItem;
    *col = ht.iSubItem;
    return 1;
}

/* a small menu at the mouse pointer: "Set..." (when withSet) and "Clear"; returns 1 (Set), 2 (Clear) or 0 */
int cell_menu(HWND owner, int withSet) {
    HMENU m = CreatePopupMenu();
    POINT pt;
    int r;
    GetCursorPos(&pt);
    if (withSet) AppendMenuW(m, MF_STRING, 1, L"Set\u2026");
    AppendMenuW(m, MF_STRING, 2, L"Clear");
    r = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, NULL);
    DestroyMenu(m);
    return r;
}

/* ---------------------------------------------------------------- modal windows */


static LRESULT CALLBACK modal_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Modal *md = (Modal *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_COMMAND:
        if (!md) break;
        if (LOWORD(w) == IDCANCEL) { md->done = 1; return 0; }
        if (LOWORD(w) == IDOK) {
            if (md->onok) { if (!md->onok(md)) return 0; md->changed = 1; }   /* the callback reads the controls; 0 keeps the dialog open */
            if (md->kind == 2) {
                int i = (int)SendMessageW(md->c1, CB_GETCURSEL, 0, 0);
                if (i >= 0) { strncpy(md->dst, pad_item_token(md->isX, i), md->cap - 1); md->dst[md->cap - 1] = 0; md->changed = 1; }
            }
            md->done = 1;
            return 0;
        }
        if (md->oncmd && LOWORD(w) >= 1000 && md->oncmd(md, LOWORD(w))) return 0;
        if (LOWORD(w) == 901 && md->kind == 2) { md->dst[0] = 0; md->changed = 1; md->done = 1; return 0; }   /* Unbind */
        if (LOWORD(w) == 900 && md->kind == 3) { ShellExecuteW(h, L"open", REPO_URL, NULL, NULL, SW_SHOWNORMAL); return 0; }
        break;
    case WM_NOTIFY:
        if (md && md->onnotify) return md->onnotify(md, (NMHDR *)l);
        break;
    case WM_CTLCOLORSTATIC:   /* the link: blue, underlined text on the dialog colour */
        if (md && md->link && (HWND)l == md->link) {
            SetTextColor((HDC)w, g_dark ? RGB(90, 170, 255) : RGB(0, 102, 204));
            SetBkMode((HDC)w, TRANSPARENT);
            return (LRESULT)th_face_brush();
        }
        if (g_dark) return (LRESULT)th_ctlcolor(m, (HDC)w, (HWND)l, 0);
        break;
    case WM_SETCURSOR:
        if (md && md->link && (HWND)w == md->link) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
        break;
    case WM_TIMER:
        if (md && md->kind == 1) {
            int vk;
            for (vk = 8; vk < 255; vk++) {
                BOOL d;
                if (vk == 0x10 || vk == 0x11 || vk == 0x12 || (vk >= 1 && vk <= 6)) continue;
                d = key_down(vk);
                if (d && !md->was[vk]) {
                    char nm[24];
                    if (vk == 0x1B) { /* Esc cancels */ }
                    else if (vk == 0x2E) { md->dst[0] = 0; md->changed = 1; }
                    else { strncpy(md->dst, vk_name(vk, nm), md->cap - 1); md->dst[md->cap - 1] = 0; md->changed = 1; }
                    md->done = 1;
                    return 0;
                }
                md->was[vk] = d;
            }
        }
        return 0;
    case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN: case WM_CTLCOLORLISTBOX:
        if (g_dark) return (LRESULT)th_ctlcolor(m, (HDC)w, (HWND)l, 0);
        break;
    case WM_APP + 10:   /* the ROM check is done (romcheck.c): the text of the report */
        if (md && md->kind == 7) { SetWindowTextW(md->c1, (const wchar_t *)l); free((void *)l); return 0; }
        break;
    case WM_CONTEXTMENU:   /* a right click in the key / button windows: a menu with Clear (unbind) */
        if (md && (md->kind == 1 || md->kind == 2)) {
            if (cell_menu(h, 0) == 2) { md->dst[0] = 0; md->changed = 1; md->done = 1; }
            return 0;
        }
        break;
    case WM_CLOSE:
        if (md) md->done = 1;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void run_modal(Modal *md) {
    MSG msg;
    EnableWindow(md->owner, FALSE);
    ShowWindow(md->dlg, SW_SHOW);
    UpdateWindow(md->dlg);
    while (!md->done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        /* the key capture window must see every key (Enter, Tab ...): no dialog keyboard handling there */
        if (md->kind == 1 || !IsDialogMessageW(md->dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(md->owner, TRUE);
    DestroyWindow(md->dlg);
    SetForegroundWindow(md->owner);
}

HWND make_modal(Modal *md, int cw, int ch) {
    RECT rc = {0, 0, S(cw), S(ch)}, ow;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    int w, h, x, y;
    md->owner = ui_owner(md->owner);   /* from the settings window: a dialog of that window */
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    w = rc.right - rc.left; h = rc.bottom - rc.top;
    GetWindowRect(md->owner, &ow);
    x = ow.left + ((ow.right - ow.left) - w) / 2;
    y = ow.top + ((ow.bottom - ow.top) - h) / 2;
    md->dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ZModal", md->title, style, x, y, w, h, md->owner, NULL, g_inst, NULL);
    SetWindowLongPtrW(md->dlg, GWLP_USERDATA, (LONG_PTR)md);
    SendMessageW(md->dlg, WM_SETFONT, (WPARAM)g_font, 0);
    if (g_icon) { SendMessageW(md->dlg, WM_SETICON, ICON_BIG, (LPARAM)g_icon); SendMessageW(md->dlg, WM_SETICON, ICON_SMALL, (LPARAM)g_iconSm); }
    return md->dlg;
}

HWND mkat(HWND p, const wchar_t *cls, const wchar_t *t, DWORD st, int x, int y, int w, int h, int id) {
    HWND c = mk(p, cls, t, st, 0, id);
    SetWindowPos(c, NULL, S(x), S(y), S(w), S(h), SWP_NOZORDER);
    return c;
}

/* asks for a key press and stores its name in dst (Esc cancels, Delete unbinds); 1 when dst changed */
int dlg_capture_key(HWND owner, const wchar_t *label, char *dst, int cap) {
    Modal md;
    wchar_t text[300];
    int vk;
    memset(&md, 0, sizeof md);
    md.kind = 1; md.owner = owner; md.dst = dst; md.cap = cap;
    wcscpy(md.title, L"Set Keyboard Key");
    make_modal(&md, 330, 150);
    swprintf(text, 300, L"%ls\n\nPress the key you want to use…\n(Esc = cancel, Delete = unbind, right click = menu)", label);
    mkat(md.dlg, L"STATIC", text, SS_CENTER, 10, 10, 310, 85, 0);
    mkat(md.dlg, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 115, 110, 100, 26, IDCANCEL);
    for (vk = 8; vk < 255; vk++) md.was[vk] = key_down(vk);
    SetTimer(md.dlg, 1, 8, NULL);
    run_modal(&md);
    return md.changed;
}

/* lists the controller buttons of one kind (XInput / DirectInput); 1 when dst changed */
int dlg_pick_pad(HWND owner, const wchar_t *title, int isX, char *dst, int cap) {
    Modal md;
    int i, n = pad_item_count(isX), sel = 0;
    memset(&md, 0, sizeof md);
    md.kind = 2; md.owner = owner; md.dst = dst; md.cap = cap; md.isX = isX;
    wcsncpy(md.title, title, 119);
    make_modal(&md, 320, 100);
    md.c1 = mkat(md.dlg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 10, 12, 300, 300, 0);
    for (i = 0; i < n; i++) {
        wchar_t w[40];
        wide_from_ascii(pad_item_label(isX, i), w, 40);
        SendMessageW(md.c1, CB_ADDSTRING, 0, (LPARAM)w);
        if (!strcmp(pad_item_token(isX, i), dst)) sel = i;
    }
    SendMessageW(md.c1, CB_SETCURSEL, sel, 0);
    mkat(md.dlg, L"BUTTON", L"Unbind", BS_PUSHBUTTON | WS_TABSTOP, 10, 62, 75, 26, 901);
    mkat(md.dlg, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 150, 62, 75, 26, IDOK);
    mkat(md.dlg, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 235, 62, 75, 26, IDCANCEL);
    run_modal(&md);
    return md.changed;
}

/* one line of text (a profile name, ...): returns 1 when OK was pressed */
typedef struct { wchar_t *text; int cap; } TextCtx;
static int text_ok(Modal *md) {
    TextCtx *c = (TextCtx *)md->user;
    GetWindowTextW(md->c1, c->text, c->cap);
    trim_w(c->text);
    return c->text[0] != 0;
}
int dlg_text(HWND owner, const wchar_t *title, const wchar_t *prompt, wchar_t *text, int cap) {
    Modal md;
    TextCtx c;
    memset(&md, 0, sizeof md);
    c.text = text; c.cap = cap;
    md.kind = 5; md.owner = owner; md.onok = text_ok; md.user = &c;
    wcsncpy(md.title, title, 119);
    make_modal(&md, 330, 118);
    mkat(md.dlg, L"STATIC", prompt, SS_LEFT, 10, 10, 310, 18, 0);
    md.c1 = mkat(md.dlg, L"EDIT", text, ES_AUTOHSCROLL | WS_TABSTOP, 10, 34, 310, 22, 0);
    SetWindowLongW(md.c1, GWL_EXSTYLE, GetWindowLongW(md.c1, GWL_EXSTYLE) | WS_EX_CLIENTEDGE);
    SetWindowPos(md.c1, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    SendMessageW(md.c1, EM_SETLIMITTEXT, (WPARAM)(cap - 1), 0);
    mkat(md.dlg, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 150, 76, 80, 26, IDOK);
    mkat(md.dlg, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 240, 76, 80, 26, IDCANCEL);
    SetFocus(md.c1);
    SendMessageW(md.c1, EM_SETSEL, 0, -1);
    run_modal(&md);
    return md.changed;
}

void dlg_about(HWND owner) {
    Modal md;
    memset(&md, 0, sizeof md);
    md.kind = 3; md.owner = owner;
    wcscpy(md.title, L"About");
    make_modal(&md, 330, 175);
    {   /* the logo, from the program's icon at 64 x 64 */
        int id;
        for (id = 1; id <= 32 && !md.logo; id++) md.logo = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(id), IMAGE_ICON, S(64), S(64), 0);
        if (md.logo) {
            HWND ic = mkat(md.dlg, L"STATIC", L"", SS_ICON, 14, 14, 64, 64, 0);
            SendMessageW(ic, STM_SETICON, (WPARAM)md.logo, 0);
        }
    }
    mkat(md.dlg, L"STATIC", sizeof(void *) == 8 ? L"ZiNc EX v" APP_VERSION_W L" (64-bit)" : L"ZiNc EX v" APP_VERSION_W L" (32-bit)", SS_LEFT, 94, 12, 230, 18, 0);
    mkat(md.dlg, L"STATIC", L"A front-end for the ZiNc arcade emulator.", SS_LEFT, 94, 36, 230, 18, 0);
    mkat(md.dlg, L"STATIC", L"ZiNc EX by Hazem Abdelghani (2026).", SS_LEFT, 94, 60, 230, 18, 0);
    /* a clickable link: a static text that reports clicks (SS_NOTIFY), drawn blue and underlined, with the hand cursor */
    md.link = mkat(md.dlg, L"STATIC", L"ZiNc EX", SS_LEFT | SS_NOTIFY, 94, 88, 90, 18, 900);
    {
        LOGFONTW lf;
        GetObjectW(g_font, sizeof lf, &lf);
        lf.lfUnderline = TRUE;
        md.linkFont = CreateFontIndirectW(&lf);
        SendMessageW(md.link, WM_SETFONT, (WPARAM)md.linkFont, TRUE);
    }
    mkat(md.dlg, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 235, 135, 80, 26, IDOK);
    run_modal(&md);
    DeleteObject(md.linkFont);
    if (md.logo) DestroyIcon(md.logo);
}

/* a two-column table in a window: shortcuts, legends */
void dlg_table(HWND owner, const wchar_t *title, const wchar_t *c0, const wchar_t *c1, const TableRow *rows, int n, int w0, int w1) {
    Modal md;
    HWND lv;
    LVCOLUMNW col;
    int i, cw = w0 + w1 + 28 + 20, ch = 520;
    memset(&md, 0, sizeof md);
    md.kind = 8; md.owner = owner;
    wcsncpy(md.title, title, 119);
    make_modal(&md, cw, ch);
    lv = mkat(md.dlg, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER | WS_TABSTOP | WS_VSCROLL | WS_BORDER, 10, 10, cw - 20, ch - 58, 0);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    memset(&col, 0, sizeof col);
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (LPWSTR)c0; col.cx = S(w0); ListView_InsertColumn(lv, 0, &col);
    col.pszText = (LPWSTR)c1; col.cx = S(w1); ListView_InsertColumn(lv, 1, &col);
    for (i = 0; i < n; i++) {
        LVITEMW it;
        memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (LPWSTR)rows[i].a;
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, i, 1, (LPWSTR)(rows[i].b ? rows[i].b : L""));
    }
    mkat(md.dlg, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, cw - 90, ch - 36, 80, 26, IDOK);
    run_modal(&md);
}

/* ---------------------------------------------------------------- start up */
void ui_init(HINSTANCE inst) {
    INITCOMMONCONTROLSEX icc;
    NONCLIENTMETRICSW ncm;
    WNDCLASSW wc;
    HDC dc;
    int id;
    g_inst = inst;
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    if (g_dpi < 96) g_dpi = 96;
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    /* the icon group embedded in the exe (its resource id depends on the resource layout) */
    for (id = 1; id <= 32 && !g_icon; id++) {
        g_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(id), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        if (g_icon) g_iconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(id), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    }
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = modal_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = th_face_brush();
    wc.lpszClassName = L"ZModal";
    wc.hIcon = g_icon;
    RegisterClassW(&wc);
}
