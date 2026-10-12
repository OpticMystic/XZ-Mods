/* SPDX-License-Identifier: MIT */
#ifndef XZ_WAVE_RIDER_H
#define XZ_WAVE_RIDER_H
#include "wave_rider_source.h"
#include "game_feel.h"
#include <stddef.h>

#define XZ_RIDER_WATER 342
#define XZ_RIDER_PLAYHEAD 190
#define XZ_RIDER_WORLD_SCALE 2.f
#define XZ_RIDER_OBJECTS 192
#define XZ_RIDER_TRAIL 48
#define XZ_RIDER_JOG_GAIN .00075f
#define XZ_RIDER_AIR_IMPULSE .0065f
#define XZ_RIDER_AIR_BRAKE 3.f
#define XZ_RIDER_GRADE .055f
#define XZ_RIDER_MAX_PITCH 1.2f
static inline float xz_rider_base_at(double at){return (float)(-at*XZ_RIDER_WORLD_SCALE*XZ_RIDER_GRADE);}
enum xz_rider_phase { XZ_RIDER_SETUP, XZ_RIDER_TITLE, XZ_RIDER_RIDE, XZ_RIDER_HOLD, XZ_RIDER_RESULT };
enum xz_rider_mode { XZ_RIDER_SOLO, XZ_RIDER_COOP, XZ_RIDER_DUEL };
enum xz_rider_difficulty { XZ_RIDER_EASY, XZ_RIDER_NORMAL, XZ_RIDER_HARD };
enum xz_rider_key { XZ_RIDER_CUE, XZ_RIDER_PAD_A, XZ_RIDER_PAD_B, XZ_RIDER_PAD_C,
    XZ_RIDER_PAD_D, XZ_RIDER_PAD_E, XZ_RIDER_PAD_F, XZ_RIDER_PAD_G, XZ_RIDER_PAD_H, XZ_RIDER_PLATTER };
enum xz_rider_power { XZ_RIDER_POWER_NONE, XZ_RIDER_POWER_AURORA, XZ_RIDER_POWER_COMET };
enum xz_rider_pickup { XZ_RIDER_PICKUP_GEM, XZ_RIDER_PICKUP_AURORA, XZ_RIDER_PICKUP_COMET };
struct xz_rider_object { uint32_t at; float altitude; enum xz_rider_pickup kind; };
struct xz_rider_surface { float height, slope, curvature, thermal; };
struct xz_rider_course {
    /* World height. Derivatives use world x = source sample * 2. */
    float height[XZ_WAVE_RIDER_SAMPLES], slope[XZ_WAVE_RIDER_SAMPLES];
    float curvature[XZ_WAVE_RIDER_SAMPLES], thermal[XZ_WAVE_RIDER_SAMPLES];
    struct xz_rider_object objects[XZ_RIDER_OBJECTS];
    unsigned object_count;
};
struct xz_rider_player {
    struct xz_game_effects effects;
    float lift,downforce,landing_quality,launch_strength;
    double contact_time,contact_at;float contact_impact,contact_angle;
    double at, highwater, launch_at, last_landing, last_good_landing, fever_until, feedback_until;
    double launch_position, last_good_position;
    double flick_started, flick_until, flick_launch_at;
    float flick_pulses, flick_strength;
    int flick_direction;
    unsigned flick_launches;
    float altitude, vx, vy, pitch, pitch_target, tuck, peak, best_height, flight_peak;
    float jog_direction;
    unsigned grounded, held, score, streak, best_streak, landings, perfects, launches, hard_landings, catches;
    unsigned jog_seen, hold_seen, trail_count, trail_head, charge, powers_used;
    float air_rotation, spin_velocity, landing_boost;
    float angular_velocity, ground_lean;
    float launch_boost;
    unsigned perfect_chain, rocket_ignitions;
    double rocket_until;
    unsigned air_tricks, tricks_landed, last_tricks, power_pickups, contact_bad;
    unsigned air_crests, air_points, combo_points, combo_failed, best_combo, landing_points;
    double trick_at, boost_until, pickup_at, combo_until, air_boost_until, last_air_crest;
    enum xz_rider_pickup last_pickup;
    enum xz_rider_power power;
    double power_until, comet_until;
    uint32_t jog_ms;
    double trail_at[XZ_RIDER_TRAIL]; float trail_alt[XZ_RIDER_TRAIL];
    const char *feedback;
};
struct xz_rider {
    struct xz_wave_rider_snapshot wave;
    struct xz_rider_course course;
    struct xz_rider_player player[2];
    enum xz_rider_phase phase;
    enum xz_rider_mode mode;
    enum xz_rider_difficulty difficulty;
    unsigned deck, setup_step, quiet, leave, initialized, valid, spin_lift;
    unsigned team_score, harmonies, team_chain;
    double position, sim_at, source_position, team_pair_at, team_feedback_until;
    float source_rate, zoom, camera_alt, music[3], pulse;
    uint32_t last_ms, feed_ms;
    uint64_t course_hash; uint32_t course_generation; unsigned course_kind;
    int touch_down, touch_player;
};

void xz_rider_analyze(const struct xz_wave_rider_snapshot *,struct xz_rider_course *);
struct xz_rider_surface xz_rider_surface_at(const struct xz_rider *,double position);
unsigned xz_rider_players(const struct xz_rider *);
int xz_rider_player_for_deck(const struct xz_rider *,int deck);
void xz_rider_init(struct xz_rider *,unsigned deck,uint32_t now);
void xz_rider_feed(struct xz_rider *,const struct xz_wave_rider_snapshot *,uint32_t now);
void xz_rider_step(struct xz_rider *,uint32_t now);
void xz_rider_begin(struct xz_rider *,uint32_t now);
void xz_rider_jog(struct xz_rider *,unsigned player,float pulses,uint32_t now);
void xz_rider_button(struct xz_rider *,unsigned player,enum xz_rider_key,int down,uint32_t now);
void xz_rider_touch(struct xz_rider *,int x,int y,int down,uint32_t now);
int xz_rider_render(const struct xz_rider *,uint16_t *,size_t count,size_t stride);
struct xz_game_targets;
/* Same frame, also listing the controls drawn on it (see game_nav.h). */
int xz_rider_render_targets(const struct xz_rider *,uint16_t *,size_t count,size_t stride,struct xz_game_targets *);
/* BACK key: one setup step back, or leave from the first screen, title and result. */
void xz_rider_back(struct xz_rider *,uint32_t now);
#endif
