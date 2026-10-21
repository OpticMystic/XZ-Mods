/* SPDX-License-Identifier: MIT */
#ifndef XZ_SYNC_LINK_PLAN_H
#define XZ_SYNC_LINK_PLAN_H
#include <stdint.h>
#define XZ_SYNC_LINK_MAX_AGE_US 100000u
#define XZ_SYNC_LINK_MAX_UNCERTAINTY_US 2000u
#define XZ_SYNC_LINK_CALIBRATION_AGE_US 1000000u
enum xz_sync_link_reason {
 XZ_SYNC_LINK_OK, XZ_SYNC_LINK_BAD_INPUT, XZ_SYNC_LINK_SLOT_BUSY,
 XZ_SYNC_LINK_LEASE_LOST, XZ_SYNC_LINK_SOURCE_CHANGED, XZ_SYNC_LINK_NO_COMPLETED_PCM,
 XZ_SYNC_LINK_STALE_PCM, XZ_SYNC_LINK_NO_PHASE, XZ_SYNC_LINK_CLOCK_UNCERTAIN,
 XZ_SYNC_LINK_CLOCK_STEP, XZ_SYNC_LINK_TIME_RANGE
};
struct xz_sync_link_clock_pair { uint64_t mono_us,realtime_us;uint32_t uncertainty_us; };
struct xz_sync_link_clock { int valid,stepped;int64_t offset_us;struct xz_sync_link_clock_pair pair; };
struct xz_sync_link_claim {
 uint32_t source_id;                 /* reserved candidate IDs3/4 only */
 uint64_t owner,route_epoch,source_revision,playback_epoch,external_writer_epoch,master_choice_epoch;
 unsigned char music_id[8];          /* exact known native music_id; never fabricate from a title */
 int music_id_known,foreign_present;
 int32_t previous_master_id;         /* effective native ID; -2 unknown is refused */
};
struct xz_sync_link_lease { struct xz_sync_link_claim claim;int active,master_committed;uint64_t publication; };
struct xz_sync_link_observation {
 uint64_t external_writer_epoch,master_choice_epoch;
 int foreign_present,payload_still_ours;
 int32_t current_master_id;
};
struct xz_sync_link_cleanup { int inhibit_phase,clear_owned_payload,restore_master;int restore_input_id; };
struct xz_sync_link_pcm_anchor {
 uint64_t route_epoch,source_revision,playback_epoch,completed_output_frame,captured_mono_us;
 unsigned char music_id[8];
 int completed_pcm,clock_domain_mapped,playing,phase_eligible;
 uint32_t source_rate,bpm100,uncertainty_us;
 double source_frame,beat_source_frame,effective_speed;
 unsigned beat_in_bar,lane;
 uint32_t future_beat_ms[8];          /* actual projected PCM grid intervals after current beat */
};
struct xz_sync_link_plan {
 int valid;
 enum xz_sync_link_reason reason;
 uint64_t publication,owner,route_epoch,source_revision,playback_epoch,external_writer_epoch,master_choice_epoch;
 uint32_t source_id,bpm100,q20_speed;
 unsigned char music_id[8],beat_packet[60];
 int32_t beat_sec,beat_usec;
 uint32_t uncertainty_us;
 int request_master,input_master_id; /* request intent only; not native acceptance or audio lock */
};
/* Pure owner-serialized planning. No native writes, hooks, clock calls, I/O or allocation.
   A runtime arbiter must serialize native writers and native consumer-frame snapshots BEFORE use.
   All anchors must describe completed PCM, not a queued/render-request playhead. */
int xz_sync_link_clock_update(struct xz_sync_link_clock *,const struct xz_sync_link_clock_pair *);
int xz_sync_link_lease_claim(struct xz_sync_link_lease *,const struct xz_sync_link_claim *);
int xz_sync_link_lease_observe(struct xz_sync_link_lease *,const struct xz_sync_link_observation *,struct xz_sync_link_cleanup *);
void xz_sync_link_lease_release(struct xz_sync_link_lease *,const struct xz_sync_link_observation *,struct xz_sync_link_cleanup *);
int xz_sync_link_build(struct xz_sync_link_lease *,const struct xz_sync_link_clock *,const struct xz_sync_link_pcm_anchor *,
 uint64_t now_mono_us,struct xz_sync_link_plan *);
#endif
