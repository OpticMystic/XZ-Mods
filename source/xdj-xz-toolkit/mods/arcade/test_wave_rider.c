/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Wave Rider acceptance requires assertions
#endif
#include "wave_rider.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

enum {N=18000,W=800,H=480};
static uint8_t track[N*3];
static struct xz_rider game;
static struct xz_wave_rider_snapshot wave;
static float wrap(float x){while(x>3.14159265f)x-=6.2831853f;while(x< -3.14159265f)x+=6.2831853f;return x;}
static void song(void){
    for(unsigned i=0;i<N;i++){
        track[i*3]=(uint8_t)(i%75<10?240-i%75*21:4);
        track[i*3+1]=(uint8_t)(65+18*sinf(i*.009f));
        track[i*3+2]=(uint8_t)(i%300<25?100-i%25*3:4);
    }
}
static void snapshot(struct xz_wave_rider_snapshot *w,double at,uint32_t now){
    assert(!xz_wave_rider_window(w,track,N,at));w->observed_ms=now;
}
static void start(enum xz_rider_mode mode){
    memset(&wave,0,sizeof(wave));wave.normalization=300;wave.generation=1;wave.track_hash=12;
    wave.kind=1;wave.playing=1;wave.pulse_period=75;wave.pulse_origin=0;wave.pulse_confidence=1;
    strcpy(wave.title,"MUSICAL HILL FIXTURE");snapshot(&wave,300,1000);
    xz_rider_init(&game,0,1000);game.mode=mode;game.difficulty=XZ_RIDER_NORMAL;xz_rider_feed(&game,&wave,1000);xz_rider_begin(&game,1000);
    assert(game.phase==XZ_RIDER_RIDE);
}
static void feed_at(double at,uint32_t now){
    snapshot(&wave,at,now);xz_rider_feed(&game,&wave,now);xz_rider_step(&game,now);
}
static void terrain(void){
    song();struct xz_wave_rider_snapshot rhythm={0};xz_wave_rider_rhythm(&rhythm,track,N);
    assert(rhythm.pulse_confidence>.5f&&fabsf(rhythm.pulse_period-75)<1);
    start(XZ_RIDER_SOLO);
    struct xz_rider_surface previous=xz_rider_surface_at(&game,600);
    float lo=1000,hi=0;
    for(double at=600.1;at<1400;at+=.1){
        struct xz_rider_surface s=xz_rider_surface_at(&game,at);
        assert(isfinite(s.height)&&isfinite(s.slope)&&isfinite(s.curvature));
        assert(fabsf(s.height-previous.height)<.65f);
        assert(fabsf(s.slope-previous.slope)<.08f);
        assert(fabsf((s.height-previous.height)/.2f-(s.slope+previous.slope)*.5f)<.04f);
        if(s.height<lo)lo=s.height;if(s.height>hi)hi=s.height;previous=s;
    }
    assert(hi-lo>35);
    struct xz_rider_surface a=xz_rider_surface_at(&game,1100.5);
    snapshot(&wave,1200,1020);xz_rider_feed(&game,&wave,1020);
    struct xz_rider_surface b=xz_rider_surface_at(&game,1100.5);
    assert(fabsf(a.height-b.height)<.001f&&fabsf(a.slope-b.slope)<.0001f);
    for(unsigned i=0;i<N*3;i++)track[i]=(uint8_t)(track[i]/3);
    snapshot(&wave,1200,1040);xz_rider_feed(&game,&wave,1040);
    assert(xz_rider_surface_at(&game,1100.5).height<a.height*.7f);
    memset(track,0,sizeof(track));xz_wave_rider_rhythm(&rhythm,track,N);
    assert(!rhythm.pulse_confidence);start(XZ_RIDER_SOLO);
    for(unsigned i=0;i<wave.count;i++)assert(!game.course.height[i]&&!game.course.slope[i]);
    assert(!game.course.object_count);
    memset(track,60,sizeof(track));xz_wave_rider_rhythm(&rhythm,track,N);assert(!rhythm.pulse_confidence);
    wave.pulse_confidence=0;snapshot(&wave,900,1060);xz_rider_feed(&game,&wave,1060);
    for(unsigned i=100;i<1500;i++)assert(fabsf(game.course.height[i]-game.course.height[900])<.001f);
    for(unsigned i=0;i<N;i++){track[i*3]=(uint8_t)(100+80*sinf(i*.00025f));track[i*3+1]=track[i*3+2]=0;}
    xz_wave_rider_rhythm(&rhythm,track,N);assert(!rhythm.pulse_confidence);
    puts("PASS waveform rhythm and energy compose smooth hills; moving windows agree; silence and constant data stay flat");
}
/* A predictive controller tests achievable play through the public inputs.
   It estimates ballistic contact; it never writes player position or velocity. */
static float landing_prediction(const struct xz_rider *g,const struct xz_rider_player *p,int down,float *angle){
    float gravity=430*(1+3*down);if(p->streak>=3&&!down)gravity*=.82f;
    for(float t=.02f;t<2.4f;t+=.02f){
        struct xz_rider_surface s=xz_rider_surface_at(g,p->at+p->vx*t/2);
        if(p->altitude+p->vy*t-.5f*gravity*t*t<=s.height+7){
            *angle=atanf(s.slope);
            return (s.slope<-.08f?150.f:0.f)-fabsf(p->vy-gravity*t-p->vx*s.slope)*.08f-t*4;
        }
    }
    *angle=atan2f(p->vy,p->vx);return -200;
}
static void pilot(unsigned i,uint32_t now){
    struct xz_rider_player *p=&game.player[i];struct xz_rider_surface s=xz_rider_surface_at(&game,p->at);
    float aim=atanf(s.slope)+(s.slope>.05f?.18f:-.06f);
    if(!p->grounded){float predicted;landing_prediction(&game,p,0,&predicted);aim=predicted;}
    if(p->grounded){float lean=fmaxf(-.4f,fminf(.42f,aim-atanf(s.slope)));xz_rider_jog(&game,i,(p->ground_lean-lean)/XZ_RIDER_JOG_GAIN,now);}
    else{float velocity=wrap(aim-p->pitch)*10; xz_rider_jog(&game,i,(p->angular_velocity-velocity)/XZ_RIDER_AIR_IMPULSE,now);}
}
static struct xz_rider_player replay(int policy,enum xz_rider_mode mode){
    song();start(mode);
    for(unsigned ms=20;ms<=90000;ms+=20){
        uint32_t now=1000+ms;snapshot(&wave,300+ms*.15,now);xz_rider_feed(&game,&wave,now);
        xz_rider_step(&game,now);
        for(unsigned p=0;p<xz_rider_players(&game);p++){
            if(policy==1)pilot(p,now);
            else xz_rider_button(&game,p,XZ_RIDER_CUE,policy==2,now);
        }
        xz_rider_step(&game,now);
        for(unsigned p=0;p<xz_rider_players(&game);p++){
            struct xz_rider_player *r=&game.player[p];
            assert(isfinite(r->at)&&isfinite(r->altitude)&&isfinite(r->vx)&&isfinite(r->vy)&&isfinite(r->pitch));
            assert(r->altitude>=xz_rider_surface_at(&game,r->at).height+6.7f);
            assert(r->at>=game.sim_at-75.01&&r->at<=game.sim_at+280.01);
        }
    }
    return game.player[0];
}
static void surfing(void){
    struct xz_rider_player idle=replay(0,XZ_RIDER_SOLO),held=replay(2,XZ_RIDER_SOLO),skilled=replay(1,XZ_RIDER_SOLO);
    assert(!held.launches&&!held.landings&&held.catches>20&&held.score==held.catches*40);
    fprintf(stderr,"ANGLE_CHAIN idle=%u score=%u perfect=%u streak=%u launches=%u lands=%u hard=%u height=%.1f\n",idle.score,skilled.score,skilled.perfects,skilled.best_streak,skilled.launches,skilled.landings,skilled.hard_landings,skilled.best_height);
    assert(skilled.perfects>5&&skilled.best_streak>=3&&skilled.rocket_ignitions>0&&skilled.best_height>120);
    assert(skilled.best_height>250&&skilled.best_height>idle.best_height*1.15f);
    printf("SURF_JSON {\"idle_score\":%u,\"skilled_score\":%u,\"perfect_landings\":%u,\"best_streak\":%u,\"best_air_height\":%.2f,\"holding_never_launches\":true}\n",
        idle.score,skilled.score,skilled.perfects,skilled.best_streak,(double)skilled.best_height);
    puts("PASS downhill momentum, crest takeoff, rotation-aware landings, rhythmic streaks and earned high flights");
}
static void multiplayer(void){
    (void)replay(1,XZ_RIDER_DUEL);
    assert(game.player[0].score==game.player[1].score&&game.player[0].perfects==game.player[1].perfects);
    assert(fabs(game.player[0].at-game.player[1].at)<.001);
    assert(fabsf(game.player[0].altitude-game.player[1].altitude)<.001);
    unsigned score0=game.player[0].score;float pitch0=game.player[0].pitch_target;
    xz_rider_jog(&game,1,40,game.last_ms);assert(game.player[0].pitch_target==pitch0&&game.player[1].pitch_target!=pitch0);
    xz_rider_button(&game,1,XZ_RIDER_CUE,1,game.last_ms);assert(game.player[1].tuck==1);
    xz_rider_button(&game,1,XZ_RIDER_CUE,0,game.last_ms);assert(game.player[1].tuck==0&&game.player[0].score==score0);
    (void)replay(1,XZ_RIDER_COOP);
    assert(game.harmonies>=5&&game.team_score>game.player[0].score+game.player[1].score);
    assert(game.player[0].catches==game.player[1].catches);
    game.mode=XZ_RIDER_SOLO;game.deck=1;assert(xz_rider_player_for_deck(&game,0)==-1&&xz_rider_player_for_deck(&game,1)==0);
    game.mode=XZ_RIDER_DUEL;assert(xz_rider_player_for_deck(&game,0)==0&&xz_rider_player_for_deck(&game,1)==1);
    puts("PASS same-screen duel fairness and independent controls; co-op harmony rewards; independent collectible scoring");
}
static void lifecycle(void){
    song();start(XZ_RIDER_DUEL);
    feed_at(309,1060);
    struct xz_rider_player before=game.player[0];
    for(unsigned now=1080;now<1480;now+=20){wave.playing=0;feed_at(309,now);}
    assert(game.phase==XZ_RIDER_HOLD&&game.player[0].score==before.score);
    assert(game.player[0].altitude==before.altitude&&game.player[0].vx==before.vx);
    wave.playing=1;feed_at(310,1500);assert(game.phase==XZ_RIDER_RIDE);
    game.player[0].score=400;game.player[1].score=700;game.player[0].highwater=1000;game.player[1].highwater=1000;
    feed_at(100,1520);
    for(unsigned i=0;i<200;i++)feed_at(100+i,1540+i*20);
    assert(game.player[0].score==400&&game.player[1].score==700);
    unsigned now=5600;wave.valid=0;wave.error=XZ_WAVE_RIDER_NO_TRACK;wave.observed_ms=now;xz_rider_feed(&game,&wave,now);
    assert(!game.valid&&game.phase==XZ_RIDER_SETUP);
    wave.generation=2;wave.track_hash=35;snapshot(&wave,500,now+20);xz_rider_feed(&game,&wave,now+20);
    assert(game.phase==XZ_RIDER_SETUP&&game.setup_step==2&&!game.player[0].score&&!game.player[1].score);
    xz_rider_begin(&game,now+20);xz_rider_step(&game,now+500);assert(!game.valid&&game.phase==XZ_RIDER_SETUP);
    snapshot(&wave,N-1,now+600);wave.playing=0;xz_rider_feed(&game,&wave,now+600);
    game.phase=XZ_RIDER_RIDE;xz_rider_feed(&game,&wave,now+600);assert(game.phase==XZ_RIDER_RESULT);
    xz_rider_button(&game,0,XZ_RIDER_CUE,1,now+600);assert(game.leave);
    puts("PASS pause freezes physics, replay cannot farm, track change resets both riders, stale data/setup and track-end handoff");
}
static void powers_and_rotation(void){
    song();start(XZ_RIDER_DUEL);
    struct xz_rider_player *p=&game.player[0];
    xz_rider_button(&game,0,XZ_RIDER_PAD_F,1,1000);assert(!p->power&&!p->powers_used);
    p->charge=8;float y=p->altitude,vy=p->vy;
    xz_rider_button(&game,0,XZ_RIDER_PAD_F,1,1000);
    assert(p->power==XZ_RIDER_POWER_COMET&&p->charge==3&&p->altitude==y&&p->vy==vy);
    unsigned launches=p->launches;
    for(unsigned ms=20;ms<=4000&&p->power;ms+=20)feed_at(300+ms*.15,1000+ms);
    assert(p->launches>launches&&!p->power);
    unsigned charge=p->charge;
    xz_rider_button(&game,0,XZ_RIDER_PAD_E,1,game.last_ms);assert(p->power==XZ_RIDER_POWER_AURORA&&p->charge==charge-3);
    xz_rider_button(&game,0,XZ_RIDER_CUE,1,game.last_ms);
    xz_rider_button(&game,0,XZ_RIDER_PAD_A,1,game.last_ms);
    xz_rider_button(&game,0,XZ_RIDER_CUE,0,game.last_ms);assert(p->tuck);
    xz_rider_button(&game,0,XZ_RIDER_PAD_A,0,game.last_ms);assert(!p->tuck);
    float aim=p->pitch_target;xz_rider_jog(&game,0,NAN,game.last_ms);assert(p->pitch_target==aim);
    for(unsigned i=0;i<1000;i++)xz_rider_jog(&game,0,1e9f,game.last_ms);
    assert(fabsf(p->pitch_target-p->pitch)<=6.284f);
    puts("PASS powers spend earned charge and amplify terrain launch without free jump; chord release and bounded rotational input");
}
static struct xz_rider_player cadence(unsigned step){
    song();start(XZ_RIDER_SOLO);
    for(unsigned ms=step;ms<=6000;ms+=step){
        uint32_t now=1000+ms;
        if(ms%40==0){snapshot(&wave,300+ms*.15,now);xz_rider_feed(&game,&wave,now);}
        xz_rider_step(&game,now);
        if(ms%80==0){
            xz_rider_button(&game,0,XZ_RIDER_CUE,ms%960<320,now);
            xz_rider_jog(&game,0,ms%640<320?3:-3,now);
        }
    }
    return game.player[0];
}
static void cadence_test(void){
    struct xz_rider_player a=cadence(8),b=cadence(16);
    assert(fabs(a.at-b.at)<.05&&fabsf(a.altitude-b.altitude)<.1f);
    assert(fabsf(a.vx-b.vx)<.1f&&fabsf(a.vy-b.vy)<.1f&&a.score==b.score);
    puts("PASS fixed-step contact and flight agree across 8/16ms render cadence with a 40ms source publisher");
}
static void cue_and_jog_control(void){
    song();start(XZ_RIDER_DUEL);
    for(unsigned i=0;i<2;i++){game.player[i].grounded=0;game.player[i].altitude=400;game.player[i].vy=0;}
    xz_rider_button(&game,0,XZ_RIDER_PLATTER,1,1000);
    assert(!game.player[0].tuck);
    xz_rider_button(&game,0,XZ_RIDER_CUE,1,1000);
    feed_at(315,1100);
    float difference=game.player[1].vy-game.player[0].vy;
    assert(difference>90&&game.player[1].altitude-game.player[0].altitude>4);
    xz_rider_button(&game,0,XZ_RIDER_CUE,0,1100);assert(!game.player[0].tuck);
    feed_at(330,1200);
    assert(fabsf((game.player[1].vy-game.player[0].vy)-difference)<10);
    game.phase=XZ_RIDER_SETUP;game.player[0].ground_lean=0;game.player[0].pitch_target=game.player[0].pitch=atanf(xz_rider_surface_at(&game,game.player[0].at).slope);
    float travel=0;
    for(unsigned i=0;i<103;i++){
        float prior=game.player[0].pitch;xz_rider_jog(&game,0,80,1200);
        travel+=fabsf(game.player[0].pitch-prior);
        assert(fabsf(game.player[0].pitch-prior)<=.061f);
        assert(fabsf(game.player[0].pitch)<=XZ_RIDER_MAX_PITCH);
    }
    assert(travel<.41f);
    xz_rider_jog(&game,0,-80,1200);assert(game.player[0].pitch>-XZ_RIDER_MAX_PITCH);
    printf("CONTROL_JSON {\"cue_extra_downward_speed_100ms\":%.2f,\"long_turn_radians\":%.3f,\"platter_does_not_hold_dive\":true}\n",(double)difference,(double)travel);
}
static void spin_lift(void){
    song();start(XZ_RIDER_DUEL);
    double at=0;
    for(double x=wave.position;x<wave.position+300;x+=.25){
        struct xz_rider_surface s=xz_rider_surface_at(&game,x);
        if(s.slope>.3f&&-360*360*s.curvature>600){at=x;break;}
    }
    assert(at>0);feed_at(at,1020);
    game.position=game.source_position=game.sim_at=at;
    for(unsigned i=0;i<2;i++){
        struct xz_rider_player *p=&game.player[i];p->at=at;p->grounded=1;p->vx=360;p->vy=0;
        p->altitude=xz_rider_surface_at(&game,at).height+7;p->pitch=p->pitch_target=0;
    }
    float altitude=game.player[0].altitude;
    static struct xz_rider crest;static struct xz_wave_rider_snapshot crest_wave;
    crest=game;crest_wave=wave;
    xz_rider_jog(&game,0,-1200,1020);
    assert(game.player[0].flick_strength==1&&game.player[0].grounded&&game.player[0].altitude==altitude&&!game.player[0].vy);
    game.player[1].ground_lean=game.player[0].ground_lean;game.player[1].pitch=game.player[0].pitch;game.player[1].pitch_target=game.player[0].pitch_target;game.player[1].jog_ms=1020;
    feed_at(at+1,1027);
    assert(!game.player[0].grounded&&!game.player[1].grounded);
    assert(game.player[0].flick_launches==1&&!game.player[1].flick_launches);
    assert(game.player[0].vy>game.player[1].vy*1.15f);
    xz_rider_jog(&game,0,4000,1027);assert(!game.player[0].flick_strength);
    float boosted=game.player[0].vy,ordinary=game.player[1].vy;
    game=crest;wave=crest_wave;game.spin_lift=0;
    xz_rider_jog(&game,0,-1200,1020);assert(!game.player[0].flick_strength);
    feed_at(at+1,1027);assert(!game.player[0].flick_launches);
    game=crest;wave=crest_wave;
    xz_rider_button(&game,0,XZ_RIDER_CUE,1,1020);xz_rider_jog(&game,0,-1200,1020);
    assert(game.player[0].flick_strength==1);
    for(unsigned i=1;i<=40;i++)feed_at(at+i,1020+i*7);
    assert(!game.player[0].flick_strength&&!game.player[0].flick_launches&&game.player[0].grounded);
    game.phase=XZ_RIDER_SETUP;game.setup_step=3;
    xz_rider_touch(&game,400,306,1,game.last_ms);xz_rider_touch(&game,400,306,0,game.last_ms);assert(!game.spin_lift);
    xz_rider_touch(&game,400,306,1,game.last_ms);xz_rider_touch(&game,400,306,0,game.last_ms);assert(game.spin_lift);
    memset(track,0,sizeof(track));start(XZ_RIDER_SOLO);
    for(unsigned ms=20;ms<=3000;ms+=20){
        feed_at(300+ms*.15,1000+ms);xz_rider_jog(&game,0,4000,1000+ms);
        xz_rider_button(&game,0,XZ_RIDER_CUE,ms%500<250,1000+ms);
        assert(game.player[0].altitude>=xz_rider_surface_at(&game,game.player[0].at).height+6.7f);
    }
    assert(game.player[0].flick_launches>0);
    song();start(XZ_RIDER_SOLO);xz_rider_button(&game,0,XZ_RIDER_CUE,1,1000);
    for(unsigned ms=20;ms<=6000;ms+=20){
        feed_at(300+ms*.15,1000+ms);
        if(ms%100==0)xz_rider_jog(&game,0,80,1000+ms);
        assert(game.player[0].flick_strength<1&&!game.player[0].flick_launches);
    }
    printf("SPIN_LIFT_JSON {\"ordinary_launch_vy\":%.2f,\"flick_launch_vy\":%.2f,\"fast_spin_ground_hop\":true,\"slow_steering_no_boost\":true,\"air_spin_no_repeat\":true}\n",(double)ordinary,(double)boosted);
}
static void slow_terrain_history(void){
    for(unsigned i=0;i<N;i++){track[i*3]=(uint8_t)(i%150<20?200-i%150*8:4);track[i*3+1]=(uint8_t)(90+60*sinf(i*.01f));track[i*3+2]=20;}
    start(XZ_RIDER_SOLO);game.phase=XZ_RIDER_SETUP;wave.pulse_period=150;
    for(unsigned at=1000;at<15000;at+=17){
        snapshot(&wave,at-20,at);xz_rider_feed(&game,&wave,at);
        struct xz_rider_surface a=xz_rider_surface_at(&game,at-70);
        snapshot(&wave,at,at+1);xz_rider_feed(&game,&wave,at+1);
        struct xz_rider_surface b=xz_rider_surface_at(&game,at-70);
        assert(fabsf(a.height-b.height)<.001f&&fabsf(a.slope-b.slope)<.001f);
    }
    puts("PASS slow-tempo hills remain identical under a trailing rider as the source window advances");
}
#ifndef XZ_RIDER_ENGINE_ONLY
static uint16_t pixels[W*H];
static void image(const char *directory,const char *name){
    if(!directory)return;char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
    for(unsigned i=0;i<W*H;i++){unsigned v=pixels[i];unsigned char c[3]={(unsigned char)((v>>11)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};fwrite(c,1,3,f);}
    fclose(f);
}
static double milliseconds(void){
#ifdef _WIN32
    LARGE_INTEGER count,freq;QueryPerformanceCounter(&count);QueryPerformanceFrequency(&freq);return 1000.*count.QuadPart/freq.QuadPart;
#else
    return (double)clock()*1000/CLOCKS_PER_SEC;
#endif
}
static int cmp(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return x<y?-1:x>y?1:0;}
static void rendering(const char *directory){
    song();start(XZ_RIDER_COOP);
    assert(!xz_rider_render(&game,NULL,W*H,W));
    assert(!xz_rider_render(&game,pixels,W*H-1,W));
    assert(!xz_rider_render(&game,pixels,W*H,W-1));
    uint16_t *guard=malloc((W+13)*H*2+4);assert(guard);
    for(unsigned i=0;i<(W+13)*H+2;i++)guard[i]=0x517a;
    assert(xz_rider_render(&game,guard+1,(W+13)*H,W+13));
    assert(guard[0]==0x517a&&guard[(W+13)*H+1]==0x517a);
    for(unsigned y=0;y<H;y++)for(unsigned x=W;x<W+13;x++)assert(guard[1+y*(W+13)+x]==0x517a);
    free(guard);
    for(unsigned mode=0;mode<3;mode++){
        game.mode=(enum xz_rider_mode)mode;
        for(unsigned step=0;step<4;step++){
            game.phase=XZ_RIDER_SETUP;game.setup_step=step;xz_rider_render(&game,pixels,W*H,W);
            if(mode==1){char name[80];snprintf(name,sizeof(name),"wave-rider-setup-%u",step);image(directory,name);}
            if(step==3)for(unsigned d=0;d<3;d++){
                char name[80];game.difficulty=(enum xz_rider_difficulty)d;xz_rider_render(&game,pixels,W*H,W);
                if(mode==0||mode==1){snprintf(name,sizeof(name),"wave-rider-difficulty-%u-%u",mode,d);image(directory,name);}
            }
            game.difficulty=XZ_RIDER_NORMAL;
        }
    }
    start(XZ_RIDER_COOP);
    for(unsigned ms=20;ms<=14000;ms+=20){snapshot(&wave,300+ms*.15,1000+ms);xz_rider_feed(&game,&wave,1000+ms);pilot(0,1000+ms);pilot(1,1000+ms);xz_rider_step(&game,1000+ms);}
    game.player[0].power=XZ_RIDER_POWER_AURORA;game.player[0].power_until=game.sim_at+900;
    xz_rider_render(&game,pixels,W*H,W);image(directory,"wave-rider-coop");
    game.mode=XZ_RIDER_DUEL;xz_rider_render(&game,pixels,W*H,W);image(directory,"wave-rider-duel");
    game.wave.kind=0;xz_rider_render(&game,pixels,W*H,W);image(directory,"wave-rider-three-band");game.wave.kind=1;
    game.phase=XZ_RIDER_HOLD;xz_rider_render(&game,pixels,W*H,W);image(directory,"wave-rider-paused");
    game.phase=XZ_RIDER_RESULT;xz_rider_render(&game,pixels,W*H,W);image(directory,"wave-rider-result");
    game.phase=XZ_RIDER_SETUP;game.setup_step=2;game.valid=game.wave.valid=0;game.wave.error=XZ_WAVE_RIDER_NO_ANALYSIS;
    xz_rider_render(&game,pixels,W*H,W);image(directory,"wave-rider-no-data");
    start(XZ_RIDER_COOP);double draw[180],full[180];
    for(unsigned i=0;i<180;i++){
        double begin=milliseconds();feed_at(300+i*3,1000+i*20);
        for(unsigned p=0;p<2;p++){game.player[p].streak=9;game.player[p].fever_until=game.sim_at+600;game.player[p].power=p?XZ_RIDER_POWER_COMET:XZ_RIDER_POWER_AURORA;}
        double before=milliseconds();assert(xz_rider_render(&game,pixels,W*H,W));double end=milliseconds();draw[i]=end-before;full[i]=end-begin;
    }
    double mean=0,all=0;for(unsigned i=0;i<180;i++){mean+=draw[i];all+=full[i];}
    qsort(draw,180,sizeof(double),cmp);qsort(full,180,sizeof(double),cmp);
    printf("BENCH_JSON {\"frames\":180,\"render_mean_ms\":%.3f,\"render_p95_ms\":%.3f,\"render_worst_ms\":%.3f,\"full_mean_ms\":%.3f,\"full_p95_ms\":%.3f,\"full_worst_ms\":%.3f,\"two_players_powers\":true}\n",mean/180,draw[170],draw[179],all/180,full[170],full[179]);
    puts("PASS native two-player/power rendering bounds, all modes/setup, and moving-frame benchmark");
}
#endif
int main(int argc,char **argv){
    terrain();surfing();multiplayer();lifecycle();powers_and_rotation();cadence_test();cue_and_jog_control();spin_lift();slow_terrain_history();
#ifndef XZ_RIDER_ENGINE_ONLY
    rendering(argc>1?argv[1]:NULL);
#else
    (void)argc;(void)argv;
#endif
    return 0;
}
