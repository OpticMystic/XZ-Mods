/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#ifdef NDEBUG
#error Assertions are required for controller-bound input checks
#endif
#include "rhythm_lab.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define PI 3.14159265358979323846f
enum { N=40000 };
static uint8_t track[N*3];
static unsigned sample_count=N,bench_frames=45;
static struct xz_rhythm_lab g;
static struct xz_wave_rider_snapshot w;
static void music(void){
    for(unsigned i=0;i<sample_count;i++){
        track[i*3]=(uint8_t)(i%75<12?240-i%75*18:6);
        track[i*3+1]=(uint8_t)(100+80*sinf(i*.007f));
        track[i*3+2]=(uint8_t)(80+65*sinf(i*.019f));
    }
}
static void feed(double at,uint32_t now){
    assert(!xz_wave_rider_window(&w,track,N,at));w.observed_ms=now;xz_lab_feed(&g,&w,now);xz_lab_step(&g,now);
}
static void start(unsigned mode,unsigned duo){
    xz_lab_init(&g,1000);g.mode=(enum xz_lab_mode)mode;g.duo=duo;
    memset(&w,0,sizeof(w));w.normalization=500;w.generation=1;w.track_hash=7;w.playing=1;w.kind=1;
    w.pulse_period=75;w.pulse_origin=0;w.pulse_confidence=1;strcpy(w.title,"CONTROLLER LOOP FIXTURE");
    feed(3000,1000);xz_lab_begin(&g,1000);
}
static void turn_to(unsigned p,float cursor){float pulses=(cursor-g.player[p].cursor)*PI/(2*PI/g.turn_pulses[p]);xz_lab_jog(&g,(int)p,pulses,g.now);}
static void aim_to(unsigned p,float angle){float d=atan2f(sinf(angle-g.player[p].angle),cosf(angle-g.player[p].angle));xz_lab_jog(&g,(int)p,d/(2*PI/g.turn_pulses[p]),g.now);}
static void cue(unsigned p,int down){xz_lab_button(&g,p,LAB_CUE,down,g.now);}
static void tap(unsigned p){cue(p,1);cue(p,0);}
static void pilot(unsigned index){
    struct xz_lab_player *p=&g.player[index];
    if(g.position<g.practice_until)return;
    if(g.mode==LAB_RIBBON){cue(index,1);turn_to(index,xz_lab_ribbon(&g,g.position));return;}
    if(g.mode==LAB_STEPS){
        double phase=(g.position-g.origin)/g.period;
        if(fabs(phase-round(phase))<.021){int dx=p->target_x-p->gx,dy=p->target_y-p->gy;aim_to(index,dx>0?0:dx<0?PI:dy>0?PI/2:-PI/2);tap(index);}return;
    }
    if(g.mode==LAB_PUZZLE){
        double phase=(g.position-g.origin)/g.period;
        if(fabs(phase-round(phase))<.021){turn_to(index,(float)(p->attacks%3)*.4f);tap(index);}return;
    }
    for(unsigned i=0;i<g.event_count;i++){
        struct xz_lab_event *e=&g.events[i];
        if(e->grade[index]==LAB_HOLDING){if(g.position>=e->end-1.6)cue(index,0);continue;}
        if(e->grade[index]!=LAB_PENDING)continue;
        if(g.mode==LAB_WAVE_LANES||g.mode==LAB_WHEEL_MATCH){if(e->at>=g.position-5&&e->at<g.position+g.period*.5)turn_to(index,(e->lane+.5f)/3);}
        if(g.mode==LAB_SPRING&&e->at>g.position&&e->at<g.position+g.period&&!p->held){cue(index,1);xz_lab_jog(&g,(int)index,-g.turn_pulses[index]*.2f,g.now);}
        if(fabs(g.position-e->at)>1.6)continue;
        if(g.mode==LAB_SCRATCH)xz_lab_jog(&g,(int)index,e->direction*.25f/(2*PI/g.turn_pulses[index]),g.now);
        else if(g.mode==LAB_SPRING)cue(index,0);
        else if(g.mode==LAB_BLASTER){aim_to(index,e->angle);tap(index);}
        else if(g.mode!=LAB_WAVE_LANES){cue(index,1);if(e->kind!=LAB_HOLD)cue(index,0);}
    }
}
static void completion_regression(void){
 xz_lab_init(&g,1000);w.valid=w.playing=1;w.count=2400;w.total=50000;w.position=500;w.pulse_confidence=1;w.pulse_period=75;w.observed_ms=1000;
 xz_lab_feed(&g,&w,1000);xz_lab_begin(&g,1000);g.practice_until=0;g.elapsed=90;xz_lab_step(&g,1001);assert(g.phase==LAB_RESULTS&&g.record[0].played==1);
 for(unsigned mode=1;mode<10;mode++){
  g.mode=(enum xz_lab_mode)mode;g.phase=LAB_BRIEF;xz_lab_begin(&g,1001);xz_lab_step(&g,1020);assert(g.phase==LAB_RUN&&g.elapsed==0&&!g.record[mode].played);
 }
 w.position=w.total-1;w.observed_ms=1040;xz_lab_feed(&g,&w,1040);
 for(unsigned mode=0;mode<10;mode++){
  unsigned played=g.record[mode].played;g.mode=(enum xz_lab_mode)mode;g.phase=LAB_BRIEF;xz_lab_begin(&g,1040);xz_lab_step(&g,1060);
  if(g.phase==LAB_RESULTS||g.record[mode].played!=played){fprintf(stderr,"FAIL study%u auto-completed at song end after %.2fs\n",mode+1,g.elapsed);assert(0);}
  assert(g.phase==LAB_PAUSE);
 }
 w.position=100;w.observed_ms=1080;xz_lab_feed(&g,&w,1080);xz_lab_step(&g,1080);assert(g.phase==LAB_RUN&&g.elapsed==0);
 puts("PASS sequential study starts reset duration; song end pauses without completing unplayed studies; restart resumes");
}
static void all_loops(void){
    for(unsigned mode=0;mode<10;mode++){
        start(mode,1);unsigned frames=0;
        for(unsigned ms=20;ms<=94000;ms+=20){
            feed(3000+ms*.15,1000+ms);for(unsigned p=0;p<xz_lab_players(&g);p++)pilot(p);frames++;
            assert(g.event_count<=XZ_LAB_EVENTS);
            for(unsigned p=0;p<2;p++)assert(isfinite(g.player[p].cursor)&&isfinite(g.player[p].angle)&&g.player[p].cursor>=0&&g.player[p].cursor<=1);
            if(g.phase==LAB_RESULTS)break;
        }
        printf("CASE %02u %s frames=%u elapsed=%.2f hits=%u misses=%u score=%u coverage=%.3f\n",mode+1,xz_lab_description(mode)->name,frames,(double)g.elapsed,g.player[0].hits,g.player[0].misses,g.player[0].score,g.player[0].coverage_total?g.player[0].coverage/g.player[0].coverage_total:0);fflush(stdout);
        assert(g.phase==LAB_RESULTS&&g.elapsed>=90&&g.elapsed<90.1f&&g.record[mode].played==1);
        if(mode==LAB_RIBBON)assert(g.player[0].coverage>g.player[0].coverage_total*.85f);
        else if(mode!=LAB_PUZZLE)assert(g.player[0].hits>10&&g.player[0].score>100);
        if(xz_lab_players(&g)==2)assert(g.player[0].hits==g.player[1].hits&&g.player[0].score==g.player[1].score);
    }
    puts("PASS all ten 90-second studies continue through misses and preserve independent two-player outcomes");
}
static void ownership_pause_and_setup(void){
    start(LAB_PULSE_HOLD,1);g.practice_until=g.position;
    xz_lab_button(&g,0,LAB_PLATTER,1,g.now);assert(!g.player[0].held);
    cue(0,1);xz_lab_button(&g,0,LAB_PAD_A,1,g.now);cue(0,0);assert(g.player[0].held);
    xz_lab_button(&g,0,LAB_PAD_A,0,g.now);assert(!g.player[0].held);
    w.playing=0;feed(3000,1100);assert(g.phase==LAB_PAUSE);
    unsigned attacks=g.player[0].attacks;float elapsed=g.elapsed;
    tap(0);tap(0);feed(3000,1300);assert(g.player[0].attacks==attacks&&g.elapsed==elapsed);
    w.playing=1;feed(3015,1400);assert(g.phase==LAB_RUN);
    feed(2000,1420);assert(g.position==2000&&g.practice_until>g.position);
    w.track_hash=9;w.generation++;feed(100,1440);assert(g.phase==LAB_BRIEF&&!g.manual_pulse);
    xz_lab_step(&g,1800);assert(!g.valid);
    g.phase=LAB_SETUP;g.setup_step=2;w.playing=1;
    for(unsigned i=0;i<5;i++){feed(1500+i*90,2000+i*600);tap(0);}
    assert(g.manual_pulse&&fabsf(g.period-90)<.1f);
    g.setup_step=3;g.calibrating=1;g.cal_deck=0;g.cal_pulses[0]=0;
    for(unsigned i=0;i<80;i++)xz_lab_jog(&g,0,100,g.now);
    tap(0);assert(!g.calibrating&&g.turn_pulses[0]==8000);
    g.phase=LAB_MENU;xz_lab_touch(&g,450,220,1,g.now);xz_lab_touch(&g,450,220,0,g.now);assert(g.mode==LAB_BLASTER&&g.phase==LAB_BRIEF);
    puts("PASS platter/CUE separation, chord ownership, pause, seek, track reset, tap alignment and full-turn calibration");
}
static void rule_contrasts(void){
    start(LAB_PUZZLE,0);g.practice_until=g.position;
    unsigned cells=0;tap(0);for(unsigned i=0;i<24;i++)cells+=!!g.player[0].grid[i];assert(cells==2&&!g.player[0].score);
    memset(g.player[0].grid,0,24);g.player[0].grid[12]=g.player[0].grid[13]=g.player[0].grid[18]=g.player[0].grid[19]=1;
    assert(xz_lab_matches(&g.player[0]));
    for(unsigned ms=20;ms<=2500;ms+=20)feed(3000+ms*.15,1000+ms);
    assert(g.player[0].cleared==4&&g.player[0].score==100);
    start(LAB_BLASTER,0);g.practice_until=g.position;feed(3025,1100);
    unsigned shots=g.player[0].shots;cue(0,1);assert(g.player[0].shots==shots+1);cue(0,1);assert(g.player[0].shots==shots+1);cue(0,0);
    start(LAB_SPRING,0);g.practice_until=g.position;cue(0,1);
    for(unsigned i=0;i<100;i++)xz_lab_jog(&g,0,-8000,g.now);assert(g.player[0].charge==1);
    cue(0,0);assert(g.player[0].launch_charge==1&&!g.player[0].charge);
    start(LAB_WAVE_LANES,0);float c=g.player[0].cursor;
    xz_lab_jog(&g,0,80,g.now);assert(g.player[0].cursor>c&&g.player[0].cursor-c<.025f);
    puts("PASS distinct mechanics: placement before sweep, immediate free shots, capped stored travel, gentle absolute positioning");
}
static void publisher_cadence(void){
    for(unsigned step=8;step<=16;step+=8){
        start(LAB_RIBBON,0);unsigned publication=0;
        for(unsigned ms=step;ms<=4000;ms+=step){
            uint32_t now=1000+ms;
            unsigned latest=ms/40*40;
            if(latest!=publication){publication=latest;assert(!xz_wave_rider_window(&w,track,N,3000+publication*.15));w.observed_ms=1000+publication;}
            xz_lab_feed(&g,&w,now);xz_lab_step(&g,now);
            assert(fabsf(g.rate-.15f)<.001f);
            assert(fabs(g.position-(3000+ms*.15))<.01);
        }
    }
    memset(track,0,sizeof(track));start(LAB_PULSE_HOLD,0);g.practice_until=g.position;feed(3001,1007);assert(!g.event_count);
    g.study=1;g.next_event=40;feed(3004,1027);assert(g.event_count);
    music();puts("PASS repeated source publications preserve timing at8/16ms cadence; silent TRACK rests differ from STUDY cues");
}
static void rating_handoff(void){
    start(LAB_RIBBON,0);g.practice_until=g.position;feed(3003,1020);cue(0,1);
    xz_lab_button(&g,0,LAB_PAD_F,1,g.now);assert(g.phase==LAB_RESULTS&&!g.player[0].held&&g.record[LAB_RIBBON].seconds>0);
    xz_lab_touch(&g,630,180,1,g.now);xz_lab_touch(&g,630,180,0,g.now);assert(g.record[LAB_RIBBON].fun==5);
    xz_lab_touch(&g,280,235,1,g.now);xz_lab_touch(&g,280,235,0,g.now);assert(g.record[LAB_RIBBON].clarity==1);
    xz_lab_touch(&g,400,365,1,g.now);xz_lab_touch(&g,400,365,0,g.now);assert(g.phase==LAB_RUN&&g.duration>90);
    puts("PASS early end/rate, editable in-memory ratings and extend without restarting the song");
}
#ifndef XZ_LAB_ENGINE_ONLY
static uint16_t pixels[800*480];
static double milliseconds(void){
#ifdef _WIN32
    LARGE_INTEGER a,b;QueryPerformanceCounter(&a);QueryPerformanceFrequency(&b);return 1000.*a.QuadPart/b.QuadPart;
#else
    struct timespec t;assert(!syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t));return t.tv_sec*1000.+t.tv_nsec*.000001;
#endif
}
static int compare_time(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return x<y?-1:x>y?1:0;}
static void picture(const char *folder,const char *name){
    if(!folder)return;char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",folder,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n800 480\n255\n");
    for(unsigned i=0;i<800*480;i++){unsigned v=pixels[i];unsigned char rgb[3]={(unsigned char)((v>>11)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};fwrite(rgb,1,3,f);}fclose(f);
}
static void render_checks(const char *folder){
    start(0,0);g.phase=LAB_MENU;assert(!xz_lab_render(&g,NULL,800*480,800));assert(!xz_lab_render(&g,pixels,800*480-1,800));assert(xz_lab_render(&g,pixels,800*480,800));picture(folder,"lab-menu");
    double sum=0,worst=0;unsigned frames=0;
    for(unsigned mode=0;mode<10;mode++){
        start(mode,1);g.practice_until=g.position;char name[64];double costs[45],mode_sum=0;
        for(unsigned i=0;i<bench_frames;i++){feed(3000+i*3,1000+i*20);for(unsigned p=0;p<xz_lab_players(&g);p++)pilot(p);double before=milliseconds();assert(xz_lab_render(&g,pixels,800*480,800));double cost=milliseconds()-before;costs[i]=cost;mode_sum+=cost;sum+=cost;if(cost>worst)worst=cost;frames++;}
        qsort(costs,bench_frames,sizeof(double),compare_time);printf("CASE_BENCH %02u mean_ms=%.3f p95_ms=%.3f worst_ms=%.3f\n",mode+1,mode_sum/bench_frames,costs[bench_frames*95/100],costs[bench_frames-1]);fflush(stdout);
        snprintf(name,sizeof(name),"lab-case-%02u",mode+1);picture(folder,name);
    }
    for(unsigned step=0;step<4;step++){g.phase=LAB_SETUP;g.setup_step=step;assert(xz_lab_render(&g,pixels,800*480,800));char name[64];snprintf(name,sizeof(name),"lab-setup-%u",step);picture(folder,name);}
    g.phase=LAB_RESULTS;xz_lab_render(&g,pixels,800*480,800);picture(folder,"lab-results");
    g.phase=LAB_CONTROLS;g.action_binding[0]=1;g.action_binding[1]=4;xz_lab_render(&g,pixels,800*480,800);picture(folder,"lab-buttons");
    g.phase=LAB_PAUSE;g.wave.position=g.wave.total-1;xz_lab_render(&g,pixels,800*480,800);picture(folder,"lab-track-ended");
    printf("BENCH_JSON {\"frames\":%u,\"render_mean_ms\":%.3f,\"render_worst_ms\":%.3f,\"hardware_measurement\":false}\n",frames,sum/frames,worst);
}
#endif
int main(int argc,char **argv){
#ifndef XZ_LAB_ENGINE_ONLY
    if(argc>1&&!strcmp(argv[1],"--render-only")){sample_count=6000;bench_frames=10;music();render_checks(NULL);return 0;}
#endif
    music();completion_regression();all_loops();ownership_pause_and_setup();rule_contrasts();publisher_cadence();rating_handoff();
#ifndef XZ_LAB_ENGINE_ONLY
    render_checks(argc>1?argv[1]:NULL);
#else
    (void)argc;(void)argv;
#endif
    return 0;
}
