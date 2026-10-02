/* SPDX-License-Identifier: MIT */
#ifndef XZ_BEAT_ARCADE_H
#define XZ_BEAT_ARCADE_H
#include <stddef.h>
#include <stdint.h>

enum xz_arcade_mode { XZ_ARCADE_PONG_SOLO, XZ_ARCADE_PONG_DUEL, XZ_ARCADE_BREAK_SOLO, XZ_ARCADE_BREAK_COOP };
enum xz_arcade_phase { XZ_ARCADE_MENU, XZ_ARCADE_COUNT_IN, XZ_ARCADE_PLAY, XZ_ARCADE_MUSIC, XZ_ARCADE_RESULT, XZ_ARCADE_CLOCK, XZ_ARCADE_WAIT_TRACK };
enum xz_arcade_key { XZ_ARCADE_PLAY_KEY, XZ_ARCADE_CUE_KEY, XZ_ARCADE_PAD_A, XZ_ARCADE_PAD_B, XZ_ARCADE_PAD_C, XZ_ARCADE_PAD_D, XZ_ARCADE_PAD_E, XZ_ARCADE_PAD_F, XZ_ARCADE_PAD_G, XZ_ARCADE_PAD_H, XZ_ARCADE_PAUSE_KEY };
enum xz_arcade_contact { XZ_ARCADE_WALL, XZ_ARCADE_PADDLE, XZ_ARCADE_BRICK, XZ_ARCADE_GOAL };
struct xz_arcade_player {
    float position, velocity, aim;
    double strike, contact, wide_until, drop_until;
    unsigned energy, score, perfects, shield, ready;
    int contact_graded;
    double last_note, note_at;
    float timing_ms;
    unsigned combo,best_combo,notes;
};
struct xz_arcade_flight {
    float x0,y0,x1,y1,nx,ny;
    double start,end;
    enum xz_arcade_contact contact;
    int index;
};
struct xz_arcade {
    enum xz_arcade_mode mode;
    enum xz_arcade_phase phase;
    struct xz_arcade_player player[2];
    struct xz_arcade_flight flight;
    uint8_t bricks[48];
    float x,y,dx,dy,bpm,trail_x[12],trail_y[12];
    double beat,clock,count_until,feedback_until,drop_visual_until;
    uint32_t last_ms,tap_ms,live_ms,tap_intervals[4];
    unsigned initialized,round_active,taps,live,clock_deck,lives,stage,rally,best,team_score,remaining,style,trail_count,trail_head;
    float impact_x,impact_y;
    double impact_until;
    unsigned difficulty,sensitivity,clock_selected,clock_source,music_known,music_playing,phase_aligned;
    uint32_t track_id,music_at;
    unsigned groove,drop_pending,drops,clock_return;
    double note_flash,drop_at;
    float menu_jog;
    int last_player,winner,feedback_player,touch_down,quiet;
    const char *feedback;
};
void xz_arcade_init(struct xz_arcade *,uint32_t now);
void xz_arcade_start(struct xz_arcade *,enum xz_arcade_mode,uint32_t now);
void xz_arcade_step(struct xz_arcade *,uint32_t now);
void xz_arcade_move(struct xz_arcade *,int deck,float pixels,uint32_t now);
void xz_arcade_key(struct xz_arcade *,int deck,enum xz_arcade_key,uint32_t now);
void xz_arcade_touch(struct xz_arcade *,int x,int y,int down,uint32_t now);
/* Validated read-only beat observation. No DJ transport calls. */
void xz_arcade_sync(struct xz_arcade *,float bpm,unsigned beat_in_bar,uint32_t at);
void xz_arcade_music(struct xz_arcade *,float bpm,float bar_phase,unsigned playing,uint32_t track,unsigned deck,uint32_t at);
void xz_arcade_music_missing(struct xz_arcade *);
void xz_arcade_tempo(struct xz_arcade *,float bpm,uint32_t at);
float xz_arcade_jog_gain(const struct xz_arcade *);
float xz_arcade_paddle_size(const struct xz_arcade *,int deck);
int xz_arcade_render(const struct xz_arcade *,uint16_t *,size_t count,size_t stride);
#endif
