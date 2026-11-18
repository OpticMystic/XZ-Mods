#ifndef XZ_CLASSIC_POPUP_H
#define XZ_CLASSIC_POPUP_H
#include <stddef.h>
#include <stdint.h>
#define XZ_CP_CAPACITY 20u
#define XZ_CP_TOKEN_PREFIX 0x58430000u
#define XZ_CP_TIMEOUT_MS 1000u
struct xz_cp_row { uint32_t reserved, kind; uint16_t text_tag, reserved2; uint32_t reserved3; uint16_t text[256]; };
_Static_assert(sizeof(struct xz_cp_row)==0x210,"native popup row stride");
_Static_assert(offsetof(struct xz_cp_row,text)==0x10,"native popup UTF16 offset");
/* Atomic with respect to validation: invalid native shape leaves all bytes unchanged. */
int xz_cp_extend(struct xz_cp_row *rows, uint32_t *count);
/* 0/1 native deck; 2/3 extra deck; -1 cue/cancel; -2 invalid/unknown. */
int xz_cp_target(const struct xz_cp_row *rows, uint32_t count, uint32_t index);
struct xz_cp_pending { uint32_t token, until_ms, route_epoch; uint64_t media_generation[2]; };
struct xz_cp_state { uint32_t serial; struct xz_cp_pending pending[2]; };
struct xz_cp_load { unsigned deck, source; uint32_t track_id, route_epoch; uint64_t media_generation; };
uint32_t xz_cp_begin(struct xz_cp_state *, unsigned extra, uint32_t route_epoch, const uint64_t media[2], uint32_t now);
/* 0 unowned, 1 current request refused, 2 ready, 3 obsolete/duplicate (silent). Owned tokens NEVER reach native stock. */
int xz_cp_reply(struct xz_cp_state *, uint32_t token, uint32_t route_epoch, uint32_t hi, uint32_t lo, uint32_t now, struct xz_cp_load *out);
#endif
