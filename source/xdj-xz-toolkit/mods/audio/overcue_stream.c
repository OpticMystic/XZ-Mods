/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "overcue.h"
#include "foreign_stems.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__) && defined(__arm__)
#include <sched.h>
#include <unistd.h>
#include <sys/prctl.h>
#endif

#define WINDOW XZ_OC_WINDOW_FRAMES
#define CAPTURE 2048u
#define SEARCH 8192
/* At 44.1 kHz: <=2.90 ms fades; allow ordinary seek refill 1.5 s before fail-open. */
#define STARVE_RAMP_FRAMES 128u
#define STARVE_UNITY_AFTER_MS 1500u
int xz_oc_worker_schedule(void){
#if defined(__linux__) && defined(__arm__)
    if(sysconf(_SC_NPROCESSORS_ONLN)<4)return -1;
    cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(1,&mask);CPU_SET(2,&mask);
    struct sched_param priority={0};
    if(pthread_setschedparam(pthread_self(),SCHED_OTHER,&priority))return -1;
    if(pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask))return -1;
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
    int loop_published;
    int32_t loop_start;
    int starved, starve_fallback; /* enum xz_oc_starvation, audio-thread writer */
    uint32_t starve_frames, starve_last_frames, starve_max_frames, starve_runs;
    float held_source, last_output[2];
    int last_output_valid;
    double offset, scale;
    int confirmations, attempts;
    float score;
    uint32_t blocks, misses;
    uint32_t previous_levels[4], request_masks;
    uint32_t mix_cache_busy,mix_cache_epoch,mix_cache_levels[4];
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
        if(!s->foreign&&(w->masks&15)&&((w->masks>>4)&15))spawned[r]=pthread_create(threads+r,NULL,refill_role,jobs+r)==0;
        if(!spawned[r])refill_role(jobs+r);
    }
    int ok=1;for(unsigned r=0;r<count;r++){if(spawned[r])pthread_join(threads[r],NULL);if(jobs[r].result)ok=0;}return ok;
}
static int fill_slot(struct xz_oc_stream *s,int slot,int64_t first,unsigned masks){
    struct window *w=&s->window[slot];int idle=0;
    if(!__atomic_compare_exchange_n(&w->access,&idle,-1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    w->valid=0;w->first=first;w->masks=masks;
    int ok=refill_window(s,w);w->valid=ok;
    __atomic_store_n(&w->access,0,__ATOMIC_RELEASE);
    return ok?1:-1;
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
        if(!aligned&&__atomic_load_n(&s->capture_state,__ATOMIC_ACQUIRE)==2){
            int result=align(s);
            if(result<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
            if(result>0){aligned=1;__atomic_store_n(&s->capture_state,3,__ATOMIC_RELEASE);}
            else { s->capture_count=0; __atomic_store_n(&s->capture_state,0,__ATOMIC_RELEASE); }
        }
        if(aligned){
            unsigned masks=__atomic_load_n(&s->request_masks,__ATOMIC_ACQUIRE);
            if(!masks){__atomic_store_n(&s->state,XZ_OC_READY,__ATOMIC_RELEASE);struct timespec idle={0,5000000};nanosleep(&idle,NULL);continue;}
            int64_t desired=__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE);
            int64_t at=(int64_t)floor(((double)desired+s->offset)*stem_rate(s)/s->rate);
            int current=__atomic_load_n(&s->published,__ATOMIC_SEQ_CST);
            int32_t loop=__atomic_load_n(&s->loop_start,__ATOMIC_ACQUIRE);
            int64_t loop_at=(int64_t)floor(((double)loop+s->offset)*stem_rate(s)/s->rate)-9600;
            int loop_current=__atomic_load_n(&s->loop_published,__ATOMIC_SEQ_CST);
            struct window *pinned=&s->window[loop_current<0?2:loop_current];
            if(loop>=0&&(!pinned->valid||pinned->first!=loop_at||pinned->masks!=masks)){
                int candidate=loop_current==2?3:2;
                int ok=fill_slot(s,candidate,loop_at,masks);
                if(ok>0){__atomic_store_n(&s->loop_published,candidate,__ATOMIC_SEQ_CST);pinned=&s->window[candidate];}
                if(ok<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
            }
            int loop_covered=pinned->valid&&pinned->masks==masks&&at>=pinned->first&&at+72000<pinned->first+WINDOW;
            int refill=!loop_covered&&(current<0||s->window[current].masks!=masks||
                (s->window[current].first>0&&at<s->window[current].first+4800)||at>s->window[current].first+WINDOW/3);
            if(refill){
                int candidate=current==0?1:0;
                int ok=fill_slot(s,candidate,at-9600,masks);
                if(ok<0){__atomic_store_n(&s->state,XZ_OC_ERROR,__ATOMIC_RELEASE);break;}
                if(ok)__atomic_store_n(&s->published,candidate,__ATOMIC_SEQ_CST);
            }
            int64_t latest=(int64_t)floor(((double)__atomic_load_n(&s->desired,__ATOMIC_ACQUIRE)+s->offset)*stem_rate(s)/s->rate);
            int ready=0;
            for(unsigned i=0;i<4;i++){
                struct window *w=&s->window[i];
                ready|=w->valid&&w->masks==masks&&latest>=w->first&&latest+72000<w->first+WINDOW;
            }
            __atomic_store_n(&s->state,ready?XZ_OC_READY:XZ_OC_BUFFERING,__ATOMIC_RELEASE);
        }
        struct timespec delay={0,5000000};nanosleep(&delay,NULL);
    }return NULL;
}
struct xz_oc_stream *xz_oc_stream_open(const char *path,uint32_t rate,char *error,size_t capacity){
    if(rate<32000||rate>96000){snprintf(error,capacity,"Unsupported XZ reader rate");return NULL;}
    struct xz_oc_stream *s=calloc(1,sizeof(*s));if(!s)return NULL;s->rate=rate;s->published=-1;s->loop_published=-1;s->loop_start=-1;s->state=XZ_OC_ALIGNING;
    if(xz_oc_open(path,&s->assets,error,capacity)){
        char declared[4096];
        if(!xz_oc_bundle(path,declared,sizeof(declared))){free(s);return NULL;}
        s->foreign=xz_foreign_open(path,error,capacity);if(!s->foreign){free(s);return NULL;}
    }
    s->role_count=s->foreign?xz_foreign_stem_count(s->foreign):3;
    s->source_rate=s->foreign?xz_foreign_rate(s->foreign):s->assets.sample_rate;
    if(!s->foreign&&xz_oc_assets_pin(&s->assets)<0){snprintf(error,capacity,"STEM RAM HANDOFF FAILED");xz_oc_stream_close(s);return NULL;}
    for(unsigned r=0;r<4;r++){float one=1;memcpy(&s->previous_levels[r],&one,4);}
    for(unsigned i=0;i<4;i++)for(unsigned r=0;r<stem_count(s)-1;r++){
        s->window[i].pcm[r]=malloc(WINDOW*2*sizeof(float));
        if(!s->window[i].pcm[r]){xz_oc_stream_close(s);return NULL;}
    }
    s->running=1;
    if(pthread_create(&s->worker,NULL,stream_worker,s)){s->running=0;xz_oc_stream_close(s);return NULL;}
    return s;
}
void xz_oc_stream_close(struct xz_oc_stream *s){
    if(!s)return;if(s->running){__atomic_store_n(&s->running,0,__ATOMIC_RELEASE);pthread_join(s->worker,NULL);}
    for(unsigned i=0;i<4;i++)for(unsigned r=0;r<stem_count(s)-1;r++)free(s->window[i].pcm[r]);
    xz_oc_close(&s->assets);xz_foreign_close(s->foreign);free(s);
}
size_t xz_oc_stream_ram_bytes(const struct xz_oc_stream *s){return s?xz_oc_assets_ram_bytes(&s->assets):0;}
void xz_oc_stream_loop_start(struct xz_oc_stream *s,int32_t position){
    if(s)__atomic_store_n(&s->loop_start,position,__ATOMIC_RELEASE);
}
static struct window *find_window(struct xz_oc_stream *s,const struct xz_oc_plan *plan,double at,double span,int headroom){
    int current=__atomic_load_n(&s->published,__ATOMIC_SEQ_CST);
    int loop=__atomic_load_n(&s->loop_published,__ATOMIC_SEQ_CST);
    int order[4]={current<0?0:current,loop<0?2:loop,loop==2?3:2,current==0?1:0};
    for(unsigned i=0;i<4;i++){
        struct window *w=&s->window[order[i]];
        if(!window_acquire(w))continue;
        double first=at-w->first,last=first+span;
        if(w->valid&&window_has(w,plan)&&first>=0&&last<WINDOW-1&&(!headroom||WINDOW-last>=72000))return w;
        window_release(w);
    }
    return NULL;
}
size_t xz_oc_stream_mix4_trace(struct xz_oc_stream *s,float *pcm,size_t frames,int64_t position,const float levels[4],uint32_t out[16]){
    if(out){memset(out,0,64);out[4]=UINT32_MAX;}
    if(!s||!pcm||!frames||frames>65536||position<0)return 0;
    struct xz_oc_plan requested;
    if(plan_levels(levels,stem_count(s),&requested))return 0;
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
    struct xz_oc_plan old; plan_levels(previous,stem_count(s),&old);
    double at=aligned?((double)position+s->offset)*stem_rate(s)/s->rate:0;
    double span=(double)(frames-1)*stem_rate(s)/s->rate;
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
