/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XZ_DOOM_CONTROLS_H
#define XZ_DOOM_CONTROLS_H
#include <stdint.h>
struct xz_doom_controls {
    unsigned char desired[256], physical[256], sent[256];
    int held, fire, quit;
};
void xz_doom_touch(struct xz_doom_controls *, int down, int x, int y);
int xz_doom_key(struct xz_doom_controls *, int *pressed, unsigned char *key);
void xz_doom_compose(uint16_t *out, const uint32_t *game, const struct xz_doom_controls *);
void xz_doom_physical(struct xz_doom_controls *, uint32_t buttons, int turn);
#endif
