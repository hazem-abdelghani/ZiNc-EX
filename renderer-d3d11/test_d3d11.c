/* test harness: draws a small scene through the plugin and lets it dump the picture (ZN_D3D11_DUMP=file.bmp)
 *   test_d3d11.exe renderer.znc [cfgfile] [scene]      (scene 1 = shapes, 2 = blend modes, 3 = sprites / flip)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned version; HWND hwnd; unsigned rotate, mode; const char *gameName, *cfgFile; } OpenInfo;
static long (__stdcall *Init)(void), (__stdcall *Open)(OpenInfo *), (__stdcall *Close)(void), (__stdcall *Shut)(void);
static void (__stdcall *WrS)(unsigned), (__stdcall *WrD)(unsigned), (__stdcall *Lace)(void);
static HWND win;
static void W(unsigned v) { WrD(v); }
static unsigned XY(int x, int y) { return ((unsigned)y << 16) | ((unsigned)x & 0xffff); }
static unsigned RGBc(int r, int g, int b) { return (unsigned)(r | (g << 8) | (b << 16)); }
static void upload(int x, int y, int w, int h, const unsigned short *px) {
    int i, n = (w * h + 1) / 2;
    W(0xA0000000); W(XY(x, y)); W(XY(w, h));
    for (i = 0; i < n; i++) { unsigned lo = px[i * 2], hi = (i * 2 + 1 < w * h) ? px[i * 2 + 1] : 0; W(lo | (hi << 16)); }
}
static unsigned short c15(int r, int g, int b, int stp) { return (unsigned short)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (stp ? 0x8000 : 0)); }
static void fill(int x, int y, int w, int h, unsigned rgb) { W(0x02000000 | rgb); W(XY(x, y)); W(XY(w, h)); }
static void texpage(int px, int py, int abr, int depth) { W(0xE1000000 | (px & 15) | ((py & 1) << 4) | (abr << 5) | (depth << 7)); }
static void tri(unsigned cmd, int sv) { (void)cmd; (void)sv; }
int main(int argc, char **argv) {
    HMODULE h = LoadLibraryA(argc > 1 ? argv[1] : "renderer.znc");
    OpenInfo oi;
    static unsigned short tex[64 * 64], tex4[32 * 32];
    unsigned short clut[16];
    int i, x, y, scene = argc > 3 ? atoi(argv[3]) : 1;
    MSG m;
    WNDCLASSA wc;
    (void)tri;
    if (!h) { printf("load failed %lu\n", GetLastError()); return 2; }
    Init = (void *)GetProcAddress(h, "ZN_GPUinit"); Open = (void *)GetProcAddress(h, "ZN_GPUopen"); Close = (void *)GetProcAddress(h, "ZN_GPUclose");
    Shut = (void *)GetProcAddress(h, "ZN_GPUshutdown"); WrS = (void *)GetProcAddress(h, "ZN_GPUwriteStatus"); WrD = (void *)GetProcAddress(h, "ZN_GPUwriteData");
    Lace = (void *)GetProcAddress(h, "ZN_GPUupdateLace");
    memset(&wc, 0, sizeof wc); wc.lpfnWndProc = DefWindowProcA; wc.lpszClassName = "zntest"; wc.hInstance = GetModuleHandleA(NULL);
    RegisterClassA(&wc);
    win = CreateWindowA("zntest", "test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 656, 519, NULL, NULL, wc.hInstance, NULL);
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
    memset(&oi, 0, sizeof oi); oi.version = 1; oi.hwnd = win; oi.gameName = "testgame"; oi.cfgFile = argc > 2 ? argv[2] : NULL;
    if (Init() != 0) { puts("init failed"); return 3; }
    if (Open(&oi) != 0) { puts("open failed"); return 4; }
    if (GetEnvironmentVariableA("ZN_TEST_ALTENTER", (char[8]){0}, 8)) {   /* Alt+Enter: window -> fullscreen -> window */
        int k;
        printf("prop=%d\n", GetPropA(win, "ZincFullscreenHandled") != NULL);
        for (k = 0; k < (GetEnvironmentVariableA("ZN_TEST_FS", (char[8]){0}, 8) ? 2 : 3); k++) {
            RECT r;
            if (k) SendMessageA(win, WM_SYSKEYDOWN, VK_RETURN, 1 << 29);
            GetWindowRect(win, &r);
            printf("step %d: rect %ld,%ld,%ld,%ld caption=%d\n", k, r.left, r.top, r.right, r.bottom, (GetWindowLongA(win, GWL_STYLE) & WS_CAPTION) != 0);
        }
    }
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
        int chk = ((x >> 3) ^ (y >> 3)) & 1;
        tex[y * 64 + x] = (x < 4 && y < 4) ? 0 : c15(chk ? 255 : x * 4, chk ? y * 4 : 40, chk ? 40 : 255 - y * 4, (x > 40 && y > 40));
    }
    for (y = 0; y < 32; y++) for (x = 0; x < 8; x++) tex4[y * 8 + x] = (unsigned short)((((x * 2) & 15)) | (((x * 2 + 1) & 15) << 4) | (((y & 15)) << 8) | (((y + 1) & 15) << 12));
    for (i = 0; i < 16; i++) clut[i] = i == 0 ? 0 : c15(i * 16, 255 - i * 16, 128, 0);
    WrS(0x03000000 /* display on */); WrS(0x08000001 /* 320 wide */);
    upload(640, 0, 64, 64, tex);               /* 15-bit page at x=640 */
    upload(704, 0, 8, 32, tex4);               /* 4-bit page at x=704: 32 texels wide */
    upload(0, 480, 16, 1, clut);
    W(0xE3000000); W(0xE4000000 | 319 | (239 << 10)); W(0xE5000000); W(0xE2000000);
    fill(0, 0, 320, 240, RGBc(20, 40, 90));
    if (scene == 1) {
        W(0x20000000 | RGBc(255, 200, 0)); W(XY(10, 10)); W(XY(70, 20)); W(XY(30, 60));          /* flat triangle */
        W(0x38000000 | RGBc(255, 0, 0)); W(XY(90, 10)); W(RGBc(0, 255, 0)); W(XY(150, 10)); W(RGBc(0, 0, 255)); W(XY(90, 60)); W(RGBc(255, 255, 0)); W(XY(150, 60)); /* gouraud quad */
        texpage(10, 0, 0, 2);                                                                      /* 15-bit texture page, modulated */
        W(0x2C000000 | RGBc(128, 128, 128)); W(XY(170, 10)); W(0 | (0 << 8)); W(XY(234, 10)); W(63 | (0 << 8) | ((10 | 0 << 4 | 2 << 7) << 16)); W(XY(170, 74)); W(0 | (63 << 8)); W(XY(234, 74)); W(63 | (63 << 8));
        texpage(11, 0, 0, 0);                                                                      /* 4-bit with CLUT */
        W(0x64000000 | RGBc(128, 128, 128)); W(XY(10, 100)); W(0 | (0 << 8) | ((0 | (480 << 6)) << 16)); W(XY(64, 64));
        W(0x40000000 | RGBc(255, 255, 255)); W(XY(90, 120)); W(XY(150, 200));                       /* line */
        W(0x40000000 | RGBc(255, 255, 255)); W(XY(150, 120)); W(XY(90, 200));
        W(0x50000000 | RGBc(255, 0, 0)); W(XY(200, 120)); W(RGBc(0, 255, 0)); W(XY(300, 130)); W(0x55555555);  /* shaded line */
    } else if (scene == 2) {
        int a;
        for (a = 0; a < 4; a++) {
            texpage(10, 0, a, 2);
            W(0x2E000000 | RGBc(128, 128, 128)); W(XY(10 + a * 78, 10)); W(0); W(XY(74 + a * 78, 10)); W(63 | ((10 | (a << 5) | (2 << 7)) << 16)); W(XY(10 + a * 78, 74)); W(63 << 8); W(XY(74 + a * 78, 74)); W(63 | (63 << 8));
            texpage(10, 0, a, 2);
            W(0x2A000000 | RGBc(200, 200, 200)); W(XY(10 + a * 78, 110)); W(XY(74 + a * 78, 110)); W(XY(10 + a * 78, 170));
        }
        fill(0, 190, 320, 20, RGBc(200, 100, 50));
        for (a = 0; a < 4; a++) { texpage(0, 0, a, 0); W(0x62000000 | RGBc(120, 120, 120)); W(XY(10 + a * 78, 195)); W(XY(40, 10)); }
    } else if (scene == 5) {   /* a turned (not axis aligned) textured quad, magnified: shows the 3D texture smoothing */
        static unsigned short tb[64 * 64];
        for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) tb[y * 64 + x] = c15(((x >> 3) ^ (y >> 3)) & 1 ? 250 : 120, 200, 255, 0);
        upload(768, 0, 64, 64, tb);
        texpage(12, 0, 0, 2);
        W(0x2E000000 | RGBc(128, 128, 128)); W(XY(80, 10)); W(0); W(XY(150, 70)); W(63 | ((12 | (0 << 5) | (2 << 7)) << 16)); W(XY(10, 70)); W(63 << 8); W(XY(80, 130)); W(63 | (63 << 8));
    } else if (scene == 4) {
        static unsigned short tb[64 * 64];
        for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) tb[y * 64 + x] = c15(((x >> 3) ^ (y >> 3)) & 1 ? 250 : 120, 200, 255, 0);
        upload(768, 0, 64, 64, tb);
        fill(0, 100, 320, 40, RGBc(180, 60, 60));
        texpage(10, 0, 1, 2);   /* mixed texture, additive */
        W(0x2E000000 | RGBc(128, 128, 128)); W(XY(10, 20)); W(0); W(XY(74, 20)); W(63 | ((10 | (1 << 5) | (2 << 7)) << 16)); W(XY(10, 84)); W(63 << 8); W(XY(74, 84)); W(63 | (63 << 8));
        texpage(12, 0, 1, 2);   /* texture without any blended texel, additive */
        W(0x2E000000 | RGBc(128, 128, 128)); W(XY(110, 20)); W(0); W(XY(174, 20)); W(63 | ((12 | (1 << 5) | (2 << 7)) << 16)); W(XY(110, 84)); W(63 << 8); W(XY(174, 84)); W(63 | (63 << 8));
    } else {
        texpage(10, 0, 0, 2);
        W(0x65000000 | RGBc(128, 128, 128)); W(XY(10, 10)); W(0 | (0 << 8)); W(XY(48, 48));                  /* sprite */
        W(0xE1000000 | 10 | (2 << 7) | (1 << 12));                                                        /* flip x */
        W(0x65000000 | RGBc(128, 128, 128)); W(XY(70, 10)); W(47 | (47 << 8)); W(XY(48, 48));
        W(0xE1000000 | 10 | (2 << 7) | (1 << 13));                                                        /* flip y */
        W(0x65000000 | RGBc(128, 128, 128)); W(XY(130, 10)); W(47 | (47 << 8)); W(XY(48, 48));
        W(0xE1000000 | 10 | (2 << 7) | (3 << 12));                                                        /* flip both */
        W(0x65000000 | RGBc(128, 128, 128)); W(XY(190, 10)); W(47 | (47 << 8)); W(XY(48, 48));
        W(0xE1000000 | 10 | (2 << 7));
        W(0x75000000 | RGBc(128, 128, 128)); W(XY(10, 100)); W(0 | (0 << 8));                              /* 16x16 */
        W(0x6D000000 | RGBc(128, 128, 128)); W(XY(40, 100)); W(0 | (0 << 8));                              /* 8x8 raw */
        W(0xE2000000 | 3 | (3 << 5) | (4 << 10) | (4 << 15));                                              /* texture window */
        W(0x65000000 | RGBc(128, 128, 128)); W(XY(100, 100)); W(0 | (0 << 8)); W(XY(64, 64));
        W(0xE2000000);
        /* VRAM copy + readback path */
        W(0x80000000); W(XY(0, 0)); W(XY(0, 120)); W(XY(160, 100));
    }
    Lace();
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
    Lace();
    Close(); Shut();
    puts("done");
    return 0;
}
