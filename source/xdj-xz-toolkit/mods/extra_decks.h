#ifndef XZ_EXTRA_DECKS_H
#define XZ_EXTRA_DECKS_H
/* Decks 3 and 4: software players inside the runtime, heard on mixer CH3 and CH4 through the USB
   loopback route (VJToolsLibrary docs/decisions/2026-10-20-xz-four-deck-handoff.md). This is the
   state both interfaces draw: the FLAGSHIP shell maps it into its deck model, the classic interface
   draws it into the native waveform surface. The players publish it; the interfaces only read it.

   Every readout carries a known bit, as in the shell model: a clear bit draws the blank convention,
   never a guess. Text is copied into the snapshot; wave, grid and cue arrays are runtime-owned and
   stay valid until the next snapshot of the same deck. */
#include <stdint.h>
#include "sync_master.h"

#define XZ_EXTRA_DECKS 2           /* index 0 = DECK 3, 1 = DECK 4 */
#define XZ_EXTRA_FIRST_DECK 2      /* the zero-based deck number of index 0 */
#define XZ_EXTRA_HOT_CUES 8
#define XZ_EXTRA_TEXT 96

enum xz_extra_field {
 XZ_EXTRA_F_TITLE, XZ_EXTRA_F_ARTIST, XZ_EXTRA_F_SOURCE, XZ_EXTRA_F_ELAPSED, XZ_EXTRA_F_TOTAL,
 XZ_EXTRA_F_PLAYING, XZ_EXTRA_F_BPM, XZ_EXTRA_F_TEMPO, XZ_EXTRA_F_MASTER, XZ_EXTRA_F_KEY,
 XZ_EXTRA_F_CUE, XZ_EXTRA_F_HOT_CUES, XZ_EXTRA_F_GRID, XZ_EXTRA_F_WAVE, XZ_EXTRA_F_LOOP,
 XZ_EXTRA_F_TEMPO_RANGE, XZ_EXTRA_F_MASTER_TEMPO, XZ_EXTRA_F_VINYL, XZ_EXTRA_F_TIMELINE, XZ_EXTRA_F_SYNC,
 XZ_EXTRA_F_QUANTIZE, XZ_EXTRA_F_TIME_MODE,
 XZ_EXTRA_FIELD_COUNT
};
#define XZ_EXTRA_KNOWN(field) ((uint32_t)1<<(field))

/* One waveform column, the rekordbox three-band analysis (PWV7 low, mid, high), 0..255 each.
   Same layout as struct xz_shell_column. */
struct xz_extra_column { uint8_t low, mid, high; };
/* count columns cover span_ms of audio from the track start, evenly. normalization is the
   low+mid+high sum drawn at full height, ui/layered_wave_source.c xz_wave_normalization() (the 99.5th
   percentile), computed once by the player so the draw thread never ranks a whole track; 0 = 765. */
struct xz_extra_wave { const struct xz_extra_column *columns; uint32_t count; uint32_t span_ms; uint32_t normalization; };
#define XZ_EXTRA_WAVE_CHUNK_COLUMNS 512u
struct xz_extra_wave_key {
 uint64_t media_generation;
 uint32_t route_epoch, load_revision;
 int source;
 uint32_t column_count, span_ms, normalization;
};
struct xz_extra_wave_chunk {
 struct xz_extra_wave_key key;
 uint32_t first, count;
 struct xz_extra_column columns[XZ_EXTRA_WAVE_CHUNK_COLUMNS];
};
enum xz_extra_wave_copy_result {
 XZ_EXTRA_WAVE_COPY_INVALID=-3, XZ_EXTRA_WAVE_COPY_BUSY=-2,
 XZ_EXTRA_WAVE_COPY_CHANGED=-1, XZ_EXTRA_WAVE_COPY_ABSENT=0, XZ_EXTRA_WAVE_COPY_OK=1
};
struct xz_extra_beat { int32_t ms; uint8_t beat_in_bar; };   /* 1..4 */
/* end_ms > ms: a loop hot cue from ms to end_ms; 0 for a point. */
struct xz_extra_cue { int set; int32_t ms; uint32_t rgb; int32_t end_ms; };
enum xz_extra_timeline_flag {
 XZ_EXTRA_TIMELINE_ANCHOR=1u, XZ_EXTRA_TIMELINE_RATE=2u,
 XZ_EXTRA_TIMELINE_MOTION=4u, XZ_EXTRA_TIMELINE_SINK_FRAME=8u
};
/* Completed-PCM source tag observed at at_ms, with slope measured against completed output frames.
   This is a USB transport-boundary proxy, not an audible-position/DAC timestamp measurement.
   sink_frame, when SINK_FRAME is set, is the transport's exact matching tagged-block wire coordinate;
   a contemporaneous wire_completed_frames counter must not be substituted. flags separately qualify
   anchor, signed rate, motion and exact sink coordinate. Stale/future timestamps or ambiguous slope
   clear RATE/ANCHOR as appropriate; MOTION still permits live refresh while extrapolation is held.
   moving remains useful for refresh cadence while queued scratch PCM finishes. */
struct xz_extra_timeline {
 uint64_t source_frame, sink_frame, media_generation;
 uint64_t observed_wire_frame; /* same observation as at_ms; sink_frame is the earlier tagged block end */
 uint32_t source_rate, at_ms, revision, route_epoch, load_revision, transport_generation, flags;
 int32_t rate_ppm;                   /* signed source seconds / output second, millionths */
 int moving;
};
enum xz_extra_sync_reason {
 XZ_EXTRA_SYNC_OFF, XZ_EXTRA_SYNC_WAIT_MASTER, XZ_EXTRA_SYNC_WAIT_ANCHOR,
 XZ_EXTRA_SYNC_NO_GRID, XZ_EXTRA_SYNC_MANUAL, XZ_EXTRA_SYNC_DISCONTINUITY,
 XZ_EXTRA_SYNC_LOOP_UNALIGNED, XZ_EXTRA_SYNC_OUT_OF_RANGE, XZ_EXTRA_SYNC_ACQUIRING,
 XZ_EXTRA_SYNC_LOCKED, XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE, XZ_EXTRA_SYNC_MASTER_CLOCK,
 XZ_EXTRA_SYNC_NATIVE_MASTER_UNSUPPORTED, XZ_EXTRA_SYNC_NO_SOURCE_BPM,
 XZ_EXTRA_SYNC_TEMPO_FOLLOW          /* QUANTIZE off: following the master's tempo, beat position untouched */
};
enum xz_extra_master_scope {
 XZ_EXTRA_MASTER_SCOPE_NONE=0u, XZ_EXTRA_MASTER_SCOPE_NATIVE=3u,
 XZ_EXTRA_MASTER_SCOPE_SOFTWARE=12u /* bits deck1..4; 15 requires a separately verified native bridge */
};
struct xz_extra_sync {
 int requested,locked;
 int master_deck;                    /* 2/3: software group, 0/1: native tempo source, -1: unavailable */
 enum xz_extra_sync_reason reason;
 int32_t phase_error_us,correction_ppm;
 uint32_t master_scope;              /* authority group, not a claim that every deck currently follows */
};

struct xz_extra_deck {
 uint32_t known;
 int loaded;                           /* a track is in the player (may still be reading) */
 char title[XZ_EXTRA_TEXT], artist[XZ_EXTRA_TEXT];
 int source;                           /* 1 USB1, 2 USB2 */
 int32_t elapsed_ms, total_ms;
 uint32_t elapsed_at_ms;               /* monotonic ms when elapsed_ms was true; 0 = not stamped */
 int playing;
 int32_t bpm_x100, track_bpm_x100;     /* playing BPM, the track's analysed BPM */
 int32_t tempo_x100;                   /* 0.01 % */
 int tempo_range, master_tempo, vinyl;
 int quantize;                         /* QUANTIZE: SYNC snaps to the master's beat (and keeps it) */
 unsigned quantize_den;                /* worker-applied 1/2/4/8; zero means unknown */
 int remain, auto_cue;                 /* TIME MODE shows remaining time; AUTO CUE (manual, Auto Cue) */
 int tempo_reset;                      /* TEMPO RESET on: plays at the track's own speed */
 int reverse, slip;                    /* DIRECTION REV and SLIP modes */
 int pad_mode;                         /* enum xz_extra_pad_mode */
 int active_pad;                       /* -1, or the BEAT LOOP / SLIP LOOP pad whose loop is running */
 uint32_t tempo_revision;             /* changes when SYNC sets a tempo away from the fader */
 int master;
 uint32_t master_scope;
 char key[8];                          /* as the library writes it, e.g. "Fm" or "4A" */
 int32_t cue_ms;
 struct xz_extra_cue hot_cue[XZ_EXTRA_HOT_CUES];
 /* stored: a loop RELOOP can return to; edit: 0 none, 1 LOOP IN adjust, 2 LOOP OUT adjust. */
 struct { int active; int32_t in_ms, out_ms; int stored, edit; } loop;
 int held_playing;                     /* a vinyl hold that resumes playback on release */
 int platter_contact;                  /* physical TOP contact, distinct from released coast */
 int slip_active;                      /* SLIP: playback continues silently in the background */
 const struct xz_extra_beat *grid; uint32_t grid_count;
 struct xz_extra_wave wave;
 struct xz_extra_timeline timeline;
 struct xz_extra_sync sync;
 uint32_t jog_content_epoch;          /* accepted LOAD/instance identity, captured under publication lock; 0 invalid */
};

/* What pads A..H do, chosen by the HOT CUE / BEAT LOOP / SLIP LOOP / BEAT JUMP buttons (manual pp.52-62).
   NONE is SHIFT + HOT CUE (CALL): the pads do nothing on a software deck. */
enum xz_extra_pad_mode {
 XZ_EXTRA_PAD_HOT_CUE, XZ_EXTRA_PAD_BEAT_LOOP, XZ_EXTRA_PAD_SLIP_LOOP, XZ_EXTRA_PAD_BEAT_JUMP,
 XZ_EXTRA_PAD_NONE, XZ_EXTRA_PAD_CALL
};

enum xz_extra_mode { XZ_EXTRA_MODE_QUANTIZE, XZ_EXTRA_MODE_REMAIN, XZ_EXTRA_MODE_AUTO_CUE };

struct xz_extra_decks {
 int route_up;                         /* the CH3/CH4 route is live: interfaces show four decks */
 int left_on_3, right_on_4;            /* the left deck controls drive DECK 3 instead of DECK 1; the
                                          right ones DECK 4 instead of DECK 2 */
 struct xz_extra_deck deck[XZ_EXTRA_DECKS];
 /* One short caps line from the players (a refused control, a load error, the route going down), raised
    at notice_at_ms (monotonic); notice_seq changes with every new notice. Empty when none. */
 char notice[64]; uint32_t notice_at_ms, notice_seq;
 uint32_t snapshot_revision; /* exact player revision represented by the locked deck/notice copy */
 uint32_t control_revision;  /* packed atomic control state represented by route/sides, compare for change */
 struct xz_master_intent master_intent;
 struct xz_master_observation master_actual;
};

/* Copies the published state. Returns route_up; with no players running it clears out and returns 0,
   and every interface keeps its two-deck screen. Never blocks the caller on IO. left_on_3 and
   right_on_4 are only ever set while route_up is. */
int xz_extra_decks_snapshot(struct xz_extra_decks *out);
/* Connector service only. Its owner joins the connector before stopping the players.
   deck is 0/1. Copies values, never lends analysis pointers. See xz_xp_copy_wave. */
enum xz_extra_wave_copy_result xz_extra_decks_copy_wave(int deck,
 const struct xz_extra_wave_key *expected, uint32_t first, struct xz_extra_wave_chunk *out);
uint32_t xz_extra_decks_jog_content_epoch(int deck); /* process-lifetime atomic acquire, no player pointer */
uint32_t xz_extra_decks_revision(void); /* atomic player visible-state revision, no snapshot copy */
uint32_t xz_extra_decks_control_revision(void); /* packed route/sides/availability state; no locks */
int xz_extra_decks_needs_native_sync(void); /* atomic; cached native clock publisher demand */
int xz_extra_decks_use_native_master(void); /* withdraw software intent; not an explicit native press */
void xz_extra_decks_native_master_selected(unsigned native_deck);
void xz_extra_decks_set_quantize_grid(unsigned denominator,int known);
struct xz_sync_native_snapshot;
int xz_extra_native_sync_clock(struct xz_sync_native_snapshot *); /* cache copy only, never native I/O */

/* The cheap read the input layer makes on every key: whether the players run at all, and whether the
   CH3/CH4 route is live. Lock-free. */
struct xz_extra_route { int available, route_up; };
struct xz_extra_route xz_extra_decks_route(void);
/* The input layer says which decks the controls drive; the snapshot carries it to both interfaces. */
void xz_extra_decks_publish_sides(int left_on_3, int right_on_4);

/* What the DJ asks of DECK 3 or DECK 4. Independent of the producer: the onPhysicalKey router
   (deck_select.c) is one, a reader of the documented XDJ-XZ MIDI messages can be another, and the
   browser (LOAD) a third. Each producer converts to these units. */
/* Worker-built immutable displayed order. At most 8 live orders of 65536 rows (2 MiB IDs).
   Zero IDs represent non-track rows. UI code only retains/releases pointers; no list copy. */
#define XZ_EXTRA_ORDER_ROWS 65536u
#define XZ_EXTRA_ORDER_SLOTS 8u
struct xz_extra_order;
struct xz_extra_order *xz_extra_order_create(int source, uint64_t media_generation,
 const uint32_t *ids, const uint8_t *kinds, uint32_t count, uint8_t track_kind);
void xz_extra_order_retain(struct xz_extra_order *);
void xz_extra_order_release(struct xz_extra_order *);
int xz_extra_order_matches(const struct xz_extra_order *, int source, uint64_t media_generation,
 uint32_t row, uint32_t track_id);
unsigned xz_extra_order_live(void); /* live allocation count, for bounded-ownership tests */
enum xz_extra_input {
 XZ_EXTRA_IN_PLAY, XZ_EXTRA_IN_CUE, XZ_EXTRA_IN_JOG_TURN, XZ_EXTRA_IN_JOG_TOUCH, XZ_EXTRA_IN_TEMPO,
 XZ_EXTRA_IN_SYNC, XZ_EXTRA_IN_MASTER_TEMPO, XZ_EXTRA_IN_HOT_CUE, XZ_EXTRA_IN_LOAD,
 XZ_EXTRA_IN_SEEK,                  /* needle search from a screen: seek_ms */
 XZ_EXTRA_IN_TEMPO_RANGE, XZ_EXTRA_IN_TEMPO_RESET, XZ_EXTRA_IN_VINYL,
 XZ_EXTRA_IN_LOOP_IN, XZ_EXTRA_IN_LOOP_OUT, XZ_EXTRA_IN_RELOOP,
 XZ_EXTRA_IN_LOOP_HALF, XZ_EXTRA_IN_LOOP_DOUBLE,
 XZ_EXTRA_IN_TRACK_NEXT,             /* next TRACK occurrence in the captured FLAGSHIP order */
 XZ_EXTRA_IN_MASTER,                 /* explicit software clock-master request */
 XZ_EXTRA_IN_PAD_MODE,               /* pad-mode button: hot_cue carries enum xz_extra_pad_mode */
 XZ_EXTRA_IN_REVERSE,                /* DIRECTION REV */
 XZ_EXTRA_IN_SLIP,
 XZ_EXTRA_IN_VINYL_SPEED,
 XZ_EXTRA_IN_QUANTIZE,              /* DECK QUANTIZE; shift: this deck only (manual), else every deck */
 XZ_EXTRA_IN_MODE_SET,              /* the other software deck's QUANTIZE / TIME MODE / AUTO CUE press:
                                       hot_cue = enum xz_extra_mode * 2 + (1 on, 0 off) */
 XZ_EXTRA_IN_TIME_MODE,             /* TIME MODE press; shift: this deck only, else every deck */
 XZ_EXTRA_IN_AUTO_CUE,              /* TIME MODE held one second; shift: this deck only */
 XZ_EXTRA_IN_TRACK_PREV,            /* TRACK SEARCH back: start of the track; twice quickly: previous track */
 XZ_EXTRA_IN_SEARCH,                /* SEARCH held: hot_cue 0 forward, 1 back */            /* VINYL SPEED ADJUST: tempo_position, +SPAN fully clockwise = slowest */
 XZ_EXTRA_IN_INSTANT_DOUBLES,
 XZ_EXTRA_IN_LOOP_4_8,
 XZ_EXTRA_INPUT_COUNT
};
#define XZ_EXTRA_TEMPO_SPAN 10000  /* tempo position at either end of the fader */
struct xz_extra_deck_event {
 int deck;                          /* 0 DECK 3, 1 DECK 4 */
 enum xz_extra_input kind;
 int pressed;                       /* buttons and JOG_TOUCH: 1 down, 0 up; every down gets its up */
 int shift;                         /* SHIFT was held: the player applies the documented SHIFT meaning */
 int held;                          /* one native hold notification; the player suppresses repeats */
 union {
  /* Jog ticks, clockwise positive. speed is the platter speed when the producer reports one. */
  struct { int32_t delta; float speed; int speed_known; } jog;
  /* Fader position, -XZ_EXTRA_TEMPO_SPAN at the minus end to +XZ_EXTRA_TEMPO_SPAN at the plus end;
     the player applies its own tempo range. Already past pickup when it arrives. */
  int32_t tempo_position;
  int hot_cue;                      /* 0..7, pads A..H; the deck's pad mode decides what a pad does */
  /* Caller keeps its reference; accepted queue and loaded deck each retain their own. */
  struct { int source; uint32_t track_id; uint64_t media_generation; uint32_t route_epoch;
           struct xz_extra_order *order; uint32_t order_index; } load;
  int32_t seek_ms;
 };
};
/* 1 when the player took the event, 0 when it refused (no player, deck not ready). Never blocks. */
int xz_extra_deck_input(const struct xz_extra_deck_event *event);

/* What a load needs, filled by the resolver below on the players' worker (IO allowed there). */
struct xz_extra_track {
 int source; uint32_t track_id;        /* as in the LOAD event */
 char path[1024];                      /* absolute path of the audio file on the stick */
 char anlz_dir[512];                   /* the track's ANLZ folder, "" when the stick has none */
 char title[XZ_EXTRA_TEXT], artist[XZ_EXTRA_TEXT], key[8];
 int32_t track_bpm_x100;               /* 0 when the library has none */
};
/* Track id to file, implemented once (shell_runtime.c) for every producer of LOAD, FLAGSHIP and classic
   alike. Called only on the players' worker. 0 on success. Weak: without it every LOAD is refused. */
int xz_extra_resolve_track(int source, uint32_t track_id, struct xz_extra_track *out);
uint64_t xz_extra_source_generation(int source);
int xz_extra_resolve_track_checked(int source, uint32_t track_id, uint64_t media_generation, struct xz_extra_track *out);
uint32_t xz_extra_decks_epoch(void);
/* The master deck's playing BPM x100 for SYNC, from the native decks; 0 when no native deck is master
   or it is unknown. Implemented by shell_runtime.c; weak: SYNC on DECK 3/4 is refused without it. */
int32_t xz_extra_native_master_bpm(void);
/* The RAM character node of the CH3/CH4 USB route. The host bridge creates it after binding the hub;
   the players open it when it appears. XZ_MODS_EXTRA_USB_NODE overrides the default. */
#define XZ_EXTRA_USB_NODE_DEFAULT "/dev/shm/xz-four-deck/usb-audio"
/* Starts the players and the route connector from the runtime's init path; XZ_MODS_FOUR_DECK=0 keeps them
   off (route().available stays 0). 0 on success or when disabled. Stop runs from the destructor. */
int xz_extra_decks_start(void);
void xz_extra_decks_stop(void);
#endif
