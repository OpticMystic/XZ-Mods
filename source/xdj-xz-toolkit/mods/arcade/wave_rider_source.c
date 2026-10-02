/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wave_rider_source.h"
#include "../ui/layered_wave_source.h"
#include "../ui/layered_wave.h"
#include "../runtime.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef XZ_WAVE_RIDER_SOURCE_PORTABLE
#include <sys/syscall.h>
#include <unistd.h>
#endif

static int word(xz126_read_fn read,void *context,uint32_t address,uint32_t *out){
    if(address<0x10000||address>UINT32_MAX-4||address%4)return -1;
    return read(context,address,out,4);
}
int xz_wave_rider_position_read(xz126_read_fn read,void *context,uint32_t player,
                               struct xz_wave_rider_position *out){
    uint32_t owner,expected,current,again,normal,paused;uint8_t playing;
    if(!out)return -1;
    memset(out,0,sizeof(*out));
    if(!read||player<0x10000||player>UINT32_MAX-0xee0||player%4||
       word(read,context,player+0xec8,&owner)||owner>UINT32_MAX-12||
       word(read,context,player+0xed8,&expected)||
       word(read,context,owner+8,&current)||
       word(read,context,player+0xe78,&normal)||
       word(read,context,player+0xedc,&paused)||
       read(context,player+0xe77,&playing,1)||playing>1||
       word(read,context,player+0xec8,&again)||again!=owner||
       word(read,context,player+0xed8,&again)||again!=expected)return -1;
    /* Exact 1.26 getPlayingPosition/isAudiblePausing branch. getPlayingTime
       multiplies these signed ticks by 1000/44100 before rounding upward. */
    out->audible_pause=current==expected;
    uint32_t ticks=out->audible_pause?paused:normal;
    memcpy(&out->ticks,&ticks,4);out->playing=playing;
    return 0;
}
int xz_wave_rider_window(struct xz_wave_rider_snapshot *out,const uint8_t *bands,
                         uint32_t total,double position){
    if(!out)return -1;
    out->first=out->count=out->total=0;out->valid=0;
    if(!bands||!total||total>1000000u||!isfinite(position))return -1;
    if(position<0)position=0;
    if(position>total)position=total;
    uint32_t at=(uint32_t)position,first=at>XZ_WAVE_RIDER_PAST?at-XZ_WAVE_RIDER_PAST:0;
    uint32_t count=total-first;if(count>XZ_WAVE_RIDER_SAMPLES)count=XZ_WAVE_RIDER_SAMPLES;
    memcpy(out->bands,bands+(size_t)first*3,(size_t)count*3);
    out->first=first;out->count=count;out->total=total;out->position=position;out->valid=1;
    return 0;
}

#ifndef XZ_WAVE_RIDER_SOURCE_PORTABLE
static pthread_mutex_t publication_lock=PTHREAD_MUTEX_INITIALIZER;
static struct xz_wave_rider_snapshot published;
static int selected=-1,started;
static uint32_t selection_epoch,published_epoch;
static uint32_t milliseconds(void){
    struct timespec ts;if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&ts))return 0;
    return (uint32_t)((uint64_t)ts.tv_sec*1000+(uint32_t)ts.tv_nsec/1000000);
}
static int read_native(void *context,uint32_t address,void *out,size_t count){
    (void)context;return xz_read_memory(address,out,count);
}
static int source(unsigned deck,struct xz126_deck_source *out){
    uint32_t engine;
    return word(read_native,NULL,XZ126_PLAY_ENGINE_CELL,&engine)||
        xz126_deck_source_read(read_native,NULL,engine,deck,out);
}
static void publish(const struct xz_wave_rider_snapshot *value,uint32_t epoch){
    pthread_mutex_lock(&publication_lock);
    if(epoch==__atomic_load_n(&selection_epoch,__ATOMIC_ACQUIRE)){published=*value;published_epoch=epoch;}
    pthread_mutex_unlock(&publication_lock);
}
static void title(char *out,size_t capacity,const char *path){
    const char *name=strrchr(path,'/');name=name?name+1:path;
    snprintf(out,capacity,"%s",name);
    char *dot=strrchr(out,'.');if(dot)*dot=0;
}
static void *worker(void *unused){
    (void)unused;
    struct xz_wave_load wave={0};struct xz126_deck_source identity={0};
    struct xz_wave_rider_snapshot value={0};
    uint32_t generation=0,epoch=UINT32_MAX,retry_at=0;int deck=-1;
    for(;;){
        uint32_t wanted_epoch=__atomic_load_n(&selection_epoch,__ATOMIC_ACQUIRE);
        int wanted=__atomic_load_n(&selected,__ATOMIC_ACQUIRE);
        if(epoch!=wanted_epoch||deck!=wanted){
            xz_wave_load_free(&wave);memset(&identity,0,sizeof(identity));
            memset(&value,0,sizeof(value));deck=wanted;epoch=wanted_epoch;
            value.deck=deck;value.generation=++generation;retry_at=0;
            if(deck<0)xz_wave_index_reset();
        }
        value.observed_ms=milliseconds();
        if(deck>=0){
            struct xz126_deck_source current;
            if(source((unsigned)deck,&current)){
                value.valid=value.loading=value.playing=0;value.error=XZ_WAVE_RIDER_NO_TRACK;
                if(identity.path[0]){xz_wave_load_free(&wave);memset(&identity,0,sizeof(identity));
                    value.generation=++generation;value.track_hash=0;value.title[0]=0;}
            }else{
                int changed=!identity.path[0]||identity.file_frames!=current.file_frames||
                    identity.file_rate!=current.file_rate||strcmp(identity.path,current.path);
                if(changed){
                    xz_wave_load_free(&wave);identity=current;value.generation=++generation;
                    value.track_hash=xz_layered_path_hash(current.path);title(value.title,sizeof(value.title),current.path);
                    value.valid=value.playing=0;retry_at=0;
                }else identity=current;
                if(!wave.bands&&(!retry_at||(int32_t)(value.observed_ms-retry_at)>=0)){
                    value.loading=1;value.error=XZ_WAVE_RIDER_SOURCE_OK;publish(&value,epoch);
                    int rc=xz_wave_load_stems(current.path,&wave);
                    if(rc)rc=xz_wave_load_three_band(current.path,&wave);
                    value.loading=0;retry_at=milliseconds()+3000;
                    struct xz126_deck_source after;
                    if(epoch!=__atomic_load_n(&selection_epoch,__ATOMIC_ACQUIRE)||
                       source((unsigned)deck,&after)||strcmp(after.path,current.path)||
                       after.file_frames!=current.file_frames||after.file_rate!=current.file_rate){
                        xz_wave_load_free(&wave);memset(&identity,0,sizeof(identity));
                        value.valid=0;value.error=XZ_WAVE_RIDER_NO_TRACK;
                    }else if(rc)value.error=XZ_WAVE_RIDER_NO_ANALYSIS;
                }
                struct xz_wave_rider_position position;
                if(wave.bands){
                    if(xz_wave_rider_position_read(read_native,NULL,current.player,&position)){
                        value.valid=value.playing=0;value.error=XZ_WAVE_RIDER_POSITION_UNAVAILABLE;
                    }else{
                        xz_wave_rider_window(&value,wave.bands,wave.count,(double)position.ticks/294.);
                        value.playing=position.playing;value.kind=wave.kind;value.normalization=wave.normalization;
                        value.error=XZ_WAVE_RIDER_SOURCE_OK;
                    }
                }
            }
        }
        value.observed_ms=milliseconds();publish(&value,epoch);
        struct timespec delay={0,40000000};nanosleep(&delay,NULL);
    }
    return NULL;
}
int xz_wave_rider_source_start(void){
    int zero=0;
    if(!__atomic_compare_exchange_n(&started,&zero,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return zero==2?0:-1;
    static const uint8_t position_guard[16]={0x0c,0x30,0x90,0xe5,0x10,0x20,0x90,0xe5,0x10,0x40,0x2d,0xe9,0x02,0x20,0x63,0xe0};
    static const uint8_t pause_guard[8]={0xc8,0x2e,0x90,0xe5,0xd8,0x3e,0x90,0xe5};
    uint8_t got[16];pthread_t thread;
    if(xz_read_memory(0x49588,got,sizeof(position_guard))||memcmp(got,position_guard,sizeof(position_guard))||
       xz_read_memory(0x62b7c,got,sizeof(pause_guard))||memcmp(got,pause_guard,sizeof(pause_guard))||
       pthread_create(&thread,NULL,worker,NULL)){
        __atomic_store_n(&started,0,__ATOMIC_RELEASE);return -1;
    }
    pthread_detach(thread);__atomic_store_n(&started,2,__ATOMIC_RELEASE);return 0;
}
void xz_wave_rider_source_select(int deck){
    if(deck< -1||deck>1)deck=-1;
    int previous=__atomic_exchange_n(&selected,deck,__ATOMIC_ACQ_REL);
    if(previous!=deck)__atomic_fetch_add(&selection_epoch,1,__ATOMIC_RELEASE);
}
int xz_wave_rider_source_read(struct xz_wave_rider_snapshot *out){
    if(!out)return 0;
    memset(out,0,sizeof(*out));out->deck=__atomic_load_n(&selected,__ATOMIC_ACQUIRE);
    if(pthread_mutex_trylock(&publication_lock))return 0;
    if(published.deck==out->deck&&out->deck>=0&&
       published_epoch==__atomic_load_n(&selection_epoch,__ATOMIC_ACQUIRE))*out=published;
    pthread_mutex_unlock(&publication_lock);
    if(out->valid&&milliseconds()-out->observed_ms>XZ_WAVE_RIDER_FRESH_MS)out->valid=0;
    return 1;
}
#endif
