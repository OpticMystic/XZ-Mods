#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
static atomic_uint writes;
static _Thread_local int callback_active;
static ssize_t observed_write(int fd,const void *p,size_t n){assert(!callback_active);atomic_fetch_add(&writes,1);return write(fd,p,n);}
#define write observed_write
#include "../trace.c"
#undef write
static void command(const char *name){char p[400];snprintf(p,sizeof p,"%s/%s",directory,name);int fd=open(p,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);}
static unsigned captures(void){unsigned n=0;DIR *d=opendir(directory);assert(d);struct dirent *e;while((e=readdir(d)))if(!strncmp(e->d_name,"capture-",8)&&strstr(e->d_name,".bin")&&!strstr(e->d_name,".part"))n++;closedir(d);return n;}
static void enabled(int expected){for(unsigned i=0;i<200&&xz_audio_trace_enabled()!=expected;i++)usleep(10000);assert(xz_audio_trace_enabled()==expected);}
static void record_failure(void){struct xz_audio_trace_context c={.frames=64};c.mix[13]=1;float pcm[128]={0};callback_active=1;xz_audio_trace_record(0,XZ_TRACE_STEMS,pcm,&c);callback_active=0;}
int main(void){
 char path[256];snprintf(path,sizeof path,"/dev/shm/xz-audio-trace-test-%ld",(long)getpid());assert(!mkdir(path,0700));assert(!setenv("XZ_AUDIO_TRACE_DIR",path,1));
 /* A stale arm marker cannot enable a subsequent launch. */
 snprintf(directory,sizeof directory,"%s",path);command("arm");assert(!xz_audio_trace_start());assert(!xz_audio_trace_enabled());
 usleep(150000);unsigned before=atomic_load(&writes);record_failure();assert(atomic_load(&writes)==before&&!captures());
 command("arm");enabled(1);usleep(150000);before=atomic_load(&writes);record_failure();enabled(0);usleep(150000);
 assert(atomic_load(&writes)==before);assert(!captures()); /* automatic freeze must not export/status-write */
 unsigned count=trace_rings[0][XZ_TRACE_STEMS].count;record_failure();assert(trace_rings[0][XZ_TRACE_STEMS].count==count);
 command("arm");usleep(200000);assert(!xz_audio_trace_enabled()); /* evidence cannot be overwritten */
 command("dump");for(unsigned i=0;i<400&&!captures();i++)usleep(10000);assert(captures()==1);
 command("arm");enabled(1);usleep(150000);
 /* In-flight callbacks must quiesce before a drain is allowed. A failed freeze stays disabled. */
 __atomic_store_n(&trace_rings[0][XZ_TRACE_STEMS].busy,1,__ATOMIC_RELEASE);before=atomic_load(&writes);command("freeze");enabled(0);usleep(650000);assert(captures()==1&&atomic_load(&writes)==before);
 command("arm");usleep(150000);assert(!xz_audio_trace_enabled());
 __atomic_store_n(&trace_rings[0][XZ_TRACE_STEMS].busy,0,__ATOMIC_RELEASE);command("retry");usleep(200000);assert(captures()==1);command("dump");for(unsigned i=0;i<400&&captures()<2;i++)usleep(10000);assert(captures()==2);
 xz_audio_trace_stop();assert(!xz_audio_trace_enabled());
 DIR *d=opendir(path);assert(d);struct dirent *e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char f[512];snprintf(f,sizeof f,"%s/%s",path,e->d_name);assert(!unlink(f));}closedir(d);assert(!rmdir(path));
 puts("PASS dormant/stale arm, explicit arm, automatic RAM-only freeze, frozen record rejection, evidence-preserving rearm, explicit export, busy freeze/retry/drain, callback no write and stop");
}
