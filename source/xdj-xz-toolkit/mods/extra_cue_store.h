/* SPDX-License-Identifier: MIT */
#ifndef XZ_EXTRA_CUE_STORE_H
#define XZ_EXTRA_CUE_STORE_H
#include "extra_decks.h"
#define XZ_CUE_MEMORY XZ_EXTRA_MEMORY_CUES
struct xz_saved_cues { struct xz_extra_cue hot[8],memory[XZ_CUE_MEMORY];unsigned memory_count; };
enum xz_cue_edit_kind { XZ_CUE_EDIT_HOT, XZ_CUE_EDIT_MEMORY, XZ_CUE_EDIT_DELETE };
struct xz_cue_edit { enum xz_cue_edit_kind kind;unsigned pad;struct xz_extra_cue point; };
typedef int (*xz_cue_read_fn)(const struct xz_extra_track *,uint64_t,int32_t,struct xz_saved_cues *);
/* Enqueue only: the audio worker must never wait for a filesystem write. */
typedef int (*xz_cue_write_fn)(int,const struct xz_extra_track *,uint64_t,int32_t,
                             const struct xz_saved_cues *,const struct xz_cue_edit *);
int xz_cues_read_at(int,uint32_t,int32_t,struct xz_saved_cues *);
int xz_cues_write_at(int,uint32_t,int32_t,const struct xz_saved_cues *);
int xz_cues_apply(struct xz_saved_cues *,const struct xz_cue_edit *);
int xz_cues_worker_start(int (*)(int,uint32_t,uint64_t,struct xz_extra_track *),
                        void (*)(const char *),const char *,const char *);
void xz_cues_worker_stop(void);
int xz_cues_restore(const struct xz_extra_track *,uint64_t,int32_t,struct xz_saved_cues *);
int xz_cues_enqueue(int,const struct xz_extra_track *,uint64_t,int32_t,
                    const struct xz_saved_cues *,const struct xz_cue_edit *);
#endif
