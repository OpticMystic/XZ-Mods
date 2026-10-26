/* SPDX-License-Identifier: MIT */
#include "classic_mixer_runtime.h"
#include "classic_mixer.h"
#include "layout_cycle.h"
#include "native_skin_runtime.h"
#include "../shell_model.h"
#include "../runtime.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define OVERVIEW_PIXELS (300u*34u)
static pthread_mutex_t state_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t overview_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct xz_shell_model_storage *storage;
static struct xz_shell_model native;
static uint32_t refreshed_at;
static int refreshed,visible,drawn_layout;
static struct { uint16_t pixels[OVERVIEW_PIXELS]; uint32_t sequence; int x,y; } captured[2];
static uint16_t overview[2][OVERVIEW_PIXELS];
static uint32_t overview_sequence[2],invalidated_sequence[2];
static unsigned pending_invalidation;

static int native_read(uintptr_t address,void *out,size_t count){
 return address<=UINT32_MAX?xz_read_memory((uint32_t)address,out,count):-1;
}
int xz_classic_mixer_runtime_start(void){
 pthread_mutex_lock(&state_mutex);
 if(!storage)storage=calloc(1,xz_shell_model_storage_size());
 int ok=storage!=NULL;
 pthread_mutex_unlock(&state_mutex);
 return ok?0:-1;
}
void xz_classic_mixer_runtime_hide(void){
 if(__atomic_exchange_n(&visible,0,__ATOMIC_ACQ_REL))xz_native_skin_request_repaint();
}
int xz_classic_mixer_runtime_layout(void){return __atomic_load_n(&drawn_layout,__ATOMIC_ACQUIRE);}
int xz_classic_mixer_runtime_visible(void){return __atomic_load_n(&visible,__ATOMIC_ACQUIRE);}
void xz_classic_mixer_runtime_stop(void){
 xz_classic_mixer_runtime_hide();
 pthread_mutex_lock(&state_mutex);free(storage);storage=NULL;refreshed=0;memset(&native,0,sizeof native);
 pthread_mutex_unlock(&state_mutex);
}
void xz_classic_mixer_native_overview(int deck,const uint16_t *pixels,size_t count,int x,int y){
 if(deck<0||deck>1||!pixels||count!=OVERVIEW_PIXELS||x<0||x>500||y<311||y>446||
    (x>=400)!=deck||pthread_mutex_trylock(&overview_mutex))return;
 memcpy(captured[deck].pixels,pixels,OVERVIEW_PIXELS*sizeof *pixels);
 captured[deck].x=x;captured[deck].y=y;
 if(!++captured[deck].sequence)++captured[deck].sequence;
 pthread_mutex_unlock(&overview_mutex);
}
int xz_classic_mixer_runtime_native_point(int deck,int local_x,int local_y,int *x,int *y){
 if(deck<0||deck>1||local_x<0||local_x>=300||local_y<0||local_y>=34||!x||!y||
    pthread_mutex_trylock(&overview_mutex))return 0;
 int valid=captured[deck].sequence!=0;
 if(valid){*x=captured[deck].x+local_x;*y=captured[deck].y+local_y;}
 pthread_mutex_unlock(&overview_mutex);return valid;
}
int xz_classic_mixer_runtime_frame(uint16_t *pixels,size_t stride,size_t count,int eligible,int theme,uint32_t now,
 const char *keys[2]){
 if(!eligible){xz_classic_mixer_runtime_hide();return 0;}
 if(!pixels||stride<800||stride>SIZE_MAX/480||count<stride*480)return 0;
 if(pthread_mutex_trylock(&state_mutex))return 0;
 struct xz_extra_decks extra={0};struct xz_extra_analysis_hold hold={0};
 int layout=xz_deck_layout_get();
 if(!storage||layout==XZ_LAYOUT_TWO||!xz_extra_decks_snapshot_held(&extra,&hold)){
  xz_extra_decks_release_analysis(&extra,&hold);
  pthread_mutex_unlock(&state_mutex);xz_classic_mixer_runtime_hide();return 0;
 }
 unsigned changed=0;
 if(!refreshed||(uint32_t)(now-refreshed_at)>=100u){
  for(int d=0;d<2;d++){
   struct xz_shell_deck input={0};
   if(keys&&keys[d]&&keys[d][0]){input.known=XZ_SHELL_KNOWN(XZ_SHELL_F_KEY);input.key=keys[d];}
   xz_shell_model_set_input(storage,d,&input,0);
  }
  changed=xz_shell_model_refresh(&native,storage,native_read,now);
  refreshed=1;refreshed_at=now;
 }
 if(xz_shell_model_caution(storage)){
  xz_extra_decks_release_analysis(&extra,&hold);
  pthread_mutex_unlock(&state_mutex);xz_classic_mixer_runtime_hide();return 0;
 }
 pending_invalidation|=changed;
 if(!pthread_mutex_trylock(&overview_mutex)){
  for(int d=0;d<2;d++){
   if(pending_invalidation&(1u<<d))invalidated_sequence[d]=captured[d].sequence;
   if(captured[d].sequence&&captured[d].sequence!=invalidated_sequence[d]){
    if(captured[d].sequence!=overview_sequence[d])memcpy(overview[d],captured[d].pixels,sizeof overview[d]);
    overview_sequence[d]=captured[d].sequence;
   }else overview_sequence[d]=0;
  }
  pending_invalidation=0;
  pthread_mutex_unlock(&overview_mutex);
 }else if(pending_invalidation){
  for(int d=0;d<2;d++)if(pending_invalidation&(1u<<d))overview_sequence[d]=0;
 }
 if(changed)xz_native_skin_request_repaint();
 struct xz_classic_mixer_view view={.native=native.deck,.extra=&extra,.now_ms=now,.theme=theme,.layout=layout};
 for(int d=0;d<2;d++){
  uint64_t loaded=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED);
  if((native.deck[d].known&loaded)&&native.deck[d].loaded&&overview_sequence[d])view.native_overview[d]=overview[d];
 }
 int drawn=xz_classic_mixer_draw(pixels,stride,count,&view);
 xz_extra_decks_release_analysis(&extra,&hold);
 if(drawn){__atomic_store_n(&drawn_layout,layout,__ATOMIC_RELEASE);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);}
 pthread_mutex_unlock(&state_mutex);
 return drawn;
}
