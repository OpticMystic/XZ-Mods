/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wave_rider_runtime.h"
#include "wave_rider.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include "../clock_compat.h"
#include <unistd.h>

static struct xz_rider rider;
static int present,opening_touch,touch_owned,source_deck=-1,source_ready;
static uint32_t owned[2];
static uint8_t owner_player[2][10];
__attribute__((visibility("default"))) struct xz_wave_rider_runtime_proof xz_wave_rider_proof_v2={.version=2};
static uint64_t micros(void){
    struct timespec t;if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000000+(uint64_t)t.tv_nsec/1000;
}
static void select_deck(void){
    if(source_deck==(int)rider.deck)return;
    source_deck=(int)rider.deck;rider.valid=0;rider.wave.valid=0;rider.wave.loading=1;
    rider.wave.error=XZ_WAVE_RIDER_SOURCE_OK;rider.wave.title[0]=0;
    if(source_ready)xz_wave_rider_source_select(source_deck);
}
void xz_wave_rider_open(uint32_t now){
    unsigned deck=rider.initialized?rider.deck:0,quiet=rider.quiet,spin_lift=rider.initialized?rider.spin_lift:1;
    enum xz_rider_mode mode=rider.mode;
    enum xz_rider_difficulty difficulty=rider.initialized?rider.difficulty:XZ_RIDER_EASY;
    xz_rider_init(&rider,deck,now);rider.mode=mode;rider.quiet=quiet;rider.spin_lift=spin_lift;rider.difficulty=difficulty;
    rider.leave=0;rider.touch_down=1;present=opening_touch=1;
    source_ready=xz_wave_rider_source_start()==0;source_deck=-1;select_deck();
    if(!source_ready){rider.wave.loading=0;rider.wave.error=XZ_WAVE_RIDER_POSITION_UNAVAILABLE;}
}
void xz_wave_rider_close(void){
    present=0;source_deck=-1;xz_wave_rider_source_select(-1);
    for(unsigned p=0;p<2;p++){rider.player[p].held=0;rider.player[p].tuck=0;}
}
int xz_wave_rider_present(void){return present;}
int xz_wave_rider_leave_requested(void){return present&&rider.leave;}
int xz_wave_rider_input(int deck,const void *event,uint32_t now){
    if(!event||deck<0||deck>1)return 0;
    const unsigned char *p=event;uint16_t key;memcpy(&key,p+8,2);unsigned op=p[11]&15;
    int player=xz_rider_player_for_deck(&rider,deck),active=present&&player>=0;
    if(key==0x4305&&op==4&&active){
        int32_t delta;float speed;memcpy(&delta,p+12,4);memcpy(&speed,p+16,4);
        float pulses=delta?(float)delta:isfinite(speed)?speed:0;
        xz_rider_jog(&rider,(unsigned)player,pulses,now);xz_wave_rider_proof_v2.inputs++;return 1;
    }
    /* PLAY, load, skip, sync and the other deck retain native transport. */
    int index=key==0x4306?XZ_RIDER_PLATTER:key==0x4102?XZ_RIDER_CUE:key>=0x4119&&key<=0x4120?(int)key-0x4119+XZ_RIDER_PAD_A:-1;
    if(index<0)return 0;
    uint32_t bit=1u<<(unsigned)index;
    if(op==2||op==3){
        int captured=!!(owned[deck]&bit);owned[deck]&=~bit;
        if(captured)xz_rider_button(&rider,owner_player[deck][index],(enum xz_rider_key)index,0,now);
        return captured;
    }
    if(active&&op==0){
        if(!(owned[deck]&bit)){
            owned[deck]|=bit;owner_player[deck][index]=(uint8_t)player;
            xz_rider_button(&rider,(unsigned)player,(enum xz_rider_key)index,1,now);
            xz_wave_rider_proof_v2.inputs++;
        }
        return 1;
    }
    return !!(owned[deck]&bit)||active;
}
int xz_wave_rider_touch(int x,int y,int down,uint32_t now){
    if(!present&&!touch_owned)return 0;
    if(present){
        touch_owned=!!down;
        if(opening_touch){if(!down){opening_touch=0;rider.touch_down=0;}}
        else{xz_rider_touch(&rider,x,y,down,now);select_deck();}
    }else if(!down)touch_owned=0;
    return 1;
}
void xz_wave_rider_back(uint32_t now){if(present)xz_rider_back(&rider,now);}
int xz_wave_rider_draw(uint16_t *pixels,size_t count,size_t stride,uint32_t now,struct xz_game_targets *targets){
    if(!present)return 0;
    uint64_t began=micros();
    struct xz_wave_rider_snapshot wave;
    int copied=source_ready&&xz_wave_rider_source_read(&wave);
    /* Publication can happen after the caller sampled its frame timestamp. */
    now=(uint32_t)(micros()/1000);
    if(copied)xz_rider_feed(&rider,&wave,now);
    xz_rider_step(&rider,now);
    int result=xz_rider_render_targets(&rider,pixels,count,stride,targets);
    uint32_t took=(uint32_t)(micros()-began);
    struct xz_wave_rider_runtime_proof *p=&xz_wave_rider_proof_v2;
    p->frames++;p->phase=rider.phase;p->mode=rider.mode;p->deck=rider.deck;p->kind=rider.wave.kind;p->valid=rider.valid;
    p->difficulty=rider.difficulty;
    p->generation=rider.wave.generation;p->position=(float)rider.position;p->zoom=rider.zoom;p->render_us=took;
    for(unsigned i=0;i<2;i++){
        p->score[i]=rider.player[i].score;p->streak[i]=rider.player[i].streak;
        p->grounded[i]=rider.player[i].grounded;p->charge[i]=rider.player[i].charge;
        p->altitude[i]=rider.player[i].altitude;p->pitch[i]=rider.player[i].pitch;
        p->vx[i]=rider.player[i].vx;p->vy[i]=rider.player[i].vy;p->tuck[i]=rider.player[i].tuck;
    }
    if(took>p->worst_render_us)p->worst_render_us=took;
    return result;
}
int xz_wave_rider_pad(int deck,int pad,unsigned *rgb,int *enabled){
    int player=xz_rider_player_for_deck(&rider,deck);
    if(!present||player<0||pad<0||pad>7||!rgb||!enabled)return 0;
    unsigned charge=rider.player[player].charge;
    *rgb=pad<4?(player?0xff8b80:0x53e5d9):pad==4?0x8b80ff:pad==5?0xffc23d:pad==7?0xffffff:0x8b80ac;
    *enabled=pad==4?charge>=3:pad==5?charge>=5:1;return 1;
}
