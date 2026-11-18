/* SPDX-License-Identifier: MIT */
#ifndef XZ_PC_WAVE_H
#define XZ_PC_WAVE_H
#include <stdint.h>
#include <stddef.h>
#include "../ui/layered_wave_source.h"
/* The worker is the only owner of file/state IO. Draw threads query a stamp. */
int xz_pc_wave_start(void);
void xz_pc_wave_state(void);
int xz_pc_wave_load(unsigned physical,struct xz_wave_load *,unsigned *slot,uint32_t *epoch,uint32_t *serial);
int xz_pc_wave_current(unsigned physical,unsigned slot,uint32_t epoch,uint32_t serial,uint32_t count);
/* Pure boundary parser for corruption/stale-generation tests. */
int xz_pc_wave_packet(const uint8_t *,size_t,unsigned,uint64_t,uint32_t,uint32_t,const uint8_t[32],struct xz_wave_load *);
unsigned xz_pc_wave_available_roles(void);
int xz_pc_mode_packet(const unsigned char *,size_t,uint64_t,unsigned *);
#endif
