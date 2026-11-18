/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XZ_DOOM_CONTROLS_H
#define XZ_DOOM_CONTROLS_H
#include <stdint.h>
struct xz_doom_controls {
    unsigned char desired[256], physical[256], sent[256];
    int held, fire, quit;
    /* One navigation tap: pressed on the next key poll, released on the one after. */
    int tap, tapped;
};
void xz_doom_touch(struct xz_doom_controls *, int down, int x, int y);
int xz_doom_key(struct xz_doom_controls *, int *pressed, unsigned char *key);
void xz_doom_compose(uint16_t *out, const uint32_t *game, const struct xz_doom_controls *);
void xz_doom_physical(struct xz_doom_controls *, uint32_t buttons, int turn);
/* Accepts a navigation tap (enum xz_game_nav_key) once the previous one is released.
   Returns 0 while busy so the caller keeps it queued. */
int xz_doom_nav_tap(struct xz_doom_controls *, unsigned nav);
#endif
