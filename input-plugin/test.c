#include <windows.h>
#include <stdio.h>
#include <string.h>
typedef int (__stdcall *Open_t)(HWND,int,int,int); typedef int (__stdcall *Read_t)(unsigned long*); typedef int (__stdcall *Close_t)(void);
static HMODULE h; static Open_t Open; static Read_t Read; static Close_t Close;
static void (__cdecl *Key)(int,int), (__cdecl *Reset)(void), (__cdecl *AddX)(void), (__cdecl *AddJ)(void);
static void (__cdecl *SetX)(int,int,int,int,int), (__cdecl *JBtn)(int,int,int), (__cdecl *JAxis)(int,int,int), (__cdecl *JPov)(int,int);
static void (__cdecl *Time)(unsigned);
static int fails;
static void expect(const char *name, int type, unsigned long o0, unsigned long o1, unsigned long o2) {
    unsigned long o[5]; Read(o);
    if (o[0]!=o0||o[1]!=o1||o[2]!=o2) { printf("FAIL %-34s got %lx %lx %lx want %lx %lx %lx\n", name, o[0],o[1],o[2],o0,o1,o2); fails++; }
    else printf("ok   %s\n", name);
}
static void tap(int vk) { unsigned long o[5]; Key(vk,1); Read(o); Key(vk,0); }
static void drain(unsigned from) { unsigned long o[5]; int i; for (i = 1; i <= 12; i++) { Time(from + 60 * i); Read(o); } }
static void cfg(const char *text) { FILE *f=fopen("zinc-input.cfg","w"); if(f){fputs(text,f);fclose(f);} }
int main(void) {
    h = LoadLibraryA("zinc-input-test.znc"); if(!h){printf("load failed %lu\n",GetLastError());return 2;}
    Open=(Open_t)GetProcAddress(h,"ZN_JammaOpen"); Read=(Read_t)GetProcAddress(h,"ZN_JammaRead"); Close=(Close_t)GetProcAddress(h,"ZN_JammaClose");
    Key=(void*)GetProcAddress(h,"ZT_Key"); Reset=(void*)GetProcAddress(h,"ZT_Reset"); AddX=(void*)GetProcAddress(h,"ZT_AddXPad"); AddJ=(void*)GetProcAddress(h,"ZT_AddJPad");
    Time=(void*)GetProcAddress(h,"ZT_Time"); SetX=(void*)GetProcAddress(h,"ZT_X"); JBtn=(void*)GetProcAddress(h,"ZT_JBtn"); JAxis=(void*)GetProcAddress(h,"ZT_JAxis"); JPov=(void*)GetProcAddress(h,"ZT_JPov");
    remove("zinc-input.cfg");
    /* type 1 -> table 0 (Capcom/Tecmo style): up0 down1 left2 right3 b1..b3 = 4,5,6 start 8 b4..b6 = 12,13,14 */
    Open(0,1,0,0); Reset();
    expect("idle", 1, 0,0,0);
    Key('W',1); expect("P1 up = W", 1, 0, 1ul<<0, 0); Key('W',0);
    Key('D',1); expect("P1 right = D", 1, 0, 1ul<<3, 0); Key('D',0);
    Key(VK_NUMPAD4,1); expect("P1 B1 = numpad4", 1, 0, 1ul<<4, 0); Key(VK_NUMPAD4,0);
    Key(VK_NUMPAD6,1); expect("P1 B3 = numpad6", 1, 0, 1ul<<6, 0); Key(VK_NUMPAD6,0);
    Key(VK_NUMPAD1,1); expect("P1 B4 = numpad1", 1, 0, 1ul<<12, 0); Key(VK_NUMPAD1,0);
    Key(VK_NUMPAD3,1); expect("P1 B6 = numpad3", 1, 0, 1ul<<14, 0); Key(VK_NUMPAD3,0);
    Key(VK_RETURN,1); expect("P1 start = Enter", 1, 0, 1ul<<8, 0); Key(VK_RETURN,0);
    Key('1',1); expect("old P1 start key 1 unbound", 1, 0, 0, 0); Key('1',0);
    Key(VK_UP,1); expect("P2 up = arrow up", 1, 0, 0, 1ul<<0); Key(VK_UP,0);
    Key(VK_RIGHT,1); expect("P2 right = arrow right", 1, 0, 0, 1ul<<3); Key(VK_RIGHT,0);
    Key('U',1); expect("P2 B1 = U", 1, 0, 0, 1ul<<4); Key('U',0);
    Key('O',1); expect("P2 B3 = O", 1, 0, 0, 1ul<<6); Key('O',0);
    Key('J',1); expect("P2 B4 = J", 1, 0, 0, 1ul<<12); Key('J',0);
    Key('L',1); expect("P2 B6 = L", 1, 0, 0, 1ul<<14); Key('L',0);
    Key('Y',1); expect("P2 start = Y", 1, 0, 0, 1ul<<8); Key('Y',0);
    Key(VK_RSHIFT,1); expect("coin1 = Right Shift", 1, 0x10, 0, 0); Key(VK_RSHIFT,0);
    Key(VK_LSHIFT,1); expect("left shift is not a coin", 1, 0, 0, 0); Key(VK_LSHIFT,0);
    Key('H',1); expect("coin2 = H", 1, 0x20, 0, 0); Key('H',0);
    Key(0x73,1); expect("test = F4", 1, 0x1, 0, 0); Key(0x73,0);
    Key(0x76,1); expect("service = F7", 1, 0x2, 0, 0); Key(0x76,0);
    Key('W',1); Key('D',1); expect("diagonal W+D", 1, 0, 9, 0); Key('W',0); Key('D',0);
    Key('A',1); expect("old P1 B1 key A is now LEFT", 1, 0, 1ul<<2, 0); Key('A',0);
    Close();
    /* XInput pads */
    Open(0,1,0,0); Reset(); AddX(); AddX();
    SetX(0,0x4000,0,0,0); expect("XInput P1 X = B1", 1, 0, 1ul<<4, 0);
    SetX(0,0x8000,0,0,0); expect("XInput P1 Y = B2", 1, 0, 1ul<<5, 0);
    SetX(0,0x0200,0,0,0); expect("XInput P1 RB = B3", 1, 0, 1ul<<6, 0);
    SetX(0,0x1000,0,0,0); expect("XInput P1 A = B4", 1, 0, 1ul<<12, 0);
    SetX(0,0x2000,0,0,0); expect("XInput P1 B = B5", 1, 0, 1ul<<13, 0);
    SetX(0,0,0,0,200);    expect("XInput P1 RT = B6", 1, 0, 1ul<<14, 0);
    SetX(0,0,0,0,40);     expect("XInput P1 RT below threshold", 1, 0, 0, 0);
        SetX(0,0x0001,0,0,0); expect("XInput P1 dpad up", 1, 0, 1ul<<0, 0);
    SetX(0,0,30000,0,0);  expect("XInput P1 stick right", 1, 0, 1ul<<3, 0);
    SetX(0,0,10000,0,0);  expect("XInput P1 stick inside deadzone", 1, 0, 0, 0);
    SetX(0,0x0010,0,0,0); expect("XInput P1 START = start", 1, 0, 1ul<<8, 0);
    SetX(0,0x0020,0,0,0); expect("XInput P1 BACK = coin1", 1, 0x10, 0, 0);
    SetX(0,0,0,0,0); SetX(1,0x2000,0,0,0); expect("XInput P2 B = B5", 1, 0, 0, 1ul<<13);
    SetX(1,0x4000,0,0,0); expect("XInput P2 X = B1", 1, 0, 0, 1ul<<4);
    SetX(1,0,0,0,255);    expect("XInput P2 RT = B6", 1, 0, 0, 1ul<<14);
    SetX(1,0x0020,0,0,0); expect("XInput P2 BACK = coin2 only", 1, 0x20, 0, 0);
    SetX(0,0,0,0,0); SetX(1,0,0,0,0);
    Close();
    /* DirectInput pad as player 1 */
    Open(0,1,0,0); Reset(); AddJ();
    JBtn(0,0,1); expect("DInput B1", 1, 0, 1ul<<4, 0); JBtn(0,0,0);
    JBtn(0,9,1); expect("DInput B10 = start", 1, 0, 1ul<<8, 0); JBtn(0,9,0);
    JPov(0,0);    expect("DInput POV up", 1, 0, 1ul<<0, 0);
    JPov(0,9000); expect("DInput POV right", 1, 0, 1ul<<3, 0);
    JPov(0,13500);expect("DInput POV down-right", 1, 0, (1ul<<1)|(1ul<<3), 0); JPov(0,0xFFFFFFFF);
    JAxis(0,0,-900); expect("DInput axis X- = left", 1, 0, 1ul<<2, 0); JAxis(0,0,0);
    JAxis(0,1,900); expect("DInput axis Y+ = down", 1, 0, 1ul<<1, 0); JAxis(0,1,0);
    Close();
    /* config file overrides */
    cfg("p1_b1=K:SPACE\np1_up=K:UP,K:W\npad1=none\ndeadzone=20\nxinput=1\n");
    Open(0,1,0,0); Reset(); AddX();
    Key(VK_SPACE,1); expect("cfg: B1 = SPACE", 1, 0, 1ul<<4, 0); Key(VK_SPACE,0);
    Key(VK_NUMPAD4,1); expect("cfg: numpad4 no longer bound", 1, 0, 0, 0); Key(VK_NUMPAD4,0);
    SetX(0,0x1000,0,0,0); expect("cfg: pad1=none ignores pad", 1, 0, 0, 0);
    Key(VK_UP,1); expect("cfg: P1 up has two keys (UP also P2 up)", 1, 0, 1ul<<0, 1ul<<0); Key(VK_UP,0);
    Close();
    /* autofire */
    cfg("p1_b1_auto=1\nautofire_ms=40\n");
    Open(0,1,0,0); Reset(); Time(1000);
    Key(VK_NUMPAD4,1); expect("autofire: on at press", 1, 0, 1ul<<4, 0);
    Time(1050); expect("autofire: off in second half period", 1, 0, 0, 0);
    Time(1090); expect("autofire: on again", 1, 0, 1ul<<4, 0);
    Key(VK_NUMPAD4,0); expect("autofire: released", 1, 0, 0, 0);
    Key(VK_NUMPAD5,1); Time(1150); expect("autofire: other buttons unaffected", 1, 0, 1ul<<5, 0); Key(VK_NUMPAD5,0);
    Close(); remove("zinc-input.cfg");
    /* table 1 (Konami GV style, type 3): extras Q/E/R/F keep their letter keys */
    remove("zinc-input.cfg");
    Open(0,3,0,0); Reset();
    Key('Q',1); expect("GV: extra Q", 3, 0, 1ul<<1, 0); Key('Q',0);
    Key('F',1); expect("GV: extra F", 3, 0, 1ul<<4, 0); Key('F',0);
    Key('W',1); expect("GV: W is P1 up (bit 11) not extra", 3, 0, 1ul<<11, 0); Key('W',0);
    Key('R',1); expect("GV: extra R", 3, 0, 1ul<<0, 0); Key('R',0);
    Key(VK_NUMPAD1,1); expect("GV: numpad1 = B4 (bit 5)", 3, 0, 1ul<<5, 0); Key(VK_NUMPAD1,0);
    Close();
    /* combo macros */
    cfg("combo_step_ms=50\ncombo1=K:F1\ncombo1_seq=236+HP\ncombo1_player=1\ncombo1_face=R\n"
        "combo2=K:F2\ncombo2_seq=623+HP\ncombo2_player=2\ncombo2_face=L\n"
        "combo3=K:F3\ncombo3_seq=236+LP ~ HK\ncombo3_player=1\ncombo3_face=R\n"
        "combo4=K:F4\ncombo4_seq=44\ncombo4_player=1\ncombo4_face=L\n");
    Open(0,1,0,0); Reset(); Time(1000);
    Key(VK_F1,1); expect("combo1 t+0   step 1: down", 1, 0, 1ul<<1, 0);
    Key(VK_F1,0);
    Time(1060); expect("combo1 t+60  step 2: down+fwd", 1, 0, (1ul<<1)|(1ul<<3), 0);
    tap(VK_F3); Time(1070); expect("combo3 ignored while combo1 runs", 1, 0, (1ul<<1)|(1ul<<3), 0);
    Time(1110); expect("combo1 t+110 step 3: fwd + HP", 1, 0, (1ul<<3)|(1ul<<6), 0);
    Time(1160); expect("combo1 finished", 1, 0, 0, 0);
    Time(2000); Key(VK_F1,1); expect("combo1 again", 1, 0, 1ul<<1, 0);
    drain(2000); expect("held trigger does not repeat", 1, 0, 0, 0); Key(VK_F1,0);
    drain(2500);
    Time(3000); tap(VK_F2); expect("combo2 (P2, facing left) fwd = left", 1, 0, 0, 1ul<<2);
    Time(3050); expect("combo2 step 2: down", 1, 0, 0, 1ul<<1);
    Time(3100); expect("combo2 step 3: down+fwd(left) + HP", 1, 0, 0, (1ul<<1)|(1ul<<2)|(1ul<<6));
    Time(3150); expect("combo2 finished", 1, 0, 0, 0);
    Time(4000); tap(VK_F3);
    Time(4050); expect("combo3 step 2: down+fwd", 1, 0, (1ul<<1)|(1ul<<3), 0);
    Time(4100); expect("combo3 step 3: fwd + LP", 1, 0, (1ul<<3)|(1ul<<4), 0);
    Time(4150); expect("combo3 step 4: pause", 1, 0, 0, 0);
    Time(4200); expect("combo3 step 5: HK only", 1, 0, 1ul<<14, 0);
    Time(4250); expect("combo3 finished", 1, 0, 0, 0);
    /* slow polling (1 poll per 400 ms): every step must still be returned exactly once, none skipped */
    Time(6000); tap(VK_F1);
    Time(6400); expect("slow poll: step 2 not skipped", 1, 0, (1ul<<1)|(1ul<<3), 0);
    Time(6800); expect("slow poll: step 3 not skipped", 1, 0, (1ul<<3)|(1ul<<6), 0);
    Time(7200); expect("slow poll: finished", 1, 0, 0, 0);
    /* long super motion 236236+HP, polled every 50 ms: 7 steps in order */
    cfg("combo_step_ms=50\ncombo1=K:F9\ncombo1_seq=236236+HP\ncombo1_player=1\ncombo1_face=R\n"
        "combo4=K:F4\ncombo4_seq=44\ncombo4_player=1\ncombo4_face=L\n");
    Close(); Open(0,1,0,0); Reset();
    Time(8000); tap(VK_F9);
    expect("super: 2", 1, 0, 1ul<<1, 0);
    Time(8050); expect("super: 3", 1, 0, (1ul<<1)|(1ul<<3), 0);
    Time(8100); expect("super: 6", 1, 0, 1ul<<3, 0);
    Time(8150); expect("super: 2", 1, 0, 1ul<<1, 0);
    Time(8200); expect("super: 3", 1, 0, (1ul<<1)|(1ul<<3), 0);
    Time(8250); expect("super: 6+HP", 1, 0, (1ul<<3)|(1ul<<6), 0);
    Time(8300); expect("super: done", 1, 0, 0, 0);
    drain(8300);
    drain(4300);
    Time(5000); tap(VK_F4); Key('W',1); expect("combo4 (P1 facing left) 44 = fwd... back = right + physical W(up)", 1, 0, (1ul<<3)|(1ul<<0), 0); Key('W',0);
    Close(); remove("zinc-input.cfg");
    /* notation: release gap between repeated buttons, hold (charge) durations, 3-button presses */
    cfg("combo_step_ms=50\ncombo1=K:F1\ncombo1_seq=LP,LP,6,LK,HP\ncombo1_player=1\ncombo1_face=R\n"
        "combo2=K:F2\ncombo2_seq=4*400 6+HP\ncombo2_player=1\ncombo2_face=R\n"
        "combo3=K:F3\ncombo3_seq=LP+MP+HP ~ 632147+HK\ncombo3_player=1\ncombo3_face=R\n");
    Close(); Open(0,1,0,0); Reset();
    Time(10000); tap(VK_F1);
    expect("chain: LP", 1, 0, 1ul<<4, 0);
    Time(10050); expect("chain: release between identical presses", 1, 0, 0, 0);
    Time(10100); expect("chain: LP again", 1, 0, 1ul<<4, 0);
    Time(10150); expect("chain: f", 1, 0, 1ul<<3, 0);
    Time(10200); expect("chain: LK", 1, 0, 1ul<<12, 0);
    Time(10250); expect("chain: HP", 1, 0, 1ul<<6, 0);
    Time(10300); expect("chain: done", 1, 0, 0, 0);
    Time(11000); tap(VK_F2);
    expect("charge: back held", 1, 0, 1ul<<2, 0);
    Time(11300); expect("charge: still held at 300 ms", 1, 0, 1ul<<2, 0);
    Time(11399); expect("charge: still held at 399 ms", 1, 0, 1ul<<2, 0);
    Time(11400); expect("charge: forward + HP after 400 ms", 1, 0, (1ul<<3)|(1ul<<6), 0);
    Time(11450); expect("charge: done", 1, 0, 0, 0);
    Time(12000); tap(VK_F3);
    expect("3-button press", 1, 0, (1ul<<4)|(1ul<<5)|(1ul<<6), 0);
    Time(12050); expect("pause", 1, 0, 0, 0);
    Time(12100); expect("360: f", 1, 0, 1ul<<3, 0);
    Time(12150); expect("360: df", 1, 0, (1ul<<1)|(1ul<<3), 0);
    Time(12200); expect("360: d", 1, 0, 1ul<<1, 0);
    Time(12250); expect("360: db", 1, 0, (1ul<<1)|(1ul<<2), 0);
    Time(12300); expect("360: b", 1, 0, 1ul<<2, 0);
    Time(12350); expect("360: ub + HK", 1, 0, (1ul<<0)|(1ul<<2)|(1ul<<14), 0);
    Time(12400); expect("360: done", 1, 0, 0, 0);
    Close(); remove("zinc-input.cfg");
    /* charge time setting: 4*C uses combo_charge_ms */
    cfg("combo_step_ms=50\ncombo_charge_ms=300\ncombo1=K:F1\ncombo1_seq=4*C 6+HP\ncombo1_player=1\ncombo1_face=R\n");
    Close(); Open(0,1,0,0); Reset();
    Time(20000); tap(VK_F1);
    expect("charge setting: back held", 1, 0, 1ul<<2, 0);
    Time(20299); expect("charge setting: still held at 299 ms", 1, 0, 1ul<<2, 0);
    Time(20300); expect("charge setting: forward + HP at 300 ms", 1, 0, (1ul<<3)|(1ul<<6), 0);
    Time(20350); expect("charge setting: done", 1, 0, 0, 0);
    cfg("combo1=K:F1\ncombo1_seq=4*C 6+HP\ncombo1_player=1\ncombo1_face=R\n");
    Close(); Open(0,1,0,0); Reset();
    Time(30000); tap(VK_F1);
    Time(30999); expect("default charge 1000 ms: still held", 1, 0, 1ul<<2, 0);
    Time(31000); expect("default charge 1000 ms: released", 1, 0, (1ul<<3)|(1ul<<6), 0);
    Close(); remove("zinc-input.cfg");
    /* more than 9 slots: combo12 / combo20 are read, combo21 is ignored */
    cfg("combo_step_ms=50\ncombo12=K:F6\ncombo12_seq=236+HP\ncombo12_player=1\ncombo12_face=R\n"
        "combo20=K:F7\ncombo20_seq=623+HP\ncombo20_player=2\ncombo20_face=R\n"
        "combo21=K:F8\ncombo21_seq=214+HK\ncombo21_player=1\ncombo21_face=R\n");
    Close(); Open(0,1,0,0); Reset(); Time(50000);
    tap(VK_F6); expect("slot 12 works", 1, 0, 1ul<<1, 0);
    drain(50000);
    Time(52000); tap(VK_F7); expect("slot 20 works (player 2)", 1, 0, 0, 1ul<<3);
    drain(52000); Time(54000); tap(VK_F8); expect("slot 21 is ignored", 1, 0, 0, 0);
    Close(); remove("zinc-input.cfg");
    /* unsupported type: only system inputs */
    Open(0,2,0,0); Reset(); Key('W',1); expect("type 2 unsupported: no game bits", 2, 0,0,0); Key(VK_RSHIFT,1); expect("type 2: coin still works", 2, 0x10,0,0);
    Close();
    printf("%s (%d failures)\n", fails?"FAILED":"ALL PASSED", fails);
    return fails?1:0;
}
