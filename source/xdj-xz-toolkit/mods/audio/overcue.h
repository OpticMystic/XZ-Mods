/* SPDX-License-Identifier: MIT */
#ifndef XZ_OVERCUE_H
#define XZ_OVERCUE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../ram/pin.h"

#define XZ_OC_PAGE 131072u
#define XZ_OC_RATE 96000u
#define XZ_OC_PAGE_SLOTS 8u
#define XZ_OC_MAX_PAGES 4096u
#define XZ_OC_COMPRESSED_MAX (XZ_OC_PAGE * 2u)
#define XZ_OC_DECODER_AREA (1024u * 1024u)
#define XZ_OC_WINDOW_FRAMES (XZ_OC_RATE * 2u)
struct xz_oc_page { uint64_t offset; uint32_t compressed, bytes; unsigned char sha[32]; };
struct xz_oc_file {
    FILE *file;
    struct xz_oc_page *pages;
    uint32_t page_count, cached_page[XZ_OC_PAGE_SLOTS], age[XZ_OC_PAGE_SLOTS], clock;
    uint64_t bytes;
    unsigned codec, sample_rate, has_pcm_sha;
    unsigned char pcm_sha[32];
    unsigned char *raw, *compressed;
    unsigned char *arena; size_t arena_used; /* the FLAC decoder's working area, reused for every page */
    struct xz_ram_file_pin ram;
};
/* Drums, harmonics and vocal singles plus the prepared full mix. Pair files
 * (instrumental, vocals-drums, vocals-harmonics) are never opened: four-file
 * bundles omit them, and every pair is mixed from the singles. */
struct xz_oc_assets {
    struct xz_oc_file role[3], reference;
    uint64_t frames;
    unsigned sample_rate;
    float gain[3];
    unsigned char *wave[3];
    uint32_t wave_count[3];
    unsigned source_hash_verified, pair_files;
};
/* Worker-only immutable USB reader. Opens only the source's exact indexed bundle,
 * verifies source identity, metadata/table hashes, and each decoded page. */
int xz_oc_open(const char *source, struct xz_oc_assets *, char *error, size_t capacity);
int xz_oc_assets_pin(struct xz_oc_assets *);
/* 1 where the RAM pin service exists (the unit); 0 in host builds, which read the files directly. */
int xz_oc_assets_pin_supported(void);
/* xz_oc_assets_pin with a poll asked during the copy: 0 carry on, above 0 wait, below 0 abandon (returns 0). */
int xz_oc_assets_pin_polled(struct xz_oc_assets *,int (*poll)(void *),void *context);
size_t xz_oc_assets_ram_bytes(const struct xz_oc_assets *);
void xz_oc_close(struct xz_oc_assets *);
/* Owning worker only: closes the four audio files and returns their RAM. Frame count, gains and waveform
 * bins stay; later reads return silence. For a stream whose audio is fully held elsewhere. */
void xz_oc_assets_release_audio(struct xz_oc_assets *);
/* Worker-only bundle lookup. A missing exact path falls back to the complete
 * source SHA-256; no stem audio is decoded. Conflicting identities are refused. */
int xz_oc_bundle(const char *source, char *folder, size_t capacity);
int xz_oc_read(struct xz_oc_file *, int64_t first, size_t frames, int16_t *stereo);
/* Worker/desktop verification: checks every page and the complete indexed PCM hash. */
int xz_oc_verify(struct xz_oc_file *);
int xz_oc_verify_role(const char *path, uint64_t frames, const char *table_sha, const char *pcm_sha);
/* Masks 1, 2 and 4 select a single; 7 selects the full mix. Others are NULL. */
struct xz_oc_file *xz_oc_mix_file(struct xz_oc_assets *, unsigned mask);
float xz_oc_mix_gain(const struct xz_oc_assets *, unsigned mask);
struct xz_oc_plan { float source, gain[3]; unsigned mask[3]; };
int xz_oc_plan_levels(const float levels[3], struct xz_oc_plan *);
int xz_oc_plan_levels4(const float levels[4], struct xz_oc_plan *);
/* Call only on an owned background worker, never a native/audio thread. */
int xz_oc_worker_schedule(void);

struct xz_oc_stream;
enum xz_oc_state { XZ_OC_ALIGNING, XZ_OC_READY, XZ_OC_BUFFERING, XZ_OC_ERROR };
enum xz_oc_starvation { XZ_OC_STARVE_NONE, XZ_OC_STARVE_HELD, XZ_OC_STARVE_UNITY };
struct xz_oc_status {
    enum xz_oc_starvation starvation, starve_fallback;
    /* Durations include the active run; frames count source callback work. */
    uint32_t starve_runs, starve_last_frames, starve_max_frames, starve_last_ms, starve_max_ms;
    enum xz_oc_state state;
    int lag; float correlation; uint32_t blocks, misses;
    /* Segmented store: chunks of the track and chunks built now, and how long the first whole build took. 0 chunks:
       no store. Segments held, granted by the stem pool and needed for the whole track; the pool class
       (enum xz_stem_class); built audio ahead of and behind the playhead in the playing direction. */
    uint32_t flat_chunks, flat_built, flat_ms;
    uint32_t store_held, store_grant, store_need, store_class, store_lead_ms, store_behind_ms;
    /* The stems need nothing more from the stick: the store holds the whole track built, or the files are in RAM. */
    int stems_whole;
    /* The compressed files did not fit in RAM yet: stems stay off for this deck and nothing is read from the USB. */
    int waiting_ram;
};
struct xz_oc_stream *xz_oc_stream_open(const char *source, uint32_t rate, char *error, size_t capacity);
struct xz_oc_open_diagnostics { unsigned failures; char failure[3][160]; };
/* Preparation worker only; at most 3 attempts for ENOMEM/EAGAIN setup failures.
 * Cancellation is checked before/after each attempt and throughout backoff. */
struct xz_oc_stream *xz_oc_stream_open_retry(const char *,uint32_t,char *,size_t,int (*cancelled)(void *),void *,struct xz_oc_open_diagnostics *);
void xz_oc_stream_close(struct xz_oc_stream *);
/* Audio thread: no allocation, I/O, mutex or waiting. Stock unity is bit-exact.
 * An unverified timeline stays on stock audio. Pending selections retain the
 * current covered mix. A seek miss holds its native coefficient with short
 * entry/recovery fades; reader errors or 1500 ms of missing coverage ramp to
 * native unity. Readiness remains buffering until the requested mix fits. */
void xz_oc_stream_loop_start(struct xz_oc_stream *,int32_t position);
/* Engine, on a control thread, never the audio thread (it reads the clock): a stem level of this deck is off unity.
 * The stream stays engaged for XZ_OC_ENGAGED_DECAY_MS after the levels return to unity. */
#define XZ_OC_ENGAGED_DECAY_MS 120000u
void xz_oc_stream_engaged(struct xz_oc_stream *,int on);
/* Engine: deck frames the store keeps ready besides the playhead (track start, cue, hot cues), at most 11. The loop
 * start passed to xz_oc_stream_loop_start is added by the stream. Atomics only. */
void xz_oc_stream_anchors(struct xz_oc_stream *,const int32_t *deck_frames,unsigned count);
/* USB STOP or a pull of a stick (usb_release.h). Any thread, atomics only, never waits. When the stream's stem
 * files lie under prefix (a path that lives as long as the process, "/media/usb1/" on the unit), its worker lets go
 * of them:
 *  KEPT: the store holds the whole track with every chunk built. The files are closed and nothing is read or built
 *        again for this track; the store asks the pool for exactly what it holds, ahead of every other claim. If
 *        the pool must take memory back all the same, the stems end then, as below.
 *  ENDED: anything else (a bubble, an unfinished store, windows only, foreign stems): the files are closed, the
 *        store is given back, and the deck plays on without stems; levels return to unity through the usual ramp.
 *  NOTHING: no open file of the stream is on that stick (other stick, already let go, or every file copied to RAM).
 * The audio thread's side is unchanged: it reads the store only between xz_oc_flat_enter and leave.
 * done(token, outcome, why) runs once per request, on the worker, or in close (CLOSED) if the worker never saw it.
 * why is a short reason, valid during the call. */
enum xz_oc_gone { XZ_OC_GONE_NOTHING, XZ_OC_GONE_KEPT, XZ_OC_GONE_ENDED, XZ_OC_GONE_CLOSED };
typedef void (*xz_oc_gone_done)(uint32_t token, int outcome, const char *why);
void xz_oc_stream_source_gone(struct xz_oc_stream *, const char *prefix, xz_oc_gone_done done, uint32_t token);
size_t xz_oc_stream_mix(struct xz_oc_stream *, float *stereo, size_t frames, int64_t position, const float levels[3]);
void xz_oc_stream_status(struct xz_oc_stream *, struct xz_oc_status *);
void xz_oc_stream_wave(struct xz_oc_stream *, unsigned role, unsigned char *bins, size_t count, float *progress);
void xz_oc_stream_trace_state(struct xz_oc_stream *,uint32_t out[10]);
size_t xz_oc_stream_mix_trace(struct xz_oc_stream *,float *,size_t,int64_t,const float[3],uint32_t out[16]);
size_t xz_oc_stream_mix4_trace(struct xz_oc_stream *,float *,size_t,int64_t,const float[4],uint32_t out[16]);
unsigned xz_oc_stream_stem_count(const struct xz_oc_stream *);
const char *xz_oc_stream_format(const struct xz_oc_stream *);
int xz_oc_stream_has_wave(const struct xz_oc_stream *, unsigned role);
int xz_oc_stream_neutral(const struct xz_oc_stream *);
size_t xz_oc_stream_ram_bytes(const struct xz_oc_stream *);
void xz_oc_stream_position(struct xz_oc_stream *,int32_t);
#endif
