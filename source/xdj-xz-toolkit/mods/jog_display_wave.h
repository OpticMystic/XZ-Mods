/* SPDX-License-Identifier: MIT */
#ifndef XZ_JOG_DISPLAY_WAVE_H
#define XZ_JOG_DISPLAY_WAVE_H
#include "extra_decks.h"
#define XZ_JD_OVERVIEW_COLUMNS 300u
#define XZ_JD_RGB_WAVE_PACKETS 73u
#define XZ_JD_WAVE_PAYLOAD 4234u
struct xz_jd_overview {
 uint32_t count,next,cell,normalization;
 struct xz_extra_column column[XZ_JD_OVERVIEW_COLUMNS];
};
struct xz_jd_wave { uint8_t payload[XZ_JD_WAVE_PAYLOAD]; };
int xz_jd_overview_begin(struct xz_jd_overview *,uint32_t count,uint32_t normalization);
/* Ordered owned chunks only. Equal-height ties retain the earlier whole column. */
int xz_jd_overview_add(struct xz_jd_overview *,uint32_t first,const struct xz_extra_column *,uint32_t count);
int xz_jd_overview_complete(const struct xz_jd_overview *);
/* Off-assembler: derived overview to the native RGB input representation. */
int xz_jd_overview_encode(const struct xz_jd_overview *,struct xz_jd_wave *);
/* Proven native producer conversion; input is 300 six-byte RGB/height cells. */
void xz_jd_wave_encode_rgb(const uint8_t input[1800],unsigned header_flag,struct xz_jd_wave *);
/* One fixed 64-byte packet, physical receiver 1/2, packet index 0..72. */
int xz_jd_wave_packet(const struct xz_jd_wave *,unsigned side,unsigned index,uint8_t out[64]);
#endif
