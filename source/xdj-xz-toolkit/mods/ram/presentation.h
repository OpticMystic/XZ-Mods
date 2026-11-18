/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RAM_PRESENTATION_H
#define XZ_RAM_PRESENTATION_H
#include <stdint.h>
int xz_ram_presentation_start(void);
void xz_ram_presentation_snapshot(int deck,uint32_t epoch);
int xz_ram_music_snapshot(int deck,uint32_t epoch,const uint32_t record[7]);
int xz_ram_music_owned_epoch(unsigned deck,uint32_t record[7],uint32_t *epoch);
int xz_ram_music_retained_epoch(unsigned deck,uint32_t record[7],uint32_t *epoch);
int xz_ram_music_retained(unsigned deck,uint32_t record[7]);
#endif
