#ifndef XZ_STEM_PADS_H
#define XZ_STEM_PADS_H
#include "../cue/cue.h"
/* count[deck] is that deck's loaded stem set: 3 (OverCue) or 4. Anything else
 * is treated as 3, so a zeroed struct keeps three-stem behaviour. */
struct xz_stem_pads { unsigned down[2], owned[2], muted[2]; int bank, order, count[2]; };
/* Local role indices are 0 drums, 1 harmonics, 2 vocals, 3 bass; muted bit
 * 1u<<role. Bass exists only on four-stem decks. Bypass is never a muted bit:
 * a pad toggle of XZ_STEM_PAD_BYPASS flips the deck's separate bypass flag. */
#define XZ_STEM_BASS 3
#define XZ_STEM_PAD_BYPASS 4
/* Physical A/B/C and the screen columns share one saved left-to-right order.
 * Audio storage keeps its drums/harmonics/vocals indices; order 0 is the
 * original HIGH/MID/LOW: vocals/harmonics/drums. */
#define XZ_STEM_ORDER_COUNT 6
static const signed char xz_stem_orders[XZ_STEM_ORDER_COUNT][3]={{2,1,0},{2,0,1},{1,2,0},{1,0,2},{0,2,1},{0,1,2}};
static inline int xz_stem_at(int order,int col){return (unsigned)order<XZ_STEM_ORDER_COUNT&&col>=0&&col<3?xz_stem_orders[order][col]:-1;}
static inline const char *xz_stem_order_name(int order){
    static const char *const names[XZ_STEM_ORDER_COUNT]={"VOC HARM DRUM","VOC DRUM HARM","HARM VOC DRUM","HARM DRUM VOC","DRUM VOC HARM","DRUM HARM VOC"};
    return (unsigned)order<XZ_STEM_ORDER_COUNT?names[order]:names[0];
}
static inline int xz_stem_count(int count){return count==4?4:3;}
/* What pad slot 0-3 of the stem bank does. Slots 0-2 follow the saved order on
 * every deck; slot 3 (D or H) is BASS on a four-stem deck and BYPASS otherwise. */
static inline int xz_stem_pad_role(int order,int count,int slot){
    if(slot>=0&&slot<3)return xz_stem_at(order,slot);
    return slot==3?(xz_stem_count(count)==4?XZ_STEM_BASS:XZ_STEM_PAD_BYPASS):-1;
}
/* Returns ownership, including release after leaving the view. toggle is -1
 * except for a newly claimed press: a role 0..3 whose muted bit flipped, or
 * XZ_STEM_PAD_BYPASS. The toggle is resolved at press from count[deck]; the
 * release only clears ownership, so a count change while held toggles nothing. */
int xz_stem_pad_event(struct xz_stem_pads *, const struct xz_cue_event *, int active, int *toggle);
int xz_stem_control_event(struct xz_stem_pads *, const struct xz_cue_event *, int active,
    int selected_page, int shift_pages, int *toggle);
#endif
