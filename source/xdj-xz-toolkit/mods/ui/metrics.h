/* SPDX-License-Identifier: MIT */
#ifndef XZ_METRICS_H
#define XZ_METRICS_H
#include <stdint.h>
#include <stddef.h>
#include "top_bar.h"
enum { XZ_METRICS_CPU=1, XZ_METRICS_RAM=2, XZ_METRICS_TEMP=4 };
/* Top-bar columns (top_bar.h); the readout starts two pixels after DECKS (162..277), as the other gaps. */
enum { XZ_METRICS_X=594, XZ_METRICS_W=78, XZ_METRICS_READOUT_X=280, XZ_METRICS_READOUT_W=312 };
struct xz_metrics_sample {
 uint32_t sampled_ms,valid,cpu_permille,core_permille[4];
 uint32_t used_mib,total_mib,free_mib,cache_mib;
 int32_t temperature_c;
};
struct xz_metrics_counters { uint64_t cpu[5][8]; unsigned count; int initialized; };
/* Parse collected text only. No I/O, threads, allocation or device mutation. */
void xz_metrics_parse(struct xz_metrics_counters *,struct xz_metrics_sample *,const char *,const char *,const char *,const char *,uint32_t);
int xz_metrics_temperature_level(int celsius);
int xz_metrics_fresh(const struct xz_metrics_sample *,uint32_t now);
int xz_metrics_hit(int x,int y);
void xz_metrics_draw(uint16_t *,size_t stride,int theme,int enabled,const struct xz_metrics_sample *,uint32_t now);
#endif
