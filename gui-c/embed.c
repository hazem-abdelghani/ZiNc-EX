/* The two plugins are built into the launcher: .incbin pulls the files into the exe. */
#include "common.h"
#define EMBED(sym, file) \
    __asm__(".section .rdata,\"dr\"\n.balign 16\n.globl " #sym "_data\n" #sym "_data:\n.incbin \"" file "\"\n.globl " #sym "_end\n" #sym "_end:\n.byte 0\n.text\n"); \
    extern const unsigned char sym##_data[] __asm__(#sym "_data"); \
    extern const unsigned char sym##_end[] __asm__(#sym "_end");
EMBED(input_plugin, "zinc-input.znc")
EMBED(d3d11_plugin, "d3d11.znc")
EMBED(renderer_cfg, "renderer_default.cfg")
const unsigned char *embedded_input(size_t *n) { *n = (size_t)(input_plugin_end - input_plugin_data); return input_plugin_data; }
const unsigned char *embedded_d3d11(size_t *n) { *n = (size_t)(d3d11_plugin_end - d3d11_plugin_data); return d3d11_plugin_data; }
const unsigned char *embedded_renderer_cfg(size_t *n) { *n = (size_t)(renderer_cfg_end - renderer_cfg_data); return renderer_cfg_data; }
