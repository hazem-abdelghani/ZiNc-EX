/* The toolbar: flat buttons that show an icon instead of a caption.
   The icons are drawn here (no picture files): one colour, the text colour of the theme (grey when the button is disabled),
   so they fit the light and the dark theme. They are drawn four times as large into a mask and reduced, for smooth edges. */
#include "common.h"
#include <math.h>

#define SS 4            /* supersampling */
#define PI_ 3.14159265358979

static unsigned char *g_mask[TI_N * 2];   /* the alpha mask of every icon (and of the filled variant of the star) */
static int g_maskSz;

typedef struct { HDC dc; HBITMAP bmp, old; unsigned *bits; int big; double u; } Canvas;

static HPEN stroke(Canvas *c, double w, COLORREF col) {
    LOGBRUSH lb;
    lb.lbStyle = BS_SOLID; lb.lbColor = col; lb.lbHatch = 0;
    return ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, (DWORD)(w * c->u + 0.5), &lb, 0, NULL);
}
#define PX(v) ((int)((v) * c->u + 0.5))

static void poly(Canvas *c, const double *xy, int n, int filled, double w) {
    POINT p[32];
    int i;
    HPEN pen = stroke(c, w, RGB(255, 255, 255)), op;
    HBRUSH br = filled ? (HBRUSH)GetStockObject(WHITE_BRUSH) : (HBRUSH)GetStockObject(NULL_BRUSH), ob;
    for (i = 0; i < n && i < 32; i++) { p[i].x = PX(xy[2 * i]); p[i].y = PX(xy[2 * i + 1]); }
    op = (HPEN)SelectObject(c->dc, pen); ob = (HBRUSH)SelectObject(c->dc, br);
    Polygon(c->dc, p, n);
    SelectObject(c->dc, op); SelectObject(c->dc, ob); DeleteObject(pen);
}
static void line(Canvas *c, double x0, double y0, double x1, double y1, double w) {
    HPEN pen = stroke(c, w, RGB(255, 255, 255)), op = (HPEN)SelectObject(c->dc, pen);
    MoveToEx(c->dc, PX(x0), PX(y0), NULL); LineTo(c->dc, PX(x1), PX(y1));
    SelectObject(c->dc, op); DeleteObject(pen);
}
static void polyline(Canvas *c, const double *xy, int n, double w) {
    POINT p[16];
    int i;
    HPEN pen = stroke(c, w, RGB(255, 255, 255)), op = (HPEN)SelectObject(c->dc, pen);
    for (i = 0; i < n && i < 16; i++) { p[i].x = PX(xy[2 * i]); p[i].y = PX(xy[2 * i + 1]); }
    Polyline(c->dc, p, n);
    SelectObject(c->dc, op); DeleteObject(pen);
}
static void ellipse(Canvas *c, double cx, double cy, double r, int filled, int white, double w) {
    HPEN pen = stroke(c, w, white ? RGB(255, 255, 255) : RGB(0, 0, 0)), op;
    HBRUSH br = filled ? (HBRUSH)GetStockObject(white ? WHITE_BRUSH : BLACK_BRUSH) : (HBRUSH)GetStockObject(NULL_BRUSH), ob;
    op = (HPEN)SelectObject(c->dc, pen); ob = (HBRUSH)SelectObject(c->dc, br);
    Ellipse(c->dc, PX(cx - r), PX(cy - r), PX(cx + r), PX(cy + r));
    SelectObject(c->dc, op); SelectObject(c->dc, ob); DeleteObject(pen);
}
static void roundrect(Canvas *c, double x0, double y0, double x1, double y1, double rad, int filled, double w) {
    HPEN pen = stroke(c, w, RGB(255, 255, 255)), op;
    HBRUSH br = filled ? (HBRUSH)GetStockObject(WHITE_BRUSH) : (HBRUSH)GetStockObject(NULL_BRUSH), ob;
    op = (HPEN)SelectObject(c->dc, pen); ob = (HBRUSH)SelectObject(c->dc, br);
    RoundRect(c->dc, PX(x0), PX(y0), PX(x1), PX(y1), PX(rad * 2), PX(rad * 2));
    SelectObject(c->dc, op); SelectObject(c->dc, ob); DeleteObject(pen);
}

/* the icons, drawn on a 24 x 24 grid */
static void draw_icon(Canvas *c, int icon, int variant) {
    double pts[32];
    int i;
    switch (icon) {
    case TI_OK: case TI_MISSING: {   /* a disc with a tick or a cross cut out of it */
        HPEN pen;
        HGDIOBJ op;
        ellipse(c, 12, 12, 10.5, 1, 1, 0.5);
        pen = stroke(c, 2.6, RGB(0, 0, 0)); op = SelectObject(c->dc, pen);
        if (icon == TI_OK) { MoveToEx(c->dc, PX(7), PX(12.5), NULL); LineTo(c->dc, PX(10.5), PX(16)); LineTo(c->dc, PX(17), PX(8.5)); }
        else { MoveToEx(c->dc, PX(8), PX(8), NULL); LineTo(c->dc, PX(16), PX(16)); MoveToEx(c->dc, PX(16), PX(8), NULL); LineTo(c->dc, PX(8), PX(16)); }
        SelectObject(c->dc, op); DeleteObject(pen);
        break;
    }
    case TI_REFRESH: {   /* an arrow that goes round */
        double cx = 12, cy = 12, r = 7.2, a0 = -40 * PI_ / 180, a1 = 25 * PI_ / 180, tip = -3 * PI_ / 180;
        HPEN pen = stroke(c, 2.2, RGB(255, 255, 255)), op = (HPEN)SelectObject(c->dc, pen);
        Arc(c->dc, PX(cx - r), PX(cy - r), PX(cx + r), PX(cy + r), PX(cx + r * cos(a0)), PX(cy + r * sin(a0)), PX(cx + r * cos(a1)), PX(cy + r * sin(a1)));
        SelectObject(c->dc, op); DeleteObject(pen);
        {   /* the head of the arrow at the end of the arc, pointing up the right side */
            double bx = cx + r * cos(a1), by = cy + r * sin(a1);
            double tx = cx + (r + 0.2) * cos(tip), ty = cy + (r + 0.2) * sin(tip) - 1.2;
            double rx = cos(a1), ry = sin(a1);
            pts[0] = tx; pts[1] = ty - 1.8;
            pts[2] = bx + rx * 4.0 + 0.4; pts[3] = by + ry * 4.0 - 1.0;
            pts[4] = bx - rx * 4.0 - 0.4; pts[5] = by - ry * 4.0 - 1.0;
            poly(c, pts, 3, 1, 0.6);
        }
        break;
    }
    case TI_FAV:     /* a star (filled when the button is switched on) */
        for (i = 0; i < 10; i++) {
            double a = (-90 + i * 36) * PI_ / 180, r = (i & 1) ? 4.6 : 10.2;
            pts[2 * i] = 12 + r * cos(a); pts[2 * i + 1] = 12.9 + r * sin(a);
        }
        poly(c, pts, 10, variant, variant ? 0.8 : 1.9);
        break;
    case TI_AVAIL:   /* a box with a tick: only the games that are there */
        roundrect(c, 3, 3, 21, 21, 3.5, 0, 2.0);
        pts[0] = 7.2; pts[1] = 12.4; pts[2] = 10.6; pts[3] = 15.8; pts[4] = 17; pts[5] = 8.6;
        polyline(c, pts, 3, 2.2);
        break;
    case TI_AUDIO: {   /* a loudspeaker with sound waves */
        int k;
        HPEN pen;
        HGDIOBJ op;
        pts[0] = 3.5; pts[1] = 9.5; pts[2] = 7.5; pts[3] = 9.5; pts[4] = 12.5; pts[5] = 5; pts[6] = 12.5; pts[7] = 19; pts[8] = 7.5; pts[9] = 14.5; pts[10] = 3.5; pts[11] = 14.5;
        poly(c, pts, 6, 0, 1.8);
        pen = stroke(c, 1.9, RGB(255, 255, 255)); op = SelectObject(c->dc, pen);
        for (k = 0; k < 2; k++) {
            double r = 4.6 + k * 4.2, a = 42 * PI_ / 180, cx = 12.5, cy = 12;
            Arc(c->dc, PX(cx - r), PX(cy - r), PX(cx + r), PX(cy + r), PX(cx + r * cos(a)), PX(cy + r * sin(a)), PX(cx + r * cos(a)), PX(cy - r * sin(a)));
        }
        SelectObject(c->dc, op); DeleteObject(pen);
        break;
    }
    case TI_PLAY:
        pts[0] = 7.5; pts[1] = 4.5; pts[2] = 19.5; pts[3] = 12; pts[4] = 7.5; pts[5] = 19.5;
        poly(c, pts, 3, 1, 1.4);
        break;
    case TI_STOP:
        roundrect(c, 6, 6, 18, 18, 1.2, 1, 1.0);
        break;
    case TI_FULLSCREEN: {   /* four corners */
        static const double k[4][6] = {{4, 9.5, 4, 4, 9.5, 4}, {14.5, 4, 20, 4, 20, 9.5}, {20, 14.5, 20, 20, 14.5, 20}, {9.5, 20, 4, 20, 4, 14.5}};
        for (i = 0; i < 4; i++) polyline(c, k[i], 3, 2.2);
        break;
    }
    case TI_SETTINGS: {   /* a gear */
        {
            POINT p[32];
            int k2 = 0;
            for (i = 0; i < 8; i++) {
                double d[4] = {-17, -10, 10, 17}, rr[4] = {7.6, 10.2, 10.2, 7.6};
                int j;
                for (j = 0; j < 4; j++) {
                    double t = (i * 45.0 + d[j] - 90) * PI_ / 180;
                    p[k2].x = PX(12 + rr[j] * cos(t)); p[k2].y = PX(12 + rr[j] * sin(t)); k2++;
                }
            }
            {
                HPEN pen = stroke(c, 0.8, RGB(255, 255, 255)), op = (HPEN)SelectObject(c->dc, pen);
                HBRUSH ob = (HBRUSH)SelectObject(c->dc, GetStockObject(WHITE_BRUSH));
                Polygon(c->dc, p, k2);
                SelectObject(c->dc, op); SelectObject(c->dc, ob); DeleteObject(pen);
            }
        }
        ellipse(c, 12, 12, 3.6, 1, 0, 0.1);   /* the hole */
        break;
    }
    case TI_VIDEO:   /* a screen on a stand */
        roundrect(c, 3, 4.5, 21, 16.5, 1.5, 0, 2.0);
        line(c, 12, 16.5, 12, 20, 2.0);
        line(c, 7.5, 20, 16.5, 20, 2.0);
        break;
    case TI_CONTROLS:   /* a game pad */
        roundrect(c, 2.5, 6.5, 21.5, 17.5, 5.2, 0, 2.0);
        line(c, 5.6, 12, 10.4, 12, 1.9);
        line(c, 8, 9.6, 8, 14.4, 1.9);
        ellipse(c, 15.4, 10.6, 1.3, 1, 1, 0.3);
        ellipse(c, 18.0, 13.4, 1.3, 1, 1, 0.3);
        break;
    case TI_COMBOS:   /* a flash: a special move */
        pts[0] = 14.2; pts[1] = 2; pts[2] = 5; pts[3] = 13.6; pts[4] = 10.8; pts[5] = 13.6;
        pts[6] = 9.4; pts[7] = 22; pts[8] = 19; pts[9] = 10; pts[10] = 13.2; pts[11] = 10;
        poly(c, pts, 6, 1, 1.0);
        break;
    }
}

/* the alpha mask of an icon: sz x sz bytes */
unsigned char *tb_icon_mask(int icon, int variant, int sz) {
    Canvas c;
    BITMAPINFO bi;
    unsigned char *m;
    int x, y, sx, sy;
    memset(&c, 0, sizeof c);
    c.big = sz * SS; c.u = (double)c.big / 24.0;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = c.big; bi.bmiHeader.biHeight = -c.big;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    c.dc = CreateCompatibleDC(NULL);
    c.bmp = CreateDIBSection(c.dc, &bi, DIB_RGB_COLORS, (void **)&c.bits, NULL, 0);
    if (!c.bmp) { DeleteDC(c.dc); return NULL; }
    c.old = (HBITMAP)SelectObject(c.dc, c.bmp);
    memset(c.bits, 0, (size_t)c.big * c.big * 4);
    draw_icon(&c, icon, variant);
    GdiFlush();
    m = (unsigned char *)malloc((size_t)sz * sz);
    if (m) {
        for (y = 0; y < sz; y++) for (x = 0; x < sz; x++) {
            int sum = 0;
            for (sy = 0; sy < SS; sy++) for (sx = 0; sx < SS; sx++) sum += c.bits[(y * SS + sy) * c.big + x * SS + sx] & 0xFF;
            m[y * sz + x] = (unsigned char)(sum / (SS * SS));
        }
    }
    SelectObject(c.dc, c.old); DeleteObject(c.bmp); DeleteDC(c.dc);
    return m;
}

static unsigned char *mask_for(int icon, int variant, int sz) {
    int k = icon * 2 + (variant ? 1 : 0), i;
    if (sz != g_maskSz) { for (i = 0; i < TI_N * 2; i++) { free(g_mask[i]); g_mask[i] = NULL; } g_maskSz = sz; }
    if (!g_mask[k]) g_mask[k] = tb_icon_mask(icon, variant, sz);
    return g_mask[k];
}

/* ---------------------------------------------------------------- the buttons */
static LRESULT CALLBACK tb_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (m) {
    case WM_MOUSEMOVE:
        if (!GetPropW(h, L"tbhot")) {
            TRACKMOUSEEVENT te;
            te.cbSize = sizeof te; te.dwFlags = TME_LEAVE; te.hwndTrack = h; te.dwHoverTime = 0;
            TrackMouseEvent(&te);
            SetPropW(h, L"tbhot", (HANDLE)1);
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        SetPropW(h, L"tbhot", (HANDLE)0);
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_ENABLE: InvalidateRect(h, NULL, FALSE); break;
    case WM_NCDESTROY:
        RemovePropW(h, L"tbico"); RemovePropW(h, L"tbhot"); RemovePropW(h, L"tbchk");
        RemoveWindowSubclass(h, tb_proc, 99);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

HWND tb_button(HWND parent, int id, int icon) {
    HWND b = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (!b) return NULL;
    SetPropW(b, L"tbico", (HANDLE)(INT_PTR)(icon + 1));
    SetWindowSubclass(b, tb_proc, 99, 0);
    return b;
}

HWND tb_separator(HWND parent) {
    HWND s = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 4, 4, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (s) SetPropW(s, L"tbsep", (HANDLE)1);
    return s;
}

void tb_set_checked(HWND b, int on) {
    if (!b) return;
    if ((GetPropW(b, L"tbchk") != NULL) == (on != 0)) return;
    SetPropW(b, L"tbchk", (HANDLE)(INT_PTR)(on ? 1 : 0));
    InvalidateRect(b, NULL, FALSE);
}

/* WM_DRAWITEM of the window that holds the buttons; 1 when the item was one of ours */
int tb_draw(const DRAWITEMSTRUCT *di) {
    if (!di) return 0;
    if (di->CtlType == ODT_STATIC && GetPropW(di->hwndItem, L"tbsep")) {
        RECT r = di->rcItem;
        HBRUSH face = CreateSolidBrush(th_face()), ln = CreateSolidBrush(th_gray());
        RECT l = {(r.left + r.right) / 2, r.top + (r.bottom - r.top) / 5, (r.left + r.right) / 2 + 1, r.bottom - (r.bottom - r.top) / 5};
        FillRect(di->hDC, &r, face);
        FillRect(di->hDC, &l, ln);
        DeleteObject(face); DeleteObject(ln);
        return 1;
    }
    if (di->CtlType == ODT_BUTTON && GetPropW(di->hwndItem, L"tbico")) {
        int icon = (int)(INT_PTR)GetPropW(di->hwndItem, L"tbico") - 1;
        int hot = GetPropW(di->hwndItem, L"tbhot") != NULL, chk = GetPropW(di->hwndItem, L"tbchk") != NULL;
        int dis = (di->itemState & ODS_DISABLED) != 0, down = (di->itemState & ODS_SELECTED) != 0;
        RECT r = di->rcItem;
        int sz = S(20), x, y;
        COLORREF face = th_face(), bg = face, fg = dis ? th_gray() : th_text();
        unsigned char *mk;
        BITMAPINFO bi;
        unsigned *bits = NULL;
        HDC mdc;
        HBITMAP bmp, old;
        HBRUSH hb;
        if (!dis && (hot || down || chk)) {
            int d = down ? 2 : 1;
            bg = g_dark ? (d == 2 ? RGB(92, 92, 92) : RGB(68, 68, 68)) : (d == 2 ? RGB(196, 196, 196) : RGB(222, 222, 222));
            if (chk && !hot && !down) bg = g_dark ? RGB(62, 62, 62) : RGB(214, 214, 214);
        }
        hb = CreateSolidBrush(face);
        FillRect(di->hDC, &r, hb);
        DeleteObject(hb);
        if (bg != face) {   /* a soft rounded plate under the icon */
            HBRUSH pb = CreateSolidBrush(bg);
            HPEN np = (HPEN)GetStockObject(NULL_PEN);
            HGDIOBJ ob = SelectObject(di->hDC, pb), op = SelectObject(di->hDC, np);
            RoundRect(di->hDC, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, S(8), S(8));
            SelectObject(di->hDC, ob); SelectObject(di->hDC, op);
            DeleteObject(pb);
        }
        mk = mask_for(icon, icon == TI_FAV && chk, sz);
        if (!mk) return 1;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = sz; bi.bmiHeader.biHeight = -sz;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        mdc = CreateCompatibleDC(di->hDC);
        bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
        if (bmp) {
            int br = GetRValue(bg), bgc = GetGValue(bg), bb = GetBValue(bg), fr = GetRValue(fg), fgc = GetGValue(fg), fb = GetBValue(fg);
            old = (HBITMAP)SelectObject(mdc, bmp);
            for (y = 0; y < sz; y++) for (x = 0; x < sz; x++) {
                int a = mk[y * sz + x];
                int rr = (br * (255 - a) + fr * a) / 255, gg = (bgc * (255 - a) + fgc * a) / 255, b2 = (bb * (255 - a) + fb * a) / 255;
                bits[y * sz + x] = ((unsigned)rr << 16) | ((unsigned)gg << 8) | (unsigned)b2;
            }
            BitBlt(di->hDC, r.left + (r.right - r.left - sz) / 2, r.top + (r.bottom - r.top - sz) / 2, sz, sz, mdc, 0, 0, SRCCOPY);
            SelectObject(mdc, old); DeleteObject(bmp);
        }
        DeleteDC(mdc);
        return 1;
    }
    return 0;
}

/* one icon, in one colour with its alpha, added to an image list */
static void add_icon(HIMAGELIST il, int icon, int variant, int sz, COLORREF col) {
    unsigned char *m = tb_icon_mask(icon, variant, sz);
    BITMAPINFO bi;
    unsigned *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp;
    int i;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = sz; bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (bmp && m) {
        for (i = 0; i < sz * sz; i++) {   /* premultiplied alpha */
            unsigned a = m[i];
            unsigned r = GetRValue(col) * a / 255, g = GetGValue(col) * a / 255, b = GetBValue(col) * a / 255;
            bits[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        ImageList_Add(il, bmp, NULL);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    free(m);
}

/* the little coloured icons of the Status column (an image list with alpha) */
HIMAGELIST tb_status_images(int sz) {
    HIMAGELIST il = ImageList_Create(sz, sz, ILC_COLOR32, 2, 0);
    if (!il) return NULL;
    add_icon(il, TI_OK, 0, sz, RGB(46, 160, 67));
    add_icon(il, TI_MISSING, 0, sz, RGB(214, 69, 65));
    return il;
}

/* the state images of the game list instead of the check box: image 0 = not a favorite (outline star), 1 = favorite (filled star) */
HIMAGELIST tb_star_images(int sz, COLORREF off, COLORREF on) {
    HIMAGELIST il = ImageList_Create(sz, sz, ILC_COLOR32, 2, 0);
    if (!il) return NULL;
    add_icon(il, TI_FAV, 0, sz, off);   /* state 1 (unchecked) is image 0, state 2 (checked) is image 1 */
    add_icon(il, TI_FAV, 1, sz, on);
    return il;
}
