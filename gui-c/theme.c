/* The optional dark theme (System tab, applied at the next start). Plain Win32 has no dark mode for its controls, so this combines the
   documented parts (colours for static / edit controls, owner-painted tab headers and status bar) with the dark control themes of
   recent Windows 10 / 11 (set per control). Without the option nothing here changes anything. */
#include "common.h"
#include <uxtheme.h>

int g_dark;

#define C_FACE RGB(32, 32, 32)
#define C_PAGE RGB(40, 40, 40)
#define C_FIELD RGB(56, 56, 56)
#define C_TEXT RGB(232, 232, 232)
#define C_GRAY RGB(140, 140, 140)
#define C_ALT RGB(48, 48, 48)
#define C_LINE RGB(80, 80, 80)
#define C_ACCENT RGB(0, 120, 215)

COLORREF th_face(void) { return g_dark ? C_FACE : GetSysColor(COLOR_BTNFACE); }
COLORREF th_page(void) { return g_dark ? C_PAGE : GetSysColor(COLOR_WINDOW); }
COLORREF th_text(void) { return g_dark ? C_TEXT : GetSysColor(COLOR_WINDOWTEXT); }
COLORREF th_gray(void) { return g_dark ? C_GRAY : GetSysColor(COLOR_GRAYTEXT); }
COLORREF th_alt(void) { return g_dark ? C_ALT : RGB(242, 242, 242); }

static HBRUSH b_face, b_page, b_field;
HBRUSH th_face_brush(void) { if (!g_dark) return GetSysColorBrush(COLOR_BTNFACE); if (!b_face) b_face = CreateSolidBrush(C_FACE); return b_face; }
HBRUSH th_page_brush(void) { if (!g_dark) return GetSysColorBrush(COLOR_WINDOW); if (!b_page) b_page = CreateSolidBrush(C_PAGE); return b_page; }
static HBRUSH field_brush(void) { if (!b_field) b_field = CreateSolidBrush(C_FIELD); return b_field; }

typedef int (WINAPI *SetPreferredAppMode_t)(int);
typedef BOOL (WINAPI *AllowDarkModeForWindow_t)(HWND, BOOL);
typedef void (WINAPI *FlushMenuThemes_t)(void);
typedef HRESULT (WINAPI *DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
static AllowDarkModeForWindow_t pAllowWin;

void th_init(int dark) {
    HMODULE ux;
    g_dark = dark;
    if (!dark) return;
    ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (ux) {   /* undocumented, by ordinal (Windows 10 1903 and later); when they are missing the colours below still apply */
        SetPreferredAppMode_t spm = (SetPreferredAppMode_t)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        FlushMenuThemes_t fmt = (FlushMenuThemes_t)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        pAllowWin = (AllowDarkModeForWindow_t)GetProcAddress(ux, MAKEINTRESOURCEA(133));
        if (spm) spm(2);   /* force dark: the popup menus too */
        if (fmt) fmt();
    }
}

/* a top-level window: dark title bar */
void th_window(HWND h) {
    HMODULE dw;
    DwmSetWindowAttribute_t set;
    BOOL on = TRUE;
    if (!g_dark) return;
    if (pAllowWin) pAllowWin(h, TRUE);
    dw = LoadLibraryW(L"dwmapi.dll");
    if (!dw) return;
    set = (DwmSetWindowAttribute_t)GetProcAddress(dw, "DwmSetWindowAttribute");
    if (set) { if (FAILED(set(h, 20, &on, sizeof on))) set(h, 19, &on, sizeof on); }
}

/* a control: the dark theme of its class */
static LRESULT CALLBACK header_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref);

void th_control(HWND h, const wchar_t *cls) {
    const wchar_t *theme = NULL;
    if (!g_dark || !h) return;
    if (pAllowWin) pAllowWin(h, TRUE);
    if (!_wcsicmp(cls, L"BUTTON") || !_wcsicmp(cls, WC_LISTVIEWW) || !_wcsicmp(cls, L"SCROLLBAR")) theme = L"DarkMode_Explorer";
    else if (!_wcsicmp(cls, L"COMBOBOX") || !_wcsicmp(cls, L"EDIT")) theme = L"DarkMode_CFD";
    if (!_wcsicmp(cls, L"EDIT") && (GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE)) theme = L"DarkMode_Explorer";   /* a multi-line text has a scroll bar */
    if (theme) SetWindowTheme(h, theme, NULL);
    if (!_wcsicmp(cls, L"COMBOBOX")) {   /* the drop-down list of a combo box has a scroll bar of its own */
        COMBOBOXINFO ci;
        ci.cbSize = sizeof ci;
        if (GetComboBoxInfo(h, &ci) && ci.hwndList) { if (pAllowWin) pAllowWin(ci.hwndList, TRUE); SetWindowTheme(ci.hwndList, L"DarkMode_Explorer", NULL); }
    }
    if (!_wcsicmp(cls, WC_LISTVIEWW)) {
        HWND hdr = ListView_GetHeader(h);
        if (hdr) { if (pAllowWin) pAllowWin(hdr, TRUE); SetWindowTheme(hdr, L"DarkMode_ItemsView", NULL); SetWindowSubclass(hdr, header_proc, 1, 0); }
        ListView_SetBkColor(h, C_PAGE);
        ListView_SetTextBkColor(h, C_PAGE);
        ListView_SetTextColor(h, C_TEXT);
    }
}

/* WM_CTLCOLOR* of a child: the brush, or NULL (light theme: the caller's own handling). onPage: the child sits on a tab page */
HBRUSH th_ctlcolor(UINT msg, HDC dc, HWND ctl, int onPage) {
    if (!g_dark) return NULL;
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_FIELD);
        return field_brush();
    }
    SetTextColor(dc, ctl && (!IsWindowEnabled(ctl) || GetPropW(ctl, L"dim")) ? C_GRAY : C_TEXT);
    SetBkColor(dc, onPage ? C_PAGE : C_FACE);
    return onPage ? th_page_brush() : th_face_brush();
}

/* ---- the tab control: painted here */
static LRESULT CALLBACK tab_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        int n = TabCtrl_GetItemCount(h), cur = TabCtrl_GetCurSel(h), i;
        HGDIOBJ of = SelectObject(dc, g_font);
        GetClientRect(h, &rc);
        FillRect(dc, &rc, th_face_brush());
        SetBkMode(dc, TRANSPARENT);
        for (i = 0; i < n; i++) {
            RECT r;
            wchar_t t[64];
            TCITEMW ti;
            HBRUSH b = CreateSolidBrush(i == cur ? C_PAGE : C_FACE);
            TabCtrl_GetItemRect(h, i, &r);
            FillRect(dc, &r, b);
            DeleteObject(b);
            memset(&ti, 0, sizeof ti);
            ti.mask = TCIF_TEXT; ti.pszText = t; ti.cchTextMax = 64;
            t[0] = 0;
            TabCtrl_GetItem(h, i, &ti);
            SetTextColor(dc, i == cur ? C_TEXT : C_GRAY);
            DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (i == cur) { RECT a = r; a.bottom = a.top + 2; b = CreateSolidBrush(C_ACCENT); FillRect(dc, &a, b); DeleteObject(b); }
        }
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, tab_proc, id);
    return DefSubclassProc(h, m, w, l);
}
void th_tab(HWND tab) { if (g_dark) SetWindowSubclass(tab, tab_proc, 1, 0); }

/* ---- the status bar */
static LRESULT CALLBACK status_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        wchar_t t[600] = L"";
        HGDIOBJ of = SelectObject(dc, g_font);
        HBRUSH ln = CreateSolidBrush(C_LINE);
        GetClientRect(h, &rc);
        FillRect(dc, &rc, th_face_brush());
        { RECT top = rc; top.bottom = top.top + 1; FillRect(dc, &top, ln); }
        DeleteObject(ln);
        { int n = LOWORD(SendMessageW(h, SB_GETTEXTLENGTHW, 0, 0)); if (n > 0 && n < 599) SendMessageW(h, SB_GETTEXTW, 0, (LPARAM)t); }
        rc.left += S(6);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_TEXT);
        DrawTextW(dc, t, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, status_proc, id);
    return DefSubclassProc(h, m, w, l);
}
void th_status(HWND sb) { if (g_dark) SetWindowSubclass(sb, status_proc, 1, 0); }

/* ---- column headers of the lists: the header themes ignore the text colour (and the parent may not get the header's custom draw), so the header
   window is painted here by a subclass */
static void paint_header(HWND hdr, HDC dc) {
    RECT rc;
    int n = Header_GetItemCount(hdr), i;
    HBRUSH bg = CreateSolidBrush(RGB(48, 48, 48)), ln = CreateSolidBrush(C_LINE);
    HGDIOBJ of = SelectObject(dc, g_font);
    GetClientRect(hdr, &rc);
    FillRect(dc, &rc, bg);
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < n; i++) {
        RECT r, e;
        wchar_t t[64];
        HDITEMW hi;
        if (!Header_GetItemRect(hdr, i, &r)) continue;
        memset(&hi, 0, sizeof hi);
        t[0] = 0;
        hi.mask = HDI_TEXT | HDI_FORMAT;
        hi.pszText = t;
        hi.cchTextMax = 64;
        Header_GetItem(hdr, i, &hi);
        e = r; e.left = e.right - 1;
        FillRect(dc, &e, ln);
        e = r; e.top = e.bottom - 1;
        FillRect(dc, &e, ln);
        { RECT tr = r; tr.left += S(6); tr.right -= S(14); SetTextColor(dc, C_TEXT); DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS); }
        if (hi.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {   /* the sort arrow */
            POINT p[3];
            int cx = r.right - S(10), cy = (r.top + r.bottom) / 2, d = S(3);
            HBRUSH ab = CreateSolidBrush(C_TEXT);
            HGDIOBJ ob = SelectObject(dc, ab), op = SelectObject(dc, GetStockObject(NULL_PEN));
            if (hi.fmt & HDF_SORTUP) { p[0].x = cx - d; p[0].y = cy + d / 2; p[1].x = cx + d; p[1].y = cy + d / 2; p[2].x = cx; p[2].y = cy - d; }
            else { p[0].x = cx - d; p[0].y = cy - d / 2; p[1].x = cx + d; p[1].y = cy - d / 2; p[2].x = cx; p[2].y = cy + d; }
            Polygon(dc, p, 3);
            SelectObject(dc, ob); SelectObject(dc, op);
            DeleteObject(ab);
        }
    }
    SelectObject(dc, of);
    DeleteObject(bg); DeleteObject(ln);
}

static LRESULT CALLBACK header_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT || m == WM_PRINTCLIENT) {
        PAINTSTRUCT ps;
        HDC dc = m == WM_PAINT ? BeginPaint(h, &ps) : (HDC)w;
        paint_header(h, dc);
        if (m == WM_PAINT) EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, header_proc, id);
    return DefSubclassProc(h, m, w, l);
}

/* ---- the menu bar (undocumented messages of the Windows menu bar, as used by other dark mode programs) */
#define WM_UAHDRAWMENU 0x91
#define WM_UAHDRAWMENUITEM 0x92
typedef struct { HMENU hmenu; HDC hdc; DWORD dwFlags; } UAHMENU;
typedef struct { DWORD cx, cy; } UAHSIZE;
typedef struct { int iPosition; UAHSIZE bar[2]; UAHSIZE popup[4]; DWORD rgcx[4]; DWORD fUpdate; } UAHMENUITEM;
typedef struct { DRAWITEMSTRUCT dis; UAHMENU um; UAHMENUITEM umi; } UAHDRAWMENUITEM;

static HBRUSH b_bar, b_hot, b_sel;

LRESULT th_menu_msg(HWND h, UINT m, WPARAM w, LPARAM l, int *handled) {
    *handled = 0;
    if (!g_dark) return 0;
    if (!b_bar) { b_bar = CreateSolidBrush(C_FACE); b_hot = CreateSolidBrush(RGB(62, 62, 62)); b_sel = CreateSolidBrush(RGB(78, 78, 78)); }
    if (m == WM_UAHDRAWMENU) {
        UAHMENU *um = (UAHMENU *)l;
        MENUBARINFO mbi;
        RECT rc, rw;
        memset(&mbi, 0, sizeof mbi);
        mbi.cbSize = sizeof mbi;
        if (!GetMenuBarInfo(h, OBJID_MENU, 0, &mbi)) return 0;
        GetWindowRect(h, &rw);
        rc = mbi.rcBar;
        OffsetRect(&rc, -rw.left, -rw.top);
        FillRect(um->hdc, &rc, b_bar);
        *handled = 1;
        return TRUE;
    }
    if (m == WM_UAHDRAWMENUITEM) {
        UAHDRAWMENUITEM *di = (UAHDRAWMENUITEM *)l;
        wchar_t t[256];
        MENUITEMINFOW mi;
        DWORD fl = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
        HBRUSH b = b_bar;
        HGDIOBJ of;
        memset(&mi, 0, sizeof mi);
        t[0] = 0;
        mi.cbSize = sizeof mi;
        mi.fMask = MIIM_STRING;
        mi.dwTypeData = t;
        mi.cch = 255;
        GetMenuItemInfoW(di->um.hmenu, (UINT)di->umi.iPosition, TRUE, &mi);
        if (di->dis.itemState & ODS_NOACCEL) fl |= DT_HIDEPREFIX;
        if (di->dis.itemState & ODS_HOTLIGHT) b = b_hot;
        if (di->dis.itemState & ODS_SELECTED) b = b_sel;
        FillRect(di->um.hdc, &di->dis.rcItem, b);
        of = SelectObject(di->um.hdc, g_font);
        SetBkMode(di->um.hdc, TRANSPARENT);
        SetTextColor(di->um.hdc, (di->dis.itemState & (ODS_INACTIVE | ODS_DISABLED | ODS_GRAYED)) ? C_GRAY : C_TEXT);
        DrawTextW(di->um.hdc, t, (int)mi.cch, &di->dis.rcItem, fl);
        SelectObject(di->um.hdc, of);
        *handled = 1;
        return TRUE;
    }
    return 0;
}

/* after the window frame is painted: the light 1 px line under the menu bar gets the bar colour */
void th_menu_line(HWND h) {
    MENUBARINFO mbi;
    RECT rc, rw, line;
    HDC dc;
    if (!g_dark) return;
    if (!b_bar) b_bar = CreateSolidBrush(C_FACE);
    memset(&mbi, 0, sizeof mbi);
    mbi.cbSize = sizeof mbi;
    if (!GetMenuBarInfo(h, OBJID_MENU, 0, &mbi)) return;
    GetClientRect(h, &rc);
    MapWindowPoints(h, NULL, (POINT *)&rc, 2);
    GetWindowRect(h, &rw);
    OffsetRect(&rc, -rw.left, -rw.top);
    line = rc;
    line.bottom = line.top;
    line.top--;
    dc = GetWindowDC(h);
    FillRect(dc, &line, b_bar);
    ReleaseDC(h, dc);
}
