#ifndef XZ_WAVE_VIEWPORT_H
#define XZ_WAVE_VIEWPORT_H
#include <stddef.h>
#include <stdint.h>
/* Persisted stems_overlay values; existing 0 hidden / 1 below retain their meaning. */
enum xz_stem_layout {
 XZ_STEM_LAYOUT_HIDDEN=0, XZ_STEM_LAYOUT_BELOW=1, XZ_STEM_LAYOUT_BELOW_HALF=2,
 XZ_STEM_LAYOUT_OUTER=3, XZ_STEM_LAYOUT_OUTER_HALF=4, XZ_STEM_LAYOUT_COUNT=5
};
#define XZ_WAVE_WIDTH 536
#define XZ_WAVE_HEIGHT 268
#define XZ_WAVE_NATIVE_LANE_HEIGHT 132
#define XZ_WAVE_HALF_CONTROL_HEIGHT 24
/* Maximum row buffers and original full-height geometry. */
#define XZ_WAVE_LANE_HEIGHT 84
#define XZ_WAVE_CONTROL_HEIGHT 48
#define XZ_WAVE_SECOND_LANE_Y 132
#define XZ_WAVE_FIRST_CONTROL_Y 102
#define XZ_WAVE_SECOND_CONTROL_Y 234
#define XZ_WAVE_INLINE_HEIGHT 96
struct xz_wave_layout { unsigned control_height,lane_height,lane_y[2],control_y[2]; };
static inline int xz_wave_layout_get(int mode,struct xz_wave_layout *out) {
 if(!out||(unsigned)mode>=XZ_STEM_LAYOUT_COUNT)return 0;
 unsigned h=mode==XZ_STEM_LAYOUT_HIDDEN?0:
  mode==XZ_STEM_LAYOUT_BELOW_HALF||mode==XZ_STEM_LAYOUT_OUTER_HALF?24:48;
 unsigned lane=XZ_WAVE_NATIVE_LANE_HEIGHT-h;
 int outer=mode==XZ_STEM_LAYOUT_OUTER||mode==XZ_STEM_LAYOUT_OUTER_HALF;
 *out=(struct xz_wave_layout){h,lane,{outer?h:0,outer?136:lane+h},
   {outer?0:lane,outer?268-h:lane*2+h}};
 if(!h)out->lane_y[1]=136;
 return 1;
}
static inline int xz_wave_layout_next(int mode) {
 return (unsigned)mode<XZ_STEM_LAYOUT_COUNT?(mode+1)%XZ_STEM_LAYOUT_COUNT:XZ_STEM_LAYOUT_BELOW;
}
/* MODERN keeps its own layout (settings modern_stems): the five above plus PADS, small stem buttons in the
   pad row beside the hot-cue lamps, which the classic screen has no room for. */
enum { XZ_MODERN_STEMS_PADS=XZ_STEM_LAYOUT_COUNT, XZ_MODERN_STEMS_COUNT };
/* The STEMS button's order in MODERN: PADS, the four row layouts, HIDDEN, then PADS again. */
static const int xz_modern_stems_order[XZ_MODERN_STEMS_COUNT]={XZ_MODERN_STEMS_PADS,XZ_STEM_LAYOUT_BELOW,XZ_STEM_LAYOUT_BELOW_HALF,
 XZ_STEM_LAYOUT_OUTER,XZ_STEM_LAYOUT_OUTER_HALF,XZ_STEM_LAYOUT_HIDDEN};
static inline int xz_modern_stems_position(int mode) {
 for(int i=0;i<XZ_MODERN_STEMS_COUNT;i++)if(xz_modern_stems_order[i]==mode)return i;
 return -1;
}
static inline int xz_modern_stems_next(int mode) {
 int i=xz_modern_stems_position(mode);
 return xz_modern_stems_order[i<0?0:(i+1)%XZ_MODERN_STEMS_COUNT];
}
static inline const char *xz_wave_layout_name(int mode);
static inline const char *xz_modern_stems_name(int mode) {
 return mode==XZ_MODERN_STEMS_PADS?"PAD ROW / SMALL":xz_wave_layout_name(mode);
}
static inline const char *xz_wave_layout_name(int mode) {
 static const char *const names[XZ_STEM_LAYOUT_COUNT]={"HIDDEN","BELOW / FULL","BELOW / HALF","OUTER / FULL","OUTER / HALF"};
 return names[(unsigned)mode<XZ_STEM_LAYOUT_COUNT?mode:0];
}
static inline int xz_wave_layout_lane_at(const struct xz_wave_layout *g,int row) {
 for(int d=0;d<2;d++)if(row>=(int)g->lane_y[d]&&row<(int)(g->lane_y[d]+g->lane_height))return d;
 return -1;
}
static inline int xz_wave_layout_control_at(const struct xz_wave_layout *g,int row) {
 for(int d=0;d<2;d++)if(g->control_height&&row>=(int)g->control_y[d]&&row<(int)(g->control_y[d]+g->control_height))return d;
 return -1;
}
unsigned xz_wave_layout_source_row(const struct xz_wave_layout *,unsigned row);
/* Row of a native 132-row lane shown at row of a lane height rows tall: the ten cue rows at each
   edge keep their size, the 112 interior rows are resampled. */
unsigned xz_wave_lane_row(unsigned row,unsigned height);
/* Native draw lock only. Directional moves preserve source rows while shrinking. */
int xz_wave_layout_pixels(uint16_t *,size_t count,size_t stride,int mode);

/* Four decks (../extra_decks.h). DECK 3 and DECK 4 have no native lane: the 268 rows hold four 64-row
   lanes with 4-row gaps, and native DECK 1 and DECK 2 shrink into theirs as the stem layouts do.
   Interleaved puts DECK 3 under DECK 1 beside the native DECK 1 column and DECK 4 under DECK 2,
   as the controller's left (1/3) and right (2/4) sides; stacked is 1, 2, 3, 4. Mixer order is 3, 1, 2, 4. Decks are
   zero-based here: 0 and 1 native, 2 and 3 the software players. */
enum xz_four_order { XZ_FOUR_INTERLEAVED=0, XZ_FOUR_STACKED=1, XZ_FOUR_MIXER=2, XZ_FOUR_ORDER_COUNT=3 };
#include "deck_layout.h"
#define XZ_FOUR_ORDER XZ_FOUR_STACKED
#define XZ_FOUR_DECKS 4
#define XZ_FOUR_LANE_HEIGHT 64
#define XZ_FOUR_GAP 4
struct xz_four_layout { unsigned lane_y[XZ_FOUR_DECKS]; };
static inline int xz_four_layout_get(int order,struct xz_four_layout *out) {
 if(!out||(unsigned)order>=XZ_FOUR_ORDER_COUNT)return 0;
 const unsigned char *decks=order==XZ_FOUR_INTERLEAVED?(const unsigned char[]){0,2,1,3}:
  xz_deck_layout_order(order==XZ_FOUR_MIXER?XZ_LAYOUT_MIXER:XZ_LAYOUT_FOUR);
 for(int slot=0;slot<XZ_FOUR_DECKS;slot++)out->lane_y[decks[slot]]=slot*(XZ_FOUR_LANE_HEIGHT+XZ_FOUR_GAP);
 return 1;
}
static inline int xz_four_layout_lane_at(const struct xz_four_layout *g,int row) {
 for(int d=0;d<XZ_FOUR_DECKS;d++)if(row>=(int)g->lane_y[d]&&row<(int)(g->lane_y[d]+XZ_FOUR_LANE_HEIGHT))return d;
 return -1;
}
/* The deck at row and, in *lane_row, the native lane row it shows (0..131); -1 in a gap. */
int xz_four_layout_row(const struct xz_four_layout *,unsigned row,unsigned *lane_row);
/* Native source row for touches on DECK 1 or 2; 132 (the separator) elsewhere. */
unsigned xz_four_layout_source_row(const struct xz_four_layout *,unsigned row);
/* Native draw lock only: shrink both native lanes into place, clear the gaps and the DECK 3/4 lanes. */
int xz_four_layout_pixels(uint16_t *,size_t count,size_t stride,int order);
#endif
