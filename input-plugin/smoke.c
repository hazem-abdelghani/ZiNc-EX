#include <windows.h>
#include <stdio.h>
int main(void){ HMODULE h=LoadLibraryA("zinc-input-real.znc"); if(!h){puts("load fail");return 2;}
 int (__stdcall *O)(HWND,int,int,int)=(void*)GetProcAddress(h,"ZN_JammaOpen"); int (__stdcall *R)(unsigned long*)=(void*)GetProcAddress(h,"ZN_JammaRead"); int (__stdcall *C)(void)=(void*)GetProcAddress(h,"ZN_JammaClose");
 int r=O(GetDesktopWindow(),1,0,0); unsigned long o[5]; int i; for(i=0;i<5;i++){ int rr=R(o); Sleep(50); if(rr){puts("read fail");return 3;} }
 printf("open=%d read ok out=%lx %lx %lx %lx %lx close=%d\n",r,o[0],o[1],o[2],o[3],o[4],C()); return 0;}
