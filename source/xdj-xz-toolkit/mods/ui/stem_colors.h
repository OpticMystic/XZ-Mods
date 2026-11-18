/* SPDX-License-Identifier: MIT */
#ifndef XZ_STEM_COLORS_H
#define XZ_STEM_COLORS_H
#include <stdint.h>
#define XZ_STEM_COLOR_CHOICES 9
/* 0 follows the active theme; the other choices stay fixed across themes. */
static inline uint32_t xz_stem_palette_color(int choice,uint32_t fallback) {
 static const uint32_t colors[XZ_STEM_COLOR_CHOICES]={0,0x2997ff,0xff3b30,0x30d158,0xff9f0a,0xbf5af2,0x64d2ff,0xff375f,0xffffff};
 return choice>0&&choice<XZ_STEM_COLOR_CHOICES?colors[choice]:fallback;
}
static inline const char *xz_stem_palette_name(int choice) {
 static const char *const names[XZ_STEM_COLOR_CHOICES]={"THEME","BLUE","RED","GREEN","AMBER","PURPLE","CYAN","PINK","WHITE"};
 return names[choice>=0&&choice<XZ_STEM_COLOR_CHOICES?choice:0];
}
#endif
