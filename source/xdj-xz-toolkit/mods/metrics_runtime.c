/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "metrics_runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "doom/native_bridge.h"
#include "extra_decks.h"
#include "key/runtime.h"
#pragma weak xz_extra_decks_worker_load
#pragma weak xz_key_active_mask

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static int created,running,enabled;
static struct xz_metrics_sample latest;
/* Published diagnostic counters are observational; they never control playback. */
__attribute__((visibility("default"))) struct {uint32_t version,samples,valid,enabled,cpu_permille,used_mib,temperature_c;} xz_mods_metrics_proof_v1={.version=1};
/* Independent versioned audio readout; old metrics proof layout stays stable. */
__attribute__((visibility("default"))) struct {
 uint32_t version,sequence,sampled_ms,period_us,safe_us,key_active_mask;
 struct xz_worker_load worker[2];
} xz_mods_audio_load_proof_v1={.version=1,.period_us=XZ_RENDER_BLOCK_US,.safe_us=XZ_RENDER_SAFE_US};
static void audio_proof(const struct xz_metrics_sample *s) {
 __atomic_add_fetch(&xz_mods_audio_load_proof_v1.sequence,1,__ATOMIC_ACQ_REL);
 __atomic_store_n(&xz_mods_audio_load_proof_v1.sampled_ms,s->sampled_ms,__ATOMIC_RELAXED);
 __atomic_store_n(&xz_mods_audio_load_proof_v1.key_active_mask,s->key_active_mask,__ATOMIC_RELAXED);
 for(unsigned i=0;i<2;i++){
  const struct xz_worker_load *v=&s->worker[i];struct xz_worker_load *d=&xz_mods_audio_load_proof_v1.worker[i];
  __atomic_store_n(&d->samples,v->samples,__ATOMIC_RELAXED);
  __atomic_store_n(&d->p99_us,v->p99_us,__ATOMIC_RELAXED);
  __atomic_store_n(&d->max_us,v->max_us,__ATOMIC_RELAXED);
  __atomic_store_n(&d->key_max_us,v->key_max_us,__ATOMIC_RELAXED);
  __atomic_store_n(&d->sampled_ms,v->sampled_ms,__ATOMIC_RELAXED);
  __atomic_store_n(&d->key_active,v->key_active,__ATOMIC_RELAXED);
 }
 __atomic_add_fetch(&xz_mods_audio_load_proof_v1.sequence,1,__ATOMIC_RELEASE);
}
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
  if(xz_extra_decks_worker_load&&xz_extra_decks_worker_load(sample.worker))sample.valid|=XZ_METRICS_AUDIO;
  sample.key_active_mask=xz_key_active_mask?xz_key_active_mask():0;
  for(unsigned i=0;i<2;i++)if(sample.worker[i].key_active)sample.key_active_mask|=1u<<(i+2);
  pthread_mutex_lock(&mutex);
  if(enabled){latest=sample;audio_proof(&sample);
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
