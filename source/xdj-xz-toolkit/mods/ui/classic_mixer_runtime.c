/* SPDX-License-Identifier: MIT */
#include "classic_mixer_runtime.h"
#include "classic_mixer.h"
#include "classic_column.h"
#include "layout_cycle.h"
#include "native_wave_runtime.h"
#include "native_skin_runtime.h"
#include "../shell_model.h"
#include "../runtime.h"
#include <pthread.h>
#include <stdio.h>
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
/* The native deck's load record, read twice and compared, as shell_runtime.c read_loaded() does: byte 9 is
   the device the loaded track came from (2 USB1, 3 USB2). 0 when unread or unsettled. */
static unsigned loaded_device(int deck){
 unsigned char a[16],b[16];uint32_t at=0x2247cacu+0x1610u+(uint32_t)deck*0x60u,state,id;
 if(xz_read_memory(at,a,16)||xz_read_memory(at,b,16)||memcmp(a,b,16))return 0;
 memcpy(&state,a,4);memcpy(&id,a+12,4);
 return state!=7&&state!=9&&id&&id!=UINT32_MAX&&(a[9]==2||a[9]==3)?a[9]:0;
}
/* The READY hold and wipe after decks 3 and 4 came up under the card: they stay NOT CONNECTED until it ends.
   Weak: hosts without the wave transaction have no card. */
#pragma weak xz_native_four_card_success
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
 const struct xz_classic_mixer_key keys[2]){
 if(!eligible){xz_classic_mixer_runtime_hide();return 0;}
 if(!pixels||stride<800||stride>SIZE_MAX/480||count<stride*480)return 0;
 if(pthread_mutex_trylock(&state_mutex))return 0;
 struct xz_extra_decks extra={0};struct xz_extra_analysis_hold hold={0};
 int layout=xz_deck_layout_get();
 if(!storage||layout==XZ_LAYOUT_TWO){
  xz_extra_decks_release_analysis(&extra,&hold);
  pthread_mutex_unlock(&state_mutex);xz_classic_mixer_runtime_hide();return 0;
 }
 /* DECK VIEW alone chooses four channels; decks 3 and 4 are live only with their route up. */
 int live=xz_extra_decks_snapshot_held(&extra,&hold)&&!(xz_native_four_card_success&&xz_native_four_card_success(now));
 unsigned changed=0;
 if(!refreshed||(uint32_t)(now-refreshed_at)>=100u){
  for(int d=0;d<2;d++){
   struct xz_shell_deck input={0};
   if(keys&&keys[d].valid&&keys[d].effective[0]){input.known=XZ_SHELL_KNOWN(XZ_SHELL_F_KEY);input.key=keys[d].effective;}
   unsigned device=loaded_device(d);if(device)input.known|=XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE);
   xz_shell_model_set_input(storage,d,&input,device);
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
 struct xz_classic_mixer_view view={.native=native.deck,.extra=&extra,.now_ms=now,.theme=theme,.layout=layout,.placeholders=!live};
 for(int d=0;d<2;d++){
  uint64_t loaded=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED);
  if((native.deck[d].known&loaded)&&native.deck[d].loaded&&overview_sequence[d])view.native_overview[d]=overview[d];
 }
 int drawn=xz_classic_mixer_draw(pixels,stride,count,&view);
 if(drawn){
  /* The band's top rows and the side column are the mod's too, repainted with it every frame, so stock
     text under them never shows through. */
  for(int y=309;y<311;y++)memset(pixels+(size_t)y*stride,0,800*sizeof *pixels);
  struct xz_classic_column_view column={.layout=layout};
  for(int d=0;d<4;d++){
   struct xz_classic_column_deck *c=&column.deck[d];
   if(d<2){
    const struct xz_shell_deck *n=&native.deck[d];
    c->live=1;c->driven=!(live&&(d?extra.right_on_4:extra.left_on_3));
    c->source=n->known&XZ_SHELL_KNOWN(XZ_SHELL_F_SOURCE)?(int)n->source:0;
    if(keys&&keys[d].valid){
     snprintf(c->key,sizeof c->key,"%s",keys[d].effective);snprintf(c->original,sizeof c->original,"%s",keys[d].original);
     c->cents=keys[d].cents;c->cents_known=1;
    }
   }else if(live){
    const struct xz_extra_deck *x=&extra.deck[d-2];
    c->live=1;c->driven=d==2?extra.left_on_3:extra.right_on_4;
    c->source=x->source==1||x->source==2?x->source:0;
    if((x->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_KEY))&&x->key[0])snprintf(c->key,sizeof c->key,"%s",x->key);
   }
  }
  xz_classic_column_draw(pixels,stride,count,&column);
 }
 xz_extra_decks_release_analysis(&extra,&hold);
 if(drawn){__atomic_store_n(&drawn_layout,layout,__ATOMIC_RELEASE);__atomic_store_n(&visible,1,__ATOMIC_RELEASE);}
 pthread_mutex_unlock(&state_mutex);
 return drawn;
}
