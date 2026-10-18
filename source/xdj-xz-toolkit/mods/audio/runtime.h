/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef XZ_AUDIO_RUNTIME_H
#define XZ_AUDIO_RUNTIME_H
#include "stems.h"
#include "overcue.h"
#include <stdint.h>
struct xz_audio_batch {void *previous,*deck,*publication;uint32_t generation;};
void xz_audio_batch_begin(int deck,int32_t position,struct xz_audio_batch *);
void xz_audio_batch_end(struct xz_audio_batch *);

enum xz_audio_state {
    XZ_AUDIO_STOPPED, XZ_AUDIO_WAITING_TRACK, XZ_AUDIO_LOADING_TRACK,
    XZ_AUDIO_SOURCE_UNSUPPORTED, XZ_AUDIO_CACHE_MISSING,
    XZ_AUDIO_CACHE_AMBIGUOUS, XZ_AUDIO_DECODING, XZ_AUDIO_DECODE_FAILED,
    XZ_AUDIO_ALIGNMENT_BLOCKED, XZ_AUDIO_EXPERIMENTAL_READY,
    XZ_AUDIO_HOOK_FAILED, XZ_AUDIO_DISABLED, XZ_AUDIO_CACHE_CHOICE_INVALID,
    XZ_AUDIO_PREPARED_ALIGNING, XZ_AUDIO_PREPARED_BUFFERING, XZ_AUDIO_PREPARED_ERROR
};
struct xz_audio_status {
    enum xz_audio_state state;
    uint32_t generation;
    uint32_t reader_rate;
    uint32_t reader_frames;
    uint32_t mixed_blocks;
    uint32_t skipped_blocks;
    size_t pcm_bytes;
    char path[1024];
    int prepared, alignment_frames;
    float alignment_correlation;
    size_t retained_stems_bytes;
    unsigned stem_count;
    uint32_t starve_runs, starve_last_frames, starve_max_frames, starve_last_ms, starve_max_ms;
    enum xz_oc_starvation starvation, starve_fallback;
    char format_name[32], error[160];
};
int xz_audio_start(void);
void xz_audio_stop(void);
void xz_audio_set_enabled(int enabled);
void xz_audio_set_levels(int deck, struct xz_stem_levels levels);
void xz_audio_set_levels4(int deck, const float levels[4]);
/* UI controls bind to a source generation; a late command cannot affect the next track. */
int xz_audio_set_levels4_for_generation(int deck,uint32_t generation,const float levels[4]);
int xz_audio_get_status(int deck, struct xz_audio_status *out);
int xz_audio_waveform(int deck, unsigned role, unsigned char *bins, size_t count, float *progress);
/* Audio-thread lookup for a manager captured by the successful load snapshot.
 * Atomic 32-bit reads only. The returned generation changes only for a true
 * stream discontinuity: load, unload, or runtime stop. Stem enable/disable does
 * not invalidate the identity. */
int xz_audio_output_deck(const void *manager, uint32_t *rate_out,
                         uint32_t *generation_out);
const char *xz_audio_state_name(enum xz_audio_state state);
/* Host/target self-test for publication and load-generation cancellation.
 * Does not install hooks, read firmware or access any device. */
int xz_audio_lifecycle_self_test(void);
#endif
