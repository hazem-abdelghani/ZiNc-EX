/*
 * Direct3D 11 renderer plugin for ZiNc.
 *
 * Implements the 16 ZN_GPU* exports ZiNc expects (the PSEmu "GPU plugin" interface). Command decoding, VRAM
 * transfers and the register model are done on the CPU; polygons, sprites, lines and fills are drawn on the GPU
 * at a multiple (InternalScale) of the console's resolution into a scaled copy of VRAM, so the picture is sharp at
 * any window size. The result is scaled to the window (aspect, rotation, optional scanlines) and presented through DXGI.
 *
 * Textures are read from a CPU-side copy of VRAM that is uploaded when the game writes to it. Things the GPU drew
 * are not copied back into that CPU copy (except when the game reads VRAM back), so render-to-texture effects
 * are not reproduced exactly.
 *
 * Build (32-bit, ZiNc is a 32-bit program):
 *   python -m ziglang cc -target x86-windows-gnu -O2 -shared -o renderer_d3d11.znc d3d11gpu.c d3d11gpu.def -luser32 -lgdi32 -lwinmm -ld3d11 -ldxgi -ldxguid
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;

static const char *HLSL =
"Texture2D<uint> vramTex : register(t0);\n"
"\n"
"struct VSO {\n"
"    float4 p : SV_Position;\n"
"    noperspective float2 uv : UV;\n"
"    noperspective float4 c : COL;\n"
"    nointerpolation uint4 i : INFO;\n"
"};\n"
"\n"
"VSO vs_draw(float2 pos : POS, float2 uv : UV, float4 col : COL, uint4 info : INFO) {\n"
"    VSO o;\n"
"    o.p = float4(pos.x / 512.0 - 1.0, 1.0 - pos.y / 512.0, 0.0, 1.0);\n"
"    o.uv = uv;\n"
"    o.c = col;\n"
"    o.i = info;\n"
"    return o;\n"
"}\n"
"\n"
"uint fetch15(int x, int y) { return vramTex.Load(int3(x & 1023, y & 1023, 0)); }\n"
"\n"
"uint texel(int u, int w, uint tpd, bool swz, int mx, int my, int ox, int oy, int tpx, int tpy, int cx, int cy) {\n"
"    u &= 255; w &= 255;\n"
"    u = (u & ~mx) | ((ox * 8) & mx);\n"
"    w = (w & ~my) | ((oy * 8) & my);\n"
"    uint t;\n"
"    if (tpd == 0u) {\n"
"        uint q = swz ? fetch15(tpx + ((u >> 2) & 3) + 4 * (w & 15), tpy + (w & ~15) + ((u >> 4) & 15)) : fetch15(tpx + (u >> 2), tpy + w);\n"
"        t = fetch15(cx + int((q >> uint((u & 3) * 4)) & 15u), cy);\n"
"    } else if (tpd == 1u) {\n"
"        uint q = swz ? fetch15(tpx + ((u >> 1) & 7) + 64 * ((u >> 4) & 1) + 8 * (w & 7), tpy + (w & ~7) + ((u >> 5) & 7)) : fetch15(tpx + (u >> 1), tpy + w);\n"
"        t = fetch15(cx + int((q >> uint((u & 1) * 8)) & 255u), cy);\n"
"    } else {\n"
"        t = fetch15(tpx + u, tpy + w);\n"
"    }\n"
"    return t;\n"
"}\n"
"\n"
"struct PSO { float4 c0 : SV_Target0; float4 c1 : SV_Target1; };\n"
"\n"
"PSO ps_draw(VSO v) {\n"
"    PSO o;\n"
"    uint fl = v.i.z;\n"
"    bool tex = (fl & 1u) != 0u;\n"
"    bool raw = (fl & 2u) != 0u;\n"
"    bool blit = (fl & 4u) != 0u;\n"
"    uint tpd = (fl >> 3) & 3u;\n"
"    uint passm = (fl >> 5) & 3u;\n"
"    bool semi = (fl & 128u) != 0u;\n"
"    bool allsemi = (fl & 1024u) != 0u;\n"
"    bool is2d = (fl & 2048u) != 0u;\n"
"    bool swz = (fl & 4096u) != 0u;\n"
"    bool smooth = (fl & 8192u) != 0u;\n"
"    uint abr = (fl >> 8) & 3u;\n"
"    float3 rgb;\n"
"    bool isSemi = semi;\n"
"    if (blit) {\n"
"        int2 p = int2(floor(v.uv));\n"
"        uint t = fetch15(p.x, p.y);\n"
"        o.c0 = float4(float3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u) / 31.0, 1.0);\n"
"        o.c1 = float4(1, 1, 1, 0);\n"
"        return o;\n"
"    }\n"
"    if (tex) {\n"
"        uint tw = v.i.w;\n"
"        int mx = int(tw & 255u) * 8, my = int((tw >> 8) & 255u) * 8;\n"
"        int ox = int((tw >> 16) & 255u), oy = int((tw >> 24) & 255u);\n"
"        int tpx = int(v.i.x & 65535u), tpy = int(v.i.x >> 16);\n"
"        int cx = int(v.i.y & 65535u), cy = int(v.i.y >> 16);\n"
"        uint t = texel(int(floor(v.uv.x)), int(floor(v.uv.y)), tpd, swz, mx, my, ox, oy, tpx, tpy, cx, cy);\n"
"        if (t == 0u) discard;\n"
"        bool stp = (t & 0x8000u) != 0u;\n"
"        if (semi) {\n"
"            if (passm == 1u && stp) discard;\n"
"            if (passm == 2u && !stp) discard;\n"
"            isSemi = stp || allsemi;\n"
"        }\n"
"        float3 tc = float3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u) / 31.0;\n"
"        if (smooth) {\n"
"            float2 pp = v.uv - 0.5;\n"
"            int2 b0 = int2(floor(pp));\n"
"            float2 fr = pp - floor(pp);\n"
"            float3 acc = float3(0.0, 0.0, 0.0);\n"
"            float sw = 0.0;\n"
"            for (int k = 0; k < 4; k++) {\n"
"                int kx = k & 1, ky = k >> 1;\n"
"                uint tk = texel(b0.x + kx, b0.y + ky, tpd, swz, mx, my, ox, oy, tpx, tpy, cx, cy);\n"
"                float wk = (kx != 0 ? fr.x : 1.0 - fr.x) * (ky != 0 ? fr.y : 1.0 - fr.y);\n"
"                if (tk != 0u && wk > 0.0) { acc += wk * float3(tk & 31u, (tk >> 5) & 31u, (tk >> 10) & 31u) / 31.0; sw += wk; }\n"
"            }\n"
"            if (sw > 0.0) tc = acc / sw;\n"
"        }\n"
"        rgb = raw ? tc : saturate(tc * v.c.rgb * (255.0 / 128.0));\n"
"    } else {\n"
"        rgb = v.c.rgb;\n"
"    }\n"
"    o.c0 = float4(rgb, is2d ? 1.0 : 0.0);\n"
"    float f = 1.0, a = 0.0;\n"
"    if (abr == 0u) { f = isSemi ? 0.5 : 1.0; }\n"
"    else if (abr == 1u) { f = isSemi ? 1.0 : 0.0; }\n"
"    else if (abr == 3u) { f = isSemi ? 0.25 : 1.0; a = isSemi ? 1.0 : 0.0; }\n"
"    o.c1 = float4(f, f, f, a);\n"
"    return o;\n"
"}\n"
"\n"
"/* ---------------------------------------------------------------- display */\n"
"Texture2D<float4> srcTex : register(t1);\n"
"SamplerState smp : register(s0);\n"
"cbuffer Disp : register(b0) { float4 params; float4 region; float4 xs; }   /* params.x: scanlines mode; region: x y w h of the picture in texels and xs: output pixels per source texel (xBRZ) */\n"
"\n"
"struct DSO { float4 p : SV_Position; float2 uv : UV; };\n"
"\n"
"DSO vs_disp(float2 pos : POS, float2 uv : UV) {\n"
"    DSO o;\n"
"    o.p = float4(pos, 0.0, 1.0);\n"
"    o.uv = uv;\n"
"    return o;\n"
"}\n"
"\n"
"/* dither smoothing: a pixel whose four neighbours are alike but which is a little different itself (the checkerboard of a dithered gradient) is blended with them.\n"
"   q: the pixel in the texture, st: texels per console pixel; params.y is the largest difference (0..1) that still counts as dither */\n"
"float ditherMax(float3 a, float3 b) { float3 d = abs(a - b); return max(d.r, max(d.g, d.b)); }\n"
"float3 ditherDelta(Texture2D<float4> t, int2 q, int st) {\n"
"    int lim = 1024 * st - 1;\n"
"    float3 c = t.Load(int3(q, 0)).rgb;\n"
"    float3 n = t.Load(int3(clamp(q + int2(0, -st), 0, lim), 0)).rgb;\n"
"    float3 s = t.Load(int3(clamp(q + int2(0, st), 0, lim), 0)).rgb;\n"
"    float3 w = t.Load(int3(clamp(q + int2(-st, 0), 0, lim), 0)).rgb;\n"
"    float3 e = t.Load(int3(clamp(q + int2(st, 0), 0, lim), 0)).rgb;\n"
"    float th = params.y;\n"
"    float3 a = (n + s + w + e) * 0.25;\n"
"    bool alike = ditherMax(n, s) < th && ditherMax(w, e) < th && ditherMax(n, w) < th;\n"
"    bool near = ditherMax(c, a) < th * 2.0 && ditherMax(c, a) > 0.0;\n"
"    return (alike && near) ? (a - c) * 0.5 : float3(0.0, 0.0, 0.0);\n"
"}\n"
"\n"
"float4 ps_disp(DSO v) : SV_Target {\n"
"    float3 c = srcTex.Sample(smp, v.uv).rgb;\n"
"    if (params.y > 0.0) { int st = int(params.z); c += ditherDelta(srcTex, int2(floor(v.uv * 1024.0)) * st + st / 2, st); }\n"
"    if (params.x > 0.5 && (int(v.p.y) & 1) == 1) c = (params.x < 1.5) ? float3(0, 0, 0) : c * 0.6;\n"
"    return float4(c, 1.0);\n"
"}\n"
"\n"

"\n"
"/* ---------------------------------------------------------------- xBRZ (\"freescale\" port of the xBRZ algorithm, GPU)\n"
"   Based on the 4xBRZ shader of the DeSmuME team (GPL) and Hyllian's xBR code, from the libretro shader collection. */\n"
"static const int BLEND_NONE = 0;\n"
"static const int BLEND_NORMAL = 1;\n"
"static const int BLEND_DOMINANT = 2;\n"
"static const float LUMINANCE_WEIGHT = 1.0;\n"
"static const float EQUAL_COLOR_TOLERANCE = 0.1176470588235294; // 30.0/255.0\n"
"static const float STEEP_DIRECTION_THRESHOLD = 2.2;\n"
"static const float DOMINANT_DIRECTION_THRESHOLD = 3.6;\n"
"\n"
"\n"
"\n"
"static int2 gBase;\n"
"\n"
"float3 P(float x, float y) {\n"
"    int2 p = clamp(gBase + int2((int)x, (int)y), int2((int)region.x, (int)region.y), int2((int)(region.x + region.z) - 1, (int)(region.y + region.w) - 1));\n"
"    float3 c = srcTex.Load(int3(p, 0)).rgb;\n"
"    if (params.y > 0.0) c += ditherDelta(srcTex, p, 1);\n"
"    return c;\n"
"}\n"
"\n"
"float DistYCbCr(float3 pixA, float3 pixB)\n"
"{\n"
"  const float3 w = float3(0.2627, 0.6780, 0.0593);\n"
"  const float scaleB = 0.5 / (1.0 - w.b);\n"
"  const float scaleR = 0.5 / (1.0 - w.r);\n"
"  float3 diff = pixA - pixB;\n"
"  float Y = dot(diff.rgb, w);\n"
"  float Cb = scaleB * (diff.b - Y);\n"
"  float Cr = scaleR * (diff.r - Y);\n"
"\n"
"  return sqrt(((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr));\n"
"}\n"
"\n"
"bool IsPixEqual(const float3 pixA, const float3 pixB)\n"
"{\n"
"  return (DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE);\n"
"}\n"
"\n"
"float get_left_ratio(float2 center, float2 origin, float2 direction, float2 scale)\n"
"{\n"
"  float2 P0 = center - origin;\n"
"  float2 proj = direction * (dot(P0, direction) / dot(direction, direction));\n"
"  float2 distv = P0 - proj;\n"
"  float2 orth = float2(-direction.y, direction.x);\n"
"  float side = sign(dot(P0, orth));\n"
"  float v = side * length(distv * scale);\n"
"\n"
"//  return step(0, v);\n"
"  return smoothstep(-sqrt(2.0)/2.0, sqrt(2.0)/2.0, v);\n"
"}\n"
"\n"
"\n"
"bool eq(float3 a, float3 b)\n"
"{\n"
"   return ((a.x==b.x)&&(a.y==b.y)&&(a.z==b.z));\n"
"}\n"
"\n"
"bool neq(float3 a, float3 b)\n"
"{\n"
"   return !((a.x==b.x)&&(a.y==b.y)&&(a.z==b.z));\n"
"}\n"
"\n"
"\n"
"float4 xbrz(float2 uv)\n"
"{\n"
"  //---------------------------------------\n"
"  // Input Pixel Mapping:  -|x|x|x|-\n"
"  //                       x|A|B|C|x\n"
"  //                       x|D|E|F|x\n"
"  //                       x|G|H|I|x\n"
"  //                       -|x|x|x|-\n"
"\n"
"  float2 scale = xs.xy;\n"
"  float2 tc = region.xy + uv * region.zw;\n"
"  float2 pos = frac(tc) - float2(0.5, 0.5);\n"
"  gBase = int2(floor(tc));\n"
"\n"
"  float3 A = P(-1.,-1.);\n"
"  float3 B = P( 0.,-1.);\n"
"  float3 C = P( 1.,-1.);\n"
"  float3 D = P(-1., 0.);\n"
"  float3 E = P( 0., 0.);\n"
"  float3 F = P( 1., 0.);\n"
"  float3 G = P(-1., 1.);\n"
"  float3 H = P( 0., 1.);\n"
"  float3 I = P( 1., 1.);\n"
"\n"
"  // blendResult Mapping: x|y|\n"
"  //                      w|z|\n"
"  int4 blendResult = int4(BLEND_NONE,BLEND_NONE,BLEND_NONE,BLEND_NONE);\n"
"  \n"
"  // Preprocess corners\n"
"  // Pixel Tap Mapping: -|-|-|-|-\n"
"  //                    -|-|B|C|-\n"
"  //                    -|D|E|F|x\n"
"  //                    -|G|H|I|x\n"
"  //                    -|-|x|x|-\n"
"  if (!((eq(E,F) && eq(H,I)) || (eq(E,H) && eq(F,I))))\n"
"  {\n"
"    float dist_H_F = DistYCbCr(G, E) + DistYCbCr(E, C) + DistYCbCr(P(0,2), I) + DistYCbCr(I, P(2.,0.)) + (4.0 * DistYCbCr(H, F));\n"
"    float dist_E_I = DistYCbCr(D, H) + DistYCbCr(H, P(1,2)) + DistYCbCr(B, F) + DistYCbCr(F, P(2.,1.)) + (4.0 * DistYCbCr(E, I));\n"
"    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_H_F) < dist_E_I;\n"
"    blendResult.z = ((dist_H_F < dist_E_I) && neq(E,F) && neq(E,H)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;\n"
"  }\n"
"\n"
"\n"
"  // Pixel Tap Mapping: -|-|-|-|-\n"
"  //                    -|A|B|-|-\n"
"  //                    x|D|E|F|-\n"
"  //                    x|G|H|I|-\n"
"  //                    -|x|x|-|-\n"
"  if (!((eq(D,E) && eq(G,H)) || (eq(D,G) && eq(E,H))))\n"
"  {\n"
"    float dist_G_E = DistYCbCr(P(-2.,1.)  , D) + DistYCbCr(D, B) + DistYCbCr(P(-1.,2.), H) + DistYCbCr(H, F) + (4.0 * DistYCbCr(G, E));\n"
"    float dist_D_H = DistYCbCr(P(-2.,0.)  , G) + DistYCbCr(G, P(0.,2.)) + DistYCbCr(A, E) + DistYCbCr(E, I) + (4.0 * DistYCbCr(D, H));\n"
"    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_H) < dist_G_E;\n"
"    blendResult.w = ((dist_G_E > dist_D_H) && neq(E,D) && neq(E,H)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;\n"
"  }\n"
"\n"
"  // Pixel Tap Mapping: -|-|x|x|-\n"
"  //                    -|A|B|C|x\n"
"  //                    -|D|E|F|x\n"
"  //                    -|-|H|I|-\n"
"  //                    -|-|-|-|-\n"
"  if (!((eq(B,C) && eq(E,F)) || (eq(B,E) && eq(C,F))))\n"
"  {\n"
"    float dist_E_C = DistYCbCr(D, B) + DistYCbCr(B, P(1.,-2.)) + DistYCbCr(H, F) + DistYCbCr(F, P(2.,-1.)) + (4.0 * DistYCbCr(E, C));\n"
"    float dist_B_F = DistYCbCr(A, E) + DistYCbCr(E, I) + DistYCbCr(P(0.,-2.), C) + DistYCbCr(C, P(2.,0.)) + (4.0 * DistYCbCr(B, F));\n"
"    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_B_F) < dist_E_C;\n"
"    blendResult.y = ((dist_E_C > dist_B_F) && neq(E,B) && neq(E,F)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;\n"
"  }\n"
"\n"
"  // Pixel Tap Mapping: -|x|x|-|-\n"
"  //                    x|A|B|C|-\n"
"  //                    x|D|E|F|-\n"
"  //                    -|G|H|-|-\n"
"  //                    -|-|-|-|-\n"
"  if (!((eq(A,B) && eq(D,E)) || (eq(A,D) && eq(B,E))))\n"
"  {\n"
"    float dist_D_B = DistYCbCr(P(-2.,0.), A) + DistYCbCr(A, P(0.,-2.)) + DistYCbCr(G, E) + DistYCbCr(E, C) + (4.0 * DistYCbCr(D, B));\n"
"    float dist_A_E = DistYCbCr(P(-2.,-1.), D) + DistYCbCr(D, H) + DistYCbCr(P(-1.,-2.), B) + DistYCbCr(B, F) + (4.0 * DistYCbCr(A, E));\n"
"    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_B) < dist_A_E;\n"
"    blendResult.x = ((dist_D_B < dist_A_E) && neq(E,D) && neq(E,B)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;\n"
"  }\n"
"\n"
"  float3 res = E;\n"
"\n"
"  // Pixel Tap Mapping: -|-|-|-|-\n"
"  //                    -|-|B|C|-\n"
"  //                    -|D|E|F|x\n"
"  //                    -|G|H|I|x\n"
"  //                    -|-|x|x|-\n"
"  if(blendResult.z != BLEND_NONE)\n"
"  {\n"
"    float dist_F_G = DistYCbCr(F, G);\n"
"    float dist_H_C = DistYCbCr(H, C);\n"
"    bool doLineBlend = (blendResult.z == BLEND_DOMINANT ||\n"
"                !((blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) || (blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||\n"
"                  (IsPixEqual(G, H) && IsPixEqual(H, I) && IsPixEqual(I, F) && IsPixEqual(F, C) && !IsPixEqual(E, I))));\n"
"\n"
"    float2 origin = float2(0.0, 1.0 / sqrt(2.0));\n"
"    float2 direction = float2(1.0, -1.0);\n"
"    if(doLineBlend)\n"
"    {\n"
"      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_F_G <= dist_H_C) && neq(E,G) && neq(D,G);\n"
"      bool haveSteepLine = (STEEP_DIRECTION_THRESHOLD * dist_H_C <= dist_F_G) && neq(E,C) && neq(B,C);\n"
"      origin = haveShallowLine? float2(0.0, 0.25) : float2(0.0, 0.5);\n"
"      direction.x += haveShallowLine? 1.0: 0.0;\n"
"      direction.y -= haveSteepLine? 1.0: 0.0;\n"
"    }\n"
"\n"
"    float3 blendPix = lerp(H,F, step(DistYCbCr(E, F), DistYCbCr(E, H)));\n"
"    res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));\n"
"  }\n"
"\n"
"  // Pixel Tap Mapping: -|-|-|-|-\n"
"  //                    -|A|B|-|-\n"
"  //                    x|D|E|F|-\n"
"  //                    x|G|H|I|-\n"
"  //                    -|x|x|-|-\n"
"  if(blendResult.w != BLEND_NONE)\n"
"  {\n"
"    float dist_H_A = DistYCbCr(H, A);\n"
"    float dist_D_I = DistYCbCr(D, I);\n"
"    bool doLineBlend = (blendResult.w == BLEND_DOMINANT ||\n"
"                !((blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) || (blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||\n"
"                  (IsPixEqual(A, D) && IsPixEqual(D, G) && IsPixEqual(G, H) && IsPixEqual(H, I) && !IsPixEqual(E, G))));\n"
"\n"
"    float2 origin = float2(-1.0 / sqrt(2.0), 0.0);\n"
"    float2 direction = float2(1.0, 1.0);\n"
"    if(doLineBlend)\n"
"    {\n"
"      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_H_A <= dist_D_I) && neq(E,A) && neq(B,A);\n"
"      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_D_I <= dist_H_A) && neq(E,I) && neq(F,I);\n"
"      origin = haveShallowLine? float2(-0.25, 0.0) : float2(-0.5, 0.0);\n"
"      direction.y += haveShallowLine? 1.0: 0.0;\n"
"      direction.x += haveSteepLine? 1.0: 0.0;\n"
"    }\n"
"    origin = origin;\n"
"    direction = direction;\n"
"\n"
"    float3 blendPix = lerp(H,D, step(DistYCbCr(E, D), DistYCbCr(E, H)));\n"
"    res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));\n"
"  }\n"
"\n"
"  // Pixel Tap Mapping: -|-|x|x|-\n"
"  //                    -|A|B|C|x\n"
"  //                    -|D|E|F|x\n"
"  //                    -|-|H|I|-\n"
"  //                    -|-|-|-|-\n"
"  if(blendResult.y != BLEND_NONE)\n"
"  {\n"
"    float dist_B_I = DistYCbCr(B, I);\n"
"    float dist_F_A = DistYCbCr(F, A);\n"
"    bool doLineBlend = (blendResult.y == BLEND_DOMINANT ||\n"
"                !((blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) || (blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||\n"
"                  (IsPixEqual(I, F) && IsPixEqual(F, C) && IsPixEqual(C, B) && IsPixEqual(B, A) && !IsPixEqual(E, C))));\n"
"\n"
"    float2 origin = float2(1.0 / sqrt(2.0), 0.0);\n"
"    float2 direction = float2(-1.0, -1.0);\n"
"\n"
"    if(doLineBlend)\n"
"    {\n"
"      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_B_I <= dist_F_A) && neq(E,I) && neq(H,I);\n"
"      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_F_A <= dist_B_I) && neq(E,A) && neq(D,A);\n"
"      origin = haveShallowLine? float2(0.25, 0.0) : float2(0.5, 0.0);\n"
"      direction.y -= haveShallowLine? 1.0: 0.0;\n"
"      direction.x -= haveSteepLine? 1.0: 0.0;\n"
"    }\n"
"\n"
"    float3 blendPix = lerp(F,B, step(DistYCbCr(E, B), DistYCbCr(E, F)));\n"
"    res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));\n"
"  }\n"
"\n"
"  // Pixel Tap Mapping: -|x|x|-|-\n"
"  //                    x|A|B|C|-\n"
"  //                    x|D|E|F|-\n"
"  //                    -|G|H|-|-\n"
"  //                    -|-|-|-|-\n"
"  if(blendResult.x != BLEND_NONE)\n"
"  {\n"
"    float dist_D_C = DistYCbCr(D, C);\n"
"    float dist_B_G = DistYCbCr(B, G);\n"
"    bool doLineBlend = (blendResult.x == BLEND_DOMINANT ||\n"
"                !((blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) || (blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||\n"
"                  (IsPixEqual(C, B) && IsPixEqual(B, A) && IsPixEqual(A, D) && IsPixEqual(D, G) && !IsPixEqual(E, A))));\n"
"\n"
"    float2 origin = float2(0.0, -1.0 / sqrt(2.0));\n"
"    float2 direction = float2(-1.0, 1.0);\n"
"    if(doLineBlend)\n"
"    {\n"
"      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_D_C <= dist_B_G) && neq(E,C) && neq(F,C);\n"
"      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_B_G <= dist_D_C) && neq(E,G) && neq(H,G);\n"
"      origin = haveShallowLine? float2(0.0, -0.25) : float2(0.0, -0.5);\n"
"      direction.x -= haveShallowLine? 1.0: 0.0;\n"
"      direction.y += haveSteepLine? 1.0: 0.0;\n"
"    }\n"
"\n"
"    float3 blendPix = lerp(D,B, step(DistYCbCr(E, B), DistYCbCr(E, D)));\n"
"    res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));\n"
"  }\n"
"\n"
"   return float4(res, 1.0);\n"
"}\n"
"\n"
"float4 ps_xbrz(DSO v) : SV_Target {\n"
"    float3 c = xbrz(v.uv).rgb;\n"
"    if (params.x > 0.5 && (int(v.p.y) & 1) == 1) c = (params.x < 1.5) ? float3(0, 0, 0) : c * 0.6;\n"
"    return float4(c, 1.0);\n"
"}\n"
"\n"
"/* xBRZ for 2D objects only: srcTex is the picture at the console resolution with a 2D marker in its alpha, hiTex the high resolution one */\n"
"Texture2D<float4> hiTex : register(t2);\n"
"float4 ps_mix(DSO v) : SV_Target {\n"
"    float2 tc = region.xy + v.uv * region.zw;\n"
"    float m = srcTex.Load(int3(int2(floor(tc)), 0)).a;\n"
"    float3 c;\n"
"    if (m > 0.5) c = xbrz(v.uv).rgb;\n"
"    else {\n"
"        c = hiTex.SampleLevel(smp, tc / 1024.0, 0).rgb;\n"
"        if (params.y > 0.0) { int st = int(params.z); c += ditherDelta(hiTex, int2(floor(tc)) * st + st / 2, st); }\n"
"    }\n"
"    if (params.x > 0.5 && (int(v.p.y) & 1) == 1) c = (params.x < 1.5) ? float3(0, 0, 0) : c * 0.6;\n"
"    return float4(c, 1.0);\n"
"}\n"

"\n"
"/* bezel image over the picture: straight alpha */\n"
"float4 ps_bezel(DSO v) : SV_Target { return srcTex.SampleLevel(smp, v.uv, 0); }\n"
"\n"
"/* ---------------------------------------------------------------- FXAA (after Timothy Lottes' FXAA 2: edge detection by luma and a blur along the edge) */\n"
"float fxLuma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }\n"
"float4 ps_fxaa(DSO v) : SV_Target {\n"
"    float2 px = params.yz;   /* one pixel in uv units */\n"
"    float3 nw = srcTex.SampleLevel(smp, v.uv + float2(-1.0, -1.0) * px, 0).rgb;\n"
"    float3 ne = srcTex.SampleLevel(smp, v.uv + float2( 1.0, -1.0) * px, 0).rgb;\n"
"    float3 sw = srcTex.SampleLevel(smp, v.uv + float2(-1.0,  1.0) * px, 0).rgb;\n"
"    float3 se = srcTex.SampleLevel(smp, v.uv + float2( 1.0,  1.0) * px, 0).rgb;\n"
"    float3 m  = srcTex.SampleLevel(smp, v.uv, 0).rgb;\n"
"    float lNW = fxLuma(nw), lNE = fxLuma(ne), lSW = fxLuma(sw), lSE = fxLuma(se), lM = fxLuma(m);\n"
"    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
"    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
"    float2 dir;\n"
"    dir.x = -((lNW + lNE) - (lSW + lSE));\n"
"    dir.y =  ((lNW + lSW) - (lNE + lSE));\n"
"    float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * (1.0 / 8.0)), 1.0 / 128.0);\n"
"    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n"
"    dir = min(float2(8.0, 8.0), max(float2(-8.0, -8.0), dir * rcpDirMin)) * px;\n"
"    float3 rgbA = 0.5 * (srcTex.SampleLevel(smp, v.uv + dir * (1.0 / 3.0 - 0.5), 0).rgb + srcTex.SampleLevel(smp, v.uv + dir * (2.0 / 3.0 - 0.5), 0).rgb);\n"
"    float3 rgbB = rgbA * 0.5 + 0.25 * (srcTex.SampleLevel(smp, v.uv + dir * -0.5, 0).rgb + srcTex.SampleLevel(smp, v.uv + dir * 0.5, 0).rgb);\n"
"    float lB = fxLuma(rgbB);\n"
"    return float4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);\n"
"}\n"
"\n";

/* ------------------------------------------------------------------ configuration (renderer.cfg) */
typedef struct {
    int xsize, ysize, fullscreen, depth, scanlines, filtering, dithering;
    int showfps, framelimit, frameskip, ratedetect, ratemanual, keepaspect, iscale, xbrz, logging, fxaa, dedither, vsync, borderless, crop, texsmooth;
} Cfg;
static Cfg cfg = {640, 480, 0, 32, 0, 1, 1, 1, 1, 0, 1, 60, 0, 0, 2, 0, 0, 0, 0, 0, 0, 1};   /* TextureSmoothing (0 / 1) is on by default; FXAA (0 / 1), Dedither (0-3), VSync and Borderless (0 / 1) are off by default; InternalScale (default 0 = auto), XBRZ (default 2 = xBRZ for 2D objects only), Logging (default off) */
static char cfgPath[260];
static char bezelPath[520];   /* BezelImage: a PNG with a transparent window for the picture (empty = none) */
static char gameName[64];   /* from ZN_GPUopen: names the screenshot folder like the OpenGL renderer does */

#define REL(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = 0; } } while (0)
static void load_cfg(const char *path) {
    FILE *f;
    char line[256];
    if (!path || !path[0] || !(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        char *semi = strchr(line, ';'), *eq = strchr(line, '=');
        char key[64];
        int n = 0, v;
        if (semi) *semi = 0;
        if (!eq) continue;
        for (char *p = line; p < eq && n < 63; p++) if (*p != ' ' && *p != '\t') key[n++] = *p;
        key[n] = 0;
        v = atoi(eq + 1);
        if (!_stricmp(key, "BezelImage")) {   /* the only text value: the rest of the line without blanks around it */
            char *a = eq + 1, *b;
            while (*a == ' ' || *a == '\t') a++;
            b = a + strlen(a);
            while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n')) b--;
            *b = 0;
            if (strlen(a) < sizeof bezelPath) strcpy(bezelPath, a);
            continue;
        }
        if (!_stricmp(key, "XSize")) cfg.xsize = v;
        else if (!_stricmp(key, "YSize")) cfg.ysize = v;
        else if (!_stricmp(key, "FullScreen")) cfg.fullscreen = v;
        else if (!_stricmp(key, "ColorDepth")) cfg.depth = v;
        else if (!_stricmp(key, "ScanLines")) cfg.scanlines = v;
        else if (!_stricmp(key, "Filtering")) cfg.filtering = v;
        else if (!_stricmp(key, "Dithering")) cfg.dithering = v;
        else if (!_stricmp(key, "ShowFPS")) cfg.showfps = v;
        else if (!_stricmp(key, "FrameLimitation")) cfg.framelimit = v;
        else if (!_stricmp(key, "FrameSkipping")) cfg.frameskip = v;
        else if (!_stricmp(key, "FramerateDetection")) cfg.ratedetect = v;
        else if (!_stricmp(key, "FramerateManual")) cfg.ratemanual = v;
        else if (!_stricmp(key, "KeepAspect")) cfg.keepaspect = v;
        else if (!_stricmp(key, "InternalScale")) cfg.iscale = v;
        else if (!_stricmp(key, "XBRZ")) cfg.xbrz = v;
        else if (!_stricmp(key, "Logging")) cfg.logging = v;
        else if (!_stricmp(key, "FXAA")) cfg.fxaa = v;
        else if (!_stricmp(key, "VSync")) cfg.vsync = v;
        else if (!_stricmp(key, "Borderless")) cfg.borderless = v;
        else if (!_stricmp(key, "Dedither")) cfg.dedither = v;
        else if (!_stricmp(key, "TextureSmoothing")) cfg.texsmooth = v ? 1 : 0;
        else if (!_stricmp(key, "Overscan")) cfg.crop = v;
    }
    fclose(f);
    if (cfg.keepaspect < 0 || cfg.keepaspect > 3) cfg.keepaspect = cfg.keepaspect ? 1 : 0;
    cfg.fxaa = cfg.fxaa != 0; cfg.vsync = cfg.vsync != 0; cfg.borderless = cfg.borderless != 0;
    if (cfg.dedither < 0 || cfg.dedither > 3) cfg.dedither = 0;
    if (cfg.crop < 0) cfg.crop = 0;
    if (cfg.crop > 64) cfg.crop = 64;
    if (cfg.xsize < 160) cfg.xsize = 160;
    if (cfg.ysize < 120) cfg.ysize = 120;
    if (cfg.xsize > 7680) cfg.xsize = 7680;
    if (cfg.ysize > 4320) cfg.ysize = 4320;
}
/* ------------------------------------------------------------------ GPU state */
#define VRAM_W 1024
static u16 *vram;          /* VRAM_W x 1024 pixels: ZN boards have 2 MB of VRAM */
static int VH = 1024;
static int rectFlipX, rectFlipY;
static int wide;           /* register layout of the extended ZN GPU (Y fields 2 bits higher); VRAM is 1024 lines either way */
static u32 status = 0x14802000;

/* draw environment */
static int daX1, daY1, daX2 = 1023, daY2 = 511;
static int offX, offY;
static int tpx, tpy, tpd, abr, dither, swz;   /* swz: wide layout, texture page bit 13 - 4/8-bit textures are stored in tiles */
static int twMX, twMY, twOX, twOY;
static int setMask, checkMask;
static u32 rawE2, rawE3, rawE4, rawE5;

/* display */
static int dispX, dispY;
static int hrX1 = 0x260, hrX2 = 0xC60, vrY1 = 0x10, vrY2 = 0x100;

/* GP0 command assembly */
static u32 cmd[1024];
static int cmdN, cmdNeed, cmdOp;
static const u8 lenTable[256] = {
    0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    4,4,4,4,7,7,7,7,5,5,5,5,9,9,9,9, 6,6,6,6,9,9,9,9,8,8,8,8,12,12,12,12,
    3,3,3,3,0,0,0,0,254,254,254,254,254,254,254,254, 4,4,4,4,0,0,0,0,255,255,255,255,255,255,255,255,
    3,3,3,3,4,4,4,4,2,2,2,2,0,0,0,0, 2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,
    4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

/* VRAM <-> CPU image transfer */
static int imgMode;                /* 0 idle, 1 CPU->VRAM, 2 VRAM->CPU */
static int imgX, imgY, imgW, imgH, imgCX, imgCY;
static u32 dataRet;

static inline int sx11(u32 v) { return (int)((v & 0x7ff) ^ 0x400) - 0x400; }
static inline int vmaskY(int y) { return y & (VH - 1); }


/* ------------------------------------------------------------------ Direct3D 11 objects */
typedef struct { float x, y, u, v; u8 c[4]; u32 info[4]; } Vtx;       /* 36 bytes */
typedef struct { float x, y, u, v; } DVtx;
#define MAXV 49152
#define NSTATES 5   /* 0 opaque, 1 semi mode 0 (B/2+F/2), 2 semi mode 1 (B+F), 3 semi mode 3 (B+F/4), 4 semi mode 2 (B-F) */

static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static IDXGISwapChain *sc;
static ID3D11RenderTargetView *bbRTV;
static int bbW, bbH;
static ID3D11Texture2D *scaledTex;
static ID3D11RenderTargetView *scaledRTV;
static ID3D11Texture2D *nativeTex;                    /* xBRZ "2D only": the picture at the console resolution, alpha = drawn by a 2D object */
static ID3D11RenderTargetView *nativeRTV;
static ID3D11ShaderResourceView *nativeSRV;
static int cur2d;                                     /* the primitives being queued are 2D objects */
static ID3D11ShaderResourceView *scaledSRV;
static ID3D11Texture2D *vramGpu, *dispTex, *fpsTex;   /* fpsTex: the frame rate counter text */
static ID3D11ShaderResourceView *fpsSRV;
static int fpsDrawn = -1;
static ID3D11ShaderResourceView *vramSRV, *dispSRV;
static ID3D11Buffer *vbDraw, *vbDisp, *cbDisp;
static ID3D11InputLayout *ilDraw, *ilDisp;
static ID3D11VertexShader *vsDraw, *vsDisp;
static ID3D11PixelShader *psDraw, *psDisp, *psXbrz, *psMix, *psFxaa;
static ID3D11Texture2D *fxTex;                         /* FXAA: the picture before the filter, window sized */
static ID3D11RenderTargetView *fxRTV;
static ID3D11ShaderResourceView *fxSRV;
static int fxW, fxH;
static ID3D11BlendState *bstate[NSTATES];
static ID3D11RasterizerState *rsScissor;
static ID3D11SamplerState *smpPoint, *smpLinear;
static ID3D11Texture2D *bezTex;                        /* bezel image */
static ID3D11ShaderResourceView *bezSRV;
static ID3D11PixelShader *psBezel;
static ID3D11BlendState *bsAlpha;
static int bezW, bezH;
static float bezHole[4];                               /* the transparent window of the bezel: x y w h as fractions of the image */
static unsigned gOpenMode;
static int N = 2;                      /* internal scale: the scaled VRAM is (1024 * N) squared */
static Vtx queue[MAXV];
static int qn, curKey = -1, fullScissor;
/* ------------------------------------------------------------------ log (zinc-d3d11.log, only with Logging = 1)
 * One session per file: system and graphics card details, the settings in use, events (window, fullscreen, display
 * mode changes, device errors), one statistics line per second, a diagnosis line for every hitch (a frame that took
 * far longer than it should) and a summary when the game closes. Every line starts with the seconds since the start. */
static char logPath[260];
static FILE *logF;
static DWORD logT0;
static long logBytes;
#define LOG_MAX (4L * 1024 * 1024)

static void logmsg(const char *fmt, ...) {
    va_list ap;
    DWORD t;
    int n;
    if (!logF) return;
    if (logBytes > LOG_MAX) {
        if (logBytes != LOG_MAX + 1) { fputs("(log size limit reached: the rest is not written)\n", logF); fflush(logF); logBytes = LOG_MAX + 1; }
        return;
    }
    t = GetTickCount() - logT0;
    n = fprintf(logF, "[%4lu.%03lu] ", (unsigned long)(t / 1000), (unsigned long)(t % 1000));
    va_start(ap, fmt); n += vfprintf(logF, fmt, ap); va_end(ap);
    fputc('\n', logF); fflush(logF);
    logBytes += n + 1;
}

/* what happened since the last statistics line, and for the whole session */
static double accDrawMs, maxCallMs;                    /* time spent in the game's GPU commands, and the slowest single call */
static double frameDrawMs, framePresentMs, frameWaitMs;/* the same, for the frame in progress */
static long statDraws, statVerts, statUploads, statUploadPx, statBlits;
static unsigned long totFrames, hitchCount, over20, over40, over100;
static int hitchLogged, hitchSecond, modeLogs, resetLogs;
static double worstFrameMs;
static DWORD sessionT0;
static unsigned histo[402];                            /* frame times in steps of 0.5 ms, for the 1% low */
static HRESULT lastPresentHr;
static int usedWarp;

typedef HRESULT (WINAPI *PFN_COMPILE)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3D10Blob **, ID3D10Blob **);

static ID3D10Blob *compile(PFN_COMPILE fn, const char *entry, const char *target) {
    ID3D10Blob *code = 0, *err = 0;
    HRESULT hr = fn(HLSL, strlen(HLSL), "zn.hlsl", NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr)) {
        logmsg("shader %s failed: %08lx %s", entry, (unsigned long)hr, err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "");
        if (err) ID3D10Blob_Release(err);
        return 0;
    }
    if (err) ID3D10Blob_Release(err);
    return code;
}

#define BP(b) ID3D10Blob_GetBufferPointer(b)
#define BS(b) ID3D10Blob_GetBufferSize(b)

static void load_bezel(void);

static int make_shaders(void) {
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    PFN_COMPILE fn;
    ID3D10Blob *bvd, *bpd, *bvp, *bpp, *bpx, *bpm;
    D3D11_INPUT_ELEMENT_DESC ld[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"UV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COL", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"INFO", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    D3D11_INPUT_ELEMENT_DESC ld2[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"UV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (!m) m = LoadLibraryA("d3dcompiler_43.dll");
    if (!m || !(fn = (PFN_COMPILE)GetProcAddress(m, "D3DCompile"))) { logmsg("no d3dcompiler"); return 0; }
    { char mn[MAX_PATH]; if (GetModuleFileNameA(m, mn, sizeof mn)) logmsg("shader compiler: %s", strrchr(mn, '\\') ? strrchr(mn, '\\') + 1 : mn); }
    bvd = compile(fn, "vs_draw", "vs_4_0"); bpd = compile(fn, "ps_draw", "ps_4_0");
    bvp = compile(fn, "vs_disp", "vs_4_0"); bpp = compile(fn, "ps_disp", "ps_4_0");
    bpx = compile(fn, "ps_xbrz", "ps_4_0");
    bpm = compile(fn, "ps_mix", "ps_4_0");
    if (bezelPath[0]) {
        ID3D10Blob *bpb = compile(fn, "ps_bezel", "ps_4_0");
        if (!bpb || FAILED(ID3D11Device_CreatePixelShader(dev, BP(bpb), BS(bpb), NULL, &psBezel))) logmsg("bezel: the shader could not be built");
    }
    if (cfg.fxaa) {
        ID3D10Blob *bpf = compile(fn, "ps_fxaa", "ps_4_0");
        if (bpf && SUCCEEDED(ID3D11Device_CreatePixelShader(dev, BP(bpf), BS(bpf), NULL, &psFxaa))) logmsg("FXAA: on");
        else { cfg.fxaa = 0; logmsg("FXAA: the shader could not be built, FXAA is off"); }
    }
    if (!bpm || FAILED(ID3D11Device_CreatePixelShader(dev, BP(bpm), BS(bpm), NULL, &psMix))) return 0;
    if (!bvd || !bpd || !bvp || !bpp || !bpx) return 0;
    if (FAILED(ID3D11Device_CreatePixelShader(dev, BP(bpx), BS(bpx), NULL, &psXbrz))) return 0;
    if (FAILED(ID3D11Device_CreateVertexShader(dev, BP(bvd), BS(bvd), NULL, &vsDraw))) return 0;
    if (FAILED(ID3D11Device_CreatePixelShader(dev, BP(bpd), BS(bpd), NULL, &psDraw))) return 0;
    if (FAILED(ID3D11Device_CreateVertexShader(dev, BP(bvp), BS(bvp), NULL, &vsDisp))) return 0;
    if (FAILED(ID3D11Device_CreatePixelShader(dev, BP(bpp), BS(bpp), NULL, &psDisp))) return 0;
    if (FAILED(ID3D11Device_CreateInputLayout(dev, ld, 4, BP(bvd), BS(bvd), &ilDraw))) return 0;
    if (FAILED(ID3D11Device_CreateInputLayout(dev, ld2, 2, BP(bvp), BS(bvp), &ilDisp))) return 0;
    return 1;
}

static int make_states(void) {
    int i;
    D3D11_BLEND_DESC bd;
    D3D11_RASTERIZER_DESC rd;
    D3D11_SAMPLER_DESC sd;
    for (i = 0; i < NSTATES; i++) {
        memset(&bd, 0, sizeof bd);
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        if (i == 1) { bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC1_COLOR; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC1_COLOR; }
        if (i == 2) { bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC1_COLOR; }
        if (i == 3) { bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC1_COLOR; bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC1_ALPHA; }
        if (i == 4) { bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE; }
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(ID3D11Device_CreateBlendState(dev, &bd, &bstate[i]))) return 0;
    }
    memset(&rd, 0, sizeof rd);
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
    if (FAILED(ID3D11Device_CreateRasterizerState(dev, &rd, &rsScissor))) return 0;
    memset(&sd, 0, sizeof sd);
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(ID3D11Device_CreateSamplerState(dev, &sd, &smpPoint))) return 0;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(ID3D11Device_CreateSamplerState(dev, &sd, &smpLinear))) return 0;
    return 1;
}

static int make_backbuffer(void) {
    ID3D11Texture2D *bb = 0;
    if (FAILED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb))) return 0;
    if (FAILED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, NULL, &bbRTV))) { ID3D11Texture2D_Release(bb); return 0; }
    ID3D11Texture2D_Release(bb);
    return 1;
}

static int make_gpu(HWND h, int clientW, int clientH) {
    D3D_FEATURE_LEVEL lv[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0}, got;
    DXGI_SWAP_CHAIN_DESC scd;
    IDXGIDevice *dxd = 0;
    IDXGIAdapter *ad = 0;
    IDXGIFactory *fac = 0;
    D3D11_TEXTURE2D_DESC td;
    D3D11_BUFFER_DESC bd;
    HRESULT hr;
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, lv, 3, D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (FAILED(hr)) {
        logmsg("hardware device failed (%08lx), trying the WARP software renderer", (unsigned long)hr);
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, lv, 3, D3D11_SDK_VERSION, &dev, &got, &ctx);
        usedWarp = 1;
    }
    if (FAILED(hr)) { logmsg("D3D11CreateDevice failed %08lx", (unsigned long)hr); return 0; }
    logmsg("Direct3D 11 device created: feature level %x%s", (unsigned)got, usedWarp ? " (WARP software renderer: slow)" : " (hardware)");
    if (FAILED(ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void **)&dxd))) return 0;
    IDXGIDevice_GetParent(dxd, &IID_IDXGIAdapter, (void **)&ad);
    IDXGIAdapter_GetParent(ad, &IID_IDXGIFactory, (void **)&fac);
    {   /* every graphics adapter, and which one runs this (a hybrid or remote setup may not use the expected card) */
        DXGI_ADAPTER_DESC used;
        IDXGIAdapter *a = 0;
        UINT i;
        memset(&used, 0, sizeof used);
        if (ad) IDXGIAdapter_GetDesc(ad, &used);
        for (i = 0; IDXGIFactory_EnumAdapters(fac, i, &a) == S_OK; i++) {
            DXGI_ADAPTER_DESC d;
            LARGE_INTEGER ver;
            char drv[48] = "unknown";
            if (SUCCEEDED(IDXGIAdapter_GetDesc(a, &d))) {
                if (SUCCEEDED(IDXGIAdapter_CheckInterfaceSupport(a, &IID_IDXGIDevice, &ver))) {
                    unsigned p1 = HIWORD(ver.HighPart), p2 = LOWORD(ver.HighPart), p3 = HIWORD(ver.LowPart), p4 = LOWORD(ver.LowPart);
                    snprintf(drv, sizeof drv, "%u.%u.%u.%u", p1, p2, p3, p4);
                    if (d.VendorId == 0x10de) { unsigned v = (p3 % 10) * 10000 + p4; snprintf(drv + strlen(drv), sizeof drv - strlen(drv), " (NVIDIA %u.%02u)", v / 100, v % 100); }
                }
                logmsg("adapter %u%s: %ls | vendor %04x device %04x | video memory %lu MB, shared %lu MB | driver %s", i, d.AdapterLuid.LowPart == used.AdapterLuid.LowPart && d.AdapterLuid.HighPart == used.AdapterLuid.HighPart ? " (in use)" : "",
                       d.Description, d.VendorId, d.DeviceId, (unsigned long)(d.DedicatedVideoMemory >> 20), (unsigned long)(d.SharedSystemMemory >> 20), drv);
            }
            IDXGIAdapter_Release(a);
        }
    }
    hr = E_FAIL;
    {   /* the modern flip model first (no copy by the desktop compositor, smoother pacing); the old bit-blit model as fallback */
        IDXGIFactory2 *fac2 = NULL;
        if (SUCCEEDED(IDXGIFactory_QueryInterface(fac, &IID_IDXGIFactory2, (void **)&fac2))) {
            DXGI_SWAP_CHAIN_DESC1 d1;
            IDXGISwapChain1 *sc1 = NULL;
            memset(&d1, 0, sizeof d1);
            d1.Width = clientW; d1.Height = clientH;
            d1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d1.SampleDesc.Count = 1;
            d1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            d1.BufferCount = 2;
            d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            hr = IDXGIFactory2_CreateSwapChainForHwnd(fac2, (IUnknown *)dev, h, &d1, NULL, NULL, &sc1);
            if (SUCCEEDED(hr)) {
                hr = IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain, (void **)&sc);
                IDXGISwapChain1_Release(sc1);
            }
            IDXGIFactory2_Release(fac2);
            if (SUCCEEDED(hr)) logmsg("swap chain: flip model, %dx%d, 2 buffers, one queued frame at most", clientW, clientH);
        }
    }
    if (FAILED(hr)) {
        logmsg("flip model swap chain not available (%08lx)", (unsigned long)hr);
        sc = NULL;
        memset(&scd, 0, sizeof scd);
        scd.BufferDesc.Width = clientW; scd.BufferDesc.Height = clientH;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 1; scd.OutputWindow = h; scd.Windowed = TRUE; scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = IDXGIFactory_CreateSwapChain(fac, (IUnknown *)dev, &scd, &sc);
        if (SUCCEEDED(hr)) logmsg("swap chain: bit-blit model (the flip model was not available), %dx%d", clientW, clientH);
    }
    if (SUCCEEDED(hr)) {
        IDXGIDevice1 *d1 = NULL;
        IDXGIFactory_MakeWindowAssociation(fac, h, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        if (SUCCEEDED(IDXGIDevice_QueryInterface(dxd, &IID_IDXGIDevice1, (void **)&d1))) {   /* at most one queued frame: no extra delay */
            IDXGIDevice1_SetMaximumFrameLatency(d1, 1);
            IDXGIDevice1_Release(d1);
        }
    }
    IDXGIFactory_Release(fac); IDXGIAdapter_Release(ad); IDXGIDevice_Release(dxd);
    if (FAILED(hr)) { logmsg("CreateSwapChain failed %08lx", (unsigned long)hr); return 0; }
    bbW = clientW; bbH = clientH;
    if (!make_backbuffer()) return 0;
    if (!make_shaders() || !make_states()) return 0;
    load_bezel();
    memset(&td, 0, sizeof td);
    td.Width = td.Height = 1024 * N; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &scaledTex))) { logmsg("scaled VRAM texture failed"); return 0; }
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)scaledTex, NULL, &scaledRTV);
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)scaledTex, NULL, &scaledSRV);
    if (cfg.xbrz == 2) {
        D3D11_TEXTURE2D_DESC nd = td;
        nd.Width = nd.Height = 1024;
        if (FAILED(ID3D11Device_CreateTexture2D(dev, &nd, NULL, &nativeTex))) { logmsg("native picture texture failed"); return 0; }
        ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)nativeTex, NULL, &nativeRTV);
        ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)nativeTex, NULL, &nativeSRV);
    }
    td.Width = td.Height = 1024; td.Format = DXGI_FORMAT_R16_UINT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &vramGpu))) return 0;
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)vramGpu, NULL, &vramSRV);
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_DYNAMIC; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &dispTex))) return 0;
    ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)dispTex, NULL, &dispSRV);
    memset(&bd, 0, sizeof bd);
    bd.ByteWidth = sizeof(Vtx) * MAXV; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &vbDraw))) return 0;
    bd.ByteWidth = sizeof(DVtx) * 6;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &vbDisp))) return 0;
    bd.ByteWidth = 48; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &bd, NULL, &cbDisp))) return 0;
    {
        float black[4] = {0, 0, 0, 1};
        float clear0[4] = {0, 0, 0, 0};
        ID3D11DeviceContext_ClearRenderTargetView(ctx, scaledRTV, black);
        if (nativeRTV) ID3D11DeviceContext_ClearRenderTargetView(ctx, nativeRTV, clear0);
    }
    return 1;
}


static void destroy_gpu(void) {
    int i;
    if (ctx) ID3D11DeviceContext_ClearState(ctx);
    REL(cbDisp); REL(vbDisp); REL(vbDraw); REL(dispSRV); REL(dispTex); REL(fpsSRV); REL(fpsTex); fpsDrawn = -1; REL(vramSRV); REL(vramGpu);
    REL(fxSRV); REL(fxRTV); REL(fxTex); REL(psFxaa); fxW = fxH = 0;
    REL(nativeSRV); REL(nativeRTV); REL(nativeTex); REL(scaledSRV); REL(scaledRTV); REL(scaledTex); REL(smpLinear); REL(smpPoint); REL(bezSRV); REL(bezTex); REL(psBezel); REL(bsAlpha); REL(rsScissor);
    for (i = 0; i < NSTATES; i++) REL(bstate[i]);
    REL(ilDisp); REL(ilDraw); REL(psMix); REL(psXbrz); REL(psDisp); REL(vsDisp); REL(psDraw); REL(vsDraw); REL(bbRTV); REL(sc); REL(ctx); REL(dev);
}

/* ------------------------------------------------------------------ batching */
#define IF_TEX 1u
#define IF_RAW 2u
#define IF_BLIT 4u
#define IF_SEMI 128u
#define IF_ALLSEMI 1024u
#define IF_2D 2048u

static void flush(void) {
    D3D11_MAPPED_SUBRESOURCE ms;
    D3D11_VIEWPORT vp;
    D3D11_RECT sr;
    UINT stride = sizeof(Vtx), off = 0;
    float bf[4] = {0, 0, 0, 0};
    ID3D11ShaderResourceView *none = 0;
    int pass, passes = nativeRTV ? 2 : 1;
    if (qn == 0 || !ctx) { qn = 0; curKey = -1; return; }
    if (!fullScissor && (daX2 < daX1 || daY2 < daY1)) { qn = 0; curKey = -1; return; }
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)vbDraw, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, queue, sizeof(Vtx) * (size_t)qn);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)vbDraw, 0);
    }
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &none);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 2, 1, &none);
    ID3D11DeviceContext_RSSetState(ctx, rsScissor);
    ID3D11DeviceContext_OMSetBlendState(ctx, bstate[curKey < 0 ? 0 : curKey], bf, 0xffffffffu);
    ID3D11DeviceContext_IASetInputLayout(ctx, ilDraw);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbDraw, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsDraw, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, psDraw, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &vramSRV);
    for (pass = 0; pass < passes; pass++) {       /* pass 0: the scaled VRAM, pass 1: the console-resolution picture with the 2D marker */
        int S = pass == 0 ? N : 1;
        vp.TopLeftX = vp.TopLeftY = 0; vp.Width = vp.Height = (float)(1024 * S); vp.MinDepth = 0; vp.MaxDepth = 1;
        if (fullScissor) { sr.left = 0; sr.top = 0; sr.right = 1024 * S; sr.bottom = 1024 * S; }
        else {
            int x1 = daX1, y1 = daY1, x2 = daX2, y2 = daY2;
            if (x2 > 1023) x2 = 1023;
            if (y2 > 1023) y2 = 1023;
            sr.left = x1 * S; sr.top = y1 * S; sr.right = (x2 + 1) * S; sr.bottom = (y2 + 1) * S;
        }
        ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, pass == 0 ? &scaledRTV : &nativeRTV, NULL);
        ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
        ID3D11DeviceContext_RSSetScissorRects(ctx, 1, &sr);
        ID3D11DeviceContext_Draw(ctx, (UINT)qn, 0);
        statDraws++;
    }
    statVerts += qn;
    qn = 0; curKey = -1;
}

static void emit_tri(const Vtx *a, const Vtx *b, const Vtx *c, int key) {
    if (key != curKey || qn + 3 > MAXV) flush();
    curKey = key;
    queue[qn++] = *a; queue[qn++] = *b; queue[qn++] = *c;
}

/* ---- CPU-side VRAM changes that must reach the GPU */
typedef struct { int x0, y0, x1, y1, on; } Box;
static Box texDirty, blitDirty;

static void box_add(Box *b, int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > 1024) x1 = 1024;
    if (y1 > 1024) y1 = 1024;
    if (x1 <= x || y1 <= y) return;
    if (!b->on) { b->x0 = x; b->y0 = y; b->x1 = x1; b->y1 = y1; b->on = 1; return; }
    if (x < b->x0) b->x0 = x;
    if (y < b->y0) b->y0 = y;
    if (x1 > b->x1) b->x1 = x1;
    if (y1 > b->y1) b->y1 = y1;
}

static void quad_blit(int x, int y, int w, int h) {
    Vtx v[4];
    int i;
    float fx[4] = {(float)x, (float)(x + w), (float)x, (float)(x + w)}, fy[4] = {(float)y, (float)y, (float)(y + h), (float)(y + h)};
    for (i = 0; i < 4; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].x = fx[i]; v[i].y = fy[i]; v[i].u = fx[i]; v[i].v = fy[i];
        v[i].c[0] = v[i].c[1] = v[i].c[2] = v[i].c[3] = 255;
        v[i].info[2] = IF_BLIT;
    }
    emit_tri(&v[0], &v[1], &v[2], 0); emit_tri(&v[1], &v[3], &v[2], 0);
}

static void quad_solid(int x, int y, int w, int h, int r, int g, int b) {
    Vtx v[4];
    int i;
    float fx[4] = {(float)x, (float)(x + w), (float)x, (float)(x + w)}, fy[4] = {(float)y, (float)y, (float)(y + h), (float)(y + h)};
    for (i = 0; i < 4; i++) {
        memset(&v[i], 0, sizeof v[i]);
        v[i].x = fx[i]; v[i].y = fy[i];
        v[i].c[0] = (u8)r; v[i].c[1] = (u8)g; v[i].c[2] = (u8)b; v[i].c[3] = 255;
        v[i].info[2] = IF_2D;
    }
    emit_tri(&v[0], &v[1], &v[2], 0); emit_tri(&v[1], &v[3], &v[2], 0);
}

/* uploads CPU VRAM changes (texture data) and copies image uploads into the scaled VRAM, in order with the drawing */
static void sync_dirty(void) {
    if (!ctx || (!texDirty.on && !blitDirty.on)) return;
    flush();
    if (texDirty.on) {
        D3D11_BOX bx;
        bx.left = (UINT)texDirty.x0; bx.top = (UINT)texDirty.y0; bx.right = (UINT)texDirty.x1; bx.bottom = (UINT)texDirty.y1; bx.front = 0; bx.back = 1;
        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)vramGpu, 0, &bx,
                                              (const u8 *)vram + ((size_t)texDirty.y0 * VRAM_W + (size_t)texDirty.x0) * 2, VRAM_W * 2, 0);
        statUploads++; statUploadPx += (texDirty.x1 - texDirty.x0) * (texDirty.y1 - texDirty.y0);
        texDirty.on = 0;
    }
    if (blitDirty.on) {
        statBlits++;
        fullScissor = 1;
        quad_blit(blitDirty.x0, blitDirty.y0, blitDirty.x1 - blitDirty.x0, blitDirty.y1 - blitDirty.y0);
        flush();
        fullScissor = 0;
        blitDirty.on = 0;
    }
}

/* everything queued so far is drawn and all CPU-side VRAM changes are on the GPU */
static void sync_all(void) {
    flush();
    sync_dirty();
}

/* ------------------------------------------------------------------ primitives */
static unsigned pack_flags(int tex, int raw, int semi, int pass, int blendAll) {
    return (tex ? IF_TEX : 0) | (raw ? IF_RAW : 0) | ((unsigned)tpd << 3) | ((unsigned)pass << 5) | (semi ? IF_SEMI : 0) | ((unsigned)(abr & 3) << 8) |
           ((semi && tex && blendAll && abr != 2) ? IF_ALLSEMI : 0) | (cur2d ? IF_2D : 0) | (swz ? 4096u : 0) | (cfg.texsmooth && !cur2d ? 8192u : 0);
}

/* Smart blending: on the console only texels with the "semi-transparent" bit are blended in a semi-transparent surface.
 * A texture that has no such texel at all and fills its whole area (fog, smoke) would then be drawn solid, which is pointless,
 * so such a surface is blended as a whole. Lettering and sprites (empty texels around them) keep the console rule. */
static double accBlendMs;
static int accReadbacks;
static LARGE_INTEGER qpf, qNext;   /* (also used by the frame pacing below) */
static int smart_blend_impl(const Vtx *v, int n, int tex, int semi, int clut, int raw) {
    int i, u0 = 255, u1 = 0, v0 = 255, v1 = 0, x, y;
    long nonzero = 0, stp = 0;
    if (!tex || !semi || !vram) return 0;
    for (i = 0; i < n; i++) {
        int a = (int)floorf(v[i].u), b = (int)ceilf(v[i].u), c = (int)floorf(v[i].v), d = (int)ceilf(v[i].v);
        if (a < 0) a = 0;
        if (b > 255) b = 255;
        if (c < 0) c = 0;
        if (d > 255) d = 255;
        if (a < u0) u0 = a;
        if (b > u1) u1 = b;
        if (c < v0) v0 = c;
        if (d > v1) v1 = d;
    }
    if (u1 < u0 || v1 < v0) return 0;
    if (abr == 2 || twMX || twMY) return 0;
    for (y = v0; y <= v1; y++) {
        const u16 *row = vram + (size_t)vmaskY(tpy + y) * VRAM_W;
        for (x = u0; x <= u1; x++) {
            u16 t;
            if (swz && tpd == 0) row = vram + (size_t)vmaskY(tpy + (y & ~15) + ((x >> 4) & 15)) * VRAM_W;
            if (swz && tpd == 1) row = vram + (size_t)vmaskY(tpy + (y & ~7) + ((x >> 5) & 7)) * VRAM_W;
            switch (tpd) {
            case 0: t = row[(tpx + (swz ? ((x >> 2) & 3) + 4 * (y & 15) : (x >> 2))) & 1023]; t = vram[(size_t)vmaskY((clut >> 6) & 0x3ff) * VRAM_W + (((clut & 0x3f) * 16 + ((t >> ((x & 3) * 4)) & 15)) & 1023)]; break;
            case 1: t = row[(tpx + (swz ? ((x >> 1) & 7) + 64 * ((x >> 4) & 1) + 8 * (y & 7) : (x >> 1))) & 1023]; t = vram[(size_t)vmaskY((clut >> 6) & 0x3ff) * VRAM_W + (((clut & 0x3f) * 16 + ((t >> ((x & 1) * 8)) & 255)) & 1023)]; break;
            default: t = row[(tpx + x) & 1023]; break;
            }
            if (t) { nonzero++; if (t & 0x8000) stp++; }
        }
    }
    {
        long area = (long)(u1 - u0 + 1) * (v1 - v0 + 1);
        /* fog / smoke fills its whole texture; lettering and sprites leave empty texels around the drawing */
        (void)raw;
        return nonzero > 0 && stp == 0 && nonzero * 10 >= area * 9;
    }
}

static int smart_blend(const Vtx *v, int n, int tex, int semi, int clut, int raw) {
    LARGE_INTEGER t0, t1;
    int r;
    if (!tex || !semi) return 0;
    QueryPerformanceCounter(&t0);
    r = smart_blend_impl(v, n, tex, semi, clut, raw);
    QueryPerformanceCounter(&t1);
    accBlendMs += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qpf.QuadPart;
    return r;
}

/* emits one triangle with the current texture state; textured semi-transparent mode 2 needs two passes (opaque / blended texels) */
static void emit_prim(Vtx v[3], int tex, int raw, int semi, int clut, int blendAll) {
    u32 ix = (u32)tpx | ((u32)tpy << 16);
    u32 iy = (u32)((clut & 0x3f) * 16) | ((u32)((clut >> 6) & 0x3ff) << 16);
    u32 iw = (u32)twMX | ((u32)twMY << 8) | ((u32)twOX << 16) | ((u32)twOY << 24);
    int passes = (semi && tex && abr == 2) ? 2 : 1, p, i;   /* subtract mode always keeps the per-texel rule */
    sync_dirty();
    for (p = 0; p < passes; p++) {
        int pass = passes == 2 ? p + 1 : 0, key = 0;
        unsigned fl = pack_flags(tex, raw, semi, pass, blendAll);
        Vtx t[3];
        if (semi) key = (abr == 0) ? 1 : (abr == 1) ? 2 : (abr == 3) ? 3 : 4;
        if (passes == 2 && p == 0) key = 0;
        for (i = 0; i < 3; i++) { t[i] = v[i]; t[i].info[0] = ix; t[i].info[1] = iy; t[i].info[2] = fl; t[i].info[3] = iw; }
        emit_tri(&t[0], &t[1], &t[2], key);
    }
}

static void set_vtx(Vtx *v, int x, int y, int u, int w, u32 c) {
    v->x = (float)x; v->y = (float)y; v->u = (float)u; v->v = (float)w;
    v->c[0] = (u8)(c & 0xff); v->c[1] = (u8)((c >> 8) & 0xff); v->c[2] = (u8)((c >> 16) & 0xff); v->c[3] = 255;
    v->info[0] = v->info[1] = v->info[2] = v->info[3] = 0;
}

static void draw_rect(int x, int y, int w, int h, u32 c, int tex, int raw, int semi, int u0, int v0, int clut) {
    Vtx a[4];
    int i;
    float fx[4] = {(float)x, (float)(x + w), (float)x, (float)(x + w)}, fy[4] = {(float)y, (float)y, (float)(y + h), (float)(y + h)};
    float uu[4], vv[4];
    if (w <= 0 || h <= 0) return;
    /* texel index = floor(uv): flipped sprites run from u0 downwards, so their uv starts at u0 + 1 */
    uu[0] = uu[2] = rectFlipX ? (float)(u0 + 1) : (float)u0;
    uu[1] = uu[3] = rectFlipX ? (float)(u0 + 1 - w) : (float)(u0 + w);
    vv[0] = vv[1] = rectFlipY ? (float)(v0 + 1) : (float)v0;
    vv[2] = vv[3] = rectFlipY ? (float)(v0 + 1 - h) : (float)(v0 + h);
    for (i = 0; i < 4; i++) { set_vtx(&a[i], 0, 0, 0, 0, c); a[i].x = fx[i]; a[i].y = fy[i]; a[i].u = uu[i]; a[i].v = vv[i]; }
    {
        Vtx t1[3] = {a[0], a[1], a[2]}, t2[3] = {a[1], a[3], a[2]};
        int ba = smart_blend(a, 4, tex, semi, clut, raw);
        cur2d = 1;                                  /* sprites are 2D objects */
        emit_prim(t1, tex, raw, semi, clut, ba);
        emit_prim(t2, tex, raw, semi, clut, ba);
        cur2d = 0;
    }
}

static void draw_line(int x0, int y0, u32 c0, int x1, int y1, u32 c1, int semi) {
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0), len = sqrtf(dx * dx + dy * dy), nx, ny;
    Vtx a[4];
    if (len < 0.001f) { nx = 0; ny = 0.5f; dx = 1; dy = 0; len = 1; }
    else { nx = -dy / len * 0.5f; ny = dx / len * 0.5f; }
    {   /* a one pixel wide strip through the pixel centres, a pixel longer than the segment so both end pixels are covered */
        float ex = dx / len * 0.5f, ey = dy / len * 0.5f;
        float ax = x0 + 0.5f - ex, ay = y0 + 0.5f - ey, bx = x1 + 0.5f + ex, by = y1 + 0.5f + ey;
        set_vtx(&a[0], 0, 0, 0, 0, c0); a[0].x = ax + nx; a[0].y = ay + ny;
        set_vtx(&a[1], 0, 0, 0, 0, c0); a[1].x = ax - nx; a[1].y = ay - ny;
        set_vtx(&a[2], 0, 0, 0, 0, c1); a[2].x = bx + nx; a[2].y = by + ny;
        set_vtx(&a[3], 0, 0, 0, 0, c1); a[3].x = bx - nx; a[3].y = by - ny;
    }
    {
        Vtx t1[3] = {a[0], a[1], a[2]}, t2[3] = {a[1], a[3], a[2]};
        cur2d = 0;
        emit_prim(t1, 0, 0, semi, 0, 0);
        emit_prim(t2, 0, 0, semi, 0, 0);
    }
}

/* ------------------------------------------------------------------ VRAM commands */
static void set_texpage(u32 v, int fromE1) {
    tpx = (int)(v & 0xf) * 64;
    if (!wide) {
        tpy = (int)(((v & 0x10) << 4) | ((v >> 2) & 0x200));
        abr = (v >> 5) & 3;
        tpd = (v >> 7) & 3;
        if (fromE1) dither = (v >> 9) & 1;
    } else {
        tpy = (int)((v & 0x60) << 3);
        abr = (v >> 7) & 3;
        tpd = (v >> 9) & 3;
        if (fromE1) dither = (v >> 11) & 1;
    }
    swz = wide ? (int)((v >> 13) & 1) : 0;
    if (tpd == 3) tpd = 2;
    if (fromE1 && !wide) { rectFlipX = (v >> 12) & 1; rectFlipY = (v >> 13) & 1; }   /* in the wide layout bits 12-13 are not rectangle flips */
    if (fromE1) status = (status & ~(wide ? 0x1fffu : 0x7ffu)) | (v & (wide ? 0x1fffu : 0x7ffu));
}


/* the same copy inside a (scaled) picture of VRAM, through a temporary texture (source and destination may overlap) */
static void copy_in_texture(ID3D11Texture2D *tex, int S, int slot, int sx, int sy, int dx, int dy, int w, int h) {
    static ID3D11Texture2D *tmpTex[2];
    static int tmpS[2];
    D3D11_BOX sb;
    if (!tmpTex[slot] || tmpS[slot] != S) {
        D3D11_TEXTURE2D_DESC td;
        REL(tmpTex[slot]);
        memset(&td, 0, sizeof td);
        td.Width = td.Height = (UINT)(1024 * S); td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_DEFAULT;
        ID3D11Device_CreateTexture2D(dev, &td, NULL, &tmpTex[slot]);
        tmpS[slot] = S;
    }
    if (!tmpTex[slot]) return;
    sb.left = (UINT)(sx * S); sb.top = (UINT)(sy * S); sb.right = (UINT)((sx + w) * S); sb.bottom = (UINT)((sy + h) * S); sb.front = 0; sb.back = 1;
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)tmpTex[slot], 0, 0, 0, 0, (ID3D11Resource *)tex, 0, &sb);
    sb.left = 0; sb.top = 0; sb.right = (UINT)(w * S); sb.bottom = (UINT)(h * S);
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)tex, 0, (UINT)(dx * S), (UINT)(dy * S), 0, (ID3D11Resource *)tmpTex[slot], 0, &sb);
}

static void vram_fill(void) {
    int ymask = 0x3ff;
    int x = cmd[1] & 0x3f0, y = (cmd[1] >> 16) & ymask;
    int w = (int)(((cmd[2] & 0x3ff) + 0xf) & ~0xfu), h = (cmd[2] >> 16) & ymask;
    u32 c = cmd[0];
    u16 col = (u16)(((c & 0xff) >> 3) | ((((c >> 8) & 0xff) >> 3) << 5) | ((((c >> 16) & 0xff) >> 3) << 10));
    int i, j;
    sync_all();
    for (j = 0; j < h; j++) {
        u16 *row = vram + (size_t)vmaskY(y + j) * VRAM_W;
        for (i = 0; i < w; i++) row[(x + i) & 1023] = col;
    }
    if (w > 0 && h > 0) {
        int r = ((col & 31) << 3) | ((col & 31) >> 2), g = (((col >> 5) & 31) << 3) | (((col >> 5) & 31) >> 2), b = (((col >> 10) & 31) << 3) | (((col >> 10) & 31) >> 2);
        int w2 = x + w > 1024 ? 1024 - x : w, h2 = y + h > 1024 ? 1024 - y : h;
        fullScissor = 1;
        quad_solid(x, y, w2, h2, r, g, b);
        flush();
        fullScissor = 0;
        box_add(&texDirty, x, y, w2, h2);
    }
}

static void vram_copy(void) {
    int ymask = 0x3ff;
    int sx = cmd[1] & 0x3ff, sy = (cmd[1] >> 16) & ymask, dx = cmd[2] & 0x3ff, dy = (cmd[2] >> 16) & ymask;
    int w = (int)(((cmd[3] & 0x3ff) - 1) & 0x3ff) + 1, h = (int)((((cmd[3] >> 16) & ymask) - 1) & ymask) + 1;
    u16 *tmp = (u16 *)malloc((size_t)w * h * 2);
    int i, j;
    if (!tmp) return;
    sync_all();
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) tmp[j * w + i] = vram[(size_t)vmaskY(sy + j) * VRAM_W + ((sx + i) & 1023)];
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            u16 *d = &vram[(size_t)vmaskY(dy + j) * VRAM_W + ((dx + i) & 1023)];
            *d = tmp[j * w + i] | (setMask ? 0x8000 : 0);
        }
    free(tmp);
    if (ctx && sx + w <= 1024 && sy + h <= 1024 && dx + w <= 1024 && dy + h <= 1024) {
        copy_in_texture(scaledTex, N, 0, sx, sy, dx, dy, w, h);
        if (nativeTex) copy_in_texture(nativeTex, 1, 1, sx, sy, dx, dy, w, h);
    }
    box_add(&texDirty, dx, dy, w, h);
}

/* a game reads VRAM back (screen grabs, feedback effects): bring the GPU's picture of that area into the CPU copy */
static void readback_region(int x, int y, int w, int h) {
    D3D11_TEXTURE2D_DESC td;
    ID3D11Texture2D *st = 0;
    D3D11_MAPPED_SUBRESOURCE ms;
    D3D11_BOX sb;
    int i, j;
    if (!ctx || x < 0 || y < 0 || x + w > 1024 || y + h > 1024 || w <= 0 || h <= 0) return;
    sync_all();
    memset(&td, 0, sizeof td);
    td.Width = (UINT)(w * N); td.Height = (UINT)(h * N); td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &st))) return;
    sb.left = (UINT)(x * N); sb.top = (UINT)(y * N); sb.right = (UINT)((x + w) * N); sb.bottom = (UINT)((y + h) * N); sb.front = 0; sb.back = 1;
    accReadbacks++;
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)st, 0, 0, 0, 0, (ID3D11Resource *)scaledTex, 0, &sb);
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &ms))) {
        for (j = 0; j < h; j++) {
            const u8 *row = (const u8 *)ms.pData + (size_t)(j * N + N / 2) * ms.RowPitch;
            for (i = 0; i < w; i++) {
                const u8 *p = row + (size_t)(i * N + N / 2) * 4;
                vram[(size_t)(y + j) * VRAM_W + x + i] = (u16)((p[0] >> 3) | ((p[1] >> 3) << 5) | ((p[2] >> 3) << 10));
            }
        }
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
        box_add(&texDirty, x, y, w, h);
    }
    ID3D11Texture2D_Release(st);
}

static void img_setup(int read) {
    int ymask = 0x3ff;
    imgX = cmd[1] & 0x3ff; imgY = (cmd[1] >> 16) & ymask;
    imgW = (int)(((cmd[2] & 0x3ff) - 1) & 0x3ff) + 1;
    imgH = (int)((((cmd[2] >> 16) & ymask) - 1) & ymask) + 1;
    imgCX = imgCY = 0;
    imgMode = read ? 2 : 1;
    if (read) readback_region(imgX, imgY, imgW, imgH);
    else sync_all();
}

static void img_done(void) {
    box_add(&texDirty, imgX, imgY, imgW, imgH);
    box_add(&blitDirty, imgX, imgY, imgW, imgH);
}

static void img_put(u16 p) {
    u16 *d;
    if (imgCY >= imgH) return;
    d = &vram[(size_t)vmaskY(imgY + imgCY) * VRAM_W + ((imgX + imgCX) & 1023)];
    *d = p | (setMask ? 0x8000 : 0);
    if (++imgCX >= imgW) {
        imgCX = 0;
        if (++imgCY >= imgH) img_done();
    }
}

static u16 img_get(void) {
    u16 p;
    if (imgCY >= imgH) return 0;
    p = vram[(size_t)vmaskY(imgY + imgCY) * VRAM_W + ((imgX + imgCX) & 1023)];
    if (++imgCX >= imgW) { imgCX = 0; imgCY++; }
    return p;
}


static void gp0_polygon(void) {
    int op = cmdOp, shaded = op & 0x10, tex = op & 0x04, quad = op & 0x08;
    int nv = quad ? 4 : 3, i, idx = 1, clut = 0, semi = (op & 2) != 0, raw = (op & 1) != 0;
    Vtx v[4];
    u32 c0 = cmd[0] & 0xffffff, uvw[4] = {0, 0, 0, 0};
    for (i = 0; i < nv; i++) {
        u32 c = c0, xy, uv = 0;
        if (i > 0 && shaded) c = cmd[idx++] & 0xffffff;
        xy = cmd[idx++];
        if (tex) {
            uv = cmd[idx++];
            uvw[i] = uv;
            if (i == 0) clut = (int)(uv >> 16);
            if (i == 1) set_texpage(uv >> 16, 0);
        }
        set_vtx(&v[i], sx11(xy) + offX, sx11(xy >> 16) + offY, (int)(uv & 0xff), (int)((uv >> 8) & 0xff), c);
    }
    {
        Vtx t1[3] = {v[0], v[1], v[2]}, t2[3] = {v[1], v[2], v[3]};
        int ba = smart_blend(v, nv, tex != 0, semi, clut, raw);
        /* a quad that is a screen-aligned rectangle (images, backgrounds, panels) is a 2D object; anything else is 3D */
        cur2d = quad && ((v[0].x == v[2].x && v[1].x == v[3].x && v[0].y == v[1].y && v[2].y == v[3].y) ||
                         (v[0].x == v[1].x && v[2].x == v[3].x && v[0].y == v[2].y && v[1].y == v[3].y));
        emit_prim(t1, tex != 0, raw, semi, clut, ba);
        if (quad) emit_prim(t2, tex != 0, raw, semi, clut, ba);
        cur2d = 0;
    }
}

static void gp0_line(void) {
    int op = cmdOp, shaded = op & 0x10, poly = op & 0x08, semi = (op & 2) != 0;
    int idx = 1, px, py, cx, cy;
    u32 c = cmd[0] & 0xffffff, pc, xy;
    xy = cmd[idx++];
    px = sx11(xy) + offX; py = sx11(xy >> 16) + offY; pc = c;
    for (;;) {
        u32 cc = c;
        if (idx >= cmdN) break;
        if (shaded) { if (poly && (cmd[idx] & 0xf000f000) == 0x50005000) break; cc = cmd[idx++] & 0xffffff; if (idx >= cmdN) break; }
        else if (poly && (cmd[idx] & 0xf000f000) == 0x50005000) break;
        xy = cmd[idx++];
        cx = sx11(xy) + offX; cy = sx11(xy >> 16) + offY;
        draw_line(px, py, pc, cx, cy, cc, semi);
        px = cx; py = cy; pc = cc;
        if (!poly) break;
    }
}

static void gp0_rect(void) {
    int op = cmdOp, tex = (op & 4) != 0, size = (op >> 3) & 3, idx = 1, w, h, u0 = 0, v0 = 0, clut = 0, x, y;
    u32 c = cmd[0], xy = cmd[idx++];
    if (tex) { u32 uv = cmd[idx++]; u0 = uv & 0xff; v0 = (uv >> 8) & 0xff; clut = (int)(uv >> 16); }
    if (size == 0) { u32 wh = cmd[idx++]; w = wh & 0x3ff; h = (wh >> 16) & 0x3ff; }
    else if (size == 1) w = h = 1;
    else if (size == 2) w = h = 8;
    else w = h = 16;
    x = sx11(xy) + offX; y = sx11(xy >> 16) + offY;
    draw_rect(x, y, w, h, c & 0xffffff, tex, (op & 1) != 0, (op & 2) != 0, u0, v0, clut);
}

static void gp0_exec(void) {
    int op = cmdOp;
    if (op >= 0x20 && op <= 0x3f) gp0_polygon();
    else if (op >= 0x40 && op <= 0x5f) gp0_line();
    else if (op >= 0x60 && op <= 0x7f) gp0_rect();
    else switch (op) {
    case 0x02: vram_fill(); break;
    case 0x80: vram_copy(); break;
    case 0xa0: img_setup(0); break;
    case 0xc0: img_setup(1); break;
    case 0xe1: set_texpage(cmd[0], 1); break;
    case 0xe2:
        rawE2 = cmd[0] & 0xfffff;
        twMX = cmd[0] & 31; twMY = (cmd[0] >> 5) & 31; twOX = (cmd[0] >> 10) & 31; twOY = (cmd[0] >> 15) & 31;
        break;
    case 0xe3:
        flush();
        rawE3 = cmd[0] & 0xffffff;
        daX1 = cmd[0] & 0x3ff; daY1 = (cmd[0] >> (wide ? 12 : 10)) & 0x3ff;
        break;
    case 0xe4:
        flush();
        rawE4 = cmd[0] & 0xffffff;
        daX2 = cmd[0] & 0x3ff; daY2 = (cmd[0] >> (wide ? 12 : 10)) & 0x3ff;
        break;
    case 0xe5:
        rawE5 = cmd[0] & 0xffffff;
        offX = sx11(cmd[0]); offY = sx11(cmd[0] >> (wide ? 12 : 11));
        break;
    case 0xe6:
        setMask = cmd[0] & 1; checkMask = (cmd[0] >> 1) & 1;
        status = (status & ~0x1800u) | ((cmd[0] & 3) << 11);
        break;
    }
}

static void gpu_write(u32 w) {
    if (imgMode == 1) {
        img_put((u16)(w & 0xffff));
        img_put((u16)(w >> 16));
        if (imgCY >= imgH) imgMode = 0;
        return;
    }
    if (cmdNeed == 0) {
        int len = lenTable[w >> 24];
        if (len == 0) return;                     /* NOP / unknown */
        cmdOp = (int)(w >> 24); cmdN = 0; cmdNeed = len;
    }
    if (cmdN < 1024) cmd[cmdN++] = w;
    if (cmdNeed >= 254) {                         /* poly-line: runs until the terminator word */
        int minWords = (cmdNeed == 254) ? 3 : 4;
        if ((cmdN >= minWords && (w & 0xf000f000) == 0x50005000) || cmdN >= 1023) {
            gp0_exec(); cmdNeed = 0;
        }
    } else if (cmdN >= cmdNeed) {
        cmdNeed = 0;
        gp0_exec();
    }
}

static u32 gpu_read(void) {
    if (imgMode == 2) {
        u32 lo = img_get(), hi = img_get();
        dataRet = lo | (hi << 16);
        if (imgCY >= imgH) imgMode = 0;
    }
    return dataRet;
}

static void gpu_reset(void) {
    rectFlipX = rectFlipY = 0;
    status = 0x14802000;
    daX1 = daY1 = 0; daX2 = 1023; daY2 = VH - 1; offX = offY = 0;
    tpx = tpy = tpd = abr = dither = swz = 0;
    twMX = twMY = twOX = twOY = 0;
    setMask = checkMask = 0;
    cmdN = cmdNeed = 0; imgMode = 0;
    dispX = dispY = 0;
}

static void gp1(u32 w) {
    switch (w >> 24) {
    case 0x00: flush(); gpu_reset(); if (resetLogs++ < 20) logmsg("game reset the GPU (#%d)", resetLogs); break;
    case 0x01: cmdN = cmdNeed = 0; imgMode = 0; break;
    case 0x02: status &= ~0x01000000u; break;
    case 0x03: status = (status & ~0x00800000u) | ((w & 1) ? 0x00800000u : 0); break;
    case 0x04: status = (status & ~0x60000000u) | ((w & 3) << 29); break;
    case 0x05:
        dispX = (int)(w & 0x3ff);
        dispY = (int)((w >> (wide ? 12 : 10)) & 0x3ff);
        break;
    case 0x06: hrX1 = (int)(w & 0xfff); hrX2 = (int)((w >> 12) & 0xfff); break;
    case 0x07: vrY1 = (int)(w & 0x3ff); vrY2 = (int)((w >> (wide ? 12 : 10)) & 0x3ff); break;
    case 0x08:
        status = (status & ~0x007f4000u) | ((w & 0x3f) << 17) | ((w & 0x40) << 10) | ((w & 0x80) << 7);
        break;
    case 0x09: status = (status & ~0x8000u) | ((w & 1) ? 0x8000u : 0); break;
    case 0x10:
        switch (w & 0xff) {
        case 2: dataRet = rawE2; break;
        case 3: dataRet = wide ? ((rawE3 & 0x3ff) | (((rawE3 >> 12) & 0x3ff) << 12)) : rawE3; break;
        case 4: dataRet = wide ? ((rawE4 & 0x3ff) | (((rawE4 >> 12) & 0x3ff) << 12)) : rawE4; break;
        case 5: dataRet = rawE5; break;
        case 6: case 7: dataRet = wide ? 1 : 2; break;
        default: break;
        }
        break;
    }
}


/* ------------------------------------------------------------------ display */
static HWND hwnd;
static WNDPROC oldProc;
static int rotateMode;             /* clockwise quarter turns: 0, 1 = 90, 2 = 180, 3 = 270 */
static int isOpen, isFullscreen, snapRequested, displayChanged;
static void toggle_fullscreen(void);
static char origTitle[200];

static void display_geometry(int *sx, int *sy, int *sw, int *sh) {
    static const int hres[4] = {256, 320, 512, 640}, hdiv[4] = {10, 8, 5, 4};
    int mode = (status >> 17) & 3, wideMode = (status >> 16) & 1;
    int vres = (status >> 19) & 1;
    int w = wideMode ? 368 : hres[mode], div = wideMode ? 7 : hdiv[mode];
    int h = 240 << vres;
    if (hrX2 > hrX1) {
        int rw = (hrX2 - hrX1) / div;
        rw = (rw + 2) & ~3;
        if (rw >= 128 && rw <= 704) w = rw;
    }
    if (vrY2 > vrY1) {
        int rh = (vrY2 - vrY1) << vres;
        if (rh >= 64 && rh <= 1024) h = rh;
    }
    if (h > VH) h = VH;
    *sx = dispX; *sy = dispY; *sw = w; *sh = h;
}

/* converts the VRAM display area to 0x00RRGGBB; returns size in *w,*h */

static int skipped;
static DWORD fpsT0;
static int fpsFrames, fpsShown;

static double frame_seconds(void) {
    double fps = 60.0;
    if (cfg.ratedetect) fps = (status & 0x00100000u) ? 50.0 : 60.0;
    else if (cfg.ratemanual >= 10 && cfg.ratemanual <= 1000) fps = cfg.ratemanual;
    return 1.0 / fps;
}

static HANDLE hTimer;   /* high resolution waitable timer (the plain Sleep(1) can take up to 15 ms on current Windows versions) */
static double accWaitMs, accPresentMs, maxFrameMs;
static LARGE_INTEGER lastLace;
static int accFrames;
static LARGE_INTEGER callT0;
static void call_begin(void) { QueryPerformanceCounter(&callT0); }
static void call_end(void);

static double qpc_ms(LONGLONG ticks) { return (double)ticks * 1000.0 / (double)qpf.QuadPart; }
static void call_end(void) { LARGE_INTEGER t; double ms; QueryPerformanceCounter(&t); ms = qpc_ms(t.QuadPart - callT0.QuadPart); accDrawMs += ms; frameDrawMs += ms; if (ms > maxCallMs) maxCallMs = ms; }

/* waits until the performance counter reaches target: timer for the long part, spinning for the last millisecond */
static void wait_until(LONGLONG target) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    while (now.QuadPart < target) {
        double ms = qpc_ms(target - now.QuadPart);
        if (ms > 1.6 && hTimer) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)((ms - 1.2) * 10000.0);   /* relative, in 100 ns */
            if (SetWaitableTimer(hTimer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(hTimer, 100);
            else Sleep(1);
        } else if (ms > 3.0) Sleep(1);
        else YieldProcessor();
        QueryPerformanceCounter(&now);
    }
}

/* returns 1 when this frame should be skipped (we are running behind) */
static int pace_frame(void) {
    LARGE_INTEGER now;
    LONGLONG dur = (LONGLONG)(frame_seconds() * (double)qpf.QuadPart);
    if (!cfg.framelimit) return 0;
    QueryPerformanceCounter(&now);
    if (qNext.QuadPart == 0) qNext.QuadPart = now.QuadPart;
    qNext.QuadPart += dur;
    if (now.QuadPart < qNext.QuadPart) {
        LARGE_INTEGER t1;
        wait_until(qNext.QuadPart);
        QueryPerformanceCounter(&t1);
        accWaitMs += qpc_ms(t1.QuadPart - now.QuadPart);
        frameWaitMs += qpc_ms(t1.QuadPart - now.QuadPart);
        skipped = 0;
        return 0;
    }
    if (now.QuadPart - qNext.QuadPart > dur * 4) { qNext.QuadPart = now.QuadPart; skipped = 0; return 0; }
    if (cfg.frameskip && now.QuadPart - qNext.QuadPart > dur && skipped < 3) { skipped++; return 1; }
    skipped = 0;
    return 0;
}


static void save_bmp_rgba(const char *name, const u8 *px, int pitch, int w, int h) {
    FILE *f;
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    int y, x, pad = (4 - (w * 3) % 4) % 4;
    if (!(f = fopen(name, "wb"))) return;
    memset(&fh, 0, sizeof fh); memset(&ih, 0, sizeof ih);
    fh.bfType = 0x4d42; fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + (w * 3 + pad) * h;
    ih.biSize = sizeof ih; ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1; ih.biBitCount = 24;
    fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
    for (y = h - 1; y >= 0; y--) {
        const u8 *r = px + (size_t)y * pitch;
        for (x = 0; x < w; x++) { u8 b3[3] = {r[x * 4 + 2], r[x * 4 + 1], r[x * 4]}; fwrite(b3, 3, 1, f); }
        for (x = 0; x < pad; x++) fputc(0, f);
    }
    fclose(f);
}

/* copies the finished backbuffer to a .bmp (snapshots; the test harness uses it too) */
static void dump_backbuffer(const char *name) {
    ID3D11Texture2D *bb = 0, *st = 0;
    D3D11_TEXTURE2D_DESC td;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb))) return;
    ID3D11Texture2D_GetDesc(bb, &td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &st))) {
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)bb);
        if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &ms))) {
            save_bmp_rgba(name, (const u8 *)ms.pData, (int)ms.RowPitch, (int)td.Width, (int)td.Height);
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0);
        }
        ID3D11Texture2D_Release(st);
    }
    ID3D11Texture2D_Release(bb);
}

/* ---- bezel image: loaded with GDI+ (gdiplus.dll, part of Windows); the picture is shown in its transparent window */
typedef struct { UINT32 version; void *callback; BOOL noThread, noCodecs; } GdipInput;
typedef struct { UINT w, h; INT stride; INT format; void *scan0; UINT_PTR reserved; } GdipBitmapData;
typedef struct { INT x, y, w, h; } GdipRect;

static void load_bezel(void) {
    HMODULE g;
    int (WINAPI *startup)(ULONG_PTR *, const GdipInput *, void *);
    void (WINAPI *shutdown)(ULONG_PTR);
    int (WINAPI *fromFile)(const WCHAR *, void **);
    int (WINAPI *getW)(void *, UINT *), (WINAPI *getH)(void *, UINT *);
    int (WINAPI *lockBits)(void *, const GdipRect *, UINT, INT, GdipBitmapData *);
    int (WINAPI *unlockBits)(void *, GdipBitmapData *);
    int (WINAPI *dispose)(void *);
    ULONG_PTR token = 0;
    GdipInput in = {1, NULL, FALSE, FALSE};
    WCHAR wpath[520];
    void *bmp = NULL;
    UINT w = 0, h = 0;
    GdipBitmapData bd;
    GdipRect rc;
    unsigned char *px = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA sd;
    D3D11_BLEND_DESC bld;
    int ok = 0, x, y, cx, cy, l, rgt, t, b;
    if (!bezelPath[0]) return;
    if (!MultiByteToWideChar(CP_UTF8, 0, bezelPath, -1, wpath, 520) && !MultiByteToWideChar(CP_ACP, 0, bezelPath, -1, wpath, 520)) return;
    g = LoadLibraryA("gdiplus.dll");
    if (!g) { logmsg("bezel: gdiplus.dll is not available"); return; }
    startup = (void *)GetProcAddress(g, "GdiplusStartup"); shutdown = (void *)GetProcAddress(g, "GdiplusShutdown");
    fromFile = (void *)GetProcAddress(g, "GdipCreateBitmapFromFile"); getW = (void *)GetProcAddress(g, "GdipGetImageWidth");
    getH = (void *)GetProcAddress(g, "GdipGetImageHeight"); lockBits = (void *)GetProcAddress(g, "GdipBitmapLockBits");
    unlockBits = (void *)GetProcAddress(g, "GdipBitmapUnlockBits"); dispose = (void *)GetProcAddress(g, "GdipDisposeImage");
    if (!startup || !shutdown || !fromFile || !getW || !getH || !lockBits || !unlockBits || !dispose) { logmsg("bezel: gdiplus.dll lacks a function"); return; }
    if (startup(&token, &in, NULL) != 0) { logmsg("bezel: GDI+ did not start"); return; }
    if (fromFile(wpath, &bmp) != 0 || !bmp) { logmsg("bezel: the image could not be opened: %s", bezelPath); shutdown(token); return; }
    getW(bmp, &w); getH(bmp, &h);
    if (w < 16 || h < 16 || w > 8192 || h > 8192) { logmsg("bezel: unusable image size %ux%u", w, h); dispose(bmp); shutdown(token); return; }
    rc.x = 0; rc.y = 0; rc.w = (INT)w; rc.h = (INT)h;
    memset(&bd, 0, sizeof bd);
    if (lockBits(bmp, &rc, 1 /* read */, 0x0026200A /* 32bpp ARGB, straight alpha */, &bd) == 0 && bd.scan0) {
        px = (unsigned char *)malloc((size_t)w * h * 4);
        if (px) {
            for (y = 0; y < (int)h; y++) {
                const unsigned char *s = (const unsigned char *)bd.scan0 + (size_t)y * (size_t)bd.stride;
                unsigned char *d = px + (size_t)y * w * 4;
                for (x = 0; x < (int)w; x++) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
            }
            ok = 1;
        }
        unlockBits(bmp, &bd);
    }
    dispose(bmp); shutdown(token);
    if (!ok) { logmsg("bezel: the pixels could not be read"); free(px); return; }
    /* the window: the transparent run through the middle of the image, in both directions */
    cx = (int)w / 2; cy = (int)h / 2;
    #define BZA(X, Y) px[((size_t)(Y) * w + (size_t)(X)) * 4 + 3]
    if (BZA(cx, cy) < 8) {
        for (l = cx; l > 0 && BZA(l - 1, cy) < 8; l--) ;
        for (rgt = cx; rgt < (int)w - 1 && BZA(rgt + 1, cy) < 8; rgt++) ;
        for (t = cy; t > 0 && BZA(cx, t - 1) < 8; t--) ;
        for (b = cy; b < (int)h - 1 && BZA(cx, b + 1) < 8; b++) ;
        bezHole[0] = (float)l / (float)w; bezHole[1] = (float)t / (float)h;
        bezHole[2] = (float)(rgt - l + 1) / (float)w; bezHole[3] = (float)(b - t + 1) / (float)h;
        logmsg("bezel: %ux%u image, window at %d,%d size %dx%d", w, h, l, t, rgt - l + 1, b - t + 1);
    } else {
        bezHole[0] = bezHole[1] = 0.0f; bezHole[2] = bezHole[3] = 1.0f;
        logmsg("bezel: %ux%u image without a transparent middle: the picture fills the window under it", w, h);
    }
    #undef BZA
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sd.pSysMem = px; sd.SysMemPitch = w * 4; sd.SysMemSlicePitch = 0;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(dev, &td, &sd, &bezTex)) && SUCCEEDED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)bezTex, NULL, &bezSRV))) {
        memset(&bld, 0, sizeof bld);
        bld.RenderTarget[0].BlendEnable = TRUE; bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        bld.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(ID3D11Device_CreateBlendState(dev, &bld, &bsAlpha))) { REL(bezSRV); REL(bezTex); }
        bezW = (int)w; bezH = (int)h;
    } else { REL(bezSRV); REL(bezTex); logmsg("bezel: the texture could not be created"); }
    free(px);
}

/* the bezel image fitted into the window (keeping its shape) */
static void bezel_rect(int dw, int dh, float *bx, float *by, float *bw, float *bh) {
    float sc = (float)dw / (float)bezW;
    if ((float)dh / (float)bezH < sc) sc = (float)dh / (float)bezH;
    *bw = (float)bezW * sc; *bh = (float)bezH * sc;
    *bx = ((float)dw - *bw) * 0.5f; *by = ((float)dh - *bh) * 0.5f;
}

static void draw_bezel(int dw, int dh) {
    float bx, by, bw, bh;
    DVtx v[6];
    D3D11_VIEWPORT vp;
    D3D11_RECT sr;
    D3D11_MAPPED_SUBRESOURCE ms;
    UINT stride = sizeof(DVtx), off = 0;
    float bf[4] = {0, 0, 0, 0};
    ID3D11ShaderResourceView *none = 0;
    if (!bezSRV || !psBezel || !bsAlpha) return;
    bezel_rect(dw, dh, &bx, &by, &bw, &bh);
    {
        float x0 = bx / (float)dw * 2.0f - 1.0f, x1 = (bx + bw) / (float)dw * 2.0f - 1.0f;
        float y0 = 1.0f - by / (float)dh * 2.0f, y1 = 1.0f - (by + bh) / (float)dh * 2.0f;
        DVtx q[6] = {{x0, y0, 0, 0}, {x1, y0, 1, 0}, {x0, y1, 0, 1}, {x1, y0, 1, 0}, {x1, y1, 1, 1}, {x0, y1, 0, 1}};
        memcpy(v, q, sizeof v);
    }
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)vbDisp, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, v, sizeof v); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)vbDisp, 0); }
    vp.TopLeftX = vp.TopLeftY = 0; vp.Width = (float)dw; vp.Height = (float)dh; vp.MinDepth = 0; vp.MaxDepth = 1;
    sr.left = 0; sr.top = 0; sr.right = dw; sr.bottom = dh;
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &none);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &bbRTV, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_RSSetScissorRects(ctx, 1, &sr);
    ID3D11DeviceContext_RSSetState(ctx, rsScissor);
    ID3D11DeviceContext_OMSetBlendState(ctx, bsAlpha, bf, 0xffffffffu);
    ID3D11DeviceContext_IASetInputLayout(ctx, ilDisp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbDisp, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsDisp, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, psBezel, NULL, 0);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &smpLinear);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &bezSRV);
    ID3D11DeviceContext_Draw(ctx, 6, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &none);
    ID3D11DeviceContext_OMSetBlendState(ctx, bstate[0], bf, 0xffffffffu);
}

/* FXAA: the window sized texture the picture is drawn into before the filter */
static int ensure_fx(int dw, int dh) {
    D3D11_TEXTURE2D_DESC td;
    if (fxTex && fxW == dw && fxH == dh) return 1;
    REL(fxSRV); REL(fxRTV); REL(fxTex);
    memset(&td, 0, sizeof td);
    td.Width = (UINT)dw; td.Height = (UINT)dh; td.MipLevels = td.ArraySize = 1; td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &fxTex)) || FAILED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)fxTex, NULL, &fxRTV)) ||
        FAILED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)fxTex, NULL, &fxSRV))) {
        REL(fxSRV); REL(fxRTV); REL(fxTex); fxW = fxH = 0;
        logmsg("FXAA: the window sized texture could not be created, FXAA is off");
        cfg.fxaa = 0;
        return 0;
    }
    fxW = dw; fxH = dh;
    return 1;
}

/* FXAA: the filter, from the window sized texture to the back buffer */
static void fxaa_pass(int dw, int dh) {
    DVtx v[6] = {{-1, 1, 0, 0}, {1, 1, 1, 0}, {-1, -1, 0, 1}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}};
    D3D11_VIEWPORT vp;
    D3D11_RECT sr;
    D3D11_MAPPED_SUBRESOURCE ms;
    UINT stride = sizeof(DVtx), off = 0;
    float bf[4] = {0, 0, 0, 0};
    ID3D11ShaderResourceView *none = 0;
    if (!psFxaa || !fxSRV) return;
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)vbDisp, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, v, sizeof v); ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)vbDisp, 0); }
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cbDisp, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        float prm[12] = {0, 1.0f / (float)dw, 1.0f / (float)dh, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        memcpy(ms.pData, prm, sizeof prm);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cbDisp, 0);
    }
    vp.TopLeftX = vp.TopLeftY = 0; vp.Width = (float)dw; vp.Height = (float)dh; vp.MinDepth = 0; vp.MaxDepth = 1;
    sr.left = 0; sr.top = 0; sr.right = dw; sr.bottom = dh;
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &none);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &bbRTV, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_RSSetScissorRects(ctx, 1, &sr);
    ID3D11DeviceContext_RSSetState(ctx, rsScissor);
    ID3D11DeviceContext_OMSetBlendState(ctx, bstate[0], bf, 0xffffffffu);
    ID3D11DeviceContext_IASetInputLayout(ctx, ilDisp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbDisp, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsDisp, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, psFxaa, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cbDisp);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &smpLinear);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &fxSRV);
    ID3D11DeviceContext_Draw(ctx, 6, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &none);
}

static void draw_display(int dw, int dh) {
    int sx, sy, sw, sh, rot = rotateMode & 3, rotated = rot & 1, i;
    float black[4] = {0, 0, 0, 1};
    float dx = 0, dy = 0, dwid = (float)dw, dhei = (float)dh;
    float ax = 0, ay = 0, aw = (float)dw, ah = (float)dh;   /* where the picture may go: the window, or the transparent window of the bezel */
    ID3D11ShaderResourceView *src = scaledSRV;
    DVtx v[6], q[4];
    float tx[4] = {0, 1, 0, 1}, ty[4] = {0, 0, 1, 1};
    float u0, u1, v0, v1;
    D3D11_VIEWPORT vp;
    D3D11_RECT sr;
    D3D11_MAPPED_SUBRESOURCE ms;
    UINT stride = sizeof(DVtx), off = 0;
    float bf[4] = {0, 0, 0, 0};
    ID3D11ShaderResourceView *none = 0;
    int mix = 0;
    ID3D11RenderTargetView *tgt = bbRTV;
    if (cfg.fxaa && psFxaa && ensure_fx(dw, dh)) tgt = fxRTV;   /* FXAA: the picture goes to a window sized texture first */
    ID3D11DeviceContext_ClearRenderTargetView(ctx, tgt, black);
    display_geometry(&sx, &sy, &sw, &sh);
    if (logF) {   /* the game changed what it shows: area of VRAM, colour depth, display on/off */
        static int lx = -1, ly, lw, lh, l24, loff;
        int is24 = (status & 0x00200000u) != 0, off = (status & 0x00800000u) != 0;
        if (sx != lx || sy != ly || sw != lw || sh != lh || is24 != l24 || off != loff) {
            if (modeLogs++ < 200) logmsg("display: %dx%d taken from VRAM at %d,%d | %s colour | display %s | window %dx%d, rotation %d", sw, sh, sx, sy, is24 ? "24-bit" : "15-bit", off ? "off" : "on", dw, dh, (rotateMode & 3) * 90);
            lx = sx; ly = sy; lw = sw; lh = sh; l24 = is24; loff = off;
        }
    }
    if ((status & 0x00800000u) || sw <= 0 || sh <= 0) return;
    if (cfg.crop > 0 && sw > 2 * cfg.crop + 32 && sh > 2 * cfg.crop + 32) { sx += cfg.crop; sy += cfg.crop; sw -= 2 * cfg.crop; sh -= 2 * cfg.crop; }   /* overscan: the border of the picture is cut off */
    if (bezSRV) {
        float bx, by, bw, bh;
        bezel_rect(dw, dh, &bx, &by, &bw, &bh);
        ax = bx + bezHole[0] * bw; ay = by + bezHole[1] * bh; aw = bezHole[2] * bw; ah = bezHole[3] * bh;
        dx = ax; dy = ay; dwid = aw; dhei = ah;
    }
    if (status & 0x00200000u) {                         /* 24-bit colour: decode the area on the CPU */
        if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)dispTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            int x, y;
            for (y = 0; y < sh && y < 1024; y++) {
                const u8 *row = (const u8 *)(vram + (size_t)vmaskY(sy + y) * VRAM_W) + (sx & 1023) * 2;
                u8 *o = (u8 *)ms.pData + (size_t)y * ms.RowPitch;
                for (x = 0; x < sw && x < 1024; x++) {
                    if ((sx & 1023) * 2 + x * 3 + 2 >= VRAM_W * 2) { o[x * 4] = o[x * 4 + 1] = o[x * 4 + 2] = 0; o[x * 4 + 3] = 255; continue; }
                    o[x * 4] = row[x * 3]; o[x * 4 + 1] = row[x * 3 + 1]; o[x * 4 + 2] = row[x * 3 + 2]; o[x * 4 + 3] = 255;
                }
            }
            ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)dispTex, 0);
        }
        src = dispSRV;
        u0 = 0.5f / 1024.0f; u1 = ((float)sw - 0.5f) / 1024.0f; v0 = 0.5f / 1024.0f; v1 = ((float)sh - 0.5f) / 1024.0f;
    } else {
        float s = (float)(1024 * N);
        u0 = ((float)(sx * N) + 0.5f) / s; u1 = ((float)((sx + sw) * N) - 0.5f) / s;
        v0 = ((float)(sy * N) + 0.5f) / s; v1 = ((float)((sy + sh) * N) - 0.5f) / s;
    }
    mix = cfg.xbrz == 2 && nativeSRV && !(status & 0x00200000u);
    if (cfg.keepaspect || rotated || (isFullscreen && cfg.xsize * 3 == cfg.ysize * 4)) {   /* a 4:3 resolution stays 4:3 on a wide screen */
        /* KeepAspect: 1 = 4:3, 2 = 16:9, 3 = pixel perfect (a whole number times the console picture, square pixels) */
        float aspect = cfg.keepaspect == 2 ? (rotated ? 9.0f / 16.0f : 16.0f / 9.0f) : (rotated ? 3.0f / 4.0f : 4.0f / 3.0f);
        int pw = rotated ? sh : sw, ph = rotated ? sw : sh, k = 0;   /* the console picture as shown: width x height */
        if (cfg.keepaspect == 3 && pw > 0 && ph > 0) {
            k = (int)(aw / (float)pw);
            if ((int)(ah / (float)ph) < k) k = (int)(ah / (float)ph);
        }
        if (k >= 1) { dwid = (float)(k * pw); dhei = (float)(k * ph); }
        else {
            if (cfg.keepaspect == 3 && pw > 0 && ph > 0) aspect = (float)pw / (float)ph;   /* smaller than the picture: scaled down to fit */
            if (aw / ah > aspect) { dhei = ah; dwid = dhei * aspect; }
            else { dwid = aw; dhei = dwid / aspect; }
        }
        dx = ax + (aw - dwid) * 0.5f; dy = ay + (ah - dhei) * 0.5f;
    }
    for (i = 0; i < 4; i++) {
        float u, w;
        switch (rot) {
        case 0: u = tx[i]; w = ty[i]; break;
        case 1: u = ty[i]; w = 1.0f - tx[i]; break;
        case 2: u = 1.0f - tx[i]; w = 1.0f - ty[i]; break;
        default: u = 1.0f - ty[i]; w = tx[i]; break;
        }
        q[i].x = (dx + tx[i] * dwid) / (float)dw * 2.0f - 1.0f;
        q[i].y = 1.0f - (dy + ty[i] * dhei) / (float)dh * 2.0f;
        if (cfg.xbrz) { q[i].u = u; q[i].v = w; }   /* 0..1 over the picture: the shader maps it to texels itself */
        else { q[i].u = u0 + (u1 - u0) * u; q[i].v = v0 + (v1 - v0) * w; }
    }
    v[0] = q[0]; v[1] = q[1]; v[2] = q[2]; v[3] = q[1]; v[4] = q[3]; v[5] = q[2];
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)vbDisp, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        memcpy(ms.pData, v, sizeof v);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)vbDisp, 0);
    }
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cbDisp, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        static const float dedK[4] = {0.0f, 0.04f, 0.08f, 0.14f};
        float prm[12] = {(float)cfg.scanlines, dedK[cfg.dedither & 3], (status & 0x00200000u) ? 1.0f : (float)N, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (cfg.xbrz) {
            int is24 = (status & 0x00200000u) != 0;
            prm[4] = is24 ? 0.0f : (float)sx; prm[5] = is24 ? 0.0f : (float)sy; prm[6] = (float)sw; prm[7] = (float)sh;
            prm[8] = (rotated ? dhei : dwid) / (float)sw; prm[9] = (rotated ? dwid : dhei) / (float)sh;
        }
        memcpy(ms.pData, prm, sizeof prm);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cbDisp, 0);
    }
    vp.TopLeftX = vp.TopLeftY = 0; vp.Width = (float)dw; vp.Height = (float)dh; vp.MinDepth = 0; vp.MaxDepth = 1;
    sr.left = 0; sr.top = 0; sr.right = dw; sr.bottom = dh;
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &none);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &tgt, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_RSSetScissorRects(ctx, 1, &sr);
    ID3D11DeviceContext_RSSetState(ctx, rsScissor);
    ID3D11DeviceContext_OMSetBlendState(ctx, bstate[0], bf, 0xffffffffu);
    ID3D11DeviceContext_IASetInputLayout(ctx, ilDisp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbDisp, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsDisp, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, mix ? psMix : cfg.xbrz ? psXbrz : psDisp, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cbDisp);
    {
        ID3D11SamplerState *sm = cfg.filtering > 0 ? smpLinear : smpPoint;
        ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, &sm);
    }
    if (mix) {
        ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &nativeSRV);
        ID3D11DeviceContext_PSSetShaderResources(ctx, 2, 1, &scaledSRV);
    } else ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &src);
    ID3D11DeviceContext_Draw(ctx, 6, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &none);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 2, 1, &none);
}

/* ---------------------------------------------------------------- frame rate counter (ShowFPS) */
#define FPS_TW 64
#define FPS_TH 16

/* 5x7 glyphs of the characters of "FPS 0123456789", one row per byte (bit 4 = left) */
static const u8 *glyph(char c) {
    static const u8 g[][7] = {
        {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}, {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}, {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},
        {0x1f,0x02,0x04,0x02,0x01,0x11,0x0e}, {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}, {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e},
        {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e}, {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}, {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},
        {0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c},
        {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},   /* F */
        {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},   /* P */
        {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},   /* S */
        {0,0,0,0,0,0,0}};
    if (c >= '0' && c <= '9') return g[c - '0'];
    if (c == 'F') return g[10];
    if (c == 'P') return g[11];
    if (c == 'S') return g[12];
    return g[13];
}

/* draws "FPS nn" in a black box in the top left corner of the window */
static void draw_fps(int dw, int dh) {
    static u32 px[FPS_TW * FPS_TH];
    char t[16];
    int n, i, x, y, bw = 0, bh = 9;
    float scale, w, h;
    float black[4] = {0, 0, 0, 0}, x0, y0;
    D3D11_VIEWPORT vp;
    D3D11_RECT sr;
    D3D11_MAPPED_SUBRESOURCE ms;
    ID3D11ShaderResourceView *none = 0;
    DVtx v[6];
    UINT stride = sizeof(DVtx), off = 0;
    if (!cfg.showfps) return;
    if (!fpsTex) {
        D3D11_TEXTURE2D_DESC td;
        memset(&td, 0, sizeof td);
        td.Width = FPS_TW; td.Height = FPS_TH; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(ID3D11Device_CreateTexture2D(dev, &td, NULL, &fpsTex))) return;
        ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)fpsTex, NULL, &fpsSRV);
    }
    n = snprintf(t, sizeof t, "FPS %d", fpsShown);
    bw = n * 6 + 1;
    if (fpsDrawn != fpsShown) {
        for (i = 0; i < FPS_TW * FPS_TH; i++) px[i] = 0xff000000u;
        for (i = 0; i < n; i++) {
            const u8 *gl = glyph(t[i]);
            for (y = 0; y < 7; y++) for (x = 0; x < 5; x++)
                if (gl[y] & (0x10 >> x)) px[(y + 1) * FPS_TW + 1 + i * 6 + x] = 0xffffffffu;   /* white */
        }
        ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)fpsTex, 0, NULL, px, FPS_TW * 4, 0);
        fpsDrawn = fpsShown;
    }
    scale = (float)((dh + 360) / 720) - 0.25f; if (scale < 1.25f) scale = 1.25f;   /* 1.25 at 480p, 1.75 at 1080p */
    w = bw * scale; h = bh * scale;
    x0 = scale * 2.0f; y0 = scale * 2.0f;
    {
        float l = x0 / dw * 2.0f - 1.0f, r = (x0 + w) / dw * 2.0f - 1.0f, tp = 1.0f - y0 / dh * 2.0f, bt = 1.0f - (y0 + h) / dh * 2.0f;
        float ur = (float)bw / FPS_TW, vb = (float)bh / FPS_TH;
        DVtx a = {l, tp, 0, 0}, b = {r, tp, ur, 0}, c = {l, bt, 0, vb}, d = {r, bt, ur, vb};
        v[0] = a; v[1] = b; v[2] = c; v[3] = b; v[4] = d; v[5] = c;
    }
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)vbDisp, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, v, sizeof v);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)vbDisp, 0);
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cbDisp, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        float prm[12] = {0};
        memcpy(ms.pData, prm, sizeof prm);
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cbDisp, 0);
    }
    vp.TopLeftX = vp.TopLeftY = 0; vp.Width = (float)dw; vp.Height = (float)dh; vp.MinDepth = 0; vp.MaxDepth = 1;
    sr.left = 0; sr.top = 0; sr.right = dw; sr.bottom = dh;
    ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &none);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &bbRTV, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_RSSetScissorRects(ctx, 1, &sr);
    ID3D11DeviceContext_RSSetState(ctx, rsScissor);
    ID3D11DeviceContext_OMSetBlendState(ctx, bstate[0], black, 0xffffffffu);
    ID3D11DeviceContext_IASetInputLayout(ctx, ilDisp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vbDisp, &stride, &off);
    ID3D11DeviceContext_VSSetShader(ctx, vsDisp, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, psDisp, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cbDisp);
    ID3D11DeviceContext_PSSetSamplers(ctx, 0, 1, scale == 1.0f ? &smpPoint : &smpLinear);   /* in-between sizes: smooth */
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &fpsSRV);
    ID3D11DeviceContext_Draw(ctx, 6, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx, 1, 1, &none);
}

static void present(void) {
    RECT rc;
    int dw, dh;
    if (!sc || !hwnd || !IsWindow(hwnd)) return;
    GetClientRect(hwnd, &rc);
    dw = rc.right - rc.left; dh = rc.bottom - rc.top;
    if (dw <= 0 || dh <= 0) return;
    sync_all();
    if (dw != bbW || dh != bbH) {
        ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, NULL);
        REL(bbRTV);
        HRESULT rh = IDXGISwapChain_ResizeBuffers(sc, 0, (UINT)dw, (UINT)dh, DXGI_FORMAT_UNKNOWN, 0);
        logmsg("window resized: %dx%d -> %dx%d%s", bbW, bbH, dw, dh, SUCCEEDED(rh) ? "" : " (resizing the swap chain FAILED)");
        if (SUCCEEDED(rh)) { bbW = dw; bbH = dh; }
        make_backbuffer();
    }
    draw_display(bbW, bbH);
    if (cfg.fxaa && psFxaa && fxRTV) fxaa_pass(bbW, bbH);
    if (bezSRV) draw_bezel(bbW, bbH);
    draw_fps(bbW, bbH);
    {
        char dumpPath[260];
        if (GetEnvironmentVariableA("ZN_D3D11_DUMP", dumpPath, sizeof dumpPath)) dump_backbuffer(dumpPath);
    }
    if (snapRequested) {
        char name[300];
        int i, n;
        snapRequested = 0;
        /* exactly like the OpenGL renderer: SNAP\<game>\ZNOGL001.bmp (relative to the folder ZiNc runs in, the first number that is free) */
        CreateDirectoryA("SNAP", NULL);
        n = snprintf(name, sizeof name, "SNAP");
        if (gameName[0]) { n = snprintf(name, sizeof name, "SNAP\\%s", gameName); CreateDirectoryA(name, NULL); }
        for (i = 1; i < 1000; i++) {
            snprintf(name + n, sizeof name - (size_t)n, "\\ZND11%03d.bmp", i);
            if (GetFileAttributesA(name) == INVALID_FILE_ATTRIBUTES) break;
        }
        dump_backbuffer(name);
    }
    {
        HRESULT ph = IDXGISwapChain_Present(sc, cfg.vsync ? 1 : 0, 0);   /* VSync: wait for the next screen refresh */
        if (ph != lastPresentHr) {   /* only changes are logged: occluded window, lost device, ... */
            if (ph == S_OK) logmsg("Present works again");
            else if (ph == DXGI_STATUS_OCCLUDED) logmsg("Present: the window is hidden or covered (DXGI_STATUS_OCCLUDED)");
            else {
                logmsg("Present returned %08lx", (unsigned long)ph);
                if (ph == DXGI_ERROR_DEVICE_REMOVED || ph == DXGI_ERROR_DEVICE_RESET) logmsg("graphics device lost, reason %08lx (graphics driver crash or reset)", (unsigned long)ID3D11Device_GetDeviceRemovedReason(dev));
            }
            lastPresentHr = ph;
        }
    }
}

static LRESULT CALLBACK subProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SYSKEYDOWN && w == VK_RETURN) {   /* Alt+Enter: fullscreen <-> window */
        if (!(l & (1 << 30))) toggle_fullscreen();
        return 0;
    }
    if (m == WM_SYSCHAR && w == VK_RETURN) return 0;
    if (m == WM_ACTIVATEAPP && isFullscreen) {   /* Alt+Tab: a topmost fullscreen window would stay above the program that is switched to */
        SetWindowPos(h, w ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (w) SetForegroundWindow(h);
    }
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) { ValidateRect(h, NULL); return 0; }
    return CallWindowProcA(oldProc, h, m, w, l);
}

static int fsW, fsH;

static void apply_fullscreen(void) {
    SetMenu(hwnd, NULL);
    SetWindowLongA(hwnd, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
    SetWindowLongA(hwnd, GWL_EXSTYLE, (LONG)(GetWindowLongA(hwnd, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME)));
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, fsW, fsH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

static void check_fullscreen(void) {
    RECT rc;
    if (!isFullscreen || !hwnd || !IsWindow(hwnd)) return;
    GetWindowRect(hwnd, &rc);
    if (rc.left != 0 || rc.top != 0 || rc.right - rc.left != fsW || rc.bottom - rc.top != fsH) apply_fullscreen();
}

static void apply_windowed(void) {
    int cw = cfg.xsize, ch = cfg.ysize;
    LONG style;
    RECT rc, wa;
    int ww, wh;
    if (rotateMode & 1) { int t = cw; cw = ch; ch = t; }   /* portrait window for rotated games */
    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    SetWindowLongA(hwnd, GWL_STYLE, style);
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) { wa.left = 0; wa.top = 0; wa.right = GetSystemMetrics(SM_CXSCREEN); wa.bottom = GetSystemMetrics(SM_CYSCREEN); }
    rc.left = 0; rc.top = 0; rc.right = cw; rc.bottom = ch;
    AdjustWindowRect(&rc, (DWORD)style, GetMenu(hwnd) != NULL);
    ww = rc.right - rc.left; wh = rc.bottom - rc.top;
    if (ww > wa.right - wa.left || wh > wa.bottom - wa.top) {   /* a window as large as the screen (the resolution of the desktop): smaller, with the same shape, so the title bar stays on the screen */
        int fx = ww - cw, fy = wh - ch, mw = wa.right - wa.left - fx, mh = wa.bottom - wa.top - fy;
        if (mw < 320) mw = 320;
        if (mh < 240) mh = 240;
        if ((double)mw / cw < (double)mh / ch) { ch = (int)((double)ch * mw / cw + 0.5); cw = mw; } else { cw = (int)((double)cw * mh / ch + 0.5); ch = mh; }
        rc.left = 0; rc.top = 0; rc.right = cw; rc.bottom = ch;
        AdjustWindowRect(&rc, (DWORD)style, GetMenu(hwnd) != NULL);
        ww = rc.right - rc.left; wh = rc.bottom - rc.top;
    }
    SetWindowPos(hwnd, HWND_NOTOPMOST, wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

static void setup_window(void) {
    int cw = cfg.xsize, ch = cfg.ysize;
    isFullscreen = 0;
    if (cfg.fullscreen) {
        DEVMODEA dm;
        if (cfg.borderless) logmsg("fullscreen: borderless window over the desktop, the screen mode is not changed");
        memset(&dm, 0, sizeof dm);
        dm.dmSize = sizeof dm;
        if (!cfg.borderless) {
            dm.dmPelsWidth = cw; dm.dmPelsHeight = ch; dm.dmBitsPerPel = 32;
            dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
            { LONG cr = ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN); if (cr == DISP_CHANGE_SUCCESSFUL) displayChanged = 1; logmsg("fullscreen: screen mode %dx%d %s (code %ld)", cw, ch, cr == DISP_CHANGE_SUCCESSFUL ? "set" : "NOT possible, the desktop mode stays", cr); }
        }
        cw = GetSystemMetrics(SM_CXSCREEN); ch = GetSystemMetrics(SM_CYSCREEN);
        isFullscreen = 1;
        fsW = cw; fsH = ch;
        apply_fullscreen();
    } else apply_windowed();
}

/* Alt+Enter: switches between the window and a fullscreen window over the screen (the picture scales with the window) */
static void toggle_fullscreen(void) {
    if (!hwnd || !IsWindow(hwnd)) return;
    logmsg("Alt+Enter: going %s", isFullscreen ? "to a window" : "fullscreen");
    if (isFullscreen) {
        isFullscreen = 0;
        if (displayChanged) { ChangeDisplaySettingsA(NULL, 0); displayChanged = 0; }
        apply_windowed();
    } else {
        isFullscreen = 1;
        fsW = GetSystemMetrics(SM_CXSCREEN); fsH = GetSystemMetrics(SM_CYSCREEN);
        apply_fullscreen();
    }
}


/* ------------------------------------------------------------------ log: what the session starts with */
static void log_header(void) {
    char exe[MAX_PATH] = "", brand[64] = "";
    SYSTEMTIME st;
    MEMORYSTATUSEX ms;
    SYSTEM_INFO si;
    DEVMODEA dm;
    HDC dc;
    RECT rc;
    typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW *);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    RtlGetVersion_t rgv = nt ? (RtlGetVersion_t)GetProcAddress(nt, "RtlGetVersion") : 0;
    const char *wine = nt ? (GetProcAddress(nt, "wine_get_version") ? "yes (Wine)" : "no") : "?";
    OSVERSIONINFOW ov;
    unsigned r[4], i;
    GetLocalTime(&st);
    logmsg("ZiNc Direct3D 11 renderer plugin, %d-bit", (int)(sizeof(void *) * 8));
    logmsg("started %04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    GetModuleFileNameA(NULL, exe, sizeof exe);
    logmsg("program: %s", exe);
    logmsg("settings file: %s", cfgPath[0] ? cfgPath : "none found (defaults)");
    logmsg("game (from ZiNc): %s", gameName[0] ? gameName : "not given");
    memset(&ov, 0, sizeof ov); ov.dwOSVersionInfoSize = sizeof ov;
    if (rgv && rgv(&ov) == 0) logmsg("Windows %lu.%lu build %lu | Wine: %s", ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber, wine);
    for (i = 0; i < 3; i++) {
        __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(0x80000002u + i), "c"(0));
        memcpy(brand + i * 16, r, 16);
    }
    brand[48] = 0;
    GetSystemInfo(&si);
    memset(&ms, 0, sizeof ms); ms.dwLength = sizeof ms; GlobalMemoryStatusEx(&ms);
    logmsg("CPU: %s | %lu logical processors | memory %lu MB (%lu MB free)", brand + (brand[0] == ' ' ? strspn(brand, " ") : 0), (unsigned long)si.dwNumberOfProcessors, (unsigned long)(ms.ullTotalPhys >> 20), (unsigned long)(ms.ullAvailPhys >> 20));
    memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
    dc = GetDC(NULL);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) logmsg("desktop: %lux%lu, %lu bit, %lu Hz | screen scaling %d%%", (unsigned long)dm.dmPelsWidth, (unsigned long)dm.dmPelsHeight, (unsigned long)dm.dmBitsPerPel, (unsigned long)dm.dmDisplayFrequency, dc ? GetDeviceCaps(dc, LOGPIXELSX) * 100 / 96 : 100);
    if (dc) ReleaseDC(NULL, dc);
    GetClientRect(hwnd, &rc);
    logmsg("game window: %dx%d | mode %u (%s) | rotation %d degrees | %s", (int)(rc.right - rc.left), (int)(rc.bottom - rc.top), gOpenMode, wide ? "extended GPU" : "standard GPU", (rotateMode & 3) * 90, isFullscreen ? "fullscreen" : "windowed");
    logmsg("settings: size %dx%d | fullscreen %d | scanlines %d | filtering %d | dithering %d | show fps %d | frame limiter %d | frame skipping %d | frame rate detection %d (manual %d) | keep aspect %d | xBRZ %d | internal scale %s -> %dx | FXAA %d | dither smoothing %d | VSync %d | borderless %d | overscan %d | 3D texture smoothing %d", cfg.xsize, cfg.ysize, cfg.fullscreen, cfg.scanlines, cfg.filtering, cfg.dithering, cfg.showfps, cfg.framelimit, cfg.frameskip, cfg.ratedetect, cfg.ratemanual, cfg.keepaspect, cfg.xbrz, cfg.iscale > 0 ? "set" : "automatic", N, cfg.fxaa, cfg.dedither, cfg.vsync, cfg.borderless, cfg.crop, cfg.texsmooth);
    logmsg("frame timer: %s | target %.1f fps", hTimer ? "high resolution" : "standard (Sleep)", 1.0 / frame_seconds());
}

/* a line per second and a summary at the end: how the frames were paced and where a long frame spent its time */
static float ftList[1200];
static int ftN;

static int cmp_float(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }

static void log_summary(void) {
    unsigned long seen = 0, want = totFrames / 100 + 1;
    int b;
    double secs = (GetTickCount() - sessionT0) / 1000.0, low = 0;
    if (!logF || totFrames == 0) return;
    for (b = 401; b >= 0; b--) { seen += histo[b]; if (seen >= want) { low = b * 0.5; break; } }
    logmsg("summary: %.1f s | %lu frames | average %.1f fps | 1%% low %.1f fps | worst frame %.1f ms | frames over 20 ms: %lu, over 40 ms: %lu, over 100 ms: %lu | hitches: %lu",
           secs, totFrames, secs > 0 ? totFrames / secs : 0.0, low > 0 ? 1000.0 / low : 0.0, worstFrameMs, over20, over40, over100, hitchCount);
}

static void log_frame(double f, double expectMs) {
    int b;
    totFrames++;
    if (f > worstFrameMs) worstFrameMs = f;
    b = (int)(f * 2.0); if (b > 401) b = 401;
    histo[b]++;
    if (f > 20.0) over20++;
    if (f > 40.0) over40++;
    if (f > 100.0) over100++;
    if (ftN < (int)(sizeof ftList / sizeof ftList[0])) ftList[ftN++] = (float)f;
    if (logF && f > 40.0 && f > expectMs * 2.5) {   /* a hitch: say where the time went, to tell this renderer from the emulator / sound / input */
        double outside = f - frameDrawMs - framePresentMs - frameWaitMs;
        hitchCount++; hitchSecond++;
        if (hitchLogged < 100) {
            hitchLogged++;
            logmsg("hitch: frame took %.1f ms (should be %.1f) | outside this renderer (emulator, sound, input) %.1f ms | drawing %.1f ms | present %.1f ms | limiter wait %.1f ms%s", f, expectMs, outside < 0 ? 0 : outside, frameDrawMs, framePresentMs, frameWaitMs,
                   outside > f * 0.6 ? " -> the time was spent outside the renderer" : (frameDrawMs > f * 0.6 ? " -> the time was spent drawing" : ""));
        } else if (hitchLogged == 100) { hitchLogged++; logmsg("(more than 100 hitches: only the count is kept)"); }
    }
    frameDrawMs = framePresentMs = frameWaitMs = 0;
}

static void log_second(int fps) {
    double avg = 0, p99 = 0, mx = 0, n = accFrames;
    int i;
    if (!logF || accFrames <= 0) return;
    if (ftN > 0) {
        qsort(ftList, (size_t)ftN, sizeof(float), cmp_float);
        for (i = 0; i < ftN; i++) avg += ftList[i];
        avg /= ftN;
        p99 = ftList[(int)((ftN - 1) * 0.99)];
        mx = ftList[ftN - 1];
    }
    {
        double outside = avg - accDrawMs / n - accPresentMs / n - accWaitMs / n;
        logmsg("fps %d | frame time avg %.1f, 99%% %.1f, max %.1f ms | hitches %d | per frame: outside renderer %.1f, drawing %.1f, present %.1f, limiter wait %.1f, transparency check %.2f ms | %.0f draws, %.0f triangles, %.1f texture uploads (%.0f KB), %.1f image copies, %.2f VRAM read-backs",
               fps, avg, p99, mx, hitchSecond, outside < 0 ? 0 : outside, accDrawMs / n, accPresentMs / n, accWaitMs / n, accBlendMs / n,
               statDraws / n, statVerts / 3.0 / n, statUploads / n, statUploadPx * 2.0 / 1024.0 / n, statBlits / n, accReadbacks / n);
    }
    statDraws = statVerts = statUploads = statUploadPx = statBlits = 0; ftN = 0; hitchSecond = 0;
}

/* ------------------------------------------------------------------ ZN_GPU* exports */
typedef struct {
    u32 version;
    HWND hwnd;
    u32 rotate;
    u32 mode;       /* 2 = extended (wide) GPU */
    const char *gameName;   /* the ROM set of the game ("sfex2pj"): the OpenGL renderer copies this string and names its screenshot folder after it */
    const char *cfgFile;    /* the file given with --use-renderer-cfg-file (the OpenGL renderer opens exactly this) */
} OpenInfo;

long __stdcall ZN_GPUinit(void) {
    if (!vram) vram = (u16 *)calloc((size_t)VRAM_W * 1024, 2);
    if (!vram) return -1;
    QueryPerformanceFrequency(&qpf);
    VH = 1024; wide = 0;
    gpu_reset();
    return 0;
}

long __stdcall ZN_GPUshutdown(void) {
    free(vram); vram = 0;
    return 0;
}

static void find_config(const OpenInfo *p) {
    char exe[260], *sl;
    FILE *t = 0;
    cfgPath[0] = 0;
    if (p->cfgFile && p->cfgFile[0] && (t = fopen(p->cfgFile, "r"))) strncpy(cfgPath, p->cfgFile, sizeof cfgPath - 1);
    if (!t) {   /* no file given (or not readable): look beside ZiNc.exe */
        if (GetModuleFileNameA(NULL, exe, sizeof exe) && (sl = strrchr(exe, '\\'))) {
            strcpy(sl + 1, "renderer.cfg");
            if ((t = fopen(exe, "r"))) strncpy(cfgPath, exe, sizeof cfgPath - 1);
        }
    }
    if (!t && (t = fopen("renderer.cfg", "r"))) strcpy(cfgPath, "renderer.cfg");
    if (t) fclose(t);
    bezelPath[0] = 0;
    if (cfgPath[0]) load_cfg(cfgPath);
}

long __stdcall ZN_GPUopen(OpenInfo *p) {
    RECT rc;
    char exe[260], *sl;
    int base;
    if (!p || p->version != 1) return -1;
    hwnd = p->hwnd;
    rotateMode = (int)(p->rotate & 3);
    wide = (p->mode == 2);
    gOpenMode = p->mode;
    VH = 1024;
    logPath[0] = 0;
    find_config(p);
    gameName[0] = 0;
    if (p->gameName && p->gameName[0]) {   /* a file name only: letters, digits and a few signs */
        int gi, gn = 0;
        for (gi = 0; p->gameName[gi] && gn < 63; gi++) { unsigned char ch = (unsigned char)p->gameName[gi]; gameName[gn++] = (ch < 32 || strchr("\\/:*?\"<>|", ch)) ? '_' : (char)ch; }
        gameName[gn] = 0;
    }
    if (cfg.logging && GetModuleFileNameA(NULL, exe, sizeof exe) && (sl = strrchr(exe, '\\'))) {   /* only with Logging=1 in renderer.cfg: otherwise no log file at all */
        strcpy(sl + 1, "zinc-d3d11.log");
        strcpy(logPath, exe);
        if ((logF = fopen(logPath, "w"))) { logT0 = GetTickCount(); logBytes = 0; }
    }
    sessionT0 = GetTickCount(); totFrames = hitchCount = over20 = over40 = over100 = 0; hitchLogged = hitchSecond = modeLogs = resetLogs = 0;
    worstFrameMs = 0; memset(histo, 0, sizeof histo); ftN = 0; lastPresentHr = S_OK; usedWarp = 0;
    statDraws = statVerts = statUploads = statUploadPx = statBlits = 0; frameDrawMs = framePresentMs = frameWaitMs = 0;
    memset(vram, 0, (size_t)VRAM_W * 1024 * 2);
    gpu_reset();
    texDirty.on = blitDirty.on = 0; qn = 0; curKey = -1;
    base = (rotateMode & 1) ? cfg.xsize : cfg.ysize;
    N = cfg.iscale > 0 ? cfg.iscale : (base + 239) / 240;
    if (N < 1) N = 1;
    if (N > 4) N = 4;
    if (cfg.xbrz == 1) N = 1;   /* xBRZ enlarges the picture drawn at the console's own resolution */
    timeBeginPeriod(1);
    hTimer = CreateWaitableTimerExW(NULL, NULL, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    if (!hTimer) hTimer = CreateWaitableTimerW(NULL, FALSE, NULL);
    lastLace.QuadPart = 0; accWaitMs = accPresentMs = maxFrameMs = accBlendMs = 0; accFrames = accReadbacks = 0;
    qNext.QuadPart = 0; skipped = 0; fpsT0 = GetTickCount(); fpsFrames = 0; fpsShown = 0;
    if (!hwnd || !IsWindow(hwnd)) return -1;
    GetWindowTextA(hwnd, origTitle, sizeof origTitle);
    setup_window();
    log_header();
    oldProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)subProc);
    SetPropA(hwnd, "ZincFullscreenHandled", (HANDLE)1);   /* the input plugin then leaves Alt+Enter to this renderer */
    GetClientRect(hwnd, &rc);
    if (!make_gpu(hwnd, rc.right - rc.left > 0 ? rc.right - rc.left : 640, rc.bottom - rc.top > 0 ? rc.bottom - rc.top : 480)) {
        logmsg("Direct3D 11 start-up FAILED");
        destroy_gpu();
        if (!GetEnvironmentVariableA("ZN_D3D11_DUMP", exe, 8)) MessageBoxA(hwnd, "Direct3D 11 could not be started (details are in zinc-d3d11.log when Enable Logs is on in ZiNc-EX).\nChoose another renderer in ZiNc-EX.", "ZiNc Direct3D 11", MB_ICONERROR);
        return -1;
    }
    isOpen = 1;
    logmsg("Direct3D 11 is running");
    return 0;
}

long __stdcall ZN_GPUclose(void) {
    log_summary();
    logmsg("closed");
    if (logF) { fclose(logF); logF = 0; }
    if (hwnd && IsWindow(hwnd)) RemovePropA(hwnd, "ZincFullscreenHandled");
    if (hwnd && IsWindow(hwnd) && oldProc) SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)oldProc);
    oldProc = 0;
    destroy_gpu();
    if (displayChanged) ChangeDisplaySettingsA(NULL, 0);
    isFullscreen = displayChanged = 0;
    if (hTimer) { CloseHandle(hTimer); hTimer = NULL; }
    if (isOpen) timeEndPeriod(1);
    isOpen = 0;
    hwnd = 0;
    return 0;
}

u32 __stdcall ZN_GPUreadStatus(void) {
    status ^= 0x80000000u;
    return status | 0x1c000000u;
}

void __stdcall ZN_GPUwriteStatus(u32 w) { call_begin(); gp1(w); call_end(); }
u32 __stdcall ZN_GPUreadData(void) { return gpu_read(); }
void __stdcall ZN_GPUwriteData(u32 w) { call_begin(); gpu_write(w); call_end(); }
void __stdcall ZN_GPUsetMode(u32 m) { (void)m; }

long __stdcall ZN_GPUgetMode(void) {
    long r = 0;
    if (imgMode == 1) r |= 1;
    if (imgMode == 2) r |= 2;
    return r;
}

long __stdcall ZN_GPUdmaSliceIn(u32 *base, u32 offset, u32 count) {
    u32 i;
    call_begin();
    for (i = 0; i < count; i++) gpu_write(base[offset + i]);
    call_end();
    status |= 0x14000000u;
    return 0;
}

long __stdcall ZN_GPUdmaSliceOut(u32 *base, u32 offset, u32 count) {
    u32 i;
    if (imgMode != 2) return 0;
    for (i = 0; i < count && imgMode == 2; i++) base[offset + i] = gpu_read();
    status |= 0x14000000u;
    return 0;
}

long __stdcall ZN_GPUdmaChain(u32 *base, u32 addr) {
    u32 guard = 0, prevA = 0xffffffffu, prevB = 0xffffffffu;
    addr &= 0xffffff;
    call_begin();
    do {
        u32 hdr, count, i, a = addr & 0xfffffc;
        if (addr == 0xffffff) break;
        if (addr == prevA || addr == prevB) break;     /* chain loops back on itself */
        prevB = prevA; prevA = addr;
        hdr = base[a >> 2];
        count = hdr >> 24;
        for (i = 0; i < count; i++) gpu_write(base[(a >> 2) + 1 + i]);
        addr = hdr & 0xffffff;
    } while (addr != 0 && addr != 0xffffff && guard++ < 2000000);
    call_end();
    status |= 0x14000000u;
    return 0;
}

void __stdcall ZN_GPUupdateLace(void) {
    DWORD now;
    int skip = pace_frame();
    LARGE_INTEGER q0, q1;
    if (isFullscreen && (fpsFrames & 15) == 0) check_fullscreen();
    QueryPerformanceCounter(&q0);
    if (!skip) present();
    else { sync_all(); }
    QueryPerformanceCounter(&q1);
    accPresentMs += qpc_ms(q1.QuadPart - q0.QuadPart);
    framePresentMs += qpc_ms(q1.QuadPart - q0.QuadPart);
    if (lastLace.QuadPart) { double f = qpc_ms(q1.QuadPart - lastLace.QuadPart); if (f > maxFrameMs) maxFrameMs = f; log_frame(f, frame_seconds() * 1000.0); }
    lastLace = q1;
    accFrames++;
    fpsFrames++;
    now = GetTickCount();
    if (now - fpsT0 >= 1000) {
        fpsShown = fpsFrames; fpsFrames = 0; fpsT0 = now;
        log_second(fpsShown);
        accWaitMs = accPresentMs = maxFrameMs = accBlendMs = accDrawMs = maxCallMs = 0; accFrames = accReadbacks = 0;
    }
}

void __stdcall ZN_GPUmakeSnapshot(void) { snapRequested = 1; }
long __stdcall ZN_GPUfreeze(u32 get, void *data) { (void)get; (void)data; return 0; }
