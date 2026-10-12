/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "rhythm_lab_runtime.h"
#include "rhythm_lab.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>
static struct xz_rhythm_lab lab;
static int present,opening_touch,touch_owned,source_deck=-1,source_ready;
static uint32_t owned[2];static uint8_t owner_player[2][10],owner_action[2][10];
__attribute__((visibility("default"))) struct xz_rhythm_lab_proof xz_rhythm_lab_proof_v1={.version=1};
static uint64_t micros(void){struct timespec t;if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000000+(uint64_t)t.tv_nsec/1000;}
static void select_deck(void){
    if(source_deck==(int)lab.deck)return;
    source_deck=(int)lab.deck;lab.valid=0;lab.wave.valid=0;lab.wave.loading=1;lab.wave.error=XZ_WAVE_RIDER_SOURCE_OK;
    if(source_ready)xz_wave_rider_source_select(source_deck);
}
void xz_rhythm_lab_open(uint32_t now){
    if(!lab.initialized)xz_lab_init(&lab,now);
    lab.phase=LAB_MENU;lab.leave=0;lab.valid=0;lab.playing=0;lab.last_ms=lab.now=now;lab.touch_down=1;
    for(unsigned i=0;i<2;i++)lab.player[i].held=0;
    present=opening_touch=1;source_ready=xz_wave_rider_source_start()==0;source_deck=-1;select_deck();
    if(!source_ready){lab.wave.loading=0;lab.wave.error=XZ_WAVE_RIDER_POSITION_UNAVAILABLE;}
}
void xz_rhythm_lab_open_glyph(uint32_t now){xz_rhythm_lab_open(now);lab.mode=LAB_GLYPH;lab.study=0;lab.phase=LAB_BRIEF;}
void xz_rhythm_lab_close(void){present=0;source_deck=-1;xz_wave_rider_source_select(-1);for(unsigned i=0;i<2;i++)lab.player[i].held=0;}
int xz_rhythm_lab_present(void){return present;}
int xz_rhythm_lab_leave_requested(void){return present&&lab.leave;}
int xz_rhythm_lab_input(int deck,const void *event,uint32_t now){
    if(!event||deck<0||deck>1)return 0;
    const unsigned char *p=event;uint16_t key;memcpy(&key,p+8,2);unsigned op=p[11]&15;
    int player=xz_lab_player_for_deck(&lab,deck);
    int calibration=present&&lab.phase==LAB_SETUP&&lab.setup_step==3;
    int enabled=present&&(player>=0||calibration);
    if(key==0x4305&&op==4&&enabled){
        int32_t delta;float speed;memcpy(&delta,p+12,4);memcpy(&speed,p+16,4);
        xz_lab_jog(&lab,deck,delta?(float)delta:isfinite(speed)?speed:0,now);
        xz_rhythm_lab_proof_v1.inputs++;return 1;
    }
    int index=key==0x4306?LAB_PLATTER:key==0x4102?LAB_CUE:key>=0x4119&&key<=0x4120?(int)key-0x4119+LAB_PAD_A:-1;
    if(index<0)return 0;
    uint32_t bit=1u<<(unsigned)index;
    if(op==2||op==3){int captured=!!(owned[deck]&bit);owned[deck]&=~bit;if(captured)xz_lab_button(&lab,owner_player[deck][index],(enum xz_lab_key)owner_action[deck][index],0,now);return captured;}
    if(enabled&&op==0){
        if(!(owned[deck]&bit)){owned[deck]|=bit;owner_player[deck][index]=(uint8_t)(player<0?0:player);owner_action[deck][index]=(uint8_t)xz_lab_action_key(&lab,(unsigned)deck,index);xz_lab_button(&lab,owner_player[deck][index],(enum xz_lab_key)owner_action[deck][index],1,now);xz_rhythm_lab_proof_v1.inputs++;}
        return 1;
    }
    return !!(owned[deck]&bit)||enabled;
}
int xz_rhythm_lab_touch(int x,int y,int down,uint32_t now){
    if(!present&&!touch_owned)return 0;
    if(present){touch_owned=!!down;if(opening_touch){if(!down){opening_touch=0;lab.touch_down=0;}}else{xz_lab_touch(&lab,x,y,down,now);select_deck();}}
    else if(!down)touch_owned=0;return 1;
}
void xz_rhythm_lab_back(uint32_t now){if(present){xz_lab_back(&lab,now);select_deck();}}
int xz_rhythm_lab_draw(uint16_t *pixels,size_t count,size_t stride,uint32_t now,struct xz_game_targets *targets){
    if(!present)return 0;uint64_t begin=micros();struct xz_wave_rider_snapshot w;
    int copied=source_ready&&xz_wave_rider_source_read(&w);now=(uint32_t)(micros()/1000);
    if(copied)xz_lab_feed(&lab,&w,now);xz_lab_step(&lab,now);
    int result=xz_lab_render_targets(&lab,pixels,count,stride,targets);struct xz_rhythm_lab_proof *p=&xz_rhythm_lab_proof_v1;
    p->frames++;p->phase=lab.phase;p->mode=lab.mode;p->deck=lab.deck;p->valid=lab.valid;p->playing=lab.playing;p->clock_kind=lab.manual_pulse?2:lab.wave.pulse_confidence>=.25f?1:0;
    p->position=(float)lab.position;p->elapsed=lab.elapsed;p->period=lab.period;p->render_us=(uint32_t)(micros()-begin);if(p->render_us>p->worst_render_us)p->worst_render_us=p->render_us;
    for(unsigned i=0;i<2;i++){
        p->glyph_height[i]=lab.glyph[i].height;p->glyph_collisions[i]=lab.glyph[i].collisions;p->glyph_gates[i]=lab.glyph[i].gates;
        p->action_binding[i]=lab.action_binding[i];p->turn_pulses[i]=lab.turn_pulses[i];p->cursor[i]=lab.player[i].cursor;p->angle[i]=lab.player[i].angle;p->held[i]=lab.player[i].held;
        p->hits[i]=lab.player[i].hits;p->misses[i]=lab.player[i].misses;p->score[i]=lab.player[i].score;p->cue_edges[i]=lab.player[i].cue_edges;p->jog_events[i]=lab.player[i].jog_events;
    }
    for(unsigned i=0;i<10;i++){p->ratings[i][0]=lab.record[i].played;p->ratings[i][1]=lab.record[i].fun;p->ratings[i][2]=lab.record[i].clarity;p->ratings[i][3]=lab.record[i].effort;}
    return result;
}
int xz_rhythm_lab_pad(int deck,int pad,unsigned *rgb,int *enabled){
    int player=xz_lab_player_for_deck(&lab,deck);
    if(!present||player<0||pad<0||pad>7||!rgb||!enabled)return 0;
    int action=xz_lab_action_key(&lab,(unsigned)deck,pad+LAB_PAD_A)==LAB_PAD_A;
    *rgb=action?(player?0xff8b80:0x53e5d9):pad==1?0xb19cff:pad==6?0xffcf63:0xe5edf7;
    *enabled=action||(pad==1&&lab.mode==LAB_PUZZLE)||pad>=5;return 1;
}
