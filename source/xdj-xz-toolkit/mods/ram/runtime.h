/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_RAM_RUNTIME_H
#define XZ_RAM_RUNTIME_H
#include <stdint.h>
#include "../audio/native_reader.h"
enum xz_ram_state { XZ_RAM_UNAVAILABLE, XZ_RAM_EMPTY, XZ_RAM_LOADING,
    XZ_RAM_READY, XZ_RAM_ERROR, XZ_RAM_DETACHED };
struct xz_ram_status {
    enum xz_ram_state state;
    uint32_t copied, bytes;
    char message[96];
};
int xz_ram_start(void);
void xz_ram_stop(void);
void xz_ram_invalidate(int deck);
void xz_ram_bind(int deck, const struct xz126_deck_source *);
void xz_ram_status(int deck, struct xz_ram_status *);
int xz_ram_request(int deck);
int xz_ram_cancel(int deck);
int xz_ram_release(int deck);
int xz_ram_assets_reserve(uint64_t bytes);
void xz_ram_assets_publish(uint64_t bytes);
void xz_ram_assets_cancel(uint64_t bytes);
void xz_ram_assets_release(uint64_t bytes);
int xz_ram_retained_source(unsigned deck,struct xz126_deck_source *);
#endif
