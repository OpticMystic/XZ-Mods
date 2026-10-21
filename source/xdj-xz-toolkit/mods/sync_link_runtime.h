/* SPDX-License-Identifier: MIT */
#ifndef XZ_SYNC_LINK_RUNTIME_H
#define XZ_SYNC_LINK_RUNTIME_H
#include <stdatomic.h>
#include <stdint.h>
#include "extra_decks.h"
#include "sync_link_plan.h"

struct xz_va_bundle {
 uint32_t source_id;
 uint64_t captured_mono_us; /* Immutable while the slot has readers; no 64-bit atomics. */
 unsigned char status[56];
 unsigned char beat[44];
};
struct xz_va_slot {
 _Atomic uint32_t refs;
 struct xz_va_bundle value;
};
struct xz_va {
 _Atomic uint32_t foreign_epoch[2],operator_epoch,active,pending,published,source_id,claim_foreign,claim_operator,lease_generation,valid_generation,producer_guard;
 struct xz_va_slot slots[3];
};
struct xz_va_frame {
 uint32_t depth,mode,slot,source_id,foreign_epoch,operator_epoch,lease_generation;
 void *expected_link_mgr;
 void *draining_sync_master;
 int probing_original;
};
struct xz_va_registry_slot {
 _Atomic uintptr_t owner;
 struct xz_va_frame frame;
};
struct xz_va_registry {
 struct xz_va_registry_slot slots[4];
};

void xz_va_init(struct xz_va *);
void xz_va_registry_init(struct xz_va_registry *);
struct xz_va_frame *xz_va_registry_enter(struct xz_va *,struct xz_va_registry *,uintptr_t thread_identity);
struct xz_va_frame *xz_va_registry_lookup(struct xz_va_registry *,uintptr_t thread_identity);
void xz_va_registry_leave(struct xz_va *,struct xz_va_registry *,uintptr_t thread_identity);
uint32_t xz_va_probe_epoch(struct xz_va *,uint32_t source_id);
int xz_va_claim(struct xz_va *,uint32_t source_id,int original_slot_absent,uint32_t probed_foreign_epoch);
uint32_t xz_va_ticket(struct xz_va *);
int xz_va_offer(struct xz_va *,const struct xz_va_bundle *,uint32_t prepared_generation);
void xz_va_explicit_request(struct xz_va *);
void xz_va_foreign(struct xz_va *,uint32_t source_id);
void xz_va_operator(struct xz_va *);
int xz_va_begin(struct xz_va *,struct xz_va_frame *);
int xz_va_take_request(struct xz_va *,struct xz_va_frame *);
int xz_va_get(struct xz_va *,struct xz_va_frame *,unsigned kind,void *out);
void xz_va_end(struct xz_va *,struct xz_va_frame *);

void xz_sync_link_bundle_from_plan(const struct xz_sync_link_plan *,struct xz_va_bundle *);
int xz_sync_link_anchor_from_deck(int deck_index,const struct xz_extra_deck *,uint64_t now_mono_us,struct xz_sync_link_pcm_anchor *);

int xz_sync_link_runtime_start(void);
void xz_sync_link_runtime_stop(void);
void xz_sync_link_runtime_step(const struct xz_extra_decks *,const struct xz_sync_link_clock_pair *);
void xz_sync_link_runtime_poll(const struct xz_extra_decks *);
int xz_sync_link_runtime_master_committed(int deck_index);

/* Test seam for exercising the exact hook callbacks without hardware. */
struct xz_sync_link_test_ops {
 int (*sync_mgr_sync)(void *sync_mgr);
 int (*get_player_info)(void *link_mgr,const int *id_ptr,void *out);
 int (*get_beat_info)(void *link_mgr,const int *id_ptr,const uint8_t *lane_ptr,void *out);
 int (*set_player_info)(void *link_mgr,const int *id_ptr,const void *a2,const void *a3,const void *a4,
                        const void *a5,const void *a6,const void *a7,const void *a8,const void *a9,
                        const void *a10,const void *a11,const void *a12,const void *a13);
 int (*set_beat_info)(void *link_mgr,const int *id_ptr,const void *lane_ptr,const void *packet);
 int (*set_master_on)(void *sync_master,const int *master_id);
 int (*set_link_master)(void *sync_master,const int *input_id);
 void (*on_use_native_master)(void);
 int (*monotonic_us)(uint64_t *out);
};
void xz_sync_link_runtime_test_install(const struct xz_sync_link_test_ops *originals,struct xz_sync_link_test_ops *hooks_out);
#endif
