/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual player helper, lock and retirement code. No device or player workers. */
#define _GNU_SOURCE
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void *observed_memcpy(void *,const void *,size_t);
#define memcpy observed_memcpy
#include "../extra_player.c"
#undef memcpy

static pthread_mutex_t pause_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_cv=PTHREAD_COND_INITIALIZER;
static int pause_copy,entered_copy,resume_copy;
static atomic_int writer_started,writer_done;
static const void *watched;
static struct xz_xp_players *fixture;
static struct xz_extra_wave_chunk copied;
static void *observed_memcpy(void *dst,const void *src,size_t n){
 if(src==watched&&pause_copy){
  pthread_mutex_lock(&pause_lock);entered_copy=1;pthread_cond_broadcast(&pause_cv);
  while(!resume_copy)pthread_cond_wait(&pause_cv,&pause_lock);
  pthread_mutex_unlock(&pause_lock);
 }
 return memcpy(dst,src,n);
}
static void seed(struct xz_xp_players *p,int index,unsigned count){
 struct analysis *a=calloc(1,sizeof *a);assert(a);
 a->columns=calloc(count,sizeof *a->columns);assert(a->columns);a->count=count;
 for(unsigned i=0;i<count;i++)a->columns[i]=(struct xz_extra_column){i,i>>8,i^0x55};
 p->deck[index].analysis=a;
 p->view[index]=(struct xz_extra_deck){.loaded=1,.source=index+1,
  .known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE),.wave={a->columns,count,123456,765},
  .timeline={.media_generation=0x1122334455667788ull,.route_epoch=7,.load_revision=9}};
}
static void *read_copy(void *unused){
 (void)unused;assert(xz_xp_copy_wave(fixture,0,NULL,0,&copied)==XZ_EXTRA_WAVE_COPY_OK);return NULL;
}
static void *replace_and_collect(void *unused){
 (void)unused;atomic_store(&writer_started,1);
 pthread_mutex_lock(&fixture->publish_lock);
 fixture->view[0]=(struct xz_extra_deck){0}; /* LOAD unpublishes before retirement. */
 pthread_mutex_unlock(&fixture->publish_lock);
 retire(&fixture->deck[0],0);collect(&fixture->deck[0],XZ_XP_RETIRE_MS);
 atomic_store(&writer_done,1);return NULL;
}
int main(int argc,char **argv){
 (void)argv;
 _Static_assert(sizeof copied.columns==1536,"copy work bound");
 fixture=calloc(1,sizeof *fixture);assert(fixture);
 assert(!pthread_mutex_init(&fixture->publish_lock,NULL));atomic_init(&fixture->revision,0);
 seed(fixture,0,1025);seed(fixture,1,1);
 if(argc>1){
  struct xz_extra_decks stale={0};xz_xp_read(fixture,&stale);
  replace_and_collect(NULL);
  /* Sanitizer witness for the old shallow-borrow design; expected failure. */
  volatile unsigned low=stale.deck[0].wave.columns[0].low;
  printf("unsafe borrow %u\n",low);return 2;
 }
 struct xz_extra_wave_chunk out;memset(&out,0xa5,sizeof out);
 struct xz_extra_wave_chunk untouched=out;
 assert(xz_xp_copy_wave(NULL,0,NULL,0,&out)==XZ_EXTRA_WAVE_COPY_INVALID);
 assert(xz_xp_copy_wave(fixture,-1,NULL,0,&out)==XZ_EXTRA_WAVE_COPY_INVALID);
 assert(xz_xp_copy_wave(fixture,2,NULL,0,&out)==XZ_EXTRA_WAVE_COPY_INVALID);
 assert(xz_xp_copy_wave(fixture,0,NULL,0,NULL)==XZ_EXTRA_WAVE_COPY_INVALID);
 pthread_mutex_lock(&fixture->publish_lock);
 assert(xz_xp_copy_wave(fixture,0,NULL,0,&out)==XZ_EXTRA_WAVE_COPY_BUSY);
 pthread_mutex_unlock(&fixture->publish_lock);
 assert(!memcmp(&out,&untouched,sizeof out));
 assert(xz_xp_copy_wave(fixture,0,NULL,0,&out)==XZ_EXTRA_WAVE_COPY_OK);
 assert(out.first==0&&out.count==512&&out.key.column_count==1025);
 struct xz_extra_wave_key key=out.key;
 for(unsigned first=0;first<1025;first+=512){
  assert(xz_xp_copy_wave(fixture,0,&key,first,&out)==XZ_EXTRA_WAVE_COPY_OK);
  assert(out.count==(first==1024?1:512));
  for(unsigned i=0;i<out.count;i++){
   unsigned x=first+i;assert(out.columns[i].low==(uint8_t)x);
   assert(out.columns[i].mid==(uint8_t)(x>>8));assert(out.columns[i].high==(uint8_t)(x^0x55));
  }
 }
 assert(xz_xp_copy_wave(fixture,0,&key,1025,&out)==XZ_EXTRA_WAVE_COPY_OK&&out.count==0);
 assert(xz_xp_copy_wave(fixture,0,&key,1026,&out)==XZ_EXTRA_WAVE_COPY_INVALID);
 assert(xz_xp_copy_wave(fixture,0,&key,UINT32_MAX,&out)==XZ_EXTRA_WAVE_COPY_INVALID);
 assert(xz_xp_copy_wave(fixture,1,NULL,0,&out)==XZ_EXTRA_WAVE_COPY_OK&&out.key.source==2&&out.count==1);
 /* Every identity component rejects continuation, including final validation. */
 for(unsigned field=0;field<7;field++){
  struct xz_extra_wave_key changed=key;
  switch(field){
   case 0:changed.media_generation++;break;case 1:changed.route_epoch++;break;
   case 2:changed.load_revision++;break;case 3:changed.source++;break;
   case 4:changed.column_count++;break;case 5:changed.span_ms++;break;case 6:changed.normalization++;break;
  }
  assert(xz_xp_copy_wave(fixture,0,&changed,0,&out)==XZ_EXTRA_WAVE_COPY_CHANGED);
  assert(xz_xp_copy_wave(fixture,0,&changed,key.column_count,&out)==XZ_EXTRA_WAVE_COPY_CHANGED);
 }
 fixture->view[0].known=0;
 assert(xz_xp_copy_wave(fixture,0,NULL,0,&out)==XZ_EXTRA_WAVE_COPY_ABSENT);
 assert(xz_xp_copy_wave(fixture,0,&key,0,&out)==XZ_EXTRA_WAVE_COPY_CHANGED);
 fixture->view[0].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE);
 /* Preempt the reader inside the actual value copy; replacement cannot retire
    its source until publication has acquired and released the same lock. */
 watched=fixture->view[0].wave.columns;pause_copy=1;
 pthread_t reader,writer;assert(!pthread_create(&reader,NULL,read_copy,NULL));
 pthread_mutex_lock(&pause_lock);
 while(!entered_copy)pthread_cond_wait(&pause_cv,&pause_lock);
 pthread_mutex_unlock(&pause_lock);
 assert(!pthread_create(&writer,NULL,replace_and_collect,NULL));
 while(!atomic_load(&writer_started))sched_yield();
 assert(!atomic_load(&writer_done));
 assert(xz_xp_copy_wave(fixture,0,&key,512,&out)==XZ_EXTRA_WAVE_COPY_BUSY);
 pthread_mutex_lock(&pause_lock);resume_copy=1;pthread_cond_broadcast(&pause_cv);pthread_mutex_unlock(&pause_lock);
 pthread_join(reader,NULL);pthread_join(writer,NULL);
 assert(atomic_load(&writer_done)&&!fixture->deck[0].retired_count);
 for(unsigned i=0;i<copied.count;i++)assert(copied.columns[i].low==(uint8_t)i);
 assert(xz_xp_copy_wave(fixture,0,&key,512,&out)==XZ_EXTRA_WAVE_COPY_CHANGED);
 free_analysis(fixture->deck[1].analysis);pthread_mutex_destroy(&fixture->publish_lock);free(fixture);
 puts("PASS bounded current-view copy: chunks, identity, contention, preemption, retirement");return 0;
}
