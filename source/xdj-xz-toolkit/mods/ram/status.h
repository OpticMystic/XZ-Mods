/* SPDX-License-Identifier: MIT */
#ifndef XZ_RAM_STATUS_H
#define XZ_RAM_STATUS_H
#include <stdint.h>
enum xz_ram_state { XZ_RAM_UNAVAILABLE, XZ_RAM_EMPTY, XZ_RAM_LOADING,
    XZ_RAM_READY, XZ_RAM_ERROR, XZ_RAM_DETACHED };
enum xz_ram_reason { XZ_RAM_OK, XZ_RAM_NO_ENGINE, XZ_RAM_NO_ROUTE, XZ_RAM_NO_SOURCE,
    XZ_RAM_TOO_LARGE, XZ_RAM_BUDGET_FULL, XZ_RAM_FREE_LOW, XZ_RAM_PIN_FAILED,
    XZ_RAM_INTERRUPTED, XZ_RAM_SOURCE_CHANGED, XZ_RAM_HANDOFF_FAILED };
enum xz_ram_phase { XZ_RAM_IDLE, XZ_RAM_QUEUED, XZ_RAM_COPYING, XZ_RAM_ATTACHING, XZ_RAM_RELEASING };
enum xz_ram_command { XZ_RAM_LOAD, XZ_RAM_CANCEL, XZ_RAM_RELEASE };
enum xz_ram_result { XZ_RAM_ACCEPTED, XZ_RAM_ALREADY_HELD, XZ_RAM_STALE, XZ_RAM_ROUTE_DOWN,
    XZ_RAM_SOURCE_MISSING, XZ_RAM_REFUSED };
enum { XZ_RAM_AUDIO = 1, XZ_RAM_WAVE = 2, XZ_RAM_GRID = 4, XZ_RAM_CUES = 8 };
struct xz_ram_track_key { uint64_t session, media_generation; uint32_t track_id; unsigned source; };
struct xz_ram_status {
    enum xz_ram_state state;
    uint32_t copied, bytes;
    char message[96];
    enum xz_ram_reason reason;
    enum xz_ram_phase phase;
    struct xz_ram_track_key key;
    unsigned supported, route_up, source_current, can_request, can_release, retained_assets;
};
#endif
