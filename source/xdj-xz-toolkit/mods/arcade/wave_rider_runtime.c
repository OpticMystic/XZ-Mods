/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wave_rider_runtime.h"
#include "wave_rider.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

static struct xz_rider rider;
static int present,opening_touch,touch_owned,source_deck=-1,source_ready;
static uint32_t owned[2];
struct xz_wave_rider_proof {
    uint32_t version,frames,inputs,phase,deck,kind,valid,generation;
    uint32_t first,total,spikes,gems,score,judged,render_us,worst_render_us;
    float position,altitude;
};
__attribute__((visibility("default"))) struct xz_wave_rider_proof xz_wave_rider_proof_v1={.version=1};
static uint64_t micros(void){
    struct timespec t;if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000000+(uint64_t)t.tv_nsec/1000;
}
static void select_deck(void){
    if(source_deck==(int)rider.deck)return;
    source_deck=(int)rider.deck;rider.valid=0;rider.wave.valid=0;rider.wave.loading=1;
    rider.wave.error=XZ_WAVE_RIDER_SOURCE_OK;rider.wave.title[0]=0;
    if(source_ready)xz_wave_rider_source_select(source_deck);
}
void xz_wave_rider_open(uint32_t now){
    if(!rider.initialized)xz_rider_init(&rider,0,now);
    else {rider.phase=XZ_RIDER_SETUP;rider.setup_step=2;rider.last_ms=now;}
    rider.leave=0;rider.touch_down=1;present=opening_touch=1;
    source_ready=xz_wave_rider_source_start()==0;source_deck=-1;select_deck();
    if(!source_ready){rider.wave.loading=0;rider.wave.error=XZ_WAVE_RIDER_POSITION_UNAVAILABLE;}
}
void xz_wave_rider_close(void){
    present=0;source_deck=-1;xz_wave_rider_source_select(-1);
}
int xz_wave_rider_present(void){return present;}
int xz_wave_rider_leave_requested(void){return present&&rider.leave;}
int xz_wave_rider_input(int deck,const void *event,uint32_t now){
    if(!event||deck<0||deck>1)return 0;
    const unsigned char *p=event;uint16_t key;memcpy(&key,p+8,2);unsigned op=p[11]&15;
    if(key==0x4305&&op==4&&present&&deck==(int)rider.deck){
        int32_t delta;float speed;memcpy(&delta,p+12,4);memcpy(&speed,p+16,4);
        float pulses=delta?(float)delta:isfinite(speed)?speed:0;
        xz_rider_jog(&rider,pulses,now);xz_wave_rider_proof_v1.inputs++;return 1;
    }
    /* PLAY, load, skip, sync and the other deck retain native transport. */
    int index=key==0x4306?9:key==0x4102?XZ_RIDER_CUE:key>=0x4119&&key<=0x4120?(int)key-0x4119+XZ_RIDER_PAD_A:-1;
    if(index<0)return 0;
    uint32_t bit=1u<<(unsigned)index;
    if(op==2||op==3){int captured=!!(owned[deck]&bit);owned[deck]&=~bit;return captured;}
    if(present&&deck==(int)rider.deck&&op==0){
        if(!(owned[deck]&bit)){
            owned[deck]|=bit;
            /* Vinyl touch belongs to the game too, or stock could stop the
               track while the same platter's rotation is steering the rider. */
            if(index!=9)xz_rider_key(&rider,(enum xz_rider_key)index,now);
            xz_wave_rider_proof_v1.inputs++;
        }
        return 1;
    }
    return !!(owned[deck]&bit)||(present&&deck==(int)rider.deck);
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
int xz_wave_rider_draw(uint16_t *pixels,size_t count,size_t stride,uint32_t now){
    if(!present)return 0;
    uint64_t began=micros();
    struct xz_wave_rider_snapshot wave;
    int copied=source_ready&&xz_wave_rider_source_read(&wave);
    /* Publication can happen after the caller sampled its frame timestamp. */
    now=(uint32_t)(micros()/1000);
    if(copied)xz_rider_feed(&rider,&wave,now);
    xz_rider_step(&rider,now);
    int result=xz_rider_render(&rider,pixels,count,stride);
    uint32_t took=(uint32_t)(micros()-began);
    struct xz_wave_rider_proof *p=&xz_wave_rider_proof_v1;
    p->frames++;p->phase=rider.phase;p->deck=rider.deck;p->kind=rider.wave.kind;p->valid=rider.valid;
    p->generation=rider.wave.generation;p->first=rider.wave.first;p->total=rider.wave.total;
    p->spikes=rider.course.spikes;p->gems=rider.course.gems;p->score=rider.score;p->judged=rider.judged;
    p->position=(float)rider.position;p->altitude=rider.altitude;p->render_us=took;
    if(took>p->worst_render_us)p->worst_render_us=took;
    return result;
}
int xz_wave_rider_pad(int deck,int pad,unsigned *rgb,int *enabled){
    if(!present||deck!=(int)rider.deck||pad<0||pad>7||!rgb||!enabled)return 0;
    *rgb=pad<4?(rider.wave.kind?0x53e5d9:0x50aaff):pad==7?0xffffff:0x8b80ac;*enabled=1;return 1;
}
