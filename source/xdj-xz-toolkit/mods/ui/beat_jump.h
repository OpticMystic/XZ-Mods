#ifndef XZ_BEAT_JUMP_H
#define XZ_BEAT_JUMP_H
#include "../cue/cue.h"
#define XZ_JUMP_SIZES 10
struct xz_jump_settings { int enabled, shift_page, sizes[8]; };
struct xz_jump_state { unsigned down[2], owned[2], mode_down[2]; int page[2]; };
static inline struct xz_jump_settings xz_jump_defaults(void) {
 return (struct xz_jump_settings){1,1,{0,1,2,3,4,5,6,8}};
}
static inline int xz_jump_beats(int size) {
 static const int beats[XZ_JUMP_SIZES]={1,2,4,8,16,32,64,64,128,256};
 return size>=0&&size<XZ_JUMP_SIZES?beats[size]:0;
}
static inline const char *xz_jump_label(int size) {
 static const char *labels[XZ_JUMP_SIZES]={"1 BEAT","2 BEATS","4 BEATS","8 BEATS","16 BEATS","32 BEATS","64 BEATS","16 BARS","32 BARS","64 BARS"};
 return size>=0&&size<XZ_JUMP_SIZES?labels[size]:"INVALID";
}
static inline int xz_jump_step_size(int size,int delta) {
 static const int order[]={0,1,2,3,4,5,6,8,9};
 int position=0;
 for(int i=0;i<9;i++)if(xz_jump_beats(order[i])==xz_jump_beats(size))position=i;
 int step=delta>8?8:delta < -8?-8:delta;
 position+=step;if(position<0)position=0;if(position>8)position=8;
 return order[position];
}
/* Returns ownership; a nonzero beats output is one native command on press. */
int xz_jump_event(struct xz_jump_state *,const struct xz_jump_settings *,const struct xz_cue_event *,int *beats);
#endif
