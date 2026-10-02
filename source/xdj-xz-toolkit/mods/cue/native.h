#ifndef XZ_CUE_NATIVE_H
#define XZ_CUE_NATIVE_H
#include "cue.h"
struct xz_cue_native { void *engine_if[2]; int verified; int input_blocked[2]; };
struct xz_cue_api xz_cue_native_api(struct xz_cue_native *);
int xz_cue_native_decode(struct xz_cue_native *,void *innards,const void *input,struct xz_cue_event *);
int xz_cue_native_beat_jump(struct xz_cue_native *,int deck,int beats);
#endif
