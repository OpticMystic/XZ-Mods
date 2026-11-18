/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RAM_REMOVAL_H
#define XZ_RAM_REMOVAL_H
int xz_ram_removal_start(void);
int xz_ram_preserve_usb(int deck, unsigned device);
#include <stdint.h>
int xz_ram_preserve_sentinel(uintptr_t player,unsigned device,unsigned node);
int xz_ram_preserve_player(int deck,unsigned device,uintptr_t player);
int xz_ram_ready_player(int deck,unsigned device,uintptr_t player);
void xz_ram_forget_player(uintptr_t player,unsigned device,unsigned node);
#endif
