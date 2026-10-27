/* SPDX-License-Identifier: MIT */
#ifndef XZ_KEY_CONTROL_H
#define XZ_KEY_CONTROL_H
#include "music.h"
#include "../sync_master.h"
struct xz_key_owner {
 uint32_t native_epoch,route_epoch,load_revision;
 uint64_t media_generation;
};
enum xz_key_audio_state { XZ_KEY_AUDIO_UNKNOWN, XZ_KEY_AUDIO_PREPARED,
 XZ_KEY_AUDIO_APPLIED, XZ_KEY_AUDIO_TRANSIENT, XZ_KEY_AUDIO_EMERGENCY };
struct xz_deck_key {
 struct xz_key_owner owner;
 struct xz_music_key original;
 struct xz_playing_key playing;
 uint32_t request_revision,applied_revision,transport_generation;
 int desired_cents,applied_cents,metadata_valid,engine_ready,playing_valid;
 int master_tempo,engaged,target_deck,mt_before;
 enum xz_key_audio_state audio_state;
};
enum xz_key_command_kind { XZ_KEY_STEP, XZ_KEY_RESET, XZ_KEY_SYNC_TOGGLE };
struct xz_key_command {
 enum xz_key_command_kind kind;
 struct xz_key_owner source_owner,target_owner;
 struct xz_playing_key target;
 struct xz_master_observation master;
 int step,target_deck,target_valid;
};
enum xz_key_refusal { XZ_KEY_OK, XZ_KEY_NO_SOURCE, XZ_KEY_NO_MASTER,
 XZ_KEY_IS_MASTER, XZ_KEY_NO_TARGET, XZ_KEY_EMERGENCY, XZ_KEY_RANGE };
int xz_key_owner_equal(int deck,struct xz_key_owner a,struct xz_key_owner b);
enum xz_key_refusal xz_key_plan_sync(int deck,const struct xz_deck_key *source,
 const struct xz_deck_key *target,const struct xz_master_observation *,struct xz_key_command *);
const char *xz_key_refusal_text(enum xz_key_refusal);
/* Non-audio control entry. Worker validation uses only published owner caches. */
int xz_key_control_request(int logical_deck,enum xz_key_command_kind,int step);
int xz_key_owner_current(int logical_deck,struct xz_key_owner *);
#endif
