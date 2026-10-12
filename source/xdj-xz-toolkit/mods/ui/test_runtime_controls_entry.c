/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#define XZ_STEM_COLOR_CAPTURE_TEST
static uint32_t captured_stem_colors[3];
#include "test_runtime_controls.c"
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;memcpy(captured_stem_colors,rgb,sizeof(captured_stem_colors));}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;return -1;}
