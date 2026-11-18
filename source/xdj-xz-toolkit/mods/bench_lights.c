/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
/* Private bench operations 24..25 (bench_lights.h). A pure core, then the firmware 1.26 adapter. */
#include "bench_lights.h"
#include <errno.h>
#include <string.h>

static uint32_t byte_of(int known,int bits){return known?0x80u|((unsigned)bits&0x7fu):0u;}
static uint32_t lamp_byte(int known,const struct xz_bl_lamp *l){
 if(!known)return 0;
 unsigned state=l->state>7u?7u:l->state;
 return 0x80u|(l->present?1u:0u)|state<<1|(l->forced?16u:0u);
}
static uint32_t int8_or_unknown(int known,int32_t v){return known&&v>=-127&&v<127?(uint32_t)(uint8_t)(int8_t)v:0x7fu;}

void xz_bl_encode(const struct xz_bl_view *v,int32_t page,struct xz_bench_result *r){
 memset(r->args,0,sizeof r->args);
 if(!v||page<0||page>4){r->status=-EINVAL;return;}
 if(page){
  const struct xz_bl_lamp *l=&v->lamp[page-1];
  r->args[0]=(int32_t)l->state;r->args[1]=(int32_t)l->brightness;r->args[2]=(int32_t)l->period;
  r->args[3]=(int32_t)((l->rgb&0xffffffu)|(l->forced?1u<<24:0u)|(l->present?1u<<28:0u)|(v->lamps_known?1u<<30:0u));
  r->status=v->lamps_known?0:-EAGAIN;return;
 }
 uint32_t a0=0;
 for(unsigned i=0;i<4;i++)a0|=lamp_byte(v->lamps_known,&v->lamp[i])<<(8*i);
 uint32_t sentinel=(v->sentinel_master[0]?1u:0u)|(v->sentinel_master[1]?2u:0u)|(v->sentinel_sync[0]?4u:0u)|(v->sentinel_sync[1]?8u:0u);
 uint32_t sides=((unsigned)(v->side_deck[0]>0&&v->side_deck[0]<=4?v->side_deck[0]:0))|
                ((unsigned)(v->side_deck[1]>0&&v->side_deck[1]<=4?v->side_deck[1]:0))<<4;
 uint32_t a1=int8_or_unknown(v->ui_known,v->ui_master)|int8_or_unknown(v->engine_known,v->engine_master)<<8|
             byte_of(v->sentinel_known,(int)sentinel)<<16|sides<<24;
 uint32_t stat=(v->stat_master[0]?1u:0u)|(v->stat_master[1]?2u:0u)|(v->stat_sync[0]?4u:0u)|(v->stat_sync[1]?8u:0u)|
               (v->stat_flash[0]?16u:0u)|(v->stat_flash[1]?32u:0u);
 uint32_t bridge=v->bridge_actual>=-3&&v->bridge_actual<=3?(uint32_t)(v->bridge_actual+4):1u;
 uint32_t ml=(v->ml_ui_ready?1u:0u)|(v->ml_insert_ready?2u:0u)|(v->ml_owned?4u:0u)|
             ((uint32_t)(v->ml_written+2)&15u)<<4;
 uint32_t a2=byte_of(v->engine_sync_known,(v->engine_sync[0]?1:0)|(v->engine_sync[1]?2:0))|
             byte_of(v->stat_known,(int)stat)<<8|bridge<<16|ml<<24;
 uint32_t a3=0;
 for(unsigned i=0;i<2;i++)
  a3|=byte_of(v->soft_known[i],(v->soft_loaded[i]?1:0)|(v->soft_master[i]?2:0)|(v->soft_sync[i]?4:0)|(v->soft_offset[i]?8:0))<<(8*i);
 a3|=((uint32_t)v->ml_inserted&0xffffu)<<16;
 r->args[0]=(int32_t)a0;r->args[1]=(int32_t)a1;r->args[2]=(int32_t)a2;r->args[3]=(int32_t)a3;r->status=0;
}

static int tap(const struct xz_bl_env *e,unsigned code,unsigned channel,struct xz_bench_result *r){
 int rc=e->send_key(e->ctx,code,XZ_BL_OP_KEY_PRESS,channel);
 if(rc<0)return rc;
 rc=e->send_key(e->ctx,code,XZ_BL_OP_KEY_RELEASE,channel);
 if(rc<0)rc=e->send_key(e->ctx,code,XZ_BL_OP_KEY_RELEASE,channel);
 if(rc<0){r->args[3]=1;return -EIO;}  /* a press without its release: the side now holds that key */
 r->args[0]++;return 0;
}
void xz_bl_press(const struct xz_bl_env *e,const struct xz_bench_request *q,struct xz_bench_result *r){
 memset(r->args,0,sizeof r->args);r->status=-EINVAL;
 if(!e||!e->send_key||!e->side_deck||q->deck>1||q->args[0]<XZ_BL_PRESS_MASTER||q->args[0]>XZ_BL_PRESS_DECK_SELECT||
    q->args[1]<0||q->args[1]>4)return;
 if(e->keys_available&&!e->keys_available(e->ctx)){r->status=-EOPNOTSUPP;return;}
 int side=(int)q->deck,shows=e->side_deck(e->ctx,side);
 if(shows<0){r->status=-EAGAIN;return;}
 r->args[1]=shows;
 /* A side shows DECK 1 or 3 (left), DECK 2 or 4 (right); anything else is not this side's deck. */
 if(shows!=side+1&&shows!=side+3){r->status=-EAGAIN;return;}
 if(q->args[1]&&q->args[1]!=shows){r->status=-ESTALE;return;}
 unsigned channel=(unsigned)side+1u;
 switch(q->args[0]){
 case XZ_BL_PRESS_MASTER:r->status=tap(e,XZ_BL_KEY_MASTER,channel,r);break;
 case XZ_BL_PRESS_SYNC:r->status=tap(e,XZ_BL_KEY_SYNC,channel,r);break;
 default:
  /* SHIFT twice within XZ_DS_WINDOW_MS (deck_select.h): both taps are posted back to back. */
  r->status=tap(e,XZ_BL_KEY_SHIFT,channel,r);
  if(!r->status)r->status=tap(e,XZ_BL_KEY_SHIFT,channel,r);
  break;
 }
}

#ifndef XZ_BENCH_LIGHTS_NO_RUNTIME
/* ---- Firmware 1.26 adapter. Every stock address below is covered by a code-window hash. ---- */
#include "runtime.h"
#include "clock_compat.h"
#include "deck_select.h"
#include "extra_decks.h"
#include "master_lights.h"
#include "sync_master.h"
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#if !defined(__arm__) || defined(__aarch64__)
#error bench_lights native adapter requires the XZ 32-bit ARM ABI
#endif
extern int xz_sync_link_runtime_master_observe(struct xz_master_observation *) __attribute__((weak));

/* FNV-1a 64 (master-lights/work/guard_hashes.py, stock 1.26 md5 6a7ccb45...). No stock bytes are kept here.
   The key windows are bench_keys.c's; none of the windows contains an address the mods patch. */
enum { G_KEYS = 1, G_READ = 2 };
static const struct { uint32_t address, length; uint64_t hash; unsigned use; } windows[] = {
 {0x2ea3f8u,0x09cu,0x3ab5a78786fee982ull,G_KEYS}, /* IKeyManager::sendKey */
 {0x2ea974u,0x008u,0xd8ab76f8a562dd0eull,G_KEYS}, /* its literals */
 {0x47ba58u,0x024u,0xe20e1a821e6a097aull,G_KEYS}, /* ui::KeyManager vtable: sendKey +8 */
 {0x2ecaf0u,0x100u,0xb5129cd04a221ab9ull,G_KEYS}, /* KeyMsgHandler::onMessage replays sendKey */
 {0x296430u,0x014u,0x5f1f7bbe3b016b2full,G_KEYS}, /* IUiObjManager::getKeyManager */
 {0x296444u,0x010u,0x4568fed940cffd68ull,G_READ},     /* IUiObjManager::getLedManager: [cell]+0x68 */
 {0x2b7ce4u,0x024u,0xe02c8e0808897ddbull,G_KEYS}, /* PanelComPeerLinux::isOnMessageThread */
 {0x2b7e14u,0x090u,0x14a0d143aeede186ull,G_KEYS}, /* PanelComPeerLinux::postMessage */
 {0x47c444u,0x010u,0x87e46169202cf541ull,G_KEYS}, /* its MsgManager vtable */
 {0x2b7cdcu,0x008u,0x65492929a947d8a4ull,G_KEYS}, /* MsgManager-subobject thunk */
 {0x2b7e0cu,0x008u,0x65492929a947d8a4ull,G_KEYS}, /* MsgManager-subobject thunk */
 {0x2575d0u,0x03cu,0x176e25710eac78ecull,G_KEYS}, /* IMsgHandler::postMessageInternal */
 {0x2baf80u,0x090u,0x5f08ae6418a2670cull,G_KEYS},      /* EupRxDataCheck::checkDeckData: 0x4313 / 0x4312 per channel */
 {0x2b0534u,0x020u,0x958908d78b6dcb3dull,G_KEYS}, /* TotalMainIfUi::onKey: 0x4312 */
 {0x28e9ccu,0x00cu,0xea651468db7f7cdcull,G_KEYS},      /* PlayerInnards::onPhysicalKey: 0x4103 */
 {0x09d41cu,0x010u,0x12a3ffb4c69fc815ull,G_READ},   /* TotalMainIf master number +0x34 */
 {0x09f6f4u,0x004u,0x77bf32ada74dcc09ull,G_READ},  /* TotalMainIf singleton cell */
 {0x09f710u,0x004u,0x5668cac132c6bcaaull,G_READ},      /* TotalMainIf vtable */
 {0x04bc28u,0x030u,0x7757ad1e97f0e39aull,G_READ},/* PlayEngine::getSyncMaster: +0x24, dummy +0x15, id +0x18/+0x08 */
 {0x04bc6cu,0x04cu,0x0653755720ef4784ull,G_READ},     /* PlayEngine::isSyncOn: +0x30 vector, 116-byte states */
 {0x13aa08u,0x018u,0xe39ab3350270cf6bull,G_READ},   /* CmnFunc_CmnInfo_IsMasterON / IsSyncON */
 {0x27f6f0u,0x030u,0x26eb55575f58475dull,G_READ},     /* checkLedStat reads ui::Player +0x20b */
 {0x27f458u,0x054u,0x9d622237b9043ad4ull,G_READ},
 {0x2b0e10u,0x0c8u,0x79deb2d7b26d9435ull,G_READ},     /* LedManager: active list +48, refresh list +72 */
 {0x2936c0u,0x0b0u,0x2ef3d438f3a7f30full,G_READ},
};
#define UI_OBJ_MANAGER_CELL 0x1c8a308u
#define KEY_MANAGER_VPTR 0x47ba58u
#define MAIN_MSG_MANAGER_CELL 0x1c89c94u
#define PEER_MSG_VPTR 0x47c444u
#define PEER_BASE_MSG_VPTR 0x479bd4u
#define PEER_TID_OFFSET 0x5cu
#define PEER_RUNNING_OFFSET 0x48u
#define FN_SEND_KEY 0x2ea3f8u
#define TOTAL_MAIN_CELL 0x010ea780u
#define TOTAL_MAIN_VPTR 0x003d4a08u
#define PLAY_ENGINE_CELL 0x010e9688u
#define CMN_INFO 0x0222b11cu
#define LED_ACTIVE 48u

static unsigned guard_state;
static uint64_t fnv(uint64_t h,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++){h^=p[i];h*=0x100000001b3ull;}return h;}
static unsigned guards(void){
 unsigned state=__atomic_load_n(&guard_state,__ATOMIC_ACQUIRE);
 if(state)return state&0xffu;
 unsigned failed=0;unsigned char buffer[256];
 for(unsigned i=0;i<sizeof windows/sizeof windows[0];i++){
  uint64_t h=0xcbf29ce484222325ull;
  for(uint32_t at=0;at<windows[i].length;at+=sizeof buffer){
   uint32_t n=windows[i].length-at<sizeof buffer?windows[i].length-at:(uint32_t)sizeof buffer;
   if(xz_read_memory(windows[i].address+at,buffer,n))return 0;
   h=fnv(h,buffer,n);
  }
  if(h!=windows[i].hash)failed|=windows[i].use;
 }
 unsigned passed=(G_KEYS|G_READ)&~failed;
 __atomic_store_n(&guard_state,0x100u|passed,__ATOMIC_RELEASE);
 if(failed)xz_log("Bench lights: stock code differs from firmware 1.26; the affected bench operations are refused");
 return passed;
}
static int sane(uint32_t p,uint32_t span){return p>=0x10000u&&!(p&3u)&&p<=UINT32_MAX-span;}
static int word(uint32_t address,uint32_t *out){return xz_read_memory(address,out,4);}

/* As bench_keys.c send_key: the stock key path through the UiMain message thread. */
static int msg_manager(uint32_t preferred,uint32_t *out){
 uint32_t mm=preferred,vptr,tid;unsigned char running=0;
 if(!mm&&word(MAIN_MSG_MANAGER_CELL,&mm))return -EAGAIN;
 if(!sane(mm,0x100)||word(mm,&vptr)||(vptr!=PEER_MSG_VPTR&&vptr!=PEER_BASE_MSG_VPTR)||word(mm+PEER_TID_OFFSET,&tid)||
    xz_read_memory(mm+PEER_RUNNING_OFFSET,&running,1)||!running)return -EAGAIN;
 if(tid&&tid==(uint32_t)syscall(SYS_gettid))return -EDEADLK;
 *out=mm;return 0;
}
static int send_key(void *ctx,unsigned code,unsigned operation,unsigned channel){
 (void)ctx;
 if(!(guards()&G_KEYS))return -EOPNOTSUPP;
 uint32_t manager,km,vptr,input,handler,own_mm,mm;
 if(word(UI_OBJ_MANAGER_CELL,&manager)||!sane(manager,0x100)||word(manager+0x64,&km)||!sane(km,0x100)||
    word(km,&vptr)||vptr!=KEY_MANAGER_VPTR)return -EAGAIN;
 if(word(km+0x84,&input)||!input||word(km+0x58,&handler)||!sane(handler,8)||word(km+4,&own_mm))return -EAGAIN;
 int rc=msg_manager(own_mm,&mm);
 if(rc)return rc;
 ((void (*)(void *,int,int,int,int32_t,uint32_t,int32_t))(uintptr_t)FN_SEND_KEY)(
  (void *)(uintptr_t)km,(int)code,(int)operation,(int)channel,0,0u,0);
 return 0;
}
static int side_deck(void *ctx,int side){
 (void)ctx;struct xz_ds_selection s;
 if(side<0||side>1||!xz_runtime_deck_selection_snapshot(&s))return -EAGAIN;
 return s.deck[side];
}
static int keys_available(void *ctx){(void)ctx;return !!(guards()&G_KEYS);}

/* The lamp records in the list the panel is sent, two equal reads in a row. */
struct list_meta { uint16_t count,ids,slots; uint32_t records,index; };
static int list_meta(uint32_t stat,struct list_meta *m){
 uint16_t c;uint32_t rec,idx;uint16_t dims[2];
 if(xz_read_memory(stat+8,&c,2)||word(stat+12,&rec)||xz_read_memory(stat+16,dims,4)||word(stat+20,&idx))return -1;
 if(c>128u||!sane(rec,128u*0x2cu)||dims[0]!=XZ_ML_STAT_IDS||dims[1]!=XZ_ML_STAT_SLOTS||!sane(idx,XZ_ML_STAT_IDS*4u))return -1;
 m->count=c;m->ids=dims[0];m->slots=dims[1];m->records=rec;m->index=idx;return 0;
}
static int read_lamps(struct xz_bl_lamp lamp[4]){
 static const unsigned id[4]={XZ_ML_ID_MASTER,XZ_ML_ID_BEAT_SYNC,XZ_ML_ID_MASTER,XZ_ML_ID_BEAT_SYNC},ch[4]={1,1,2,2};
 uint32_t manager,leds;
 if(word(UI_OBJ_MANAGER_CELL,&manager)||!sane(manager,0x6c)||word(manager+0x68,&leds)||!sane(leds,0x80))return -1;
 for(int attempt=0;attempt<3;attempt++){
  struct list_meta a,b;struct xz_bl_lamp got[4];int bad=0;
  memset(got,0,sizeof got);
  if(list_meta(leds+LED_ACTIVE,&a))continue;
  for(unsigned i=0;i<4&&!bad;i++){
   uint16_t n;unsigned char r[0x2c];
   if(xz_read_memory(a.index+(id[i]*a.slots+ch[i]-1)*2u,&n,2)){bad=1;break;}
   if(!n)continue;
   uint32_t rid,rch;
   if(n>a.count||xz_read_memory(a.records+(uint32_t)(n-1)*0x2cu,r,sizeof r)){bad=1;break;}
   memcpy(&rid,r,4);memcpy(&rch,r+4,4);
   if(rid!=id[i]||rch!=ch[i]){bad=1;break;}
   got[i].present=1;got[i].forced=r[0x18]!=0;
   memcpy(&got[i].state,r+0x10,4);memcpy(&got[i].brightness,r+0x14,4);memcpy(&got[i].period,r+0x1c,4);
   got[i].rgb=(uint32_t)r[0x28]<<16|(uint32_t)r[0x29]<<8|r[0x2a];
  }
  if(bad||list_meta(leds+LED_ACTIVE,&b)||memcmp(&a,&b,sizeof a))continue;
  memcpy(lamp,got,sizeof got);return 0;
 }
 return -1;
}
static int read_ui_master(int32_t *out){
 uint32_t object,vptr;
 if(word(TOTAL_MAIN_CELL,&object)||!sane(object,0x40)||word(object,&vptr)||vptr!=TOTAL_MAIN_VPTR)return -1;
 return word(object+0x34,(uint32_t *)out);
}
static int read_engine(int32_t *master,int sync[2]){
 uint32_t engine,sm,vec[2];unsigned char state[20];
 if(word(PLAY_ENGINE_CELL,&engine)||!sane(engine,0x34)||word(engine+0x24,&sm)||!sane(sm,0x20)||
    xz_read_memory(sm+8,state,sizeof state))return -1;
 memcpy(master,state+(state[0x15-8]?0x18-8:0),4);
 if(word(engine+0x30,&vec[0])||!sane(vec[0],8)||xz_read_memory(vec[0],vec,8)||vec[0]<2||vec[0]>8||!sane(vec[1],8u*116u))return -2;
 for(unsigned d=0;d<2;d++){
  unsigned char f[3];
  if(xz_read_memory(vec[1]+d*116u+9u,f,3))return -2;
  sync[d]=f[1]?f[2]:f[0];
 }
 return 0;
}
static int read_stat(struct xz_bl_view *v){
 uint32_t manager,array,count;
 if(word(UI_OBJ_MANAGER_CELL,&manager)||!sane(manager,0x4c)||word(manager+0x40,&array)||word(manager+0x48,&count)||
    count<2||!sane(array,8))return -1;
 for(unsigned d=0;d<2;d++){
  uint32_t player;unsigned char channel,bits;
  if(word(array+d*4u,&player)||!sane(player,0x210)||xz_read_memory(player+0x26,&channel,1)||channel!=d+1||
     xz_read_memory(player+0x20b,&bits,1))return -1;
  v->stat_master[d]=!!(bits&4);v->stat_sync[d]=!!(bits&8);v->stat_flash[d]=!!(bits&16);
 }
 return 0;
}
static int read_sentinel(struct xz_bl_view *v){
 for(unsigned d=0;d<2;d++){
  uint32_t m,s;
  if(word(CMN_INFO+d*16u+0x1500u,&m)||word(CMN_INFO+d*16u+0x14fcu,&s))return -1;
  v->sentinel_master[d]=m==1;v->sentinel_sync[d]=s==1;
 }
 return 0;
}
static uint32_t now_ms(void){
 struct timespec t;
 if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;
 return (uint32_t)((uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u);
}
static void read_view(struct xz_bl_view *v){
 memset(v,0,sizeof *v);v->bridge_actual=XZ_MASTER_UNKNOWN;
 if(guards()&G_READ){
  v->lamps_known=!read_lamps(v->lamp);
  v->ui_known=!read_ui_master(&v->ui_master);
  int rc=read_engine(&v->engine_master,v->engine_sync);
  v->engine_known=rc==0||rc==-2;v->engine_sync_known=rc==0;
  v->stat_known=!read_stat(v);
  v->sentinel_known=!read_sentinel(v);
 }
 v->side_deck[0]=side_deck(NULL,0);v->side_deck[1]=side_deck(NULL,1);
 struct xz_master_observation a;
 if(xz_sync_link_runtime_master_observe&&xz_sync_link_runtime_master_observe(&a)&&a.revision&&a.operator_epoch&&
    (uint32_t)(now_ms()-a.observed_ms)<=XZ_MASTER_MAX_AGE_MS)v->bridge_actual=a.deck;
 static struct xz_extra_decks s; /* tick thread only; too large for the caller's stack budget */
 if(xz_extra_decks_snapshot(&s))for(unsigned i=0;i<2;i++){
  const struct xz_extra_deck *d=&s.deck[i];
  v->soft_known[i]=1;v->soft_loaded[i]=d->loaded;v->soft_master[i]=d->loaded&&d->master;
  v->soft_sync[i]=d->loaded&&d->sync.requested;v->soft_offset[i]=v->soft_sync[i]&&d->sync.offset;
 }
 struct xz_ml_status m;memset(&m,0,sizeof m);
 xz_master_lights_status(&m);
 v->ml_ui_ready=m.ui_ready;v->ml_insert_ready=m.insert_ready;v->ml_owned=m.owned;v->ml_written=m.written;
 v->ml_inserted=m.inserted;
}

void xz_bench_lights_apply(const struct xz_bench_request *q,struct xz_bench_result *r){
 r->status=-EINVAL;
 if(q->op==XZ_BL_OP_LIGHTS&&q->deck==4){
  struct xz_bl_view v;read_view(&v);
  xz_bl_encode(&v,q->args[0],r);
 }else if(q->op==XZ_BL_OP_PRESS){
  const struct xz_bl_env env={.ctx=NULL,.send_key=send_key,.side_deck=side_deck,.keys_available=keys_available};
  xz_bl_press(&env,q,r);
 }
}
#endif
