/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Wave Rider acceptance requires assertions
#endif
#include "wave_rider.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static struct xz_rider game[3];
static void fixture(unsigned d){
    struct xz_rider *g=&game[d];xz_rider_init(g,0,1000);
    assert(g->difficulty==XZ_RIDER_EASY);g->difficulty=(enum xz_rider_difficulty)d;
    g->valid=g->wave.valid=g->wave.playing=1;g->phase=XZ_RIDER_RIDE;g->spin_lift=0;
    g->wave.count=2400;g->wave.total=60000;g->wave.pulse_period=75;g->feed_ms=1000;
    g->position=g->sim_at=g->source_position=500;
    for(unsigned i=0;i<2400;i++){g->course.height[i]=-xz_rider_base_at(i);g->course.slope[i]=XZ_RIDER_GRADE;}
    struct xz_rider_player *p=&g->player[0];
    p->at=500;p->highwater=499;p->vx=300;p->vy=90;p->grounded=0;
    p->altitude=350;p->pitch=p->pitch_target=.65f;p->launch_at=470;p->flight_peak=40;
}
static void advance(struct xz_rider *g,unsigned n){
    for(unsigned i=0;i<n;i++){
        g->source_position=g->sim_at+1;g->feed_ms=g->last_ms+7;xz_rider_step(g,g->last_ms+7);
    }
}
static void land(unsigned d,float pitch){
    fixture(d);struct xz_rider_player *p=&game[d].player[0];
    p->altitude=7.1f;p->vy=-120;p->pitch=p->pitch_target=pitch;p->flight_peak=80;
    p->streak=3;p->air_points=125;p->air_tricks=1;advance(&game[d],1);assert(p->grounded);
}
static void air_control(void){
    for(unsigned d=0;d<3;d++){fixture(d);advance(&game[d],75);}
    const struct xz_rider_player *easy=&game[0].player[0],*normal=&game[1].player[0],*hard=&game[2].player[0];
    assert(fabsf(easy->pitch-.65f)<.0001f&&easy->vy<0);
    assert(normal->pitch<easy->pitch-.1f&&hard->pitch<normal->pitch);
    printf("ARC_JSON {\"easy_pitch_drift\":%.6f,\"normal_pitch_drift\":%.3f,\"hard_pitch_drift\":%.3f,\"gravity_retained\":true}\n",
        (double)(easy->pitch-.65f),(double)(normal->pitch-.65f),(double)(hard->pitch-.65f));
    advance(&game[0],45);assert(game[0].player[0].altitude<350);
    fixture(0);game[0].player[0].pitch=0;xz_rider_jog(&game[0],0,-80,game[0].last_ms);
    advance(&game[0],12);assert(game[0].player[0].pitch>.035f);
    float coast=game[0].player[0].pitch;advance(&game[0],15);assert(game[0].player[0].pitch>coast);
    float velocity=game[0].player[0].angular_velocity;
    xz_rider_jog(&game[0],0,velocity/(XZ_RIDER_AIR_IMPULSE*XZ_RIDER_AIR_BRAKE),game[0].last_ms);
    float stopped=game[0].player[0].pitch;advance(&game[0],40);
    assert(fabsf(game[0].player[0].pitch-stopped)<.001f&&game[0].player[0].vy<0);
    float before=game[0].player[0].vy;fixture(0);game[0].player[0].held=1;advance(&game[0],40);
    assert(game[0].player[0].vy<before-100);
    puts("PASS Easy steering responds within 80ms, coasts, brakes and holds player pitch; CUE still dives");
}
static void landings(void){
    land(0,1.f);land(1,1.f);
    assert(!game[0].player[0].contact_bad&&game[0].player[0].score>0);
    assert(game[1].player[0].contact_bad&&!game[1].player[0].score);
    land(1,.62f);land(2,.62f);assert(!game[1].player[0].contact_bad&&game[2].player[0].contact_bad);
    land(0,.38f);land(1,.38f);assert(game[0].player[0].perfects&&!game[1].player[0].perfects);
    land(1,.24f);land(2,.24f);assert(game[1].player[0].perfects&&!game[2].player[0].perfects);
    for(unsigned d=0;d<3;d++)land(d,3.14159265f);
    assert(game[0].player[0].vx>game[1].player[0].vx&&game[1].player[0].vx>game[2].player[0].vx);
    assert(game[0].player[0].streak==2&&!game[1].player[0].streak&&!game[2].player[0].streak);
    for(unsigned d=0;d<3;d++)assert(game[d].player[0].combo_failed&&!game[d].player[0].score&&!game[d].player[0].air_points);
    assert(game[0].player[0].ground_lean==0);
    printf("RECOVERY_JSON {\"easy_speed\":%.1f,\"normal_speed\":%.1f,\"hard_speed\":%.1f,\"easy_streak_retained\":2}\n",
        (double)game[0].player[0].vx,(double)game[1].player[0].vx,(double)game[2].player[0].vx);
    land(0,0);struct xz_rider_player *p=&game[0].player[0];
    assert(p->landing_points>0&&p->score==p->landing_points+p->combo_points&&p->combo_until>game[0].sim_at);
    unsigned score=p->score;advance(&game[0],30);assert(p->score==score);
    fixture(0);p=&game[0].player[0];p->highwater=1000;p->altitude=7.1f;p->vy=-120;p->pitch=0;
    p->air_points=125;advance(&game[0],1);assert(!p->score&&!p->landing_points&&p->combo_failed);
    puts("PASS difficulty landing precision, forgiving recovery, visible exact points, and no replay awards");
}
static void pickups_and_pause(void){
    for(unsigned d=0;d<3;d++){
        fixture(d);struct xz_rider *g=&game[d];g->mode=XZ_RIDER_DUEL;
        struct xz_rider_player *p=&g->player[0];p->grounded=1;p->altitude=7;
        g->course.object_count=1;g->course.objects[0]=(struct xz_rider_object){501,32-xz_rider_base_at(501),XZ_RIDER_PICKUP_GEM};
        advance(g,2);assert(p->catches==(d==0));
        float pitch=p->pitch;unsigned score=p->score;g->phase=XZ_RIDER_HOLD;advance(g,20);
        assert(p->pitch==pitch&&p->score==score&&g->difficulty==(enum xz_rider_difficulty)d);
    }
    puts("PASS Easy pickup reach is wider, difficulty shared by both riders, and pause freezes rewards");
}
static void tap(int x,int y){xz_rider_touch(&game[0],x,y,1,game[0].last_ms);xz_rider_touch(&game[0],x,y,0,game[0].last_ms);}
static void selection(void){
    fixture(0);game[0].phase=XZ_RIDER_SETUP;game[0].setup_step=3;
    tap(240,220);assert(game[0].difficulty==XZ_RIDER_EASY);
    tap(400,220);assert(game[0].difficulty==XZ_RIDER_NORMAL);
    tap(560,220);assert(game[0].difficulty==XZ_RIDER_HARD);
    tap(640,220);tap(400,193);tap(400,250);assert(game[0].difficulty==XZ_RIDER_HARD);
    tap(400,306);assert(game[0].difficulty==XZ_RIDER_HARD&&game[0].spin_lift);
    game[0].player[0].jog_seen=1;game[0].wave.position=500;tap(400,380);
    assert(game[0].phase==XZ_RIDER_RIDE&&game[0].difficulty==XZ_RIDER_HARD);
    game[0].phase=XZ_RIDER_SETUP;game[0].setup_step=3;game[0].spin_lift=0;tap(400,452);
    assert(game[0].phase==XZ_RIDER_RIDE&&game[0].difficulty==XZ_RIDER_EASY&&game[0].spin_lift);
    puts("PASS all difficulty touch choices, bounds, Spin Lift, selected ride and explicit Easy defaults");
}
int main(void){air_control();landings();pickups_and_pause();selection();return 0;}
