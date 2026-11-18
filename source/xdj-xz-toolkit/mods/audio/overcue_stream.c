/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "overcue.h"
#include "foreign_stems.h"
#include "../ram/lowzone.h"
#include "overcue_flat.h"
#include "../clock_compat.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__) && defined(__arm__)
#include <sched.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#endif

#define WINDOW XZ_OC_WINDOW_FRAMES
#define CAPTURE 2048u
#define SEARCH 8192
/* At 44.1 kHz: <=2.90 ms fades; allow ordinary seek refill 1.5 s before fail-open. */
#define STARVE_RAMP_FRAMES 128u
#define STARVE_UNITY_AFTER_MS 1500u
/* Prepared frames a window keeps ahead of playback, in the playing direction. */
#define HEADROOM 72000
/* Consecutive same-direction blocks before reverse playback moves the windows. */
#define DIRECTION_BLOCKS 4
int xz_oc_worker_schedule(void){
#if defined(__linux__) && defined(__arm__)
    if(sysconf(_SC_NPROCESSORS_ONLN)<4)return -1;
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(0,&mask);CPU_SET(2,&mask); /* core 1 is the display core, 97% real-time */
    struct sched_param priority={0};
    if(pthread_setschedparam(pthread_self(),SCHED_OTHER,&priority))return -1;
    if(pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask))return -1;
    /* Last in line on the two worker cores (this thread only). At the player's own priority the stem workers
       shared core 0 evenly with the exFAT helper, which serves every track load, and with telnet. */
    setpriority(PRIO_PROCESS,0,10);
    prctl(PR_SET_NAME,"XZStemWorker",0,0,0);
#endif
    return 0;
}
struct window { int64_t first; float *pcm[3]; unsigned masks; int access, valid; };
#define MIX_CACHE_FRAMES 512u
struct mix_frame {int64_t position,window_first;uint32_t epoch;float native[2],raw[2],mixed[2];};
static int window_acquire(struct window *w){
    int readers=__atomic_load_n(&w->access,__ATOMIC_ACQUIRE);
    for(int attempt=0;attempt<3&&readers>=0;attempt++)
        if(__atomic_compare_exchange_n(&w->access,&readers,readers+1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 1;
    return 0;
}
static void window_release(struct window *w){__atomic_fetch_sub(&w->access,1,__ATOMIC_RELEASE);}

struct xz_oc_stream {
    struct xz_oc_assets assets;
    struct xz_foreign_stems *foreign;
    unsigned role_count, source_rate;
    uint32_t rate;
    pthread_t worker;
    int running, published, state, capture_state;
    int64_t desired, capture_position, previous_position;
    size_t capture_count;
    float capture[CAPTURE*2];
    struct window window[4];
    void *window_block; size_t window_block_bytes; /* the unit: one mapping behind every window buffer */
    int loop_published;
    int32_t loop_start;
    int starved, starve_fallback; /* enum xz_oc_starvation, audio-thread writer */
    uint32_t starve_frames, starve_last_frames, starve_max_frames, starve_runs;
    float held_source, last_output[2];
    int last_output_valid;
    double offset, scale;
    int confirmations, attempts;
    int reverse, direction_run; /* audio-thread writer */
    int64_t last_position;
    float score;
    uint32_t blocks, misses;
    uint32_t previous_levels[4], request_masks;
    uint32_t mix_cache_busy,mix_cache_epoch,mix_cache_levels[4];
    /* Segmented store (overcue_flat.h). `flat` is published once by the worker and stays until close; its segments
       come and go with the stem pool's grant. */
    struct xz_oc_flat *flat;
    int flat_policy, flat_failed;
    int flat_released; /* worker only: the store holds and has built the whole track (the name predates segments) */
    int flat_yielding; /* worker only: the grant takes the playhead's segments; the windows come up first */
    int ram_only, pinned, pin_tried, waiting_ram, flat_active; /* flat_active: audio thread only */
    int store_first; /* worker only: build the store from the stem files, no compressed RAM copy */
    uint32_t flat_refused_ms; int flat_refusal_said; /* worker only */
    uint32_t pin_retry_ms;
    uint32_t flat_chunks_now, flat_built_now;
    uint32_t flat_retry_ms, flat_began_ms, flat_took_ms, flat_service_ms;
    int engaged; uint32_t released_ms; /* engine writes: a stem level is off unity; when they last all returned to it */
    uint32_t anchor_count; int32_t anchor[XZ_OC_FLAT_ANCHORS - 1]; /* engine writes: deck frames to keep ready */
    uint8_t klass; uint32_t order; /* worker only: the pool class asked for, and the claim taken when it last rose */
    uint32_t store_held, store_grant, store_need, store_class, store_lead_ms, store_behind_ms; /* worker writes */
    int stems_whole; /* worker writes: the stems need nothing more from the stick (whole store, or a RAM copy) */
    /* A stick that goes (xz_oc_stream_source_gone): the request is written before gone_asked rises. */
    const char *gone_prefix; xz_oc_gone_done gone_done; uint32_t gone_token, gone_asked;
    uint32_t gone_seen; /* worker, then close: the last request answered */
    int source_gone; /* worker only: 1 the files are closed and the whole store plays on (KEPT); 2 the stems ended */
    int files_closed; /* worker only */
    char gone_why[96]; /* worker only */
    char source[1024]; /* the track's path, for the stick it is on */
    size_t ram_bytes; /* worker writes, any thread reads */
    struct mix_frame mix_cache[MIX_CACHE_FRAMES];
};
static float sample(const float *pcm,size_t frames,double at,int channel){
    if(at<0||at>=(double)(frames-1))return 0;
    size_t i=(size_t)at;double fraction=at-(double)i;
    return (float)(((1-fraction)*pcm[i*2+(unsigned)channel]+fraction*pcm[(i+1)*2+(unsigned)channel]));
}
static unsigned stem_count(const struct xz_oc_stream *s){return s->role_count==4?4:3;}
static unsigned stem_rate(const struct xz_oc_stream *s){return s->source_rate?s->source_rate:XZ_OC_RATE;}
static uint64_t stem_frames(const struct xz_oc_stream *s){return s->foreign?xz_foreign_frames(s->foreign):s->assets.frames;}
unsigned xz_oc_stream_stem_count(const struct xz_oc_stream *s){return s?stem_count(s):3;}
const char *xz_oc_stream_format(const struct xz_oc_stream *s){
    if(!s||!s->foreign)return "OVERCUE 3";
    return xz_foreign_format(s->foreign)==XZ_FOREIGN_AZ?"AZ MODS 4":stem_count(s)==4?"RX3 MODS 4":"RX3 MODS 3";
}
static int plan_levels(const float *levels,unsigned count,struct xz_oc_plan *out){
    if(!levels||!out||(count!=3&&count!=4))return -1;
    unsigned order[4]={0,1,2,3};
    for(unsigned i=0;i<count;i++)if(!isfinite(levels[i])||levels[i]<0||levels[i]>2)return -1;
    for(unsigned i=0;i<count;i++)for(unsigned j=i+1;j<count;j++)if(levels[order[j]]<levels[order[i]]){unsigned t=order[i];order[i]=order[j];order[j]=t;}
    *out=(struct xz_oc_plan){0};out->source=levels[order[0]];
    for(unsigned i=0;i+1<count;i++){
        out->gain[i]=levels[order[i+1]]-levels[order[i]];
        if(out->gain[i])for(unsigned j=i+1;j<count;j++)out->mask[i]|=1u<<order[j];
    }
    return 0;
}
int xz_oc_plan_levels(const float levels[3],struct xz_oc_plan *out){return plan_levels(levels,3,out);}
int xz_oc_plan_levels4(const float levels[4],struct xz_oc_plan *out){return plan_levels(levels,4,out);}
static unsigned plan_masks(const struct xz_oc_plan *p){return p->mask[0]|p->mask[1]<<4|p->mask[2]<<8;}
/* OverCue pairs are always mixed from the singles, as OverCue firmware 0.13.1+
 * does, so four-file and seven-file bundles share one path. The lowest stem is
 * never selected, so at most two singles are needed. */
static int stream_plan(const struct xz_oc_stream *s,const float *levels,struct xz_oc_plan *out){
    if(plan_levels(levels,stem_count(s),out))return -1;
    if(s->foreign)return 0;
    struct xz_oc_plan singles={.source=out->source};unsigned k=0;
    for(unsigned r=0;r<3;r++){
        float coefficient=0;
        for(unsigned i=0;i<3;i++)if(out->mask[i]&(1u<<r))coefficient+=out->gain[i];
        if(coefficient){singles.mask[k]=1u<<r;singles.gain[k++]=coefficient;}
    }
    *out=singles;return 0;
}
static int ahead(const struct window *w,int64_t at,int reverse){
    return reverse?at-HEADROOM>=w->first&&at<w->first+WINDOW:at>=w->first&&at+HEADROOM<w->first+WINDOW;
}
static int prepared_read(struct xz_oc_stream *s,unsigned mask,int64_t first,size_t frames,float *output){
    if(s->foreign)return mask?xz_foreign_read(s->foreign,mask,first,frames,output):xz_foreign_reference(s->foreign,first,frames,output);
    struct xz_oc_file *file=mask?xz_oc_mix_file(&s->assets,mask):&s->assets.reference;
    if(!file)return -1;
    int16_t raw[4096];
    for(size_t offset=0;offset<frames;){
        size_t n=frames-offset;if(n>2048)n=2048;
        if(xz_oc_read(file,first+(int64_t)offset,n,raw))return -1;
        for(size_t i=0;i<n*2;i++)output[offset*2+i]=raw[i]/32768.0f;
        offset+=n;
    }return 0;
}
/* Bound only MODS-owned workers; retain default scheduling inheritance. */
#define STEM_WORKER_STACK_BYTES (2u*1024u*1024u)
static int stem_worker_create(pthread_t *thread,void *(*entry)(void *),void *context){
    pthread_attr_t attributes;
    int rc=pthread_attr_init(&attributes);
    if(rc)return rc;
    rc=pthread_attr_setstacksize(&attributes,STEM_WORKER_STACK_BYTES);
    if(!rc)rc=pthread_create(thread,&attributes,entry,context);
    pthread_attr_destroy(&attributes);
    return rc;
}
struct refill { struct xz_oc_stream *stream; unsigned mask; int64_t first; float *output; int result; };
static void *refill_role(void *context){struct refill *j=context;j->result=prepared_read(j->stream,j->mask,j->first,WINDOW,j->output);return NULL;}
static int refill_window(struct xz_oc_stream *s,struct window *w){
    struct refill jobs[3];pthread_t threads[3];int spawned[3]={0,0,0};
    unsigned count=stem_count(s)-1;
    for(unsigned r=0;r<count;r++){
        unsigned mask=(w->masks>>(r*4))&15;
        jobs[r]=(struct refill){s,mask,w->first,w->pcm[r],0};
        if(!mask)continue;
        /* Foreign masks share decoder handles. The worker serializes seeks. */
        if(!s->foreign&&(w->masks&15)&&((w->masks>>4)&15))spawned[r]=stem_worker_create(threads+r,refill_role,jobs+r)==0;
        if(!spawned[r])refill_role(jobs+r);
    }
    int ok=1;for(unsigned r=0;r<count;r++){if(spawned[r])pthread_join(threads[r],NULL);if(jobs[r].result)ok=0;}return ok;
}
static int fill_slot(struct xz_oc_stream *s,int slot,int64_t first,unsigned masks){
    struct window *w=&s->window[slot];
    /* A selection may return before the previous request reached the audio
     * thread. Keep its already-valid cache readable instead of refilling it. */
    if(w->valid&&w->first==first&&w->masks==masks)return 1;
    int idle=0;
    if(!__atomic_compare_exchange_n(&w->access,&idle,-1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    w->valid=0;w->first=first;w->masks=masks;
    int ok=refill_window(s,w);w->valid=ok;
    __atomic_store_n(&w->access,0,__ATOMIC_RELEASE);
    return ok?1:-1;
}
static int fill_loop_slot(struct xz_oc_stream *s,int current,int64_t first,unsigned masks){
    int candidate=current==2?3:2;
    int ok=fill_slot(s,candidate,first,masks);
    if(ok>0)__atomic_store_n(&s->loop_published,candidate,__ATOMIC_SEQ_CST);
    return ok;
}
static int window_has(const struct window *w,const struct xz_oc_plan *p){
    if(!plan_masks(p))return 1;if(!w||!w->valid)return 0;
    for(unsigned r=0;r<3;r++)if(p->mask[r]&&p->mask[r]!=(w->masks&15)&&p->mask[r]!=((w->masks>>4)&15)&&p->mask[r]!=((w->masks>>8)&15))return 0;
    return 1;
}
struct render_plan {float source,gain[3];unsigned active[3],slot[3];};
static struct render_plan render_plan(const struct xz_oc_stream *s,const struct window *w,const struct xz_oc_plan *p){
    struct render_plan out={.source=p->source};
    for(unsigned r=0;r<3;r++)if(p->mask[r]){
        out.active[r]=1;out.slot[r]=p->mask[r]==(w->masks&15)?0:p->mask[r]==((w->masks>>4)&15)?1:2;
        out.gain[r]=(float)(s->scale*p->gain[r]/(s->foreign?1:xz_oc_mix_gain(&s->assets,p->mask[r])));
    }
    return out;
}
static float planned_sample(const struct window *w,const struct render_plan *p,double at,int channel,float native){
    float result=native*p->source;
    for(unsigned r=0;r<3;r++)if(p->active[r]){
        result+=sample(w->pcm[p->slot[r]],WINDOW,at,channel)*p->gain[r];
    }
    return result;
}
static uint32_t worker_ms(void){struct timespec t;if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;return (uint32_t)((uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u);}
static void ram_bytes_publish(struct xz_oc_stream *s){
    const struct xz_oc_flat *flat=s->flat;size_t copy=xz_oc_assets_ram_bytes(&s->assets);
    __atomic_store_n(&s->ram_bytes,copy+(flat?(size_t)__atomic_load_n(&flat->held,__ATOMIC_RELAXED)*XZ_STEM_EXTENT_BYTES:0),__ATOMIC_RELEASE);
    __atomic_store_n(&s->stems_whole,s->flat_released||copy>0,__ATOMIC_RELAXED);
}
#if defined(__linux__)
extern int xz_ram_track_load_busy(void) __attribute__((weak));
#else
static int (*xz_ram_track_load_busy)(void);
#endif
/* One stem copy from the USB at a time: the first deck is in RAM sooner than two copies sharing the stick. */
static pthread_mutex_t pin_turn=PTHREAD_MUTEX_INITIALIZER;
/* Asked during the copy. A stream being closed abandons it; a track that is loading on any deck has the USB. */
static int pin_poll(void *context){
    struct xz_oc_stream *s=context;
    if(!__atomic_load_n(&s->running,__ATOMIC_ACQUIRE))return -1;
    if(__atomic_load_n(&s->gone_asked,__ATOMIC_ACQUIRE)!=s->gone_seen)return -1; /* a stick goes: answer it first */
    return xz_ram_track_load_busy&&xz_ram_track_load_busy()?1:0;
}
#if defined(__linux__)
extern void xz_log(const char *) __attribute__((weak));
extern uint64_t xz_ram_available_now(void) __attribute__((weak));
#else
static void (*xz_log)(const char *);
static uint64_t (*xz_ram_available_now)(void);
#endif
static int flat_read(void *context,unsigned single,int64_t first,size_t frames,int16_t *stereo){
    struct xz_oc_stream *s=context;struct xz_oc_file *file=xz_oc_mix_file(&s->assets,1u<<single);
    if(!file)return -1;
    for(size_t done=0;done<frames;){
        size_t n=frames-done;if(n>96000u)n=96000u;
        if(xz_oc_read(file,first+(int64_t)done,n,stereo+done*2))return -1;
        done+=n;
    }
    return 0;
}
/* Worker: a window holds the requested stems with headroom at the playhead (or no stem is requested). */
static int windows_cover_playhead(struct xz_oc_stream *s){
    unsigned masks=__atomic_load_n(&s->request_masks,__ATOMIC_ACQUIRE);
    if(!masks)return 1;
    int reverse=__atomic_load_n(&s->reverse,__ATOMIC_RELAXED);
    int64_t at=(int64_t)floor(((double)__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE)+s->offset)*stem_rate(s)/s->rate);
    for(unsigned i=0;i<4;i++){const struct window *w=&s->window[i];if(w->valid&&w->masks==masks&&ahead(w,at,reverse))return 1;}
    return 0;
}
static uint32_t claims; /* pool claim order: a class that rises takes the next */
/* Worker, after alignment: the store asks the pool for room, slides its bubble after the playhead, builds a chunk. */
static int flat_service(struct xz_oc_stream *s){
    if(!s->flat_policy||s->foreign||s->flat_failed)return 0;
    struct xz_oc_flat *flat=s->flat;uint32_t now=worker_ms();
    if(!flat){
        if(s->flat_retry_ms&&(uint32_t)(now-s->flat_retry_ms)<5000u)return 0;
        s->flat_retry_ms=now?now:1u;
        if(!xz_oc_flat_need(s->rate,stem_rate(s),stem_frames(s),s->offset)){s->flat_failed=1;return 0;}
        /* No pool holder free, or no memory for the tables: the windows play and the store is asked for again. */
        flat=xz_oc_flat_create(s->rate,stem_rate(s),stem_frames(s),s->offset,3,flat_read,s);
        if(!flat)return 0;
        s->flat_began_ms=now;s->klass=XZ_STEM_IDLE;
        __atomic_store_n(&s->flat_chunks_now,flat->chunks,__ATOMIC_RELAXED);__atomic_store_n(&s->flat_built_now,0,__ATOMIC_RELAXED);
        __atomic_store_n(&s->store_need,flat->segments,__ATOMIC_RELAXED);
        __atomic_store_n(&s->flat,flat,__ATOMIC_SEQ_CST);
    }
    /* Engaged while a stem level is off unity and for two minutes after: a deck whose stems are in use is held whole
       first when its track fits. */
    uint32_t released=__atomic_load_n(&s->released_ms,__ATOMIC_ACQUIRE);
    uint8_t klass=__atomic_load_n(&s->engaged,__ATOMIC_ACQUIRE)||(released&&(uint32_t)(now-released)<XZ_OC_ENGAGED_DECAY_MS)?
        XZ_STEM_ENGAGED:XZ_STEM_PRELOAD;
    if(klass>s->klass)s->order=__atomic_add_fetch(&claims,1,__ATOMIC_RELAXED);
    s->klass=klass;xz_oc_flat_want(flat,klass,s->order);
    if(!s->flat_service_ms||(uint32_t)(now-s->flat_service_ms)>=250u){xz_stem_pool_service();s->flat_service_ms=now?now:1u;}
    int32_t anchors[XZ_OC_FLAT_ANCHORS];unsigned count=__atomic_load_n(&s->anchor_count,__ATOMIC_ACQUIRE);
    if(count>XZ_OC_FLAT_ANCHORS-1)count=XZ_OC_FLAT_ANCHORS-1;
    for(unsigned i=0;i<count;i++)anchors[i]=__atomic_load_n(&s->anchor[i],__ATOMIC_RELAXED);
    int32_t loop=__atomic_load_n(&s->loop_start,__ATOMIC_ACQUIRE);
    if(loop>=0)anchors[count++]=loop;
    int64_t near=__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE);int reverse=__atomic_load_n(&s->reverse,__ATOMIC_RELAXED);
    uint16_t held=__atomic_load_n(&flat->held,__ATOMIC_RELAXED);
    /* The playhead's segments go only when the windows hold the playhead: a grant too small for a store (a track
       load refused, the other deck's floor) would otherwise leave the deck with neither until a refill lands. While
       the store yields, stream_worker keeps the windows filled although the store still covers. */
    int step=xz_oc_flat_pass_keep(flat,near,reverse,anchors,count,held&&!windows_cover_playhead(s));
    unsigned grant=xz_stem_pool_grant(flat->holder);
    if(step<0){
        /* A stem file cannot be read: nothing more can be built, so the pool gets every segment back. */
        s->flat_failed=1;
        struct xz_stem_want none={XZ_STEM_IDLE,0,0,0};xz_stem_pool_want(flat->holder,&none);
        for(uint32_t g=0;g<flat->segments;g++)xz_oc_flat_drop(flat,g);
        grant=0;
    }
    int reason=xz_stem_pool_refusal();
    if(reason>0&&reason<5&&s->flat_refusal_said!=reason+1&&xz_log){ /* once per reason */
        static const char *const why[]={"no reason recorded","RAM ledger","RAM-disk space","RAM-disk file","address zone"};
        char line[176];
        snprintf(line,sizeof line,"STEMS store refused: %s; wanted %u MiB, ledger counts %u MiB available, RAM disk has %u MiB free%s",
                 why[reason],(unsigned)(((uint64_t)flat->segments*XZ_STEM_EXTENT_BYTES)>>20),
                 xz_ram_available_now?(unsigned)(xz_ram_available_now()>>20):0u,xz_stem_pool_disk_free_mib(),
                 s->store_first?"; asking again for 30 s":"");
        xz_log(line);s->flat_refusal_said=reason+1;
    }
    if(s->flat_refusal_said&&__atomic_load_n(&flat->held,__ATOMIC_RELAXED)>held){
        if(xz_log)xz_log("STEMS store admitted after a refusal");
        s->flat_refusal_said=0;
    }
    /* Store first, and the pool grants too little for a store for half a minute (the other deck may hold it, or
       the memory is short): the stem files go to RAM and the windows play from that copy. The store keeps asking. */
    if(grant>=XZ_OC_FLAT_MIN_GRANT||grant>=flat->segments)s->flat_refused_ms=0;
    else if(s->store_first&&!s->flat_failed){
        if(!s->flat_refused_ms)s->flat_refused_ms=now?now:1u;
        if((uint32_t)(now-s->flat_refused_ms)>=30000u){
            s->store_first=0;
            if(xz_log)xz_log("STEMS store still refused after 30 s: the stem files go to RAM and the windows play");
        }
    }
    s->flat_yielding=!s->flat_failed&&__atomic_load_n(&flat->held,__ATOMIC_RELAXED)&&grant<XZ_OC_FLAT_MIN_GRANT&&grant<flat->segments;
    uint64_t lead,behind;xz_oc_flat_reach(flat,near,reverse,&lead,&behind);
    __atomic_store_n(&s->store_held,__atomic_load_n(&flat->held,__ATOMIC_RELAXED),__ATOMIC_RELAXED);
    __atomic_store_n(&s->store_grant,grant,__ATOMIC_RELAXED);
    __atomic_store_n(&s->store_class,s->flat_failed?(uint32_t)XZ_STEM_IDLE:klass,__ATOMIC_RELAXED);
    __atomic_store_n(&s->store_lead_ms,(uint32_t)(lead*1000u/s->rate),__ATOMIC_RELAXED);
    __atomic_store_n(&s->store_behind_ms,(uint32_t)(behind*1000u/s->rate),__ATOMIC_RELAXED);
    __atomic_store_n(&s->flat_built_now,__atomic_load_n(&flat->built,__ATOMIC_ACQUIRE),__ATOMIC_RELAXED);
    if(xz_oc_flat_complete(flat)){
        /* The whole track is built. The stem files stay open for segments given back later. A stem copy in RAM
           stays too: the copy took the place of the stick file (ram/pin.c), so it is all there is to decode from. */
        if(!s->flat_released&&!__atomic_load_n(&s->flat_took_ms,__ATOMIC_RELAXED))
            __atomic_store_n(&s->flat_took_ms,(uint32_t)(now-s->flat_began_ms),__ATOMIC_RELAXED);
        s->flat_released=1;
    }else s->flat_released=0;
    ram_bytes_publish(s);
    return 0;
}
/* ---- a stick that goes (USB STOP or pulled) ---- */
/* Worker: an open stem file of this stream lies under prefix. Files copied to RAM (ram/pin.c) no longer name the
   stick: their descriptors were replaced by the copies. */
static int files_on(const struct xz_oc_stream *s,const char *prefix){
    if(s->files_closed||!prefix||!*prefix||strncmp(s->source,prefix,strlen(prefix)))return 0;
    if(s->foreign)return 1;
    int copied=s->assets.reference.ram.mapping!=NULL;
    for(unsigned r=0;r<3;r++)copied&=s->assets.role[r].ram.mapping!=NULL;
    return !copied;
}
static void close_files(struct xz_oc_stream *s){
    if(s->foreign)xz_foreign_release_audio(s->foreign);else xz_oc_assets_release_audio(&s->assets);
    s->files_closed=1;
}
/* Worker: the stems are over for this track. The audio thread sees the error and ramps to unity from the last
   sample it gave; the store's segments go back through the reader protocol (xz_oc_flat_drop), so a block still
   inside the store finishes with the extent it loaded. */
static void end_stems(struct xz_oc_stream *s){
    __atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);
    struct xz_oc_flat *flat=s->flat;
    if(flat){
        struct xz_stem_want none={XZ_STEM_IDLE,0,0,0};xz_stem_pool_want(flat->holder,&none);
        for(unsigned tries=0;tries<25;tries++){
            int left=0;
            for(uint32_t g=0;g<flat->segments;g++)if(!xz_oc_flat_drop(flat,g))left=1;
            if(!left)break;
        }
        s->flat_failed=1;
        __atomic_store_n(&s->store_held,__atomic_load_n(&flat->held,__ATOMIC_RELAXED),__ATOMIC_RELAXED);
        __atomic_store_n(&s->store_grant,0,__ATOMIC_RELAXED);__atomic_store_n(&s->store_class,XZ_STEM_IDLE,__ATOMIC_RELAXED);
        __atomic_store_n(&s->flat_built_now,__atomic_load_n(&flat->built,__ATOMIC_ACQUIRE),__ATOMIC_RELAXED);
    }
    close_files(s);s->flat_released=0;s->source_gone=2;
    ram_bytes_publish(s);
}
/* Worker: a store that holds the whole track built keeps it for the life of the stream. It asks the pool for exactly
   that, as a floor, with the earliest claim there is: no other deck's want can take it. */
static void keep_store(struct xz_oc_stream *s){
    struct xz_oc_flat *flat=s->flat;
    struct xz_stem_want fixed={XZ_STEM_ENGAGED,(uint16_t)flat->segments,(uint16_t)flat->segments,0};
    xz_stem_pool_want(flat->holder,&fixed);
    close_files(s);s->flat_released=1;s->source_gone=1;
    __atomic_store_n(&s->store_class,XZ_STEM_ENGAGED,__ATOMIC_RELAXED);
    __atomic_store_n(&s->state,XZ_OC_READY,__ATOMIC_RELEASE);
    ram_bytes_publish(s);
}
/* Worker, every pass while a kept store plays: nothing is built or slid. Should the pool grant less than the store
   holds (tracks first lowered the pool below it), the stems end: a segment given back could never be built again. */
static void keep_service(struct xz_oc_stream *s){
    struct xz_oc_flat *flat=s->flat;uint32_t now=worker_ms();
    if(!s->flat_service_ms||(uint32_t)(now-s->flat_service_ms)>=250u){xz_stem_pool_service();s->flat_service_ms=now?now:1u;}
    unsigned grant=xz_stem_pool_grant(flat->holder),held=__atomic_load_n(&flat->held,__ATOMIC_RELAXED);
    __atomic_store_n(&s->store_grant,grant,__ATOMIC_RELAXED);__atomic_store_n(&s->store_held,held,__ATOMIC_RELAXED);
    if(grant>=held)return;
    if(xz_log){
        char line[160];
        snprintf(line,sizeof line,"USB_RELEASE: stems: a store kept after its stick went is granted %u of its %u segments; the stems end for this track",grant,held);
        xz_log(line);
    }
    end_stems(s);
}
/* Worker: what this stream does about a stick that goes. */
static int let_go(struct xz_oc_stream *s,const char *prefix){
    if(s->source_gone||!files_on(s,prefix)){
        snprintf(s->gone_why,sizeof s->gone_why,"%s",s->source_gone?"already let go":"no open stem file on it");
        return XZ_OC_GONE_NOTHING;
    }
    const struct xz_oc_flat *flat=s->flat;
    int error=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE)==XZ_OC_ERROR;
    if(flat&&!s->flat_failed&&!s->foreign&&!error&&xz_oc_flat_complete(flat)){
        snprintf(s->gone_why,sizeof s->gone_why,"the store holds all %u chunks in %u segments",flat->chunks,flat->segments);
        keep_store(s);return XZ_OC_GONE_KEPT;
    }
    if(s->foreign)snprintf(s->gone_why,sizeof s->gone_why,"%s","foreign stems have no store");
    else if(error)snprintf(s->gone_why,sizeof s->gone_why,"%s","the stream had stopped on an error");
    else if(!flat||s->flat_failed)snprintf(s->gone_why,sizeof s->gone_why,"%s","no store: windows read the files");
    else snprintf(s->gone_why,sizeof s->gone_why,"the store held %u of %u chunks (%u of %u segments)",
                  __atomic_load_n(&flat->built,__ATOMIC_ACQUIRE),flat->chunks,(unsigned)__atomic_load_n(&flat->held,__ATOMIC_RELAXED),flat->segments);
    end_stems(s);return XZ_OC_GONE_ENDED;
}
/* Worker: answers a stick request if one waits. 1 while the stream has let go (the rest of the worker then has
   nothing to do: a kept store is only watched, ended stems are over). */
static int gone_serve(struct xz_oc_stream *s){
    uint32_t asked=__atomic_load_n(&s->gone_asked,__ATOMIC_ACQUIRE);
    if(asked!=s->gone_seen){
        const char *prefix=__atomic_load_n(&s->gone_prefix,__ATOMIC_RELAXED);
        xz_oc_gone_done done=__atomic_load_n(&s->gone_done,__ATOMIC_RELAXED);
        uint32_t token=__atomic_load_n(&s->gone_token,__ATOMIC_RELAXED);
        s->gone_seen=asked;
        int outcome=let_go(s,prefix);
        if(done)done(token,outcome,s->gone_why);
    }
    if(s->source_gone==1)keep_service(s);
    return s->source_gone!=0;
}
static double correlation(const float *native,const float *reference,size_t n,int lag,unsigned step,double *scale){
    double cross=0,original=0,prepared=0;
    const float *r=reference+(size_t)(SEARCH+lag)*2;
    for(size_t i=0;i<n;i+=step)for(unsigned c=0;c<2;c++){
        double a=native[i*2+c],b=r[i*2+c];cross+=a*b;original+=a*a;prepared+=b*b;
    }
    if(original<0.00001||prepared<0.00001)return 0;
    if(scale)*scale=cross/prepared;
    return cross/sqrt(original*prepared);
}
static double fractional_correlation(const float *native,const float *reference,size_t n,double lag,double *scale){
    int base=(int)floor(lag);double fraction=lag-base,cross=0,original=0,prepared=0;
    const float *r=reference+(size_t)(SEARCH+base)*2;
    for(size_t i=0;i<n;i++)for(unsigned c=0;c<2;c++){
        double a=native[i*2+c],b=r[i*2+c]*(1-fraction)+r[(i+1)*2+c]*fraction;
        cross+=a*b;original+=a*a;prepared+=b*b;
    }
    if(original<0.00001||prepared<0.00001)return 0;
    if(scale)*scale=cross/prepared;return cross/sqrt(original*prepared);
}
static int align(struct xz_oc_stream *s){
    size_t native_count=s->capture_count+SEARCH*2+2;
    double first=(double)(s->capture_position-SEARCH)*stem_rate(s)/s->rate;
    int64_t raw_first=(int64_t)floor(first);
    size_t raw_count=(size_t)ceil((double)native_count*stem_rate(s)/s->rate)+3;
    float *raw=malloc(raw_count*2*sizeof(float));float *reference=malloc(native_count*2*sizeof(float));
    if(!raw||!reference){free(raw);free(reference);return -1;}
    if(prepared_read(s,0,raw_first,raw_count,raw)){free(raw);free(reference);return -1;}
    for(size_t i=0;i<native_count;i++)for(unsigned c=0;c<2;c++)
        reference[i*2+c]=sample(raw,raw_count,first+(double)i*stem_rate(s)/s->rate-(double)raw_first,(int)c);
    double peaks[16];int locations[16];
    for(unsigned i=0;i<16;i++){peaks[i]=-1;locations[i]=-SEARCH*2;}
    for(int lag=-SEARCH+1;lag<SEARCH;lag++){
        double score=correlation(s->capture,reference,s->capture_count,lag,4,NULL);
        int slot=0;
        for(int i=0;i<16;i++){
            if(abs(locations[i]-lag)<=3){slot=i;break;}
            if(peaks[i]<peaks[slot])slot=i;
        }
        if(score>peaks[slot]){peaks[slot]=score;locations[slot]=lag;}
    }
    double best_score=-1,scale=1,offset=0,refined[16],positions[16];
    for(unsigned peak=0;peak<16;peak++){
        refined[peak]=-1;positions[peak]=locations[peak];
        for(int part=-15;part<=15;part++){
            double candidate=locations[peak]+part*0.05;
            if(candidate<=-SEARCH||candidate>=SEARCH)continue;
            double candidate_scale=1,score=fractional_correlation(s->capture,reference,s->capture_count,candidate,&candidate_scale);
            if(score>refined[peak]){refined[peak]=score;positions[peak]=candidate;}
            if(score>best_score){best_score=score;offset=candidate;scale=candidate_scale;}
        }
    }
    free(raw);free(reference);s->attempts++;
    if(best_score<0.995||scale<0.1||scale>8)return 0;
    for(unsigned peak=0;peak<16;peak++)if(fabs(positions[peak]-offset)>4&&refined[peak]>best_score-0.003)return 0;
    if(!s->confirmations||fabs(s->offset-offset)>1.25||fabs(s->scale-scale)>s->scale*0.08){
        s->confirmations=1;s->offset=offset;s->scale=scale;s->previous_position=s->capture_position;s->score=(float)best_score;return 0;
    }
    if(llabs(s->capture_position-s->previous_position)<512)return 0;
    s->offset=(s->offset+offset)*0.5;s->scale=(s->scale+scale)*0.5;s->score=(float)best_score;s->confirmations++;
    return 1;
}
static void *stream_worker(void *context){
    struct xz_oc_stream *s=context;int aligned=0;
    if(xz_oc_worker_schedule()){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);return NULL;}
    while(__atomic_load_n(&s->running,__ATOMIC_ACQUIRE)){
        if(gone_serve(s)){struct timespec rest={0,20000000};nanosleep(&rest,NULL);continue;}
        int store_first=s->store_first&&!s->flat_failed&&!s->foreign;
        /* Store first reads the stem files on the stick. A loading track, and the RAM copy of a playing
           track's own file, have the stick first (ram/budget.h). */
        if(store_first&&!s->pinned&&!s->flat_released&&xz_ram_track_load_busy&&xz_ram_track_load_busy()){
            struct timespec wait={0,20000000};nanosleep(&wait,NULL);continue;
        }
        /* Alignment reads the prepared full mix: with stems from a RAM copy, not before the copy is in RAM. */
        if(!aligned&&!(s->ram_only&&!s->pinned&&!store_first)&&__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==2){
            int result=align(s);
            if(result<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
            if(result>0){aligned=1;__atomic_store_n(&s->capture_state,3,__ATOMIC_RELEASE);}
            else { s->capture_count=0; __atomic_store_n(&s->capture_state,0,__ATOMIC_RELEASE); }
        }
        if(!store_first&&!s->foreign&&!s->pinned&&!s->flat_released&&xz_oc_assets_pin_supported()&&(s->ram_only||!s->pin_tried)){
            /* The track comes first: the copy of this deck's stem files to RAM starts here, on the stream's own
               worker, after the track load's quiet window and when no other deck is copying. Until it is done
               nothing of this deck's stems is read from the USB and the stock audio is left alone. A refusal
               by the RAM ledger is asked again every five seconds. */
            uint32_t now=worker_ms();
            int due=!s->pin_tried||(uint32_t)(now-s->pin_retry_ms)>=5000u;
            if(due&&!(xz_ram_track_load_busy&&xz_ram_track_load_busy())&&!pthread_mutex_trylock(&pin_turn)){
                s->pin_tried=1;s->pin_retry_ms=now?now:1u;
                int pinned=xz_oc_assets_pin_polled(&s->assets,pin_poll,s);
                pthread_mutex_unlock(&pin_turn);
                if(pinned<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
                if(pinned>0){s->pinned=1;ram_bytes_publish(s);}
            }
            __atomic_store_n(&s->waiting_ram,s->ram_only&&!s->pinned&&s->pin_tried,__ATOMIC_RELAXED);
            if(s->ram_only&&!s->pinned){struct timespec wait={0,50000000};nanosleep(&wait,NULL);continue;}
        }
        if(aligned&&flat_service(s)<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
        const struct xz_oc_flat *flat=aligned&&!s->flat_failed&&!s->flat_yielding?s->flat:NULL;
        if(flat&&(s->flat_released||xz_oc_flat_covers_toward(flat,__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE),s->rate*2u,
                                                               __atomic_load_n(&s->reverse,__ATOMIC_RELAXED)))){
            /* Two seconds ahead of the playhead are in the store: the windows have nothing to do. Short of
               that they keep playing, so a build slower than playback never starves a muted stem. */
            __atomic_store_n(&s->state,XZ_OC_READY,__ATOMIC_RELEASE);
            struct timespec pause={0,s->flat_released?20000000:1000000};nanosleep(&pause,NULL);continue;
        }
        if(aligned){
            unsigned masks=__atomic_load_n(&s->request_masks,__ATOMIC_ACQUIRE);
            if(!masks){__atomic_store_n(&s->state,XZ_OC_READY,__ATOMIC_RELEASE);struct timespec idle={0,5000000};nanosleep(&idle,NULL);continue;}
            int64_t desired=__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE);
            int reverse=__atomic_load_n(&s->reverse,__ATOMIC_RELAXED);
            int64_t at=(int64_t)floor(((double)desired+s->offset)*stem_rate(s)/s->rate);
            int current=__atomic_load_n(&s->published,__ATOMIC_SEQ_CST);
            int32_t loop=__atomic_load_n(&s->loop_start,__ATOMIC_ACQUIRE);
            int64_t loop_at=(int64_t)floor(((double)loop+s->offset)*stem_rate(s)/s->rate)-9600;
            int loop_current=__atomic_load_n(&s->loop_published,__ATOMIC_SEQ_CST);
            struct window *pinned=&s->window[loop_current<0?2:loop_current];
            /* A covering loop refill also supplies current playback. Attempt it first,
             * but a borrowed loop slot must still allow the free current slot. */
            int loop_needs=loop>=0&&(!pinned->valid||pinned->first!=loop_at||pinned->masks!=masks);
            int loop_refill_covers=loop>=0&&(reverse?at-HEADROOM>=loop_at&&at<loop_at+WINDOW:
                at>=loop_at&&at+HEADROOM<loop_at+WINDOW);
            if(loop_needs&&loop_refill_covers){
                int ok=fill_loop_slot(s,loop_current,loop_at,masks);
                if(ok<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
                if(ok>0)pinned=&s->window[loop_current==2?3:2];
            }
            int loop_covered=pinned->valid&&pinned->masks==masks&&ahead(pinned,at,reverse);
            const struct window *now=current<0?NULL:&s->window[current];
            int refill=!loop_covered&&(!now||now->masks!=masks||(reverse?
                at>now->first+WINDOW-4800||at<now->first+WINDOW*2/3:
                (now->first>0&&at<now->first+4800)||at>now->first+WINDOW/3));
            if(refill){
                int candidate=current==0?1:0;
                int ok=fill_slot(s,candidate,reverse?at+9600-WINDOW:at-9600,masks);
                if(ok<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
                if(ok)__atomic_store_n(&s->published,candidate,__ATOMIC_SEQ_CST);
            }
            /* Unrelated loop preparation must not block current coverage publication. */
            if(loop_needs&&!loop_refill_covers&&fill_loop_slot(s,loop_current,loop_at,masks)<0){
                __atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;
            }
            int64_t latest=(int64_t)floor(((double)__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE)+s->offset)*stem_rate(s)/s->rate);
            int ready=0;
            for(unsigned i=0;i<4;i++){
                struct window *w=&s->window[i];
                ready|=w->valid&&w->masks==masks&&ahead(w,latest,reverse);
            }
            __atomic_store_n(&s->state,ready?XZ_OC_READY:XZ_OC_BUFFERING,__ATOMIC_RELEASE);
        }
        struct timespec delay={0,5000000};nanosleep(&delay,NULL);
    }
    /* Stopped on an error: a stick that goes is still answered, by closing the files, until the stream closes. */
    while(__atomic_load_n(&s->running,__ATOMIC_ACQUIRE)){
        gone_serve(s);struct timespec rest={0,20000000};nanosleep(&rest,NULL);
    }
    return NULL;
}
enum open_failure { OPEN_PERMANENT, OPEN_RESOURCE };
static struct xz_oc_stream *stream_open_once(const char *path,uint32_t rate,char *error,size_t capacity,enum open_failure *failure){
    *failure=OPEN_PERMANENT;
    if(rate<32000||rate>96000){snprintf(error,capacity,"Unsupported XZ reader rate");return NULL;}
    struct xz_oc_stream *s=calloc(1,sizeof(*s));if(!s){int saved=errno;if(saved==ENOMEM)*failure=OPEN_RESOURCE;snprintf(error,capacity,"STEM STREAM ALLOCATION FAILED: bytes=%zu errno=%d",sizeof(*s),saved);return NULL;}s->rate=rate;s->published=-1;s->loop_published=-1;s->loop_start=-1;s->last_position=-1;s->state=XZ_OC_ALIGNING;
    snprintf(s->source,sizeof s->source,"%s",path);
    if(xz_oc_open(path,&s->assets,error,capacity)){
        char declared[4096];
        if(!xz_oc_bundle(path,declared,sizeof(declared))){free(s);return NULL;}
        s->foreign=xz_foreign_open(path,error,capacity);if(!s->foreign){free(s);return NULL;}
    }
    s->role_count=s->foreign?xz_foreign_stem_count(s->foreign):3;
    s->source_rate=s->foreign?xz_foreign_rate(s->foreign):s->assets.sample_rate;
    const char *policy=getenv("XZ_MODS_STEMS_FLAT");
    s->flat_policy=!s->foreign&&!(policy&&policy[0]=='0'&&!policy[1]);
    const char *usb=getenv("XZ_MODS_STEMS_USB");
    s->ram_only=!s->foreign&&xz_oc_assets_pin_supported()&&!(usb&&usb[0]=='1'&&!usb[1]);
    /* Store first (see stream_worker): two compressed copies and two stores do not fit the mods' address zone. */
    const char *copy=getenv("XZ_MODS_STEMS_COPY");
    s->store_first=s->flat_policy&&s->ram_only&&!(copy&&copy[0]=='1'&&!copy[1]);
    /* No copy here: opening must not hold the preparation worker, and the track that is loading has the USB. */
    ram_bytes_publish(s);
    for(unsigned r=0;r<4;r++){float one=1;memcpy(&s->previous_levels[r],&one,4);}
#if defined(__arm__) && defined(__linux__)
    /* One mapping for all window buffers. Every mapping in the mods' address zone starts on its own step, and
       separate 1.5 MiB buffers took 4 MiB each on r42 (64 MiB for two streams). */
    s->window_block_bytes=(size_t)4u*(stem_count(s)-1)*WINDOW*2*sizeof(float);
    s->window_block=xz_lowzone_alloc(s->window_block_bytes);
    if(!s->window_block){int saved=errno;if(saved==ENOMEM)*failure=OPEN_RESOURCE;snprintf(error,capacity,"STEM WINDOW ALLOCATION FAILED: bytes=%zu errno=%d",s->window_block_bytes,saved);xz_oc_stream_close(s);return NULL;}
    for(unsigned i=0;i<4;i++)for(unsigned r=0;r<stem_count(s)-1;r++)
        s->window[i].pcm[r]=(float *)((char *)s->window_block+(size_t)(i*(stem_count(s)-1)+r)*WINDOW*2*sizeof(float));
#else
    for(unsigned i=0;i<4;i++)for(unsigned r=0;r<stem_count(s)-1;r++){
        s->window[i].pcm[r]=xz_lowzone_alloc(WINDOW*2*sizeof(float));
        if(!s->window[i].pcm[r]){int saved=errno;if(saved==ENOMEM)*failure=OPEN_RESOURCE;snprintf(error,capacity,"STEM WINDOW ALLOCATION FAILED: window=%u role=%u bytes=%zu errno=%d",i,r,(size_t)WINDOW*2*sizeof(float),saved);xz_oc_stream_close(s);return NULL;}
    }
#endif
    s->running=1;
    int thread_error=stem_worker_create(&s->worker,stream_worker,s);
    if(thread_error){if(thread_error==EAGAIN||thread_error==ENOMEM)*failure=OPEN_RESOURCE;snprintf(error,capacity,"STEM WORKER CREATE FAILED: rc=%d",thread_error);s->running=0;xz_oc_stream_close(s);return NULL;}
    return s;
}
struct xz_oc_stream *xz_oc_stream_open(const char *path,uint32_t rate,char *error,size_t capacity){
    enum open_failure failure;return stream_open_once(path,rate,error,capacity,&failure);
}
/* Preparation worker only: bounded recovery of specifically identified resource
 * failures. File/identity/codec errors never enter this path. */
struct xz_oc_stream *xz_oc_stream_open_retry(const char *path,uint32_t rate,char *error,size_t capacity,int (*cancelled)(void *),void *context,struct xz_oc_open_diagnostics *diagnostics){
    memset(diagnostics,0,sizeof(*diagnostics));
    const unsigned backoff_ms[2]={50,150};
    for(unsigned attempt=0;attempt<3;attempt++){
        if(cancelled(context))return NULL;
        enum open_failure failure;
        struct xz_oc_stream *s=stream_open_once(path,rate,error,capacity,&failure);
        if(cancelled(context)){xz_oc_stream_close(s);return NULL;}
        if(s||failure!=OPEN_RESOURCE)return s;
        snprintf(diagnostics->failure[diagnostics->failures++],sizeof(diagnostics->failure[0]),"%s",error);
        if(attempt==2){
            char detail[160];snprintf(detail,sizeof(detail),"%s",error);
            snprintf(error,capacity,"%s (3 attempts)",detail);return NULL;
        }
        for(unsigned waited=0;waited<backoff_ms[attempt];waited+=10){
            if(cancelled(context))return NULL;
            struct timespec remaining={0,10000000};
            while(nanosleep(&remaining,&remaining)&&errno==EINTR){if(cancelled(context))return NULL;}
        }
    }
    return NULL;
}
void xz_oc_stream_close(struct xz_oc_stream *s){
    if(!s)return;if(s->running){__atomic_store_n(&s->running,0,__ATOMIC_RELEASE);pthread_join(s->worker,NULL);}
    if(__atomic_load_n(&s->gone_asked,__ATOMIC_ACQUIRE)!=s->gone_seen){
        xz_oc_gone_done done=__atomic_load_n(&s->gone_done,__ATOMIC_RELAXED);
        s->gone_seen=__atomic_load_n(&s->gone_asked,__ATOMIC_ACQUIRE);
        if(done)done(__atomic_load_n(&s->gone_token,__ATOMIC_RELAXED),XZ_OC_GONE_CLOSED,"its track was unloaded");
    }
#if defined(__arm__) && defined(__linux__)
    xz_lowzone_release(s->window_block,s->window_block_bytes); /* in short pieces: see ram/lowzone.h */
#else
    for(unsigned i=0;i<4;i++)for(unsigned r=0;r<stem_count(s)-1;r++)xz_lowzone_free(s->window[i].pcm[r],WINDOW*2*sizeof(float));
#endif
    xz_oc_flat_destroy(s->flat);
    xz_oc_close(&s->assets);xz_foreign_close(s->foreign);free(s);
}
void xz_oc_stream_source_gone(struct xz_oc_stream *s,const char *prefix,xz_oc_gone_done done,uint32_t token){
    if(!s)return;
    __atomic_store_n(&s->gone_prefix,prefix,__ATOMIC_RELAXED);
    __atomic_store_n(&s->gone_done,done,__ATOMIC_RELAXED);
    __atomic_store_n(&s->gone_token,token,__ATOMIC_RELAXED);
    __atomic_add_fetch(&s->gone_asked,1,__ATOMIC_RELEASE);
}
size_t xz_oc_stream_ram_bytes(const struct xz_oc_stream *s){return s?__atomic_load_n(&s->ram_bytes,__ATOMIC_ACQUIRE):0;}
void xz_oc_stream_loop_start(struct xz_oc_stream *s,int32_t position){
    if(s)__atomic_store_n(&s->loop_start,position,__ATOMIC_RELEASE);
}
void xz_oc_stream_engaged(struct xz_oc_stream *s,int on){
    if(!s)return;
    if(on){__atomic_store_n(&s->engaged,1,__ATOMIC_RELEASE);return;}
    /* Only the step back to unity starts the decay: repeated unity levels do not push it on. */
    if(__atomic_exchange_n(&s->engaged,0,__ATOMIC_ACQ_REL)){uint32_t now=worker_ms();__atomic_store_n(&s->released_ms,now?now:1u,__ATOMIC_RELEASE);}
}
void xz_oc_stream_anchors(struct xz_oc_stream *s,const int32_t *frames,unsigned count){
    if(!s||(count&&!frames))return;
    if(count>XZ_OC_FLAT_ANCHORS-1)count=XZ_OC_FLAT_ANCHORS-1;
    __atomic_store_n(&s->anchor_count,0,__ATOMIC_RELEASE);
    for(unsigned i=0;i<count;i++)__atomic_store_n(&s->anchor[i],frames[i],__ATOMIC_RELAXED);
    __atomic_store_n(&s->anchor_count,count,__ATOMIC_RELEASE);
}
static struct window *find_window(struct xz_oc_stream *s,const struct xz_oc_plan *plan,double at,double span,int headroom){
    int reverse=__atomic_load_n(&s->reverse,__ATOMIC_RELAXED);
    int current=__atomic_load_n(&s->published,__ATOMIC_SEQ_CST);
    int loop=__atomic_load_n(&s->loop_published,__ATOMIC_SEQ_CST);
    int order[4]={current<0?0:current,loop<0?2:loop,loop==2?3:2,current==0?1:0};
    for(unsigned i=0;i<4;i++){
        struct window *w=&s->window[order[i]];
        if(!window_acquire(w))continue;
        double first=at-w->first,last=first+span;
        if(w->valid&&window_has(w,plan)&&first>=0&&last<WINDOW-1&&(!headroom||(reverse?first>=HEADROOM:WINDOW-last>=HEADROOM)))return w;
        window_release(w);
    }
    return NULL;
}
/* Per planned single: where it lies in a chunk's base (overcue_flat.h layout), and its gain. */
struct flat_render {float source,gain[3];size_t single[3];unsigned count;};
static struct flat_render flat_plan(const struct xz_oc_stream *s,const struct xz_oc_plan *p){
    struct flat_render out={.source=p->source};
    for(unsigned r=0;r<3;r++)if(p->mask[r]){
        unsigned single=p->mask[r]==1?0:p->mask[r]==2?1:2;
        out.single[out.count]=(size_t)single*XZ_OC_FLAT_SINGLE;
        out.gain[out.count++]=(float)(s->scale*p->gain[r]/xz_oc_mix_gain(&s->assets,p->mask[r])/32768.0);
    }
    return out;
}
static inline float flat_sample(const struct flat_render *p,const int16_t *base,size_t at,float native){
    float result=native*p->source;
    for(unsigned r=0;r<p->count;r++)result+=(float)base[p->single[r]+at]*p->gain[r];
    return result;
}
/* Audio thread. Every frame of the block is built: mix by deck frame, no window and no wait. The block is walked in
   runs that end at chunk edges, each read through the base its chunk had when the block entered the store. */
static size_t flat_mix(struct xz_oc_stream *s,const struct xz_oc_flat *flat,const int16_t *const *bases,float *pcm,size_t frames,int64_t position,
                       struct xz_oc_plan requested,struct xz_oc_plan old,const float previous[4],const float applied[4],uint32_t *out){
    int was_starved=__atomic_load_n(&s->starved,__ATOMIC_RELAXED);
    if(was_starved)old=(struct xz_oc_plan){.source=was_starved==XZ_OC_STARVE_UNITY?1:s->held_source};
    int changed=was_starved!=0;__atomic_store_n(&s->starved,0,__ATOMIC_RELAXED);
    for(unsigned r=0;r<stem_count(s);r++){changed|=previous[r]!=applied[r];uint32_t bits;memcpy(&bits,applied+r,4);__atomic_store_n(&s->previous_levels[r],bits,__ATOMIC_RELAXED);}
    if(out){out[1]=plan_masks(&requested);out[2]=plan_masks(&old);out[3]|=16u;memcpy(out+8,&requested.source,4);memcpy(out+9,requested.gain,8);memcpy(out+11,&old.source,4);out[15]=(uint32_t)was_starved;}
    if(requested.source==1&&!plan_masks(&requested)&&!changed){
        memcpy(s->last_output,pcm+(frames-1)*2,sizeof(s->last_output));s->last_output_valid=1;return 0;
    }
    struct flat_render now=flat_plan(s,&requested),before=flat_plan(s,&old);
    size_t ramp=frames<STARVE_RAMP_FRAMES?frames:STARVE_RAMP_FRAMES;
    /* Frames past the end of the prepared audio carry the native share only, as the windows do. */
    uint64_t first=(uint64_t)position,inside=first>=flat->frames?0:flat->frames-first;
    static const int16_t silence[2];
    for(size_t i=0;i<frames;){
      uint64_t frame=first+i;
      size_t run=XZ_OC_FLAT_CHUNK-(size_t)(frame%XZ_OC_FLAT_CHUNK);if(run>frames-i)run=frames-i;
      const int16_t *base=i<inside?bases[frame/XZ_OC_FLAT_CHUNK-first/XZ_OC_FLAT_CHUNK]:NULL;
      for(size_t end=i+run;i<end;i++){
       for(unsigned c=0;c<2;c++){
        float native=pcm[i*2+c],value,from;
        if(i<inside){size_t at=(size_t)((first+i)%XZ_OC_FLAT_CHUNK)*2+c;value=flat_sample(&now,base,at,native);from=changed&&i<ramp?flat_sample(&before,base,at,native):0;}
        else{value=native*now.source+(float)silence[c];from=native*before.source;}
        if(changed&&i<ramp){float f=(float)(i+1)/(float)ramp;value=from*(1-f)+value*f;}
        if(out){float peak;memcpy(&peak,out+14,4);if(fabsf(value)>peak){peak=fabsf(value);memcpy(out+14,&peak,4);}out[12]+=value>1||value< -1;out[13]+=!isfinite(value);}
        pcm[i*2+c]=value>1?1:value< -1?-1:value;
       }
      }
    }
    memcpy(s->last_output,pcm+(frames-1)*2,sizeof(s->last_output));s->last_output_valid=1;
    __atomic_fetch_add(&s->blocks,1,__ATOMIC_RELAXED);
    return frames;
}
size_t xz_oc_stream_mix4_trace(struct xz_oc_stream *s,float *pcm,size_t frames,int64_t position,const float levels[4],uint32_t out[16]){
    if(out){memset(out,0,64);out[4]=UINT32_MAX;}
    if(!s||!pcm||!frames||frames>65536||position<0)return 0;
    struct xz_oc_plan requested;
    if(stream_plan(s,levels,&requested))return 0;
    if(s->last_position>=0){
        int64_t step=position-s->last_position;int backward=step<0&&step>-16384,forward=step>0&&step<16384;
        int run=s->direction_run;
        run=backward?(run<0?run-1:-1):forward?(run>0?run+1:1):0;
        s->direction_run=run;
        if(run<=-DIRECTION_BLOCKS)__atomic_store_n(&s->reverse,1,__ATOMIC_RELAXED);
        if(run>=DIRECTION_BLOCKS)__atomic_store_n(&s->reverse,0,__ATOMIC_RELAXED);
    }
    s->last_position=position;
    if(out)out[0]=plan_masks(&requested);
    __atomic_store_n(&s->request_masks,plan_masks(&requested),__ATOMIC_RELEASE);
    __atomic_store_n(&s->desired,position,__ATOMIC_RELEASE);
    int expected=0;
    if(__atomic_compare_exchange_n(&s->capture_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        int64_t end=s->capture_position+(int64_t)s->capture_count;
        if(!s->capture_count||position>end||position+(int64_t)frames<=s->capture_position){s->capture_position=position;s->capture_count=0;end=position;}
        if(position<=end&&position+(int64_t)frames>end){
            size_t skip=(size_t)(end-position),n=frames-skip;if(n>CAPTURE-s->capture_count)n=CAPTURE-s->capture_count;
            memcpy(s->capture+s->capture_count*2,pcm+skip*2,n*2*sizeof(float));s->capture_count+=n;
        }
        __atomic_store_n(&s->capture_state,s->capture_count>=1024?2:0,__ATOMIC_RELEASE);
    }
    int aligned=__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==3;
    if(!aligned&&plan_masks(&requested)){if(out)out[3]=1;return 0;}
    float previous[4]={1,1,1,1},applied[4]={1,1,1,1};
    for(unsigned r=0;r<stem_count(s);r++){uint32_t bits=__atomic_load_n(&s->previous_levels[r],__ATOMIC_RELAXED);memcpy(previous+r,&bits,4);applied[r]=levels[r];}
    struct xz_oc_plan old; stream_plan(s,previous,&old);
    double at=aligned?((double)position+s->offset)*stem_rate(s)/s->rate:0;
    double span=(double)(frames-1)*stem_rate(s)/s->rate;
    struct xz_oc_flat *flat=aligned?__atomic_load_n(&s->flat,__ATOMIC_SEQ_CST):NULL;
    const int16_t *bases[XZ_OC_FLAT_BLOCK_CHUNKS];
    /* The worker may give a segment back at any moment: stay inside the store until the block's last sample is
       read, and read only through the bases taken here (overcue_flat.h, reader protocol). */
    if(flat)xz_oc_flat_enter(flat);
    /* Move to the store when a second ahead is built, stay while this block is: no flapping at the edge of
       a store that is still being built. */
    int reversed=__atomic_load_n(&s->reverse,__ATOMIC_RELAXED);
    int use_flat=flat&&xz_oc_flat_bases(flat,position,frames,bases)&&(s->flat_active||
        xz_oc_flat_covers_toward(flat,reversed?position+(int64_t)frames-1:position,s->rate,reversed));
    s->flat_active=use_flat;
    if(use_flat){
        size_t mixed=flat_mix(s,flat,bases,pcm,frames,position,requested,old,previous,applied,out);
        xz_oc_flat_leave(flat);return mixed;
    }
    if(flat)xz_oc_flat_leave(flat);
    int switching=plan_masks(&requested)!=plan_masks(&old);
    struct window *w=find_window(s,&requested,at,span,switching);
    int error=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE)==XZ_OC_ERROR;
    if(error&&w){window_release(w);w=NULL;}
    int starved=0,fallback=0;
    if(plan_masks(&requested)&&!w){
        fallback=1;
        __atomic_fetch_add(&s->misses,1,__ATOMIC_RELAXED);
        if(!error)__atomic_store_n(&s->state,XZ_OC_BUFFERING,__ATOMIC_RELEASE);
        if(!error)w=find_window(s,&old,at,span,0);
        requested=old;memcpy(applied,previous,sizeof(applied));
        if(out)out[3]|=2;
        if(plan_masks(&old)&&!w){
            starved=1;
        }
    }
    int was_starved=__atomic_load_n(&s->starved,__ATOMIC_RELAXED);
    float previous_source=was_starved==XZ_OC_STARVE_UNITY?1:s->held_source;
    if(starved){
        if(!was_starved){
            s->starve_frames=0;s->held_source=old.source;
            __atomic_fetch_add(&s->starve_runs,1,__ATOMIC_RELAXED);
        }
        s->starve_frames=frames>UINT32_MAX-s->starve_frames?UINT32_MAX:s->starve_frames+(uint32_t)frames;
        starved=error||(uint64_t)s->starve_frames*1000>=(uint64_t)s->rate*STARVE_UNITY_AFTER_MS?
            XZ_OC_STARVE_UNITY:XZ_OC_STARVE_HELD;
        requested=(struct xz_oc_plan){.source=starved==XZ_OC_STARVE_UNITY?1:s->held_source};
        __atomic_store_n(&s->starve_fallback,starved,__ATOMIC_RELAXED);
        __atomic_store_n(&s->starve_last_frames,s->starve_frames,__ATOMIC_RELAXED);
        if(s->starve_frames>__atomic_load_n(&s->starve_max_frames,__ATOMIC_RELAXED))
            __atomic_store_n(&s->starve_max_frames,s->starve_frames,__ATOMIC_RELAXED);
    }
    if(was_starved)old=(struct xz_oc_plan){.source=previous_source};
    else if(!window_has(w,&old))old=(struct xz_oc_plan){.source=old.source};
    if(out){
        out[1]=plan_masks(&requested);out[2]=plan_masks(&old);out[3]|=(starved?4u:0u)|(error?8u:0u);
        out[4]=w?(uint32_t)(w-s->window):UINT32_MAX;out[5]=w?w->masks:0;
        if(w)memcpy(out+6,&w->first,8);
        memcpy(out+8,&requested.source,4);memcpy(out+9,requested.gain,8);memcpy(out+11,&old.source,4);out[15]=(uint32_t)was_starved;
    }
    int changed=was_starved!=starved;__atomic_store_n(&s->starved,starved,__ATOMIC_RELAXED);
    for(unsigned r=0;r<stem_count(s);r++){changed|=previous[r]!=applied[r];uint32_t bits;memcpy(&bits,applied+r,4);__atomic_store_n(&s->previous_levels[r],bits,__ATOMIC_RELAXED);}
    if(requested.source==1&&!plan_masks(&requested)&&!changed){
        memcpy(s->last_output,pcm+(frames-1)*2,sizeof(s->last_output));s->last_output_valid=1;
        if(w)window_release(w);return 0;
    }
    double first=w?at-w->first:0;
    struct render_plan render=render_plan(s,w,&requested),previous_render=render_plan(s,w,&old);
    int cache=0;uint32_t unlocked=0;
    if(!changed&&!starved&&!was_starved&&!fallback&&w)
        cache=__atomic_compare_exchange_n(&s->mix_cache_busy,&unlocked,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
    if(cache){
        uint32_t bits[4];memcpy(bits,applied,sizeof(bits));
        if(!s->mix_cache_epoch||memcmp(bits,s->mix_cache_levels,sizeof(bits))){
            if(!++s->mix_cache_epoch){memset(s->mix_cache,0,sizeof(s->mix_cache));s->mix_cache_epoch=1;}
            memcpy(s->mix_cache_levels,bits,sizeof(bits));
        }
    }
    size_t ramp=frames<STARVE_RAMP_FRAMES?frames:STARVE_RAMP_FRAMES;
    /* Decay only the last selected output sample into the held coefficient.
     * No old timeline buffer or uncovered native mix is replayed on a seek. */
    int entering=starved&&!was_starved&&s->last_output_valid;
    for(size_t i=0;i<frames;i++){
      int64_t absolute=position+(int64_t)i;
      struct mix_frame *entry=cache?&s->mix_cache[(uint64_t)absolute&(MIX_CACHE_FRAMES-1u)]:NULL;
      int hit=entry&&entry->epoch==s->mix_cache_epoch&&entry->position==absolute&&entry->window_first==w->first&&
          !memcmp(entry->native,pcm+i*2,2*sizeof(float));
      double at=hit?0:first+((double)i*stem_rate(s))/s->rate;
      float original[2],raw[2],mixed[2];memcpy(original,pcm+i*2,sizeof(original));
      for(unsigned c=0;c<2;c++){
        float native=pcm[i*2+c];
        float value=hit?entry->raw[c]:planned_sample(w,&render,at,(int)c,native);
        if(!hit&&changed&&i<ramp){float f=(float)(i+1)/(float)ramp;float from=entering?s->last_output[c]:planned_sample(w,&previous_render,at,(int)c,native);value=from*(1-f)+value*f;}
        if(out){float peak;memcpy(&peak,out+14,4);if(fabsf(value)>peak){peak=fabsf(value);memcpy(out+14,&peak,4);}out[12]+=value>1||value< -1;out[13]+=!isfinite(value);}
        raw[c]=value;mixed[c]=hit?entry->mixed[c]:(value>1?1:value< -1?-1:value);pcm[i*2+c]=mixed[c];
      }
      if(entry&&!hit){entry->position=absolute;entry->window_first=w->first;entry->epoch=s->mix_cache_epoch;memcpy(entry->native,original,sizeof(original));memcpy(entry->raw,raw,sizeof(raw));memcpy(entry->mixed,mixed,sizeof(mixed));}
    }
    memcpy(s->last_output,pcm+(frames-1)*2,sizeof(s->last_output));s->last_output_valid=1;
    if(cache)__atomic_store_n(&s->mix_cache_busy,0,__ATOMIC_RELEASE);
    __atomic_fetch_add(&s->blocks,1,__ATOMIC_RELAXED);
    if(w)window_release(w);return frames;
}

void xz_oc_stream_status(struct xz_oc_stream *s,struct xz_oc_status *out){
    memset(out,0,sizeof(*out));if(!s){out->state=XZ_OC_ERROR;return;}
    out->state=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE);
    if(__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==3){out->lag=(int)lround(s->offset);out->correlation=s->score;}
    out->starve_fallback=__atomic_load_n(&s->starve_fallback,__ATOMIC_RELAXED);
    out->starvation=__atomic_load_n(&s->starved,__ATOMIC_RELAXED);
    out->starve_runs=__atomic_load_n(&s->starve_runs,__ATOMIC_RELAXED);
    out->starve_last_frames=__atomic_load_n(&s->starve_last_frames,__ATOMIC_RELAXED);
    out->starve_max_frames=__atomic_load_n(&s->starve_max_frames,__ATOMIC_RELAXED);
    if(s->rate){out->starve_last_ms=(uint32_t)((uint64_t)out->starve_last_frames*1000/s->rate);
        out->starve_max_ms=(uint32_t)((uint64_t)out->starve_max_frames*1000/s->rate);}
    out->blocks=__atomic_load_n(&s->blocks,__ATOMIC_RELAXED);out->misses=__atomic_load_n(&s->misses,__ATOMIC_RELAXED);
    out->waiting_ram=__atomic_load_n(&s->waiting_ram,__ATOMIC_RELAXED);
    out->flat_chunks=__atomic_load_n(&s->flat_chunks_now,__ATOMIC_RELAXED);out->flat_built=__atomic_load_n(&s->flat_built_now,__ATOMIC_RELAXED);
    out->flat_ms=__atomic_load_n(&s->flat_took_ms,__ATOMIC_RELAXED);
    out->store_held=__atomic_load_n(&s->store_held,__ATOMIC_RELAXED);out->store_grant=__atomic_load_n(&s->store_grant,__ATOMIC_RELAXED);
    out->store_need=__atomic_load_n(&s->store_need,__ATOMIC_RELAXED);out->store_class=__atomic_load_n(&s->store_class,__ATOMIC_RELAXED);
    out->store_lead_ms=__atomic_load_n(&s->store_lead_ms,__ATOMIC_RELAXED);out->store_behind_ms=__atomic_load_n(&s->store_behind_ms,__ATOMIC_RELAXED);
    out->stems_whole=__atomic_load_n(&s->stems_whole,__ATOMIC_RELAXED);
}
int xz_oc_stream_neutral(const struct xz_oc_stream *s){
    if(!s||__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)!=3||
       __atomic_load_n(&s->request_masks,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&s->starved,__ATOMIC_RELAXED))return 0;
    for(unsigned r=0;r<stem_count(s);r++)if(__atomic_load_n(&s->previous_levels[r],__ATOMIC_RELAXED)!=UINT32_C(0x3f800000))return 0;
    return 1;
}
void xz_oc_stream_position(struct xz_oc_stream *s,int32_t position){
    if(s&&position>=0)__atomic_store_n(&s->desired,(int64_t)position,__ATOMIC_RELEASE);
}
size_t xz_oc_stream_mix_trace(struct xz_oc_stream *s,float *pcm,size_t frames,int64_t position,const float levels[3],uint32_t out[16]){
    if(!levels)return 0;float four[4]={levels[0],levels[1],levels[2],1};
    return xz_oc_stream_mix4_trace(s,pcm,frames,position,four,out);
}
size_t xz_oc_stream_mix(struct xz_oc_stream *s,float *pcm,size_t frames,int64_t position,const float levels[3]){
    return xz_oc_stream_mix_trace(s,pcm,frames,position,levels,NULL);
}
void xz_oc_stream_trace_state(struct xz_oc_stream *s,uint32_t out[10]){
    memset(out,0,40);if(!s)return;
    out[0]=__atomic_load_n(&s->request_masks,__ATOMIC_ACQUIRE);
    out[1]=__atomic_load_n(&s->misses,__ATOMIC_RELAXED);
    out[2]=(uint32_t)__atomic_load_n(&s->state,__ATOMIC_ACQUIRE)|((uint32_t)__atomic_load_n(&s->starved,__ATOMIC_RELAXED)<<16);
    int slot=__atomic_load_n(&s->published,__ATOMIC_SEQ_CST);out[3]=(uint32_t)slot;
    if(__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==3){memcpy(out+4,&s->scale,8);memcpy(out+6,&s->offset,8);}
    if(slot>=0&&slot<4){struct window *w=&s->window[slot];if(window_acquire(w)){memcpy(out+8,&w->first,8);window_release(w);}}
}
int xz_oc_stream_has_wave(const struct xz_oc_stream *s,unsigned role){return s&&!s->foreign&&role<3&&s->assets.wave_count[role]>0;}
void xz_oc_stream_wave(struct xz_oc_stream *s,unsigned role,unsigned char *bins,size_t count,float *progress){
    if(!s||s->foreign||role>=3||!bins||!count)return;uint32_t n=s->assets.wave_count[role];
    memset(bins,0,count);if(!n)return;
    for(size_t i=0;i<count;i++){
        size_t first=(uint64_t)i*n/count,last=(uint64_t)(i+1)*n/count;if(last<=first)last=first+1;
        for(size_t j=first;j<last&&j<n;j++)if(s->assets.wave[role][j]>bins[i])bins[i]=s->assets.wave[role][j];
    }
    if(progress){double at=(double)__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE)*stem_rate(s)/s->rate/stem_frames(s);*progress=(float)(at<0?0:at>1?1:at);}
}
