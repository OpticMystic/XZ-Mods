/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RAM_RUNTIME_H
#define XZ_RAM_RUNTIME_H
#include <stdint.h>
#include "../audio/native_reader.h"
#include "status.h"
int xz_ram_start(void);
void xz_ram_stop(void);
void xz_ram_invalidate(int deck);
void xz_ram_bind(int deck, const struct xz126_deck_source *);
void xz_ram_status(int deck, struct xz_ram_status *);
int xz_ram_request(int deck);
enum xz_ram_result xz_ram_native_command(int deck, enum xz_ram_command, uint64_t expected);
int xz_ram_cancel(int deck);
int xz_ram_release(int deck);
int xz_ram_assets_reserve(uint64_t bytes);
void xz_ram_assets_publish(uint64_t bytes);
void xz_ram_assets_cancel(uint64_t bytes);
void xz_ram_assets_release(uint64_t bytes);
int xz_ram_retained_source(unsigned deck,struct xz126_deck_source *);
#endif
