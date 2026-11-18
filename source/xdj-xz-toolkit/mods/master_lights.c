/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
/* MASTER and BEAT SYNC lights with DECK 3/4 (master_lights.h). A pure core, then the firmware 1.26 adapter. */
#include "master_lights.h"
#include <string.h>

int32_t xz_ml_ui_number(int32_t actual){
 switch(actual){
  case XZ_MASTER_DECK1:return 1;
  case XZ_MASTER_DECK2:return 2;
  case XZ_MASTER_DECK3:return 3;
  case XZ_MASTER_DECK4:return 4;
  case XZ_MASTER_NONE:return -1;
  default:return XZ_ML_NO_TARGET;
 }
}

int xz_ml_ui_step(struct xz_ml_ui *s,int32_t actual,int32_t current,int32_t *write){
 if(!s||!write)return 0;
 /* Someone else (the sentinel task) wrote the number: the master moved through stock, which also moved the
    engine. Stock owns the number again. */
 if(s->owned&&current!=s->written)s->owned=0;
 int32_t target=xz_ml_ui_number(actual);
 if(actual==XZ_MASTER_DECK3||actual==XZ_MASTER_DECK4)s->owned=1;
 else if(!s->owned||target==XZ_ML_NO_TARGET)return 0;
 s->written=target;
 if(current==target)return 0;
 *write=target;return 1;
}

static uint32_t rd32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}

unsigned xz_ml_lit_lamps(const struct xz_extra_deck *d){
 if(!d||!d->loaded)return 0;
 return (d->sync.requested?1u:0u)|(d->master?2u:0u);
}

static int slot_of(const struct xz_ml_stat *s,unsigned id,unsigned channel,unsigned *slot){
 if(id>=s->ids||channel<1||channel>s->slots)return 0;
 *slot=id*s->slots+channel-1;return 1;
}
static int shape_ok(const struct xz_ml_stat *s){
 return s&&s->count&&s->records&&s->index&&s->ids==XZ_ML_STAT_IDS&&s->slots==XZ_ML_STAT_SLOTS&&
  *s->count<=XZ_ML_STAT_CAPACITY;
}
/* The record the index names for (id, channel), checked against the record itself. -1 malformed. */
static int find(const struct xz_ml_stat *s,unsigned id,unsigned channel){
 unsigned slot;
 if(!slot_of(s,id,channel,&slot))return -1;
 unsigned n=s->index[slot];
 if(!n)return 0;
 if(n>*s->count)return -1;
 const unsigned char *r=s->records+(size_t)(n-1)*XZ_ML_RECORD_BYTES;
 return rd32(r)==id&&rd32(r+4)==channel?(int)n:-1;
}

int xz_ml_insert_missing(const struct xz_ml_stat *s,unsigned channel,const struct xz_extra_deck *d,
                         xz_ml_append_fn append,void *ctx){
 if(!append||channel<1||channel>2)return 0;
 unsigned lit=xz_ml_lit_lamps(d);
 if(!lit)return 0;
 if(!shape_ok(s))return -1;
 int added=0;
 static const unsigned ids[2]={XZ_ML_ID_BEAT_SYNC,XZ_ML_ID_MASTER};
 for(unsigned i=0;i<2;i++){
  if(!(lit&(1u<<i)))continue;
  int have=find(s,ids[i],channel);
  if(have<0)return -1;
  if(have)continue;
  /* A record id stock never wrote for this channel cannot already sit unindexed in the list: a stray one
     means the index is not what this code knows. */
  for(unsigned r=0;r<*s->count;r++){
   const unsigned char *p=s->records+(size_t)r*XZ_ML_RECORD_BYTES;
   if(rd32(p)==ids[i]&&rd32(p+4)==channel)return -1;
  }
  if(*s->count+XZ_ML_STAT_HEADROOM>=XZ_ML_STAT_CAPACITY)return added;
  unsigned before=*s->count;
  append(ctx,ids[i],channel);
  if(*s->count!=before+1||find(s,ids[i],channel)!=(int)before+1)return -1;
  added++;
 }
 return added;
}

#ifndef XZ_MASTER_LIGHTS_NO_RUNTIME
#include "runtime.h"
#include "clock_compat.h"
#include <time.h>
#if !defined(__arm__) || defined(__aarch64__)
#error master_lights runtime adapter needs the 32-bit ARM player ABI
#endif
_Static_assert(sizeof(void *)==4,"32-bit player pointers");
extern int xz_sync_link_runtime_master_observe(struct xz_master_observation *) __attribute__((weak));

#define G_UI 1u
#define G_INSERT 2u
/* FNV-1a 64 of stock code and literals, firmware 1.26 (md5 6a7ccb45...). No stock bytes are kept here. */
static const struct { uint32_t address,length; uint64_t hash; unsigned use; } windows[]={
 {0x09d41cu,0x010u,0x12a3ffb4c69fc815ull,G_UI},     /* TotalMainIf::setSyncMaster / getSyncMaster: field +0x34 */
 {0x09f560u,0x048u,0x05921ff9a7878340ull,G_UI},       /* TotalMainIf_SetLinkSyncMaster: 0 -> -1, store +0x34, engine */
 {0x09f6f4u,0x004u,0x77bf32ada74dcc09ull,G_UI},    /* its literal: the TotalMainIf singleton cell */
 {0x09f710u,0x004u,0x5668cac132c6bcaaull,G_UI},        /* TotalMainIf constructor's vtable literal */
 {0x28a614u,0x068u,0x29deed0fa4114e1bull,G_UI},       /* PlayerInnards::getStat: MASTER bit = getSyncMaster()==channel */
 {0x28b4acu,0x004u,0x77bf32ada74dcc09ull,G_UI},    /* its literal: the same singleton cell */
 {0x27f6f0u,0x030u,0x26eb55575f58475dull,G_UI|G_INSERT}, /* checkLedStat: stat bit -> setLedState(5, ch, 1, 0, 0) */
 {0x27f458u,0x054u,0x9d622237b9043ad4ull,G_INSERT},     /* checkLedStat: setLedState(4, ch, 1 or 2/250 ms) */
 {0x27bec8u,0x104u,0x204dbc7a78a0a280ull,G_INSERT},/* LedStat::setLedState [clone .constprop.123] */
 {0x2b0e10u,0x0c8u,0x79deb2d7b26d9435ull,G_INSERT},   /* LedManager::refStates: clears +72, servers fill it */
 {0x2936c0u,0x0b0u,0x2ef3d438f3a7f30full,G_INSERT},  /* UiObjectManager::init: 100 records, 60 ids x 2 */
};
#define TOTAL_MAIN_CELL 0x010ea780u
#define TOTAL_MAIN_VPTR 0x003d4a08u   /* vtable for TotalMainIf + 8 */
#define TOTAL_MAIN_MASTER 0x34u
#define UI_OBJ_MANAGER_CELL 0x1c8a308u /* [0x1c8a1f0+0x118]; getLedManager 0x296444 = [cell]+0x68 */
#define LED_MANAGER_SCRATCH 72u
#define FN_SET_LED_STATE 0x27bec8u

static unsigned guard_state; /* 0 unchecked, else 0x100 | passed */
static int enabled;
static struct xz_ml_ui ui;
static int32_t inserted,refused;
static uint32_t total_main;    /* validated object, 0 until found */
static uint32_t scratch_stat;  /* LedManager +72, 0 until found */

static uint64_t fnv(uint64_t h,const unsigned char *p,size_t n){
 for(size_t i=0;i<n;i++){h^=p[i];h*=0x100000001b3ull;}
 return h;
}
static unsigned guards(void){
 unsigned state=__atomic_load_n(&guard_state,__ATOMIC_ACQUIRE);
 if(state)return state&0xffu;
 unsigned failed=0;unsigned char buffer[128];
 for(unsigned i=0;i<sizeof windows/sizeof windows[0];i++){
  uint64_t h=0xcbf29ce484222325ull;
  for(uint32_t at=0;at<windows[i].length;at+=sizeof buffer){
   uint32_t n=windows[i].length-at<sizeof buffer?windows[i].length-at:(uint32_t)sizeof buffer;
   if(xz_read_memory(windows[i].address+at,buffer,n))return 0;
   h=fnv(h,buffer,n);
  }
  if(h!=windows[i].hash)failed|=windows[i].use;
 }
 unsigned passed=(G_UI|G_INSERT)&~failed;
 __atomic_store_n(&guard_state,0x100u|passed,__ATOMIC_RELEASE);
 if(failed)xz_log("MASTER/SYNC lights: stock code differs from firmware 1.26; that part stays off");
 return passed;
}
static int sane(uint32_t p,uint32_t span){return p>=0x10000u&&!(p&3u)&&p<=UINT32_MAX-span;}
/* The LED refresh thread runs at real-time priority: it only reads the cached result. start() and the bench's
   status read (normal priority) do the checking. */
static unsigned checked(void){
 unsigned state=__atomic_load_n(&guard_state,__ATOMIC_ACQUIRE);
 return state?state&0xffu:0u;
}

int xz_master_lights_start(void){
 unsigned g=guards();
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);
 return g==(G_UI|G_INSERT)?0:-1;
}
void xz_master_lights_stop(void){__atomic_store_n(&enabled,0,__ATOMIC_RELEASE);}

static uint32_t now_ms(void){
 struct timespec t;
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
 return (uint32_t)((uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u);
}
/* The UI master number's object, checked by its vtable. The cell is set once (juce singleton). */
static unsigned cell_tries,stat_tries;
static volatile int32_t *ui_master_cell(void){
 if(!total_main){
  if(cell_tries++&63u)return NULL;  /* a missing singleton is looked for again every 64 refreshes, not each one */
  uint32_t object,vptr;
  if(xz_read_memory(TOTAL_MAIN_CELL,&object,4)||!sane(object,0x40)||xz_read_memory(object,&vptr,4)||
     vptr!=TOTAL_MAIN_VPTR)return NULL;
  total_main=object;
 }
 if(*(volatile uint32_t *)(uintptr_t)TOTAL_MAIN_CELL!=total_main||
    *(volatile uint32_t *)(uintptr_t)total_main!=TOTAL_MAIN_VPTR){total_main=0;return NULL;}
 return (volatile int32_t *)(uintptr_t)(total_main+TOTAL_MAIN_MASTER);
}

void xz_master_lights_ui_tick(void){
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!(checked()&G_UI)||!xz_sync_link_runtime_master_observe)return;
 struct xz_master_observation a;
 int32_t actual=XZ_MASTER_UNKNOWN;
 /* The engine's master as the Link bridge last saw it inside SyncMgr::sync; stale or unproven is unknown. */
 if(xz_sync_link_runtime_master_observe(&a)&&a.revision&&a.operator_epoch&&
    (uint32_t)(now_ms()-a.observed_ms)<=XZ_MASTER_MAX_AGE_MS)actual=a.deck;
 /* Stock owns the number until a DECK 3/4 master is seen; nothing is touched before that. */
 if(!ui.owned&&actual!=XZ_MASTER_DECK3&&actual!=XZ_MASTER_DECK4)return;
 volatile int32_t *cell=ui_master_cell();
 if(!cell)return;
 int32_t write;
 if(xz_ml_ui_step(&ui,actual,*cell,&write))*cell=write;
}

struct append_ctx { uint32_t stat; };
static void stock_append(void *ctx,unsigned id,unsigned channel){
 struct append_ctx *c=ctx;
 /* LedStat::setLedState(ID, Channel, State, long period, bool, long) [clone .constprop.123]: the call
    checkLedStat makes for a steady lamp, (stat, id, channel, ON, 0, 0). */
 ((void (*)(void *,int,int,int,long,long))(uintptr_t)FN_SET_LED_STATE)((void *)(uintptr_t)c->stat,(int)id,(int)channel,1,0,0);
}
static uint32_t led_scratch(void){
 if(!scratch_stat){
  if(stat_tries++&63u)return 0;
  uint32_t manager,leds;
  if(xz_read_memory(UI_OBJ_MANAGER_CELL,&manager,4)||!sane(manager,0x6c)||xz_read_memory(manager+0x68,&leds,4)||
     !sane(leds,LED_MANAGER_SCRATCH+24))return 0;
  scratch_stat=leds+LED_MANAGER_SCRATCH;
 }
 return scratch_stat;
}
int xz_master_lights_insert(void *packet,unsigned channel,const struct xz_extra_deck *deck){
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!(checked()&G_INSERT)||!packet||!xz_ml_lit_lamps(deck))return 0;
 uint32_t stat=(uint32_t)(uintptr_t)packet;
 /* Only the refresh list itself: anything else has no proven spare room. */
 if(stat!=led_scratch())return 0;
 const unsigned char *p=packet;
 uint32_t records,index;uint16_t ids,slots;
 memcpy(&records,p+12,4);memcpy(&ids,p+16,2);memcpy(&slots,p+18,2);memcpy(&index,p+20,4);
 if(!sane(records,XZ_ML_STAT_CAPACITY*XZ_ML_RECORD_BYTES)||!sane(index,(uint32_t)XZ_ML_STAT_IDS*XZ_ML_STAT_SLOTS*2u))return 0;
 struct xz_ml_stat s={(uint16_t *)(uintptr_t)(stat+8),(unsigned char *)(uintptr_t)records,ids,slots,(uint16_t *)(uintptr_t)index};
 struct append_ctx c={stat};
 int n=xz_ml_insert_missing(&s,channel,deck,stock_append,&c);
 if(n<0){
  __atomic_fetch_add(&refused,1,__ATOMIC_RELAXED);
  /* Not the list this code knows: stop inserting until the next start. */
  __atomic_store_n(&guard_state,0x100u|(checked()&~G_INSERT),__ATOMIC_RELEASE);
  xz_log("MASTER/SYNC lights: the LED list is not as firmware 1.26 builds it; DECK 3/4 lamp records off");
  return 0;
 }
 if(n)__atomic_fetch_add(&inserted,n,__ATOMIC_RELAXED);
 return n;
}

void xz_master_lights_status(struct xz_ml_status *out){
 if(!out)return;
 unsigned g=guards();
 out->ui_ready=__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)&&(g&G_UI);
 out->insert_ready=__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)&&(g&G_INSERT);
 out->owned=ui.owned;out->written=ui.written;
 out->inserted=__atomic_load_n(&inserted,__ATOMIC_RELAXED);out->refused=__atomic_load_n(&refused,__ATOMIC_RELAXED);
}
#endif
