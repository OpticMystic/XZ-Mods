/* SPDX-License-Identifier: MIT */
#ifndef XZ_ANLZ_EXTRA_H
#define XZ_ANLZ_EXTRA_H
/* What DECK 3 / DECK 4 read from a track's analysis folder beyond shell_analysis.h: hot cues A..H and
   the beat grid. Pure parsers over a whole file buffer, plus one folder loader for the players' worker.
   Section layouts as shell_analysis.c reads the memory lists: big-endian, PMAI file header, then
   tagged sections {tag, header length, total length}. */
#include <stddef.h>
#include <stdint.h>
#include "extra_decks.h"

/* Hot cues from ANLZ0000.EXT (PCO2 list type 1, PCP2 entries with colours) or ANLZ0000.DAT (PCOB list
   type 1, PCPT entries, no colour: the default colour below). Entry hot_cue 1..8 is pad A..H; a stored loop keeps its end in end_ms (a loop hot cue), and counts as a
   cue at its start. Returns the number of pads set, -1 when the file has no hot cue list. */
int xz_anlz_hot_cues(const uint8_t *file, size_t n, struct xz_extra_cue out[XZ_EXTRA_HOT_CUES]);
/* Colour of a hot cue rekordbox left uncoloured (and every PCPT cue): green, orange for a loop. */
#define XZ_ANLZ_CUE_RGB 0x00ff00u
#define XZ_ANLZ_LOOP_RGB 0xff8000u

/* The PQTZ beat grid of ANLZ0000.DAT: {beat in bar 1..4, BPM x100, time ms} per beat. *grid is
   malloc'd; *bpm_x100 is the first beat's tempo (0 when none). 0 = found. */
int xz_anlz_beat_grid(const uint8_t *file, size_t n, struct xz_extra_beat **grid, uint32_t *count,
                      int32_t *bpm_x100);

struct xz_anlz_extra {
 int hot_cues_found;                   /* a hot cue list was read (it may hold no pads) */
 struct xz_extra_cue hot_cue[XZ_EXTRA_HOT_CUES];
 struct xz_extra_beat *grid; uint32_t grid_count; int32_t grid_bpm_x100;
};
/* Reads dir/ANLZ0000.EXT for hot cues, falling back to dir/ANLZ0000.DAT, and the DAT's grid. Worker
   thread only (file IO). Always fills *out; missing parts stay empty. */
void xz_anlz_extra_load(const char *dir, struct xz_anlz_extra *out);
void xz_anlz_extra_free(struct xz_anlz_extra *);
#endif
