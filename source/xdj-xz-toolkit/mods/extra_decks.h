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

#define XZ_EXTRA_DECKS 2           /* index 0 = DECK 3, 1 = DECK 4 */
#define XZ_EXTRA_FIRST_DECK 2      /* the zero-based deck number of index 0 */
#define XZ_EXTRA_HOT_CUES 8
#define XZ_EXTRA_TEXT 96

enum xz_extra_field {
 XZ_EXTRA_F_TITLE, XZ_EXTRA_F_ARTIST, XZ_EXTRA_F_SOURCE, XZ_EXTRA_F_ELAPSED, XZ_EXTRA_F_TOTAL,
 XZ_EXTRA_F_PLAYING, XZ_EXTRA_F_BPM, XZ_EXTRA_F_TEMPO, XZ_EXTRA_F_MASTER, XZ_EXTRA_F_KEY,
 XZ_EXTRA_F_CUE, XZ_EXTRA_F_HOT_CUES, XZ_EXTRA_F_GRID, XZ_EXTRA_F_WAVE, XZ_EXTRA_F_LOOP,
 XZ_EXTRA_FIELD_COUNT
};
#define XZ_EXTRA_KNOWN(field) ((uint32_t)1<<(field))

/* One waveform column, the rekordbox three-band analysis (PWV7 low, mid, high), 0..255 each.
   Same layout as struct xz_shell_column. */
struct xz_extra_column { uint8_t low, mid, high; };
/* count columns cover span_ms of audio from the track start, evenly. */
struct xz_extra_wave { const struct xz_extra_column *columns; uint32_t count; uint32_t span_ms; };
struct xz_extra_beat { int32_t ms; uint8_t beat_in_bar; };   /* 1..4 */
struct xz_extra_cue { int set; int32_t ms; uint32_t rgb; };

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
 int master;
 char key[8];                          /* as the library writes it, e.g. "Fm" or "4A" */
 int32_t cue_ms;
 struct xz_extra_cue hot_cue[XZ_EXTRA_HOT_CUES];
 struct { int active; int32_t in_ms, out_ms; } loop;
 const struct xz_extra_beat *grid; uint32_t grid_count;
 struct xz_extra_wave wave;
};

struct xz_extra_decks {
 int route_up;                         /* the CH3/CH4 route is live: interfaces show four decks */
 int left_on_3, right_on_4;            /* the left deck controls drive DECK 3 instead of DECK 1; the
                                          right ones DECK 4 instead of DECK 2 */
 struct xz_extra_deck deck[XZ_EXTRA_DECKS];
};

/* Copies the published state. Returns route_up; with no players running it clears out and returns 0,
   and every interface keeps its two-deck screen. Never blocks the caller on IO. */
int xz_extra_decks_snapshot(struct xz_extra_decks *out);
#endif
