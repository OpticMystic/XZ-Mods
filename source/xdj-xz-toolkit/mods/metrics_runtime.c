/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "metrics_runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "doom/native_bridge.h"

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static int created,running,enabled;
static struct xz_metrics_sample latest;
/* Published diagnostic counters are observational; they never control playback. */
__attribute__((visibility("default"))) struct {uint32_t version,samples,valid,enabled,cpu_permille,used_mib,temperature_c;} xz_mods_metrics_proof_v1={.version=1};
static uint32_t milliseconds(void){return xz_game_milliseconds();}
static void read_text(const char *path,char *out,size_t size){
 out[0]=0;FILE *f=fopen(path,"r");if(!f)return;size_t n=fread(out,1,size-1,f);out[n]=0;fclose(f);
}
static void *collect(void *unused){
 (void)unused;struct xz_metrics_counters previous={0};
 pthread_mutex_lock(&mutex);
 while(running){
  while(running&&!enabled){memset(&previous,0,sizeof(previous));pthread_cond_wait(&changed,&mutex);}
  if(!running)break;
  pthread_mutex_unlock(&mutex);
  char stat[8192],mem[4096],temp[64],type[64];struct xz_metrics_sample sample;
  read_text("/proc/stat",stat,sizeof(stat));read_text("/proc/meminfo",mem,sizeof(mem));
  read_text("/sys/class/thermal/thermal_zone0/temp",temp,sizeof(temp));read_text("/sys/class/thermal/thermal_zone0/type",type,sizeof(type));
  xz_metrics_parse(&previous,&sample,stat,mem,temp,type,milliseconds());
  pthread_mutex_lock(&mutex);
  if(enabled){latest=sample;
   __atomic_add_fetch(&xz_mods_metrics_proof_v1.samples,1,__ATOMIC_RELAXED);
   __atomic_store_n(&xz_mods_metrics_proof_v1.valid,sample.valid,__ATOMIC_RELEASE);
   __atomic_store_n(&xz_mods_metrics_proof_v1.cpu_permille,sample.cpu_permille,__ATOMIC_RELAXED);
   __atomic_store_n(&xz_mods_metrics_proof_v1.used_mib,sample.used_mib,__ATOMIC_RELAXED);
   __atomic_store_n(&xz_mods_metrics_proof_v1.temperature_c,(uint32_t)sample.temperature_c,__ATOMIC_RELAXED);
  }
  struct timeval now;gettimeofday(&now,NULL);struct timespec until={now.tv_sec+1,now.tv_usec*1000};
  if(running&&enabled)pthread_cond_timedwait(&changed,&mutex,&until);
 }
 pthread_mutex_unlock(&mutex);return NULL;
}
int xz_metrics_start(void){
 pthread_mutex_lock(&mutex);
 if(created){pthread_mutex_unlock(&mutex);return 0;}
 running=1;enabled=0;memset(&latest,0,sizeof(latest));
 int result=pthread_create(&thread,NULL,collect,NULL);if(result)running=0;else created=1;
 pthread_mutex_unlock(&mutex);return result?-1:0;
}
void xz_metrics_enable(int value){
 pthread_mutex_lock(&mutex);enabled=!!value;
 __atomic_store_n(&xz_mods_metrics_proof_v1.enabled,enabled,__ATOMIC_RELEASE);
 if(!enabled)memset(&latest,0,sizeof(latest));pthread_cond_signal(&changed);pthread_mutex_unlock(&mutex);
}
int xz_metrics_snapshot(struct xz_metrics_sample *out){
 if(pthread_mutex_trylock(&mutex))return 0;
 *out=latest;pthread_mutex_unlock(&mutex);return 1;
}
void xz_metrics_stop(void){
 pthread_mutex_lock(&mutex);int join=created;created=0;running=0;enabled=0;pthread_cond_signal(&changed);pthread_mutex_unlock(&mutex);
 if(join)pthread_join(thread,NULL);
}
