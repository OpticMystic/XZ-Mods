/* SPDX-License-Identifier: MIT */
#ifndef XZ_ARCADE_INPUT_H
#define XZ_ARCADE_INPUT_H
#include "arcade.h"
struct xz_arcade_input { uint32_t owned[2]; };
/* Paired releases remain owned after DJ pause. No stuck game press reaches stock. */
int xz_arcade_input_event(struct xz_arcade_input *,struct xz_arcade *,int active,int deck,const void *,uint32_t now);
#endif
