/* SPDX-License-Identifier: MIT */
#ifndef XZ_GAME_NAV_H
#define XZ_GAME_NAV_H
/* Browse-encoder navigation for full-screen games. Each renderer records the
 * controls it draws this frame; the encoder moves a focus ring over that list
 * and a push taps the focused control through the game's own touch handler.
 * Touch and encoder therefore share one hit test and can never disagree. */
#include <stddef.h>
#include <stdint.h>

#define XZ_GAME_TARGETS 32
/* Focus starts on the screen's primary control, else its first ordinary one.
   It never starts on a control that leaves the game: a screen with only an
   exit starts unfocused, so a stray push mid-game does nothing. */
enum xz_game_role { XZ_GAME_NORMAL, XZ_GAME_PRIMARY, XZ_GAME_EXIT };
struct xz_game_target { int16_t x, y, w, h; uint8_t role; };
struct xz_game_targets { unsigned count; struct xz_game_target at[XZ_GAME_TARGETS]; };
struct xz_game_nav {
    struct xz_game_targets shown;
    uint32_t layout;
    int focus;
    /* The ring appears once the encoder is used and hides again on touch. */
    int visible;
};

static inline void xz_game_target_add(struct xz_game_targets *t, int x, int y, int w, int h, enum xz_game_role role) {
    if (!t || t->count >= XZ_GAME_TARGETS || w <= 0 || h <= 0) return;
    t->at[t->count++] = (struct xz_game_target){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (uint8_t)role};
}
static inline uint32_t xz_game_layout(const struct xz_game_targets *t) {
    uint32_t h = 2166136261u ^ t->count;
    for (unsigned i = 0; i < t->count; i++) {
        const struct xz_game_target *a = &t->at[i];
        int16_t v[4] = {a->x, a->y, a->w, a->h};
        for (int k = 0; k < 4; k++) h = (h ^ (uint16_t)v[k]) * 16777619u;
    }
    return h;
}
static inline void xz_game_nav_reset(struct xz_game_nav *n) {
    n->shown.count = 0; n->layout = 0; n->focus = -1; n->visible = 0;
}
/* Adopt the controls of the frame just drawn. A new layout means a new
 * screen, so focus returns to its primary control. */
static inline void xz_game_nav_frame(struct xz_game_nav *n, const struct xz_game_targets *t) {
    uint32_t layout = xz_game_layout(t);
    int same = layout == n->layout && n->focus >= 0 && n->focus < (int)t->count;
    n->shown = *t; n->layout = layout;
    if (same) return;
    n->focus = -1;
    for (int role = XZ_GAME_PRIMARY; role >= XZ_GAME_NORMAL; role--)
        for (unsigned i = 0; i < t->count; i++)
            if (t->at[i].role == role) { n->focus = (int)i; return; }
}
static inline int xz_game_nav_turn(struct xz_game_nav *n, int delta) {
    int count = (int)n->shown.count;
    if (!count) return 0;
    /* The first detent shows where focus is; later ones move it. */
    if (!n->visible || n->focus < 0 || n->focus >= count) {
        n->visible = 1;
        if (n->focus < 0 || n->focus >= count) n->focus = 0;
        return 1;
    }
    if (delta) n->focus = ((n->focus + (delta > 0 ? 1 : -1)) % count + count) % count;
    return 1;
}
/* Center of the focused control; 0 when the screen has none. */
static inline int xz_game_nav_point(struct xz_game_nav *n, int *x, int *y) {
    if (n->focus < 0 || n->focus >= (int)n->shown.count) return 0;
    const struct xz_game_target *a = &n->shown.at[n->focus];
    n->visible = 1;
    *x = a->x + a->w / 2; *y = a->y + a->h / 2;
    return 1;
}
static inline void xz_game_nav_draw(const struct xz_game_nav *n, uint16_t *pixels, size_t stride, int width, int height) {
    if (!n->visible || n->focus < 0 || n->focus >= (int)n->shown.count || !pixels) return;
    const struct xz_game_target *a = &n->shown.at[n->focus];
    /* White ring with a dark outer edge reads on every game palette. */
    for (int band = 0; band < 4; band++) {
        uint16_t color = band == 3 ? 0x0000 : 0xffff;
        int x0 = a->x - 2 - band, y0 = a->y - 2 - band, x1 = a->x + a->w + 1 + band, y1 = a->y + a->h + 1 + band;
        int r = 6 + band;
        for (int y = y0; y <= y1; y++) {
            if (y < 0 || y >= height) continue;
            for (int x = x0; x <= x1; x++) {
                if (x < 0 || x >= width) continue;
                if (y != y0 && y != y1 && x != x0 && x != x1) { x = x1 - 1; continue; }
                int dx = x < x0 + r ? x0 + r - x : x > x1 - r ? x - (x1 - r) : 0;
                int dy = y < y0 + r ? y0 + r - y : y > y1 - r ? y - (y1 - r) : 0;
                if (dx && dy) continue;
                pixels[(size_t)y * stride + (size_t)x] = color;
            }
        }
        for (int c = 0; c < 4; c++) {
            int cx = c & 1 ? x1 - r : x0 + r, cy = c & 2 ? y1 - r : y0 + r;
            for (int k = 0; k <= r; k++) {
                int j = 0;
                while ((j + 1) * (j + 1) + k * k <= r * r) j++;
                int sx = c & 1 ? 1 : -1, sy = c & 2 ? 1 : -1;
                int px = cx + sx * k, py = cy + sy * j, qx = cx + sx * j, qy = cy + sy * k;
                if (px >= 0 && px < width && py >= 0 && py < height) pixels[(size_t)py * stride + (size_t)px] = color;
                if (qx >= 0 && qx < width && qy >= 0 && qy < height) pixels[(size_t)qy * stride + (size_t)qx] = color;
            }
        }
    }
}
#endif
