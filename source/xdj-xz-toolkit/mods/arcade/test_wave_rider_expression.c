/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Wave Rider acceptance requires assertions
#endif
#include "wave_rider.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846f
static struct xz_rider game,other;
static void fixture(int air){
    xz_rider_init(&game,0,1000);game.difficulty=XZ_RIDER_NORMAL;game.valid=1;game.phase=XZ_RIDER_RIDE;game.spin_lift=0;
    game.wave.valid=game.wave.playing=1;game.wave.count=2400;game.wave.total=60000;
    game.wave.pulse_period=75;game.wave.observed_ms=game.feed_ms=1000;
    game.position=game.sim_at=game.source_position=500;
    for(unsigned i=0;i<2400;i++){game.course.height[i]=-xz_rider_base_at(i);game.course.slope[i]=XZ_RIDER_GRADE;}
    for(unsigned i=0;i<2;i++){
        struct xz_rider_player *p=&game.player[i];
        p->at=500;p->highwater=499;p->vx=300;p->grounded=!air;
        p->altitude=air?350:7;p->vy=air?90:0;p->pitch=p->pitch_target=0;
        p->launch_at=470;p->flight_peak=air?40:0;
    }
}
static void advance(struct xz_rider *g,unsigned n){
    for(unsigned i=0;i<n;i++){
        g->source_position=g->sim_at+1;g->feed_ms=g->last_ms+7;
        xz_rider_step(g,g->last_ms+7);
    }
}
static void rotate(float radians){xz_rider_jog(&game,0,-radians/XZ_RIDER_JOG_GAIN,game.last_ms);}
static void flip(void){
    for(unsigned i=0;i<160&&!game.player[0].air_tricks;i++){
        if(i%4==0)xz_rider_jog(&game,0,-650,game.last_ms);
        advance(&game,1);
    }
    assert(game.player[0].air_tricks==1&&game.player[0].air_points==125);
}
static void flips_and_bank(void){
    fixture(1);
    unsigned i=0;float boosted=0;
    for(;i<160&&!game.player[0].air_tricks;i++){
        if(i%4==0)xz_rider_jog(&game,0,-650,game.last_ms);
        float before=game.player[0].vx;advance(&game,1);
        if(game.player[0].air_tricks)boosted=game.player[0].vx-before;
    }
    struct xz_rider_player *p=&game.player[0];
    assert(i<160&&p->air_points==125&&!p->score&&boosted>60);
    assert(p->air_boost_until>game.sim_at&&p->altitude>100);
    /* The same landing, with and without its pending combo, must differ by exactly 125. */
    p->angular_velocity=0;p->pitch=p->pitch_target=0;p->altitude=7.1f;p->vy=-120;p->flight_peak=100;
    other=game;other.player[0].air_points=other.player[0].air_tricks=0;
    advance(&game,1);advance(&other,1);
    assert(p->grounded&&p->combo_points==125&&!p->combo_failed&&!p->air_points);
    assert(p->score==other.player[0].score+125&&p->tricks_landed==1&&p->last_tricks==1);
    assert(p->best_combo==125&&p->combo_until>game.sim_at&&p->landing_boost>0);
    unsigned scored=p->score;advance(&game,15);assert(p->score==scored);
    printf("PASS actual 360 gives %.1f immediate speed; clean landing banks exactly 125 once\n",boosted);
}
static void crash_and_reversal(void){
    fixture(1);flip();struct xz_rider_player *p=&game.player[0];
    p->altitude=7.1f;p->vy=-120;p->angular_velocity=0;p->pitch=p->pitch_target=PI;p->flight_peak=100;
    advance(&game,1);
    assert(p->grounded&&p->hard_landings==1&&p->contact_bad);
    assert(p->combo_failed&&p->combo_points==125&&!p->score&&!p->air_points&&!p->tricks_landed);
    double expiry=p->combo_until;advance(&game,151);assert(game.sim_at>=expiry&&!p->score);
    fixture(1);rotate(2.2f);advance(&game,18);rotate(-2.2f);advance(&game,18);
    assert(!game.player[0].air_tricks&&!game.player[0].air_points);
    fixture(0);for(unsigned i=0;i<20;i++){rotate(2.2f);advance(&game,1);}
    assert(!game.player[0].air_tricks&&fabsf(game.player[0].pitch_target)<=XZ_RIDER_MAX_PITCH);
    puts("PASS hard landing loses pending combo; partial turns and grounded spins cannot earn flips");
}
static void crests_and_multiplayer(void){
    fixture(1);game.mode=XZ_RIDER_DUEL;
    for(unsigned i=0;i<2400;i++){
        float a=((float)i-500)*2*PI/32;
        game.course.height[i]=40+20*sinf(a)-xz_rider_base_at(i);
        game.course.slope[i]=20*PI/32*cosf(a)+XZ_RIDER_GRADE;
    }
    game.player[1].grounded=1;game.player[1].held=1;game.player[1].altitude=47;
    advance(&game,90);
    assert(game.player[0].air_crests>=2&&game.player[0].air_points==game.player[0].air_crests*100);
    assert(!game.player[0].score&&!game.player[1].air_crests&&!game.player[1].air_points);
    unsigned n=game.player[0].air_crests;
    fixture(1);game.player[0].highwater=1000;flip();
    game.player[0].altitude=7.1f;game.player[0].vy=-120;
    game.player[0].angular_velocity=0;game.player[0].pitch=game.player[0].pitch_target=0;advance(&game,1);
    assert(!game.player[0].score&&game.player[0].combo_failed);
    printf("PASS %u actual terrain crests accumulate before landing; players isolated; replay cannot bank\n",n);
}
static void pickups(void){
    fixture(0);game.mode=XZ_RIDER_DUEL;
    game.course.object_count=3;
    game.course.objects[0]=(struct xz_rider_object){501,17-xz_rider_base_at(501),XZ_RIDER_PICKUP_GEM};
    game.course.objects[1]=(struct xz_rider_object){504,17-xz_rider_base_at(504),XZ_RIDER_PICKUP_AURORA};
    game.course.objects[2]=(struct xz_rider_object){506,17-xz_rider_base_at(506),XZ_RIDER_PICKUP_COMET};
    advance(&game,12);
    for(unsigned i=0;i<2;i++){
        const struct xz_rider_player *p=&game.player[i];
        assert(p->grounded&&p->catches==3&&p->power_pickups==2&&p->score==120);
        assert(p->power==XZ_RIDER_POWER_COMET&&p->charge==3);
    }
    advance(&game,12);assert(game.player[0].catches==3);
    fixture(1);game.player[0].altitude=110;game.player[0].vy=0;
    game.course.object_count=2;
    game.course.objects[0]=(struct xz_rider_object){501,17-xz_rider_base_at(501),XZ_RIDER_PICKUP_GEM};
    game.course.objects[1]=(struct xz_rider_object){501,110-xz_rider_base_at(501),XZ_RIDER_PICKUP_GEM};
    advance(&game,3);assert(game.player[0].catches==1);
    struct xz_wave_rider_snapshot wave={0};wave.valid=1;wave.count=2400;wave.total=60000;
    wave.normalization=300;wave.pulse_period=75;wave.pulse_confidence=1;
    for(unsigned i=0;i<2400;i++){
        wave.bands[i*3]=i%75<10?220:8;wave.bands[i*3+1]=65;
        wave.bands[i*3+2]=i%300<12?110:5;
    }
    xz_rider_analyze(&wave,&game.course);
    unsigned ground=0,air=0,powers=0;
    for(unsigned i=0;i<game.course.object_count;i++){
        const struct xz_rider_object *o=&game.course.objects[i];
        float h=o->altitude-game.course.height[o->at];
        if(fabsf(h-17)<.01f)ground++;else {assert(h>=46);air++;}
        powers+=o->kind!=XZ_RIDER_PICKUP_GEM;
    }
    assert(ground>=50&&air>=12&&powers>=2);
    printf("PASS sliding and air collection, automatic powers, exact once per player; %u surface/%u air/%u powers\n",ground,air,powers);
}
static void inertia_and_ground(void){
    fixture(1);float start_height=game.player[0].altitude;
    advance(&game,20);assert(game.player[0].altitude>start_height);
    advance(&game,110);assert(game.player[0].vy<0&&game.player[0].altitude<start_height&&game.player[0].pitch<0);
    fixture(1);xz_rider_jog(&game,0,-300,game.last_ms);
    assert(game.player[0].pitch==0&&game.player[0].angular_velocity>.5f);
    advance(&game,15);float a=game.player[0].pitch;
    advance(&game,15);assert(game.player[0].pitch>a+.015f);
    float before=game.player[0].angular_velocity;
    xz_rider_jog(&game,0,before/(XZ_RIDER_AIR_IMPULSE*XZ_RIDER_AIR_BRAKE),game.last_ms);
    assert(fabsf(game.player[0].angular_velocity)<.001f);
    fixture(0);game.spin_lift=1;
    for(unsigned i=0;i<12;i++){
        xz_rider_jog(&game,0,80,game.last_ms);advance(&game,15);
        assert(game.player[0].grounded&&fabsf(game.player[0].pitch)<.451f);
    }
    xz_rider_jog(&game,0,-1800,game.last_ms);advance(&game,1);
    assert(!game.player[0].grounded&&game.player[0].flick_launches==1&&game.player[0].vy>200);
    fixture(0);game.spin_lift=1;xz_rider_button(&game,0,XZ_RIDER_CUE,1,game.last_ms);
    xz_rider_jog(&game,0,-1800,game.last_ms);advance(&game,45);assert(game.player[0].grounded);
    puts("PASS natural rising/falling arc and pitch; rotation coasts and brakes; ground constrained until fast spin");
}
static void air_response(void){
    fixture(1);xz_rider_jog(&game,0,-80,game.last_ms);
    unsigned onset=0;
    while(onset<150&&game.player[0].pitch<.035f){advance(&game,1);onset++;}
    fixture(1);game.player[0].angular_velocity=4;
    unsigned reversal=0;
    while(reversal<150&&game.player[0].angular_velocity>=0){
        if(reversal%3==0)xz_rider_jog(&game,0,80,game.last_ms);
        advance(&game,1);reversal++;
    }
    printf("AIR_RESPONSE onset_2deg_ms=%.1f reverse_ms=%.1f\n",onset*1000.f/150,reversal*1000.f/150);fflush(stdout);
    assert(onset<=12&&reversal<=15);
    assert(fabsf(game.player[0].angular_velocity)<1.f);
    fixture(1);game.player[0].angular_velocity=4;other=game;
    xz_rider_jog(&game,0,320,game.last_ms);
    for(unsigned i=0;i<4;i++)xz_rider_jog(&other,0,80,other.last_ms);
    assert(fabsf(game.player[0].angular_velocity-other.player[0].angular_velocity)<.001f);
    fixture(1);xz_rider_jog(&game,0,-4096,game.last_ms);advance(&game,120);
    assert(!game.player[0].air_tricks);
    fixture(1);game.player[0].at=700;game.player[0].highwater=699;game.player[0].rocket_until=501;
    advance(&game,1);assert(game.player[0].at>=700);
    puts("PASS small airborne correction visible within 80ms; opposite steering reverses within 100ms");
}
static void rocket_reward(void){
    fixture(1);struct xz_rider_player *p=&game.player[0];
    for(unsigned i=0;i<3;i++){
        p->at=game.sim_at+40;game.sim_at=p->at;p->highwater=p->at-1;
        p->altitude=7.1f;p->vy=-80;p->vx=320;p->grounded=0;
        p->pitch=p->pitch_target=p->angular_velocity=0;p->flight_peak=80;p->launch_at=game.sim_at-20;
        advance(&game,1);assert(p->perfect_chain==i+1);
        if(i<2)assert(!p->rocket_until);else assert(p->rocket_until>game.sim_at&&p->rocket_ignitions==1);
    }
    p->grounded=0;p->altitude=250;p->vy=0;p->pitch=p->angular_velocity=0;
    other=game;other.player[0].rocket_until=0;
    advance(&game,60);advance(&other,60);
    assert(p->vx>other.player[0].vx+80&&p->at>other.player[0].at+10);
    assert(p->vy<0&&p->altitude<250);
    float speed=p->vx-other.player[0].vx;
    p->altitude=7.1f;p->vy=-100;p->pitch=p->pitch_target=PI;p->angular_velocity=0;
    p->flight_peak=100;p->launch_at=game.sim_at-30;advance(&game,1);
    assert(p->contact_bad&&!p->perfect_chain&&!p->rocket_until);
    printf("PASS three consecutive perfects earn rocket: +%.1f speed, farther flight, gravity retained; crash cancels\n",speed);
}
static void lifecycle(void){
    fixture(1);flip();other=game;game.phase=XZ_RIDER_HOLD;advance(&game,20);
    assert(game.player[0].air_points==other.player[0].air_points&&game.player[0].pitch==other.player[0].pitch);
    game.wave.position=500;game.wave.observed_ms=game.last_ms;game.wave.generation=2;game.wave.track_hash=33;
    game.course_generation=1;game.course_hash=22;
    xz_rider_feed(&game,&game.wave,game.last_ms);
    assert(!game.player[0].air_points&&!game.player[0].air_tricks&&!game.player[0].combo_points);
    puts("PASS pause retains pending combo; changed track clears it");
}
static void downhill_course(void){
    fixture(0);
    for(unsigned i=0;i<2400;i++){game.course.height[i]=80;game.course.slope[i]=0;}
    struct xz_rider_surface a=xz_rider_surface_at(&game,500),b=xz_rider_surface_at(&game,1500);
    assert(fabsf(a.height-b.height-110)<.001f&&fabsf(a.slope+XZ_RIDER_GRADE)<.0001f);
    game.wave.total=1000000;game.wave.first=899000;game.wave.position=900000;
    xz_rider_begin(&game,game.last_ms);advance(&game,40);
    assert(game.camera_alt<-98000&&isfinite(game.zoom)&&game.zoom>.1f);
    assert(game.player[0].altitude>xz_rider_base_at(game.player[0].at));
    puts("PASS gentle downhill grade keeps waveform contours; camera follows deep into long tracks");
}
int main(void){air_response();flips_and_bank();crash_and_reversal();crests_and_multiplayer();pickups();inertia_and_ground();rocket_reward();lifecycle();downhill_course();return 0;}
