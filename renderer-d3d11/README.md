# Direct3D 11 renderer for ZiNc

`d3d11gpu.c` is a ZiNc renderer plugin (the 16 `ZN_GPU*` exports) that draws with Direct3D 11.

* The PlayStation-style GPU command stream, VRAM transfers and registers are decoded on the CPU.
* Polygons (flat / Gouraud / textured, 4 / 8 / 15-bit, CLUT, texture window, all four semi-transparency modes),
  sprites (with flip), lines and fills are drawn on the GPU at `InternalScale` times the console resolution
  (default 0 = auto from the window size; 1x - 4x) into a scaled copy of VRAM.
* The visible part is scaled to the window (4:3 / 3:4 letterbox when rotated or `KeepAspect = 1`, rotation,
  scanlines, point or linear filtering) and shown through DXGI.
* `Dedither` (default 0): 0 = off, 1 / 2 / 3 = low / medium / high smoothing of checkerboard dither patterns in the picture (a pixel that differs slightly from four alike neighbours is blended with them). The renderer itself does not dither; this only affects patterns in the game's own graphics.
* `KeepAspect` (default 0): 0 = stretch to the window, 1 = 4:3 (3:4 when rotated), 2 = 16:9, 3 = pixel perfect: the console picture times a whole number with square pixels (scaled down only if the window is smaller). Rotated games always keep their shape.
* `Overscan` (default 0): console pixels cut off at every edge of the picture (the rest is enlarged).
* `BezelImage` (default empty): path of a PNG with a transparent window (UTF-8 text, the only text value). The picture is shown in the window (found from the transparent area through the middle of the image, so rectangular windows) and the image is drawn over it, fitted into the game window; `KeepAspect` applies inside the window. Loaded with GDI+.
* `Borderless` (default 0): with `FullScreen = 1`, 1 uses a borderless window over the desktop at its current resolution instead of switching the screen mode to `Width` x `Height`.
* `VSync` (default 0): 1 waits for the screen refresh before showing each frame (no tearing).
* `FXAA` (default 0): 1 applies a FXAA pass to the final window picture.
* `XBRZ` (default 2): 0 = off, 1 draws the game at the console resolution and enlarges it with xBRZ; `XBRZ = 2` applies xBRZ to 2D objects only (sprites, images, screen-aligned rectangles) and keeps 3D at `InternalScale` (a GPU port of the xBRZ algorithm, based on the
  DeSmuME team / Hyllian shader from the libretro collection; GPL).
* Needs Windows 7 SP1+ with Direct3D 11 (or WARP) and `d3dcompiler_47.dll` (Windows 8.1 and later have it).

`ShowFPS = 1` draws a frame counter ("FPS 60") in the top left corner of the picture. The window title is never changed (tools such as the ZiNc 1.1 trainer find ZiNc by its exact title).

Reads `renderer.cfg`: `XSize YSize FullScreen ScanLines Filtering ShowFPS FrameLimitation FrameSkipping
FramerateDetection FramerateManual KeepAspect InternalScale XBRZ Logging`. The other keys of the file (`EnableKeys`, `FastExcel`, `TurnDisplay`, ...) belong to the OpenGL / Direct3D renderers and are ignored here. Problems are logged to `zinc-d3d11.log` next to ZiNc.exe (only when `Logging = 1` is set in renderer.cfg, i.e. "Enable Logs" is on in the GUI; off by default).

Frame pacing: the swap chain uses the flip model (Windows 10+; the old bit-blit model is the fallback) with at most one queued frame,
and the frame limiter waits with a high resolution timer and a short spin instead of `Sleep(1)`. With `Logging = 1` (GUI: "Enable Logs")
`zinc-d3d11.log` names the graphics adapter and the swap chain model, and gets one line per second:
`fps | slowest frame | frame limiter wait | present | transparency check | VRAM read-backs` (per-frame averages) to find out where the time goes when the frame rate dips.

Limits: what the GPU draws is not copied back into the CPU copy of VRAM (only when a game reads VRAM back),
so render-to-texture effects are approximate; no dithering, no mask-bit emulation.

Build (32-bit, ZiNc is a 32-bit program):

    python -m ziglang cc -target x86-windows-gnu -O2 -shared -o renderer_d3d11.znc d3d11gpu.c d3d11gpu.def -luser32 -lgdi32 -lwinmm -ld3d11 -ldxgi -ldxguid

`test_d3d11.c` draws sample scenes through the plugin; build it for 64-bit together with a 64-bit plugin build and run
it under Wine (it can create a Direct3D 11 device through Mesa): `ZN_D3D11_DUMP=out.bmp test_d3d11.exe renderer.znc renderer.cfg 1`.

## Log (zinc-d3d11.log)

Written next to ZiNc.exe only when `Logging = 1` (Enable Logs in the GUI), one session per file, every line stamped with the seconds since the start:

* start: renderer, Windows / Wine version, CPU, memory, desktop mode and scaling, every graphics adapter (which one is in use, video memory, driver version), Direct3D feature level, swap chain model, shader compiler, window size and all renderer.cfg settings in use, frame timer type
* events: window resized, Alt+Enter, screen mode changes, the game changing its display area / colour depth, GPU resets, a lost graphics device (with the reason), a hidden window
* every second: fps, frame time average / 99% / maximum, hitches, where the time of a frame goes (outside the renderer = emulator, sound, input; drawing; present; frame limiter), number of draws, triangles, texture uploads, image copies and VRAM read-backs
* every hitch (a frame much longer than it should be): how long, and how it splits into outside the renderer / drawing / present / limiter, with a hint where the time went
* when the game closes: a summary with the average fps, the 1% low and the number of slow frames

The file stops growing at 4 MB.
