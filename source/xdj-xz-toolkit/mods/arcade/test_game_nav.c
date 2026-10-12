/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Game navigation acceptance requires assertions
#endif
#include "game_nav.h"
#include "arcade.h"
#include "rhythm_lab.h"
#include "wave_rider.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { N = 18000, W = 800, H = 480 };
static uint8_t track[N * 3];
static uint16_t pixels[W * H];
static struct xz_wave_rider_snapshot wave;
static struct xz_game_targets frame;
static struct xz_game_nav nav;

static void song(void) {
    for (unsigned i = 0; i < N; i++) {
        track[i * 3] = (uint8_t)(i % 75 < 10 ? 240 - i % 75 * 21 : 4);
        track[i * 3 + 1] = (uint8_t)(65 + 18 * sinf(i * .009f));
        track[i * 3 + 2] = (uint8_t)(i % 300 < 25 ? 100 - i % 25 * 3 : 4);
    }
    memset(&wave, 0, sizeof wave);
    wave.normalization = 300; wave.generation = 1; wave.track_hash = 12; wave.kind = 1; wave.playing = 1;
    wave.pulse_period = 75; wave.pulse_confidence = 1;
    strcpy(wave.title, "ENCODER FIXTURE");
    assert(!xz_wave_rider_window(&wave, track, N, 300));
    wave.observed_ms = 1000;
}

/* Every recorded control must be live: tapping its centre changes the game,
   unless it is the option already chosen. `chosen` counts those per screen. */
#define LIVE_TARGETS(T, render, touch, game, chosen, label) do {                          \
    render(&(game), pixels, W * H, W, &frame);                                          \
    assert(frame.count > 0);                                                             \
    unsigned dead = 0;                                                                   \
    for (unsigned i = 0; i < frame.count; i++) {                                         \
        const struct xz_game_target *a = &frame.at[i];                                   \
        assert(a->x >= 0 && a->y >= 0 && a->x + a->w <= W && a->y + a->h <= H);           \
        T base = (game), tapped = (game);                                                \
        touch(&base, -1, -1, 1, 1001); touch(&base, -1, -1, 0, 1001);                     \
        touch(&tapped, a->x + a->w / 2, a->y + a->h / 2, 1, 1001);                        \
        touch(&tapped, a->x + a->w / 2, a->y + a->h / 2, 0, 1001);                        \
        if (!memcmp(&base, &tapped, sizeof base)) {                                      \
            dead++;                                                                      \
            fprintf(stderr, "%s: no change at %d,%d %dx%d\n", label, a->x, a->y, a->w, a->h); \
        }                                                                                \
    }                                                                                    \
    if (dead > (chosen)) { fprintf(stderr, "%s: %u dead of %u\n", label, dead, frame.count); assert(0); } \
    screens++; controls += frame.count;                                                  \
} while (0)

static unsigned screens, controls;

static void rider_touch(struct xz_rider *g, int x, int y, int down, uint32_t now) { xz_rider_touch(g, x, y, down, now); }
static void lab_touch(struct xz_rhythm_lab *g, int x, int y, int down, uint32_t now) { xz_lab_touch(g, x, y, down, now); }
static void arcade_touch(struct xz_arcade *g, int x, int y, int down, uint32_t now) { xz_arcade_touch(g, x, y, down, now); }

static struct xz_rider rider;
static void rider_screens(void) {
    xz_rider_init(&rider, 0, 1000); xz_rider_feed(&rider, &wave, 1000); rider.touch_down = 0;
    assert(rider.phase == XZ_RIDER_SETUP && rider.valid);
    for (unsigned step = 0; step < 4; step++) {
        rider.setup_step = step;
        /* Mode card, deck card and difficulty already chosen. */
        LIVE_TARGETS(struct xz_rider, xz_rider_render_targets, rider_touch, rider, step == 2 ? 0u : 1u, "rider setup");
    }
    rider.phase = XZ_RIDER_TITLE; LIVE_TARGETS(struct xz_rider, xz_rider_render_targets, rider_touch, rider, 0u, "rider title");
    rider.phase = XZ_RIDER_RESULT; LIVE_TARGETS(struct xz_rider, xz_rider_render_targets, rider_touch, rider, 0u, "rider result");
}

/* Encoder only: push the default control on each setup screen, then ride. */
static void rider_walk(void) {
    xz_rider_init(&rider, 0, 1000); xz_rider_feed(&rider, &wave, 1000); rider.touch_down = 0;
    xz_game_nav_reset(&nav);
    for (unsigned expected = 1; expected <= 3; expected++) {
        xz_rider_render_targets(&rider, pixels, W * H, W, &frame); xz_game_nav_frame(&nav, &frame);
        assert(frame.at[nav.focus].role == XZ_GAME_PRIMARY);
        int x, y; assert(xz_game_nav_point(&nav, &x, &y));
        xz_rider_touch(&rider, x, y, 1, 1010); xz_rider_touch(&rider, x, y, 0, 1010);
        assert(rider.phase == XZ_RIDER_SETUP && rider.setup_step == expected);
    }
    /* Ride stays disabled until the jog is turned, so focus avoids DECK. */
    xz_rider_render_targets(&rider, pixels, W * H, W, &frame); xz_game_nav_frame(&nav, &frame);
    assert(frame.at[nav.focus].role == XZ_GAME_NORMAL);
    xz_rider_jog(&rider, 0, 4, 1020);
    xz_rider_render_targets(&rider, pixels, W * H, W, &frame); xz_game_nav_frame(&nav, &frame);
    int x, y;
    xz_game_nav_turn(&nav, 1);
    for (unsigned turns = 0; frame.at[nav.focus].role != XZ_GAME_PRIMARY; turns++) { assert(turns < frame.count); xz_game_nav_turn(&nav, 1); }
    assert(xz_game_nav_point(&nav, &x, &y));
    xz_rider_touch(&rider, x, y, 1, 1030); xz_rider_touch(&rider, x, y, 0, 1030);
    assert(rider.phase == XZ_RIDER_RIDE);
    xz_rider_back(&rider, 1030); assert(!rider.leave && rider.phase == XZ_RIDER_RIDE);
    rider.phase = XZ_RIDER_SETUP; rider.setup_step = 3;
    for (int step = 2; step >= 0; step--) { xz_rider_back(&rider, 1040); assert(rider.setup_step == (unsigned)step && !rider.leave); }
    xz_rider_back(&rider, 1040); assert(rider.leave);
}

static struct xz_rhythm_lab lab;
static void lab_fresh(unsigned mode) {
    xz_lab_init(&lab, 1000); lab.mode = (enum xz_lab_mode)mode; xz_lab_feed(&lab, &wave, 1000); xz_lab_step(&lab, 1000);
    lab.touch_down = 0;
}
static void lab_screens(void) {
    lab_fresh(0);
    lab.phase = LAB_MENU; LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, 0u, "lab menu");
    for (unsigned step = 0; step < 4; step++) {
        lab.phase = LAB_SETUP; lab.setup_step = step;
        /* The chosen deck (steps 0 and 3) is already selected. */
        LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, step == 0 || step == 3 ? 1u : 0u, "lab setup");
    }
    lab.phase = LAB_BRIEF; LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, 0u, "lab brief");
    lab.phase = LAB_CONTROLS; lab.controls_return = LAB_MENU;
    LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, 2u, "lab buttons");
    lab.phase = LAB_RATINGS; LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, 0u, "lab ratings");
    lab.phase = LAB_RESULTS; LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, 0u, "lab results");
    lab_fresh(0); xz_lab_begin(&lab, 1000);
    assert(lab.phase == LAB_RUN);
    LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, 0u, "lab study");
    lab_fresh(LAB_GLYPH); lab.phase = LAB_BRIEF;
    LIVE_TARGETS(struct xz_rhythm_lab, xz_lab_render_targets, lab_touch, lab, 0u, "glyph brief");
}
static void lab_back(void) {
    lab_fresh(0);
    lab.phase = LAB_SETUP; lab.setup_step = 2; lab.calibrating = 1;
    xz_lab_back(&lab, 1010); assert(lab.setup_step == 1 && !lab.calibrating);
    xz_lab_back(&lab, 1010); xz_lab_back(&lab, 1010); assert(lab.phase == LAB_MENU && !lab.leave);
    lab.phase = LAB_CONTROLS; lab.controls_return = LAB_BRIEF; xz_lab_back(&lab, 1010); assert(lab.phase == LAB_BRIEF);
    xz_lab_back(&lab, 1010); assert(lab.phase == LAB_MENU);
    lab.phase = LAB_RESULTS; xz_lab_back(&lab, 1010); assert(lab.phase == LAB_MENU);
    xz_lab_back(&lab, 1010); assert(lab.leave);
    lab_fresh(LAB_GLYPH); lab.phase = LAB_BRIEF; xz_lab_back(&lab, 1010); assert(lab.leave);
    lab_fresh(0); xz_lab_begin(&lab, 1000); xz_lab_back(&lab, 1010); assert(lab.phase == LAB_RUN && !lab.leave);
}

static struct xz_arcade arcade;
static void arcade_screens(void) {
    xz_arcade_init(&arcade, 1000); arcade.touch_down = 0;
    arcade.phase = XZ_ARCADE_MENU; LIVE_TARGETS(struct xz_arcade, xz_arcade_render_targets, arcade_touch, arcade, 0u, "arcade menu");
    /* The chosen beat source is already selected. */
    arcade.phase = XZ_ARCADE_CLOCK; LIVE_TARGETS(struct xz_arcade, xz_arcade_render_targets, arcade_touch, arcade, 1u, "arcade beat source");
    arcade.phase = XZ_ARCADE_MUSIC; LIVE_TARGETS(struct xz_arcade, xz_arcade_render_targets, arcade_touch, arcade, 0u, "arcade paused");
    arcade.phase = XZ_ARCADE_RESULT; LIVE_TARGETS(struct xz_arcade, xz_arcade_render_targets, arcade_touch, arcade, 0u, "arcade result");
    xz_arcade_start(&arcade, XZ_ARCADE_PONG_SOLO, 1000);
    LIVE_TARGETS(struct xz_arcade, xz_arcade_render_targets, arcade_touch, arcade, 0u, "arcade play");
}
static void arcade_back(void) {
    xz_arcade_init(&arcade, 1000);
    arcade.phase = XZ_ARCADE_CLOCK; arcade.clock_return = 0; xz_arcade_back(&arcade, 1010); assert(arcade.phase == XZ_ARCADE_MENU);
    arcade.phase = XZ_ARCADE_RESULT; xz_arcade_back(&arcade, 1010); assert(arcade.phase == XZ_ARCADE_MENU);
    xz_arcade_back(&arcade, 1010); assert(arcade.phase == XZ_ARCADE_MUSIC);
    assert(!arcade.leave); xz_arcade_back(&arcade, 1010); assert(arcade.leave);
    arcade.leave = 0;
    xz_arcade_start(&arcade, XZ_ARCADE_PONG_SOLO, 1020);
    enum xz_arcade_phase playing = arcade.phase; xz_arcade_back(&arcade, 1020); assert(arcade.phase == playing);
    /* Menu default focus is the selected game, never DJ VIEW. */
    arcade.phase = XZ_ARCADE_MENU; arcade.mode = XZ_ARCADE_BREAK_SOLO; xz_game_nav_reset(&nav);
    xz_arcade_render_targets(&arcade, pixels, W * H, W, &frame); xz_game_nav_frame(&nav, &frame);
    int x, y; assert(xz_game_nav_point(&nav, &x, &y) && x == 40 + 175 && y == 208 + 38);
}

static void nav_rules(void) {
    struct xz_game_targets t = {0};
    xz_game_target_add(&t, 700, 0, 100, 48, XZ_GAME_EXIT);
    xz_game_target_add(&t, 10, 100, 100, 40, XZ_GAME_NORMAL);
    xz_game_target_add(&t, 10, 200, 100, 40, XZ_GAME_NORMAL);
    xz_game_target_add(&t, 0, 0, 0, 40, XZ_GAME_PRIMARY);
    assert(t.count == 3);
    xz_game_nav_reset(&nav); xz_game_nav_frame(&nav, &t);
    assert(nav.focus == 1 && !nav.visible);
    xz_game_nav_turn(&nav, 1); assert(nav.focus == 1 && nav.visible);
    xz_game_nav_turn(&nav, 1); assert(nav.focus == 2);
    xz_game_nav_turn(&nav, 5); assert(nav.focus == 0);
    xz_game_nav_turn(&nav, -1); assert(nav.focus == 2);
    xz_game_nav_frame(&nav, &t); assert(nav.focus == 2);
    t.at[2].y = 260; xz_game_nav_frame(&nav, &t); assert(nav.focus == 1);
    struct xz_game_targets only_exit = {0}; xz_game_target_add(&only_exit, 700, 0, 100, 48, XZ_GAME_EXIT);
    xz_game_nav_frame(&nav, &only_exit); int px, py; assert(nav.focus == -1 && !xz_game_nav_point(&nav, &px, &py));
    xz_game_nav_turn(&nav, 1); assert(nav.focus == 0 && xz_game_nav_point(&nav, &px, &py) && px == 750);
    struct xz_game_targets none = {0};
    xz_game_nav_frame(&nav, &none); int x, y; assert(!xz_game_nav_turn(&nav, 1) && !xz_game_nav_point(&nav, &x, &y));
    /* The ring stays inside the frame for controls on every edge. */
    static uint16_t guard[W * H + 2];
    struct xz_game_targets edge = {0}; xz_game_target_add(&edge, 0, 0, W, H, XZ_GAME_PRIMARY);
    xz_game_nav_frame(&nav, &edge); nav.visible = 1;
    guard[0] = guard[W * H + 1] = 0x1234;
    xz_game_nav_draw(&nav, guard + 1, W, W, H);
    assert(guard[0] == 0x1234 && guard[W * H + 1] == 0x1234);
    nav.visible = 0; memset(pixels, 0, sizeof pixels); xz_game_nav_draw(&nav, pixels, W, W, H);
    for (unsigned i = 0; i < W * H; i++) assert(!pixels[i]);
    nav.visible = 1; struct xz_game_targets mid = {0}; xz_game_target_add(&mid, 100, 100, 200, 60, XZ_GAME_PRIMARY);
    xz_game_nav_frame(&nav, &mid); xz_game_nav_draw(&nav, pixels, W, W, H);
    assert(pixels[98 * W + 200] == 0xffff && pixels[130 * W + 98] == 0xffff && pixels[130 * W + 200] == 0);
}

/* With an output folder, writes each game's first screen with the ring on
   its default control and one detent later, for visual review. */
static void picture(const char *folder, const char *name) {
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", folder, name);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (unsigned i = 0; i < W * H; i++) {
        uint16_t p = pixels[i];
        unsigned char rgb[3] = {(unsigned char)((p >> 11) * 255 / 31), (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
#define SHOT(render, game, name) do {                                                     \
    xz_game_nav_reset(&nav); render(&(game), pixels, W * H, W, &frame);                   \
    xz_game_nav_frame(&nav, &frame); xz_game_nav_turn(&nav, 1);                           \
    xz_game_nav_draw(&nav, pixels, W, W, H); picture(folder, name "-focus");              \
    xz_game_nav_turn(&nav, 1); render(&(game), pixels, W * H, W, &frame);                 \
    xz_game_nav_frame(&nav, &frame); xz_game_nav_draw(&nav, pixels, W, W, H);             \
    picture(folder, name "-next");                                                        \
} while (0)
static void pictures(const char *folder) {
    xz_rider_init(&rider, 0, 1000); xz_rider_feed(&rider, &wave, 1000);
    SHOT(xz_rider_render_targets, rider, "wave-rider-setup");
    lab_fresh(0); lab.phase = LAB_MENU; SHOT(xz_lab_render_targets, lab, "rhythm-lab-menu");
    lab_fresh(LAB_GLYPH); lab.phase = LAB_BRIEF; SHOT(xz_lab_render_targets, lab, "glyph-run-brief");
    xz_arcade_init(&arcade, 1000); arcade.phase = XZ_ARCADE_MENU; SHOT(xz_arcade_render_targets, arcade, "beat-arcade-menu");
}

int main(int argc, char **argv) {
    song();
    nav_rules(); puts("PASS focus order, wrap, layout reset, exit avoidance and bounded ring");
    rider_screens(); rider_walk(); puts("PASS Wave Rider: every control live, setup driven by encoder alone, BACK steps");
    lab_screens(); lab_back(); puts("PASS Rhythm Lab and Glyph Run: every control live and BACK steps");
    arcade_screens(); arcade_back(); puts("PASS Beat Arcade: every control live and BACK steps");
    printf("PASS %u screens, %u encoder targets\n", screens, controls);
    if (argc > 1) { pictures(argv[1]); puts("PASS focus ring pictures"); }
}
