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
    xz_wave_rider_rhythm(&wave,analysis,count);
    snprintf(wave.title,sizeof(wave.title),"%s",title?title:"ANALYSIS PREVIEW");
    position=n>4000?3000:0;last=now;xz_rider_init(&game,0,now);
    xz_wave_rider_window(&wave,analysis,count,position);wave.observed_ms=now;xz_rider_feed(&game,&wave,now);return 1;
}
EXPORT void rider_preview_step(uint32_t now){
    uint32_t dt=now-last;last=now;if(dt>500)dt=0;
    if(game.leave){enum xz_rider_mode mode=game.mode;enum xz_rider_difficulty difficulty=game.difficulty;unsigned spin_lift=game.spin_lift;xz_rider_init(&game,game.deck,now);game.mode=mode;game.spin_lift=spin_lift;game.difficulty=difficulty;}
    if(playing&&(game.phase==XZ_RIDER_RIDE||game.phase==XZ_RIDER_HOLD||game.setup_step==3))position+=dt*.15;
    if(position>=count){position=count;playing=0;}
    xz_wave_rider_window(&wave,analysis,count,position);wave.observed_ms=now;wave.playing=playing;
    xz_rider_feed(&game,&wave,now);xz_rider_step(&game,now);
}
EXPORT void rider_preview_key(int player,int key,int down,uint32_t now){if(key>=0&&key<=XZ_RIDER_PLATTER)xz_rider_button(&game,(unsigned)player,(enum xz_rider_key)key,down,now);}
EXPORT void rider_preview_move(int player,float delta,uint32_t now){xz_rider_jog(&game,(unsigned)player,delta,now);}
EXPORT void rider_preview_touch(int x,int y,int down,uint32_t now){xz_rider_touch(&game,x,y,down,now);}
EXPORT void rider_preview_pause(void){playing=!playing;}
EXPORT void rider_preview_seek(double samples){
    position=samples<0?0:samples>count?count:samples;
    if(game.phase==XZ_RIDER_RESULT&&position<count){enum xz_rider_mode mode=game.mode;enum xz_rider_difficulty difficulty=game.difficulty;unsigned spin_lift=game.spin_lift;xz_rider_init(&game,game.deck,last);game.mode=mode;game.difficulty=difficulty;game.spin_lift=spin_lift;game.phase=XZ_RIDER_TITLE;playing=1;}
}
EXPORT const uint16_t *rider_preview_frame(void){xz_rider_render(&game,pixels,800*480,800);return pixels;}
EXPORT const char *rider_preview_status(void){
    static char output[1500];snprintf(output,sizeof(output),"{\"phase\":%d,\"mode\":%d,\"setup_step\":%u,\"position\":%.2f,\"playing\":%d,\"valid\":%u,\"zoom\":%.3f,\"pulse_period\":%.3f,\"pulse_confidence\":%.3f,\"team_score\":%u,\"harmonies\":%u,\"players\":[",
        game.phase,game.mode,game.setup_step,game.position,playing,game.valid,(double)game.zoom,(double)game.wave.pulse_period,(double)game.wave.pulse_confidence,game.team_score,game.harmonies);
    for(unsigned i=0;i<2;i++){
        struct xz_rider_player *p=&game.player[i];size_t n=strlen(output);
        snprintf(output+n,sizeof(output)-n,"%s{\"at\":%.2f,\"height\":%.2f,\"pitch\":%.3f,\"vx\":%.2f,\"vy\":%.2f,\"tuck\":%.2f,\"grounded\":%u,\"score\":%u,\"streak\":%u,\"perfects\":%u,\"landings\":%u,\"best_height\":%.2f,\"charge\":%u,\"power\":%d,\"jog_seen\":%u,\"hold_seen\":%u}",
            i?",":"",p->at,(double)p->altitude,(double)p->pitch,(double)p->vx,(double)p->vy,(double)p->tuck,p->grounded,p->score,p->streak,p->perfects,p->landings,(double)p->best_height,p->charge,p->power,p->jog_seen,p->hold_seen);
    }
    size_t n=strlen(output);snprintf(output+n,sizeof(output)-n,"],\"difficulty\":%d,\"spin_lift\":%u,\"flicks\":[%u,%u]}",game.difficulty,game.spin_lift,game.player[0].flick_launches,game.player[1].flick_launches);return output;
}
