/* SPDX-License-Identifier: MIT */
#ifndef XZ_EXTRA_LANE_H
#define XZ_EXTRA_LANE_H
#include "../extra_decks.h"
#include "layered_wave.h"
#include "wave_viewport.h"
#include <stddef.h>
#include <stdint.h>

/* A software deck (DECK 3 or 4) drawn into its 64-row lane of the classic 536x268 wave surface,
   as the native lanes look after they shrink: the same 132-row lane geometry (cue bands, beat
   ticks, layered 3-band body around row 65) resampled with xz_wave_lane_row, the playhead at the
   native column, and a deck plate at the left edge because these decks have no info panel. */
#define XZ_EXTRA_LANE_PLAYHEAD_X 268
/* Native zoom when no native column has been seen: one PWV7 sample (1/150 s) per column. */
#define XZ_EXTRA_LANE_COLUMN_US 6667u
/* The deck plate: tag, SYNC/MASTER, key, clock, BPM, tempo and mode lamps, title. It darkens the
   oldest part of the lane only; the playhead and everything after it stay clear. */
#define XZ_EXTRA_LANE_READOUT_W 208
/* Rows of the plate's top line (deck tag and SYNC/MASTER chip): a side switch or a SYNC change
   redraws only these. */
#define XZ_EXTRA_LANE_TAG_ROWS 16

struct xz_extra_lane_view {
 uint32_t now_ms;      /* the players' monotonic clock (elapsed_at_ms) */
 uint32_t column_us;   /* audio per column, the native lanes' zoom */
 unsigned background;  /* the native column background mode: 1 grey, otherwise blue */
 int lit;              /* left_on_3 / right_on_4: the deck controls drive this deck */
};
/* What a lane drew in layer colours, for the theme pass. */
struct xz_extra_lane_marks { struct xz_layered_data look; struct xz_layered_col col[XZ_WAVE_WIDTH]; };

/* The playing position: elapsed_ms moved on by the time since elapsed_at_ms at the tempo, at most
   one second, inside the track. 0 when ELAPSED is unknown. */
int xz_extra_lane_position_ms(const struct xz_extra_deck *,uint32_t now_ms,int32_t *out);
/* Draws deck number 3 or 4 into rows lane_y..lane_y+63 of a surface 536 pixels wide. Every lane
   pixel is written. NULL deck draws as unloaded. marks may be NULL. */
int xz_extra_lane_draw(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,
 const struct xz_extra_deck *,const struct xz_extra_lane_view *,struct xz_extra_lane_marks *marks);
/* Four-channel layout: waveform and deck tag only; the main readouts live in the bottom mixer panels. */
int xz_extra_lane_draw_wave(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,
 const struct xz_extra_deck *,const struct xz_extra_lane_view *,struct xz_extra_lane_marks *marks);
/* Native DECK 1 or 2 in four-deck mode: the same deck tag the software lanes carry, in the lane's
   top-left corner, lit while that side's controls drive the native deck. The native left column no
   longer lines up with the lanes, so every lane names its deck. Writes only the tag's pixels. */
int xz_four_badge_draw(uint16_t *pixels,size_t stride,size_t count,unsigned lane_y,unsigned number,int lit);
/* A transient player/route notice on the existing Classic play surface, including after route loss. */
int xz_extra_notice_active(const struct xz_extra_decks *,uint32_t now_ms);
int xz_extra_notice_draw(uint16_t *pixels,size_t stride,size_t count,const struct xz_extra_decks *,uint32_t now_ms);
/* The same banner with any text (up to 63 characters), e.g. the DECK VIEW route-up notice (deck_view.h). */
int xz_extra_banner_draw(uint16_t *pixels,size_t stride,size_t count,const char *text);

/* The SYNC/MASTER state of a software deck, as the plate and the detail panel say it. */
enum xz_extra_sync_class {
 XZ_EXTRA_SYNC_CLASS_NONE,     /* SYNC off and not master (or unknown) */
 XZ_EXTRA_SYNC_CLASS_MASTER,   /* the clock master; 3/4 scope never drives DECK 1/2 */
 XZ_EXTRA_SYNC_CLASS_LOCKED,   /* beat and tempo locked to the master */
 XZ_EXTRA_SYNC_CLASS_TEMPO,    /* tempo only: QUANTIZE off, or the native beat position unavailable */
 XZ_EXTRA_SYNC_CLASS_WAIT,     /* finding the master or the beat again; clears by itself */
 XZ_EXTRA_SYNC_CLASS_BLOCKED   /* cannot lock until the DJ changes something (grid, BPM, range, loop, master) */
};
enum xz_extra_sync_class xz_extra_sync_class(const struct xz_extra_deck *);
/* Short caps text for the chip ("SYNC 1", "BPM 2", "3/4 MASTER", "WAIT", "NO GRID"); "" for NONE. */
const char *xz_extra_sync_chip(const struct xz_extra_deck *,char *out,size_t size);
/* What pads A..H do now, for the plate ("BEAT LOOP 1/4"); "" when unloaded or NULL. */
const char *xz_extra_pad_text(const struct xz_extra_deck *,char *out,size_t size);
/* One pad's meaning in the deck's pad mode: hot cue letter, loop length or jump; "" when inert. */
const char *xz_extra_pad_label(const struct xz_extra_deck *,int pad,char *out,size_t size);
/* Same fallback colours as the physical pad lamps for cues without a rekordbox colour. */
uint32_t xz_extra_cue_color(const struct xz_extra_cue *);

/* Theme pass over the four-deck surface (stride 536), the LAYERED_WAVE.md rule for four lanes:
   protect marks pixels that still hold the layer colour this frame drew for a deck (cue, beat and
   playhead marks and the readout drawn over them are not marked), the theme recolours the
   surface, restore puts the marked pixels back. col[d] NULL skips deck d. */
struct xz_four_marks { struct xz_layered_data look[XZ_FOUR_DECKS]; const struct xz_layered_col *col[XZ_FOUR_DECKS]; };
#define XZ_FOUR_KEEP_BYTES (XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT/8)
void xz_four_protect(const struct xz_four_layout *,const struct xz_four_marks *,const uint16_t *pixels,uint8_t *keep);
void xz_four_restore(const struct xz_four_layout *,const struct xz_four_marks *,uint16_t *pixels,const uint8_t *keep);
#endif
