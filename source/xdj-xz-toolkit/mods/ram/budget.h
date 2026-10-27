/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_LEDGER_H
#define XZ_RAM_LEDGER_H
#include <stdint.h>
#include "cache.h"
enum xz_ram_allocation { XZ_RAM_ORIGINAL, XZ_RAM_ASSETS };
enum xz_ram_admission { XZ_RAM_ADMITTED, XZ_RAM_BAD_SIZE, XZ_RAM_FULL, XZ_RAM_LOW_MEMORY };
/* Move-only: zero the old owner after transfer. Release only after final unmap/close. */
struct xz_ram_ticket { uint64_t bytes; unsigned assets, held; };
struct xz_ram_usage { uint64_t original_pending, original_held, assets_pending, assets_held; };
uint64_t xz_ram_round(uint64_t);
enum xz_ram_admission xz_ram_reserve(struct xz_ram_ticket *, enum xz_ram_allocation, uint64_t);
void xz_ram_publish(struct xz_ram_ticket *);
void xz_ram_return(struct xz_ram_ticket *);
void xz_ram_usage(struct xz_ram_usage *);
/* Serializes original-file materialization, never held by a playback/UI worker. */
void xz_ram_copy_lock(int manual);
void xz_ram_copy_unlock(void);
/* Test injection, installed before consumers start. NULL restores sysinfo. */
void xz_ram_free_sampler(uint64_t (*)(void));
/* Existing prepared-stream ABI. Each input is page-rounded by the pin owner. */
int xz_ram_assets_reserve(uint64_t);
void xz_ram_assets_publish(uint64_t);
void xz_ram_assets_cancel(uint64_t);
void xz_ram_assets_release(uint64_t);
#endif
