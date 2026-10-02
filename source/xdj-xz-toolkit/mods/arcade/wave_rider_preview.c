/* SPDX-License-Identifier: MIT */
#include "wave_rider.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define EXPORT __declspec(dllexport)
static struct xz_rider game;
static struct xz_wave_rider_snapshot wave;
static uint8_t *analysis;
static uint32_t count,last;
static uint16_t pixels[800*480];
static double position;
static int playing=1;
EXPORT int rider_preview_load(const uint8_t *data,uint32_t n,unsigned normalization,unsigned kind,const char *title,uint32_t now){
    if(!data||!n||n>1000000||kind>1)return 0;
    uint8_t *next=malloc((size_t)n*3);if(!next)return 0;
    memcpy(next,data,(size_t)n*3);free(analysis);analysis=next;count=n;
    memset(&wave,0,sizeof(wave));wave.normalization=normalization;wave.kind=kind;
    wave.generation=1;wave.track_hash=0x73bc120a;wave.playing=playing=1;
    snprintf(wave.title,sizeof(wave.title),"%s",title?title:"ANALYSIS PREVIEW");
    position=n>4000?3000:0;last=now;xz_rider_init(&game,0,now);
    xz_wave_rider_window(&wave,analysis,count,position);wave.observed_ms=now;xz_rider_feed(&game,&wave,now);return 1;
}
EXPORT void rider_preview_step(uint32_t now){
    uint32_t dt=now-last;last=now;if(dt>500)dt=0;
    if(game.leave)xz_rider_init(&game,0,now);
    if(playing)position+=dt*.15;
    if(position>=count){position=count;playing=0;}
    xz_wave_rider_window(&wave,analysis,count,position);wave.observed_ms=now;wave.playing=playing;
    xz_rider_feed(&game,&wave,now);xz_rider_step(&game,now);
}
EXPORT void rider_preview_key(int key,uint32_t now){if(key>=0&&key<=XZ_RIDER_PAD_H)xz_rider_key(&game,(enum xz_rider_key)key,now);}
EXPORT void rider_preview_move(float delta,uint32_t now){xz_rider_jog(&game,delta,now);}
EXPORT void rider_preview_touch(int x,int y,int down,uint32_t now){xz_rider_touch(&game,x,y,down,now);}
EXPORT void rider_preview_pause(void){playing=!playing;}
EXPORT void rider_preview_seek(double samples){
    position=samples<0?0:samples>count?count:samples;
    if(game.phase==XZ_RIDER_RESULT&&position<count){xz_rider_init(&game,0,last);game.phase=XZ_RIDER_TITLE;playing=1;}
}
EXPORT const uint16_t *rider_preview_frame(void){xz_rider_render(&game,pixels,800*480,800);return pixels;}
EXPORT const char *rider_preview_status(void){
    static char output[512];snprintf(output,sizeof(output),"{\"phase\":%d,\"setup_step\":%u,\"position\":%.2f,\"score\":%u,\"combo\":%u,\"spikes\":%u,\"gems\":%u,\"altitude\":%.2f,\"playing\":%d,\"valid\":%u,\"jog_seen\":%u,\"cue_seen\":%u}",
        game.phase,game.setup_step,game.position,game.score,game.combo,game.course.spikes,game.course.gems,(double)game.altitude,playing,game.valid,game.jog_seen,game.cue_seen);return output;
}
