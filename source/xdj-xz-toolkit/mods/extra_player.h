// SPDX-License-Identifier: GPL-2.0-only
#ifndef XZ_EXTRA_PLAYER_H
#define XZ_EXTRA_PLAYER_H
/* The DECK 3 / DECK 4 players behind extra_decks.h. Each deck is one state machine run by its own
   worker; input only enqueues. Adapted from VJToolsLibrary tools/xz-four-deck-poc/player.c at commit
   dae68bec (reader cache, linear resampler to 44.1 kHz, submit loop with AGAIN/BUSY/STALE handling).

   EMPTY -> LOADING -> CUED <-> PLAYING -> ENDED, ERROR from LOADING. CUED is paused: at the cue after a
   load or CUE, wherever PLAY paused it otherwise. SAMPLING is CUE held from CUED at the cue: it plays
   until CUE is released, then returns to the cue (the manual's Cue Sampler). */
#include <stdint.h>
#include "extra_decks.h"
#include "audio/usb_transport/usb_transport.h"

enum xz_xp_state { XZ_XP_EMPTY, XZ_XP_LOADING, XZ_XP_CUED, XZ_XP_PLAYING, XZ_XP_SAMPLING, XZ_XP_ENDED, XZ_XP_ERROR, XZ_XP_SCRATCHING };

#define XZ_XP_TEMPO_RANGE_X100 1000    /* initial +-10 % */
#define XZ_XP_GAIN 1.0f                /* level against the native decks: one line to match on the deck */
#define XZ_XP_AHEAD_FRAMES 2048u       /* output frames queued in the transport before USB staging takes them */
#define XZ_XP_BEND_PER_TICK 0.004f     /* playing jog: speed added per tick, decaying */
#define XZ_XP_BEND_MAX 0.9f
#define XZ_XP_BEND_DECAY_MS 150u
#ifndef XZ_XP_JOG_TICKS_PER_REV
#define XZ_XP_JOG_TICKS_PER_REV 360    /* reference scale for standalone host fixtures; build.py sets the hardware estimate */
#endif
#define XZ_XP_SEARCH_MS_PER_REV 1800   /* paused jog: audio one platter turn moves through */
/* VINYL SPEED ADJUST brake/start time from full speed with the knob fully counterclockwise and fully
   clockwise; the centre is ~300 ms. Provisional until timed against a stock deck. */
#define XZ_XP_SCAN_RATE 4.0            /* SEARCH held during playback: scan speed (provisional; not in the manual) */
#define XZ_XP_TRACK_PREV_MS 500u      /* TRACK SEARCH back twice within this: previous track (provisional) */
#define XZ_XP_SYNC_RESNAP_MS 1000u    /* QUANTIZE re-snaps after a SYNC break at most this often */
#define XZ_XP_MASTER_HANDOFF_MS 300u  /* a stopped native master must stay stopped this long before a software deck takes over */
#define XZ_XP_RAMP_FAST_MS 30
#define XZ_XP_RAMP_SLOW_MS 3000
#define XZ_XP_RETIRE_MS 2000u          /* wave and grid arrays outlive the load that replaced them */
#define XZ_XP_RETIRE_SLOTS 8
#define XZ_XP_QUEUE 64                 /* ordinary FIFO slots; private slots reserve one release per distinct hold */

/* Where the audio goes: the USB transport in the runtime, a fake in host tests. deck 0 = DECK 3. */
struct xz_xp_sink {
 void *ctx;
 /* A running route's instance number, 0 while it is down. Lock-free: input calls it on every key. */
 uint32_t (*link)(void *ctx);
 xz_usb_result (*submit)(void *ctx, uint32_t epoch, int deck, uint32_t generation, const float *stereo, uint32_t frames,
                         uint64_t source_after);
 xz_usb_result (*flush)(void *ctx, uint32_t epoch, int deck, uint32_t *generation);
 xz_usb_result (*position)(void *ctx, uint32_t epoch, xz_usb_position *out);
 /* All operations require the captured route epoch; reject mismatches without mutation. */
 /* First block and generation are published as one transition. Required. */
 xz_usb_result (*transition)(void *ctx, uint32_t epoch, int deck, uint32_t expected_generation,
                            const float *stereo, uint32_t frames, uint64_t source_after,
                            uint32_t *generation);
};
struct xz_xp_env {
 struct xz_xp_sink sink;
 uint32_t (*now_ms)(void);                                         /* monotonic */
 int (*resolve)(int source, uint32_t track_id, struct xz_extra_track *out);  /* NULL: LOAD refused */
 int32_t (*master_bpm)(void);                                      /* NULL: SYNC has no master */
 void (*worker_enter)(int deck); /* Optional; called once inside each owned worker, never by step/input. */
 int (*resolve_checked)(int source, uint32_t track_id, uint64_t media_generation, struct xz_extra_track *out);
 /* Start-up modes. The runtime starts in VINYL mode with QUANTIZE on, as the stock decks do; zero keeps
    the older CDJ-mode, unquantized start for embedders that do not set them. */
 int start_in_vinyl;
 int start_with_quantize;
};

struct xz_xp_players;
#define XZ_XP_HAS_QUANTIZE_GRID 1
/* Unknown or invalid settings inhibit phase alignment while retaining BPM follow. */
void xz_xp_set_quantize_grid(struct xz_xp_players *,unsigned denominator,int known);
void xz_xp_set_master_observer(struct xz_xp_players *,xz_master_observer);
void xz_xp_native_master_selected(struct xz_xp_players *,unsigned native_deck);
struct xz_xp_players *xz_xp_create(const struct xz_xp_env *env);
void xz_xp_destroy(struct xz_xp_players *);
/* The two workers, each calling xz_xp_step for its deck. Without them a test steps by hand. */
int xz_xp_start(struct xz_xp_players *);
void xz_xp_stop(struct xz_xp_players *);
/* One worker pass for one deck: commands, flush, render and submit, publish. */
void xz_xp_step(struct xz_xp_players *, int deck);

/* 1 accepted,0 refused. Every accepted TOUCH/CUE/LOOP_IN/REV or valid pad hold reserves its ordered release
   while the route survives, including under a full ordinary queue. Queued work captures the
   route epoch and arrival time privately; a new route discards stale work and resets holds. */
int xz_xp_input(struct xz_xp_players *, const struct xz_extra_deck_event *);
/* Fills deck[] and the notice of *out; route and sides are the caller's. */
void xz_xp_read(struct xz_xp_players *, struct xz_extra_decks *out);
/* One publication trylock; copy at most 512 current-view columns (1536 bytes).
   deck is 0/1. NULL expected begins a read; further chunks require the exact key.
   first==column_count checks the final identity and returns count=0.
   Only OK writes out. No callback or borrowed pointer leaves the owner lock. */
enum xz_extra_wave_copy_result xz_xp_copy_wave(struct xz_xp_players *, int deck,
 const struct xz_extra_wave_key *expected, uint32_t first, struct xz_extra_wave_chunk *out);
uint32_t xz_xp_revision(const struct xz_xp_players *); /* atomic, no locks/IO */
/* Install only before starting workers. The callback copies a publisher-owned cache; no native I/O. */
void xz_xp_set_native_sync_clock(struct xz_xp_players *,int (*)(struct xz_sync_native_snapshot *));
int xz_xp_needs_native_sync(const struct xz_xp_players *);
int xz_xp_use_native_master(struct xz_xp_players *); /* 1 changed, 0 already native-reference mode */
void xz_xp_notice(struct xz_xp_players *, const char *text);

/* Worker-private state for tests; only while no worker runs. */
struct xz_xp_debug {
 enum xz_xp_state state;
 uint64_t position, cue;               /* source frames */
 uint32_t generation, source_rate;
 unsigned retired;
 double speed;
 uint64_t loop_in, loop_out;
 int loop_active, loop_edit, tempo_range, tempo_reset;
 uint64_t scratch_target;
};
void xz_xp_debug(struct xz_xp_players *, int deck, struct xz_xp_debug *out);
#endif
