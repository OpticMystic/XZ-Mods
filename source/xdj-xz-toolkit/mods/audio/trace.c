/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "trace.h"
#include <pthread.h>
#include "../xz_thread.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "../ram/lowzone.h"
#include <unistd.h>
#include <sys/syscall.h>
#include "../clock_compat.h"
#include <fcntl.h>
#include <errno.h>

#define TRACE_FRAMES 262144u
#define TRACE_RECORDS 8192u
struct trace_record {uint32_t time_low,time_high,tid,first,saved;struct xz_audio_trace_context context;};
struct trace_ring {uint32_t busy,dropped,count,cursor;struct trace_record records[TRACE_RECORDS];float pcm[TRACE_FRAMES*2];};
struct trace_capture {uint32_t version,enabled,frames,records,record_bytes,ring_bytes;};
__attribute__((visibility("default"))) struct trace_capture xz_audio_trace_v1={.version=2,.frames=TRACE_FRAMES,.records=TRACE_RECORDS,.record_bytes=sizeof(struct trace_record),.ring_bytes=sizeof(struct trace_ring)};
enum { TRACE_DORMANT,TRACE_ARMED,TRACE_QUIESCING,TRACE_FROZEN,TRACE_EXPORTED,TRACE_ERROR };
struct trace_control {uint32_t version,state,error,exports;};
__attribute__((visibility("default"))) struct trace_control xz_audio_trace_control_v1={.version=1};
static void state(unsigned state,int error){__atomic_store_n(&xz_audio_trace_control_v1.error,(uint32_t)error,__ATOMIC_RELAXED);__atomic_store_n(&xz_audio_trace_control_v1.state,state,__ATOMIC_RELEASE);}
/* Two decks by four stages: 27.75 MiB. As a static array that was over half of the runtime's 45 MiB image,
   reserved in the player's scarce address room on every unit for a diagnostic that only a test bench arms.
   The rings are now taken from the low zone by the worker when the diagnostic is armed (the `arm` command),
   and kept from then on. A unit whose loader only names XZ_AUDIO_TRACE_DIR, and never arms, pays nothing.
   Nothing can record before that: recording needs `enabled`, which the worker sets after the rings exist. */
static struct trace_ring (*trace_rings)[XZ_TRACE_STAGES];
#define TRACE_RINGS_BYTES (2u*XZ_TRACE_STAGES*sizeof(struct trace_ring))
#define CAPTURE_BYTES (sizeof(xz_audio_trace_v1)+TRACE_RINGS_BYTES)
static pthread_t worker;
static int running,created;
static uint32_t auto_trigger_ms;
static char directory[256];
static _Thread_local uint32_t thread_id;
static _Thread_local uint32_t source_calls;

uint64_t xz_audio_trace_time(void){
    struct timespec t;
    /* Direct syscall preserves compatibility with the controller's old libc. */
    if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*1000000u+(uint32_t)t.tv_nsec/1000u;
}
int xz_audio_trace_enabled(void){return __atomic_load_n(&xz_audio_trace_v1.enabled,__ATOMIC_ACQUIRE)!=0;}
int xz_audio_trace_source_sample(void){return (++source_calls%67u)==0;}
void xz_audio_trace_record(unsigned deck,unsigned stage,const float *pcm,const struct xz_audio_trace_context *context){
    if(!xz_audio_trace_enabled()||!trace_rings||deck>=2||stage>=XZ_TRACE_STAGES||!context)return;
    struct trace_ring *ring=&trace_rings[deck][stage];uint32_t idle=0;
    if(!__atomic_compare_exchange_n(&ring->busy,&idle,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){__atomic_add_fetch(&ring->dropped,1,__ATOMIC_RELAXED);return;}
    if(!xz_audio_trace_enabled()){__atomic_store_n(&ring->busy,0,__ATOMIC_RELEASE);return;}
    if(!thread_id)thread_id=(uint32_t)syscall(SYS_gettid);
    uint64_t at=xz_audio_trace_time();uint32_t count=pcm?context->frames:0;
    if(count>TRACE_FRAMES)count=TRACE_FRAMES;
    if(count){
        uint32_t first=ring->cursor%TRACE_FRAMES,tail=TRACE_FRAMES-first;if(tail>count)tail=count;
        memcpy(ring->pcm+first*2,pcm,tail*2*sizeof(float));
        if(count>tail)memcpy(ring->pcm,pcm+tail*2,(count-tail)*2*sizeof(float));
    }
    ring->records[ring->count%TRACE_RECORDS]=(struct trace_record){(uint32_t)at,(uint32_t)(at>>32),thread_id,ring->cursor,count,*context};
    ring->cursor+=count;ring->count++;
    uint32_t heavy_clip=context->frames<128u?context->frames:128u;
    if(stage==XZ_TRACE_STEMS&&(((context->mix[3]&4u)&&!context->mix[15])||context->mix[13]||(heavy_clip&&context->mix[12]>=heavy_clip))){
        uint32_t idle_trigger=0,now=(uint32_t)(at/1000u);if(!now)now=1;
        __atomic_compare_exchange_n(&auto_trigger_ms,&idle_trigger,now,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
    }
    __atomic_store_n(&ring->busy,0,__ATOMIC_RELEASE);
}
static int write_all(int fd,const void *buffer,size_t bytes){
    const unsigned char *data=buffer;size_t done=0;
    while(done<bytes){ssize_t n=write(fd,data+done,bytes-done);if(n<0&&errno==EINTR)continue;if(n<=0){if(!n)errno=EIO;return -1;}done+=(size_t)n;}
    return 0;
}
static void report(const char *state,int error,const char *capture){
    char path[320],temporary[320],message[512];
    snprintf(path,sizeof(path),"%s/status",directory);snprintf(temporary,sizeof(temporary),"%s/status.part",directory);
    int n=snprintf(message,sizeof(message),"%s errno=%d bytes=%zu capture=%s\n",state,error,CAPTURE_BYTES,capture?capture:"none");
    int fd=open(temporary,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd>=0){int ok=write_all(fd,message,(size_t)n);int saved=errno;if(close(fd)&&!ok){ok=-1;saved=errno;}if(!ok)rename(temporary,path);errno=saved;}
}
static int quiesce_capture(void){
    if(!trace_rings){state(TRACE_FROZEN,0);return 0;}   /* never armed: nothing is recording */
    state(TRACE_QUIESCING,0);
    __atomic_store_n(&xz_audio_trace_v1.enabled,0,__ATOMIC_RELEASE);
    int busy=1;
    for(unsigned attempt=0;attempt<500&&busy;attempt++){
        busy=0;for(unsigned d=0;d<2;d++)for(unsigned s=0;s<XZ_TRACE_STAGES;s++)busy|=__atomic_load_n(&trace_rings[d][s].busy,__ATOMIC_ACQUIRE)!=0;
        if(busy)usleep(1000);
    }
    if(busy){state(TRACE_ERROR,EBUSY);return -1;}
    state(TRACE_FROZEN,0);return 0;
}
static int export_capture(unsigned *serial){
    if(!trace_rings){report("ERROR_NOT_ARMED",EINVAL,NULL);errno=EINVAL;return -1;}
    char path[320],temporary[328];int fd=-1;
    for(unsigned attempt=0;attempt<100;attempt++){
        snprintf(path,sizeof(path),"%s/capture-%ld-%llu-%u.bin",directory,(long)getpid(),(unsigned long long)xz_audio_trace_time(),(*serial)++);
        snprintf(temporary,sizeof(temporary),"%s.part",path);
        fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd>=0||errno!=EEXIST)break;
    }
    if(fd<0){report("ERROR_OPEN",errno,NULL);return -1;}
    int ok=write_all(fd,&xz_audio_trace_v1,sizeof(xz_audio_trace_v1));
    if(!ok)ok=write_all(fd,trace_rings,TRACE_RINGS_BYTES);
    int saved=errno;
    if(close(fd)&&!ok){ok=-1;saved=errno;}
    if(ok){report("ERROR_WRITE",saved,temporary);return -1;}
    if(link(temporary,path)){report("ERROR_COMMIT",errno,temporary);return -1;}
    unlink(temporary);report("EXPORTED",0,path);
    __atomic_add_fetch(&xz_audio_trace_control_v1.exports,1,__ATOMIC_RELAXED);state(TRACE_EXPORTED,0);return 0;
}
static int request_file(const char *path){struct stat status;return !lstat(path,&status)&&S_ISREG(status.st_mode);}
/* File commands belong to this worker, never the audio callback. Freeze only
   quiesces RAM: exporting a full capture is a separate explicit stopped-playback step. */
static void *drain(void *unused){
    (void)unused;char request[320],arm[320],retry[320],dump[320];unsigned serial=0;int frozen=0,exported=0;
    snprintf(request,sizeof(request),"%s/freeze",directory);snprintf(arm,sizeof(arm),"%s/arm",directory);
    snprintf(retry,sizeof(retry),"%s/retry",directory);snprintf(dump,sizeof(dump),"%s/dump",directory);
    while(__atomic_load_n(&running,__ATOMIC_ACQUIRE)){
        int manual=request_file(request),retrying=request_file(retry);
        if(manual)unlink(request);if(retrying)unlink(retry);
        uint32_t trigger=__atomic_load_n(&auto_trigger_ms,__ATOMIC_ACQUIRE);
        int automatic=trigger&&(uint32_t)(xz_audio_trace_time()/1000u-trigger)>=250u;
        if(manual||automatic||(retrying&&frozen)){
            frozen=1;exported=0;
            quiesce_capture();
            __atomic_store_n(&auto_trigger_ms,0,__ATOMIC_RELEASE);
        }
        if(request_file(dump)){
            unlink(dump);
            if(frozen&&!quiesce_capture()){
                if(!export_capture(&serial))exported=1;
                else state(TRACE_ERROR,errno);
            }else if(!frozen){state(xz_audio_trace_enabled()?TRACE_ARMED:TRACE_DORMANT,EINVAL);report("ERROR_NOT_FROZEN",EINVAL,NULL);}
        }
        if(request_file(arm)){
            unlink(arm);
            /* An unexported frozen capture is evidence; arm cannot overwrite it. */
            if((!frozen||exported)&&!trace_rings){
                /* First arm: take the rings and fault their pages in here, on this worker, before any audio
                   callback can record into them. */
                void *rings=xz_lowzone_alloc(TRACE_RINGS_BYTES);
                if(rings){memset(rings,0,TRACE_RINGS_BYTES);__atomic_store_n(&trace_rings,rings,__ATOMIC_RELEASE);}
                else{state(TRACE_ERROR,ENOMEM);report("ERROR_NO_MEMORY",ENOMEM,NULL);}
            }
            if((!frozen||exported)&&trace_rings){
                frozen=exported=0;__atomic_store_n(&auto_trigger_ms,0,__ATOMIC_RELEASE);
                __atomic_store_n(&xz_audio_trace_v1.enabled,1,__ATOMIC_RELEASE);state(TRACE_ARMED,0);report("ARMED",0,NULL);
            }
        }
        usleep(100000);
    }
    return NULL;
}
int xz_audio_trace_start(void){
    const char *path=getenv("XZ_AUDIO_TRACE_DIR");if(!path)return 0;
    size_t prefix=strlen("/dev/shm/xz-audio-trace-");
    if(strncmp(path,"/dev/shm/xz-audio-trace-",prefix)||strlen(path)>=sizeof(directory)||!path[prefix])return -1;
    for(const char *p=path+prefix;*p;p++)if(!(*p>='a'&&*p<='z')&&!(*p>='0'&&*p<='9')&&*p!='-')return -1;
    struct stat status;if(lstat(path,&status)||!S_ISDIR(status.st_mode))return -1;
    if(created)return 0;snprintf(directory,sizeof(directory),"%s",path);
    /* Clear stale commands: setting DIR or retaining an old marker never arms a new run. */
    const char *commands[]={"arm","freeze","dump","retry"};
    for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++){char command[320];snprintf(command,sizeof(command),"%s/%s",directory,commands[i]);unlink(command);}
    __atomic_store_n(&auto_trigger_ms,0,__ATOMIC_RELEASE);
    __atomic_store_n(&running,1,__ATOMIC_RELEASE);__atomic_store_n(&xz_audio_trace_v1.enabled,0,__ATOMIC_RELEASE);state(TRACE_DORMANT,0);
    if(XZ_WORKER_CREATE(&worker,drain,NULL)){__atomic_store_n(&running,0,__ATOMIC_RELEASE);__atomic_store_n(&xz_audio_trace_v1.enabled,0,__ATOMIC_RELEASE);return -1;}
    created=1;report("DORMANT",0,NULL);return 0;
}
void xz_audio_trace_stop(void){if(!created)return;__atomic_store_n(&xz_audio_trace_v1.enabled,0,__ATOMIC_RELEASE);__atomic_store_n(&running,0,__ATOMIC_RELEASE);pthread_join(worker,NULL);created=0;}
