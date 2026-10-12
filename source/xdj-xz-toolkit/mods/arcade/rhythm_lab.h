/* SPDX-License-Identifier: MIT */
#ifndef XZ_RHYTHM_LAB_H
#define XZ_RHYTHM_LAB_H
#include "wave_rider_source.h"
#include "glyph_run.h"
#include <stddef.h>

#define XZ_LAB_STUDIES 10
#define XZ_LAB_CASES 11
#define XZ_LAB_EVENTS 64
#define XZ_LAB_TURN_PULSES 8378.f
#define XZ_LAB_SECONDS 90.f
enum xz_lab_mode { LAB_PULSE_HOLD, LAB_WAVE_LANES, LAB_WHEEL_MATCH, LAB_SCRATCH,
    LAB_RIBBON, LAB_ECHO, LAB_STEPS, LAB_BLASTER, LAB_PUZZLE, LAB_SPRING, LAB_GLYPH };
enum xz_lab_phase { LAB_MENU, LAB_SETUP, LAB_BRIEF, LAB_RUN, LAB_PAUSE, LAB_RESULTS, LAB_RATINGS, LAB_CONTROLS };
enum xz_lab_note { LAB_TAP, LAB_HOLD, LAB_CUT, LAB_TARGET, LAB_RELEASE, LAB_GLYPH_SPIKE, LAB_GLYPH_GATE, LAB_GLYPH_GAP };
enum xz_lab_grade { LAB_PENDING, LAB_PERFECT, LAB_GOOD, LAB_MISSED, LAB_HOLDING };
enum xz_lab_key { LAB_CUE, LAB_PAD_A, LAB_PAD_B, LAB_PAD_C, LAB_PAD_D, LAB_PAD_E,
    LAB_PAD_F, LAB_PAD_G, LAB_PAD_H, LAB_PLATTER, LAB_TOUCH };
struct xz_lab_description { const char *name,*instruction,*question,*reference,*source; unsigned duo; };
struct xz_lab_event {
    int64_t id; double at,end;
    float angle,energy; int lane,direction; unsigned kind,band;
    unsigned char grade[2]; float covered[2],duration[2];
};
struct xz_lab_player {
    float cursor,angle,charge,stroke,travel,coverage,coverage_total,last_error_ms;
    uint32_t held,jog_ms; int stroke_direction,stroke_used;
    unsigned hits,misses,perfects,timing_hits,motion_hits,attacks,score,streak,best_streak;
    unsigned cue_edges,jog_events,shots,collections,cleared;
    int gx,gy,target_x,target_y,pair_flip; uint8_t grid[24];
    double feedback_until,shot_at,launch_at; float shot_angle,launch_charge; int launch_grade;
    const char *feedback;
};
struct xz_lab_record {
    unsigned played,fun,clarity,effort,wants_replay;
    float seconds;
    unsigned hits[2],misses[2],score[2]; float travel[2],coverage[2];
};
struct xz_rhythm_lab {
    struct xz_wave_rider_snapshot wave;
    struct xz_glyph_player glyph[2];
    struct xz_lab_player player[2]; struct xz_lab_event events[XZ_LAB_EVENTS];
    struct xz_lab_record record[XZ_LAB_CASES];
    enum xz_lab_mode mode; enum xz_lab_phase phase;
    unsigned initialized,leave,valid,deck,control_deck,duo,study,menu_selected,setup_step;
    unsigned action_binding[2],controls_return;
    unsigned event_count,playing,manual_pulse,tap_count,calibrating,cal_deck,feedback_row;
    double position,previous,source_position,origin,start_position,practice_until;
    double taps[8]; float period,rate,elapsed,duration,timing_ms,turn_pulses[2],cal_pulses[2];
    float music[3],pulse; uint32_t now,last_ms,feed_ms;
    int64_t next_event,last_sweep,echo_base; int touch_down,touch_player;
};
const struct xz_lab_description *xz_lab_description(unsigned mode);
const char *xz_lab_action_label(const struct xz_rhythm_lab *,unsigned deck);
int xz_lab_action_key(const struct xz_rhythm_lab *,unsigned deck,int key);
unsigned xz_lab_players(const struct xz_rhythm_lab *);
int xz_lab_player_for_deck(const struct xz_rhythm_lab *,int deck);
void xz_lab_init(struct xz_rhythm_lab *,uint32_t now);
void xz_lab_feed(struct xz_rhythm_lab *,const struct xz_wave_rider_snapshot *,uint32_t now);
void xz_lab_step(struct xz_rhythm_lab *,uint32_t now);
void xz_lab_begin(struct xz_rhythm_lab *,uint32_t now);
void xz_lab_jog(struct xz_rhythm_lab *,int deck,float pulses,uint32_t now);
void xz_lab_button(struct xz_rhythm_lab *,unsigned player,enum xz_lab_key,int down,uint32_t now);
void xz_lab_touch(struct xz_rhythm_lab *,int x,int y,int down,uint32_t now);
float xz_lab_ribbon(const struct xz_rhythm_lab *,double at);
float xz_lab_band(const struct xz_rhythm_lab *,double at,unsigned band);
unsigned xz_lab_lane(const struct xz_lab_player *);
uint32_t xz_lab_matches(const struct xz_lab_player *);
int xz_lab_render(const struct xz_rhythm_lab *,uint16_t *,size_t count,size_t stride);
struct xz_game_targets;
/* Same frame, also listing the controls drawn on it (see game_nav.h). */
int xz_lab_render_targets(const struct xz_rhythm_lab *,uint16_t *,size_t count,size_t stride,struct xz_game_targets *);
/* BACK key: one screen back toward the menu, or leave from the menu. A study in progress ignores it. */
void xz_lab_back(struct xz_rhythm_lab *,uint32_t now);
#endif
