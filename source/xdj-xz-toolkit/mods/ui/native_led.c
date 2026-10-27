#include "native_led.h"
#include <string.h>

static uint32_t word(const unsigned char *p) {
 uint32_t v;memcpy(&v,p,sizeof(v));return v;
}
static void put(unsigned char *p,uint32_t v) {memcpy(p,&v,sizeof(v));}

/* One qualification entry. Zero keeps every status record byte-identical. */
const struct xz_led_keysync_spec xz_led_keysync_mapping={0};
__attribute__((visibility("default"))) struct xz_led_probe xz_mods_led_probe_v1={.version=1};
/* One native input-thread writer. Fixed id slots retain latest words and change
   counts; read only entries with the same even sequence before/after capture. */
void xz_native_led_probe(const void *records,size_t bytes,unsigned count,unsigned channel){
 const unsigned char *p=records;
 if(!p||channel<1||channel>2||count>XZ_LED_MAX_RECORDS||bytes<(size_t)count*XZ_LED_RECORD_BYTES)return;
 for(unsigned i=0;i<count;i++,p+=XZ_LED_RECORD_BYTES){
  uint32_t id=word(p);if(word(p+4)!=channel||(id>=0x12&&id<=0x19))continue;
  struct xz_led_probe_entry *slot=NULL;
  for(unsigned j=0;j<XZ_LED_PROBE_IDS;j++){
   struct xz_led_probe_entry *e=&xz_mods_led_probe_v1.entry[channel-1][j];
   if(!e->seen||e->id==id){slot=e;break;}
  }
  if(!slot){__atomic_add_fetch(&xz_mods_led_probe_v1.overflow[channel-1],1,__ATOMIC_RELAXED);continue;}
  __atomic_add_fetch(&slot->sequence,1,__ATOMIC_ACQ_REL);
  int changed=0;slot->id=id;
  for(unsigned j=0;j<8;j++){
   uint32_t v=word(p+8+j*4);changed|=slot->words[j]!=v;
   __atomic_store_n(&slot->words[j],v,__ATOMIC_RELAXED);
  }
  if(changed)slot->changes++;if(slot->seen!=UINT32_MAX)slot->seen++;
  __atomic_add_fetch(&slot->sequence,1,__ATOMIC_RELEASE);
 }
}
size_t xz_native_led_keysync(void *records,size_t bytes,unsigned count,unsigned channel,int synced,const struct xz_led_keysync_spec *spec){
 unsigned char *p=records;size_t changed=0;
 if(!p||!spec||!spec->enabled||!synced||channel<1||channel>2||count>XZ_LED_MAX_RECORDS||
    bytes<(size_t)count*XZ_LED_RECORD_BYTES|| (spec->id>=0x12&&spec->id<=0x19))return 0;
 for(unsigned i=0;i<count;i++,p+=XZ_LED_RECORD_BYTES){
  if(word(p)!=spec->id||word(p+4)!=channel||p[0x18])continue;
  for(unsigned j=0;j<8;j++)if(j!=4)put(p+8+j*4,(word(p+8+j*4)&~spec->mask[j])|(spec->blink[j]&spec->mask[j]));
  changed++;
 }
 return changed;
}

size_t xz_native_led_apply(void *records,size_t bytes,unsigned count,
                           unsigned channel,xz_led_color_fn color) {
 unsigned char *p=records;size_t changed=0;
 if(!p||!color||channel<1||channel>2||count>XZ_LED_MAX_RECORDS||
    bytes<(size_t)count*XZ_LED_RECORD_BYTES)return 0;
 for(unsigned i=0;i<count;i++,p+=XZ_LED_RECORD_BYTES) {
  uint32_t id=word(p),rgb=0;int enabled=0;
  /* Forced native status has priority (e.g. device caution/test state). */
  if(word(p+4)!=channel||id<0x12||id>0x19||p[0x18])continue;
  int mode=color((int)channel-1,(int)id-0x12,&rgb,&enabled);
  if(!mode)continue;
  if(mode==XZ_LED_HUE){
   unsigned peak=p[0x28];
   if(p[0x29]>peak)peak=p[0x29];if(p[0x2a]>peak)peak=p[0x2a];
   for(unsigned k=0;k<3;k++)p[0x28+k]=(unsigned char)((((rgb>>(16-8*k))&255u)*peak+127u)/255u);
   changed++;continue;
  }
  put(p+8,0);put(p+0xc,0);
  put(p+0x10,1);put(p+0x14,enabled?0:1);
  put(p+0x1c,0);put(p+0x20,0);put(p+0x24,0);
  p[0x28]=(unsigned char)((rgb>>16)&255u);
  p[0x29]=(unsigned char)((rgb>>8)&255u);
  p[0x2a]=(unsigned char)(rgb&255u);
  /* Leave force clear so the next stock update can restore ordinary cues. */
  changed++;
 }
 return changed;
}

int xz_extra_pad_led(const struct xz_extra_deck *deck,int pad,uint32_t *rgb,int *enabled){
 if(!deck||!rgb||!enabled||pad<0||pad>=XZ_EXTRA_HOT_CUES)return XZ_LED_STOCK;
 *enabled=0;*rgb=0;
 if(!deck->loaded)return XZ_LED_REPLACE;
 switch(deck->pad_mode){
  case XZ_EXTRA_PAD_HOT_CUE:case XZ_EXTRA_PAD_CALL:{
   const struct xz_extra_cue *cue=&deck->hot_cue[pad];
   int lit=(deck->known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_HOT_CUES))&&cue->set;
   uint32_t color=cue->rgb&0xffffffu;
   *enabled=lit;*rgb=!lit?0:color?color:cue->end_ms>cue->ms?XZ_EXTRA_PAD_LOOP_DEFAULT_RGB:XZ_EXTRA_PAD_DEFAULT_RGB;
   break;
  }
  /* Every pad is available; the pad whose loop runs is marked (manual p.56: it flashes). */
  case XZ_EXTRA_PAD_BEAT_LOOP:case XZ_EXTRA_PAD_SLIP_LOOP:
   *enabled=1;
   *rgb=deck->active_pad==pad?XZ_EXTRA_PAD_ACTIVE_RGB:
        deck->pad_mode==XZ_EXTRA_PAD_BEAT_LOOP?XZ_EXTRA_PAD_BEAT_LOOP_RGB:XZ_EXTRA_PAD_SLIP_LOOP_RGB;
   break;
  case XZ_EXTRA_PAD_BEAT_JUMP:*enabled=1;*rgb=xz_jump_page2_color(pad);break;
  default:break;
 }
 return XZ_LED_REPLACE;
}

__attribute__((visibility("default"))) struct xz_led_pad_probe xz_mods_led_pad_probe_v1={.version=1};
/* Native pad records by channel, native pad page and pad: the words and colour stock writes, so
   software-deck pad colours can be matched to captured stock values. Same reader rule as the probe. */
void xz_native_led_pad_probe(const void *records,size_t bytes,unsigned count,unsigned channel,int page){
 const unsigned char *p=records;
 if(!p||channel<1||channel>2||page<0||page>3||count>XZ_LED_MAX_RECORDS||bytes<(size_t)count*XZ_LED_RECORD_BYTES)return;
 for(unsigned i=0;i<count;i++,p+=XZ_LED_RECORD_BYTES){
  uint32_t id=word(p);if(word(p+4)!=channel||id<0x12||id>0x19)continue;
  struct xz_led_pad_probe_entry *e=&xz_mods_led_pad_probe_v1.entry[channel-1][page][id-0x12];
  __atomic_add_fetch(&e->sequence,1,__ATOMIC_ACQ_REL);
  for(unsigned j=0;j<8;j++)__atomic_store_n(&e->words[j],word(p+8+j*4),__ATOMIC_RELAXED);
  __atomic_store_n(&e->rgb,word(p+0x28),__ATOMIC_RELAXED);
  if(e->seen!=UINT32_MAX)e->seen++;
  __atomic_add_fetch(&e->sequence,1,__ATOMIC_RELEASE);
 }
}

#ifdef XZ_JOG_DISPLAY_PROOF
#include <stdio.h>
extern void xz_log(const char *) __attribute__((weak));
/* One native input writer; the connector logs only meaningful MT transitions.
 * Unchanged state leaves the retained observation stable. */
struct mt_feedback_slot { uint32_t seq, value[7]; };
static struct mt_feedback_slot mt_feedback[2],sync_feedback[2];
static void feedback_record(struct mt_feedback_slot *s,const uint32_t value[7]){
 int changed=!__atomic_load_n(&s->seq,__ATOMIC_RELAXED);
 for(unsigned i=0;i<7;i++)changed|=__atomic_load_n(&s->value[i],__ATOMIC_RELAXED)!=value[i];
 if(!changed)return;
 __atomic_add_fetch(&s->seq,1,__ATOMIC_ACQ_REL);
 for(unsigned i=0;i<7;i++)__atomic_store_n(&s->value[i],value[i],__ATOMIC_RELAXED);
 __atomic_add_fetch(&s->seq,1,__ATOMIC_RELEASE);
}
static void mt_feedback_record(unsigned side,const uint32_t value[7]){feedback_record(&mt_feedback[side],value);}
static void report_feedback(struct mt_feedback_slot slots[2],uint32_t reported[2],const char *name,int quiet){
 if(!xz_log)return;
 for(unsigned side=0;side<2;side++){
  struct mt_feedback_slot *s=&slots[side];uint32_t v[7];
  uint32_t before=__atomic_load_n(&s->seq,__ATOMIC_ACQUIRE);
  if(!before||(before&1u)||before==reported[side])continue;
  for(unsigned i=0;i<7;i++)v[i]=__atomic_load_n(&s->value[i],__ATOMIC_RELAXED);
  if(before!=__atomic_load_n(&s->seq,__ATOMIC_ACQUIRE))continue;
  if(quiet&&!reported[side]&&!v[0]&&!v[1]){reported[side]=before;continue;}
  char line[192];snprintf(line,sizeof line,"%s_LED_STATE: side=%u wanted=%u records=%u before=%u after=%u force=%u raw14=%u loaded=%u",
   name,side+1,v[0],v[1],v[2],v[3],v[4],v[5],v[6]);xz_log(line);reported[side]=before;
 }
 }
void xz_native_led_service_feedback(void){
 static uint32_t mt_reported[2],sync_reported[2];
 report_feedback(mt_feedback,mt_reported,"MT",0);
 report_feedback(sync_feedback,sync_reported,"SYNC",1);
}
#else
void xz_native_led_service_feedback(void){}
#endif

void xz_native_led_sync_master_release(struct xz_led_sync_master_owner *owner,void *records,
                                       size_t bytes,unsigned count,unsigned channel){
 if(!owner)return;
 int valid=records&&channel>=1&&channel<=2&&count<=XZ_LED_MAX_RECORDS&&
           bytes>=(size_t)count*XZ_LED_RECORD_BYTES;
 for(unsigned i=0;i<2;i++){
  struct xz_led_lamp_owner *o=&owner->lamp[i];
  if(valid&&o->active&&o->records==(uintptr_t)records&&o->row<count){
   unsigned char *r=(unsigned char *)records+(size_t)o->row*XZ_LED_RECORD_BYTES;
   if(word(r+4)==channel&&!memcmp(r,o->after,XZ_LED_RECORD_BYTES))
    memcpy(r,o->before,XZ_LED_RECORD_BYTES);
  }
  o->active=0;
 }
}
size_t xz_native_led_sync_master_apply(struct xz_led_sync_master_owner *owner,void *records,
                                     size_t bytes,unsigned count,unsigned channel,const struct xz_extra_deck *d){
 if(!owner||!records||!d||channel<1||channel>2||count>XZ_LED_MAX_RECORDS||
    bytes<(size_t)count*XZ_LED_RECORD_BYTES)return 0;
 size_t changed=0;
 for(unsigned slot=0;slot<2;slot++){
  unsigned id=slot?XZ_LED_ID_MASTER:XZ_LED_ID_BEAT_SYNC,found=0,row=0;
  for(unsigned i=0;i<count;i++){
   unsigned char *r=(unsigned char *)records+(size_t)i*XZ_LED_RECORD_BYTES;
   if(word(r)==id&&word(r+4)==channel){found++;row=i;}
  }
  /* No allocation/index-table contract, and no ownership of foreign forced rows. */
  if(found!=1)continue;
  unsigned char *r=(unsigned char *)records+(size_t)row*XZ_LED_RECORD_BYTES;
  if(r[0x18])continue;
  struct xz_led_lamp_owner *o=&owner->lamp[slot];
  o->records=(uintptr_t)records;o->row=row;
  memcpy(o->before,r,XZ_LED_RECORD_BYTES);
  put(r+0x10,xz_led_written_state(xz_extra_lamp(d,id)));r[0x18]=1;
  memcpy(o->after,r,XZ_LED_RECORD_BYTES);o->active=1;changed++;
 }
 return changed;
}

/* Only existing, non-forced records have a proven writable extent. The saved
 * serializer proof does not establish spare record or index-table capacity. */
size_t xz_native_led_extra_deck(void *records,size_t bytes,
                                uint16_t *count_io,uint16_t *index_table,
                                unsigned max_rows,unsigned num_channels,
                                uint32_t dimmer,uint32_t now_ms,
                                unsigned channel,const struct xz_extra_deck *d){
 unsigned char *p=records;size_t changed=0;
 (void)index_table;(void)max_rows;(void)num_channels;
 (void)dimmer;
 if(!p||!count_io||!d||channel<1u||channel>2u||
    *count_io>XZ_LED_MAX_RECORDS||bytes<(size_t)*count_io*XZ_LED_RECORD_BYTES)return 0;
#ifdef XZ_JOG_DISPLAY_PROOF
 uint32_t mt[7]={!!d->master_tempo,0,0,0,0,0,!!d->loaded};
 uint32_t sync[7]={!!(d->loaded&&d->sync.requested),0,0,0,0,0,!!d->loaded};
#endif
 for(unsigned i=0;i<*count_io;i++){
  unsigned char *r=p+(size_t)i*XZ_LED_RECORD_BYTES;
  if(word(r+4)!=channel)continue;
#ifdef XZ_JOG_DISPLAY_PROOF
  if(word(r)==XZ_LED_ID_MASTER_TEMPO){mt[1]++;mt[2]=mt[3]=word(r+0x10);mt[4]=r[0x18];mt[5]=word(r+0x14);}
  if(word(r)==XZ_LED_ID_BEAT_SYNC){sync[1]++;sync[2]=sync[3]=word(r+0x10);sync[4]=r[0x18];sync[5]=word(r+0x14);}
#endif
  if(r[0x18])continue;
  int lamp=xz_extra_lamp(d,word(r));
  if(lamp<0)continue;
  uint32_t state=lamp==XZ_LED_STATE_BLINK?((now_ms/250u)&1u):xz_led_written_state(lamp);
  if(word(r+0x10)!=state){put(r+0x10,state);changed++;}
#ifdef XZ_JOG_DISPLAY_PROOF
  if(word(r)==XZ_LED_ID_MASTER_TEMPO)mt[3]=word(r+0x10);
  if(word(r)==XZ_LED_ID_BEAT_SYNC)sync[3]=word(r+0x10);
#endif
 }
#ifdef XZ_JOG_DISPLAY_PROOF
 mt_feedback_record(channel-1,mt);feedback_record(&sync_feedback[channel-1],sync);
#endif
 return changed;
}

int xz_extra_lamp(const struct xz_extra_deck *d,uint32_t id){
 if(!d)return -1;
 int on=XZ_LED_STATE_ON,off=XZ_LED_STATE_OFF,flash=XZ_LED_STATE_BLINK;
 int playing=d->loaded&&(d->playing||d->held_playing);
 switch(id){
  /* p.45: paused at the cue, CUE lights and PLAY/PAUSE flashes. Playing: both lit (owner, stock deck).
     Paused away from the cue: CUE flashes (p.45 cue adjust). */
  case XZ_LED_ID_PLAY_PAUSE:return !d->loaded?off:playing?on:flash;
  case XZ_LED_ID_CUE:return !d->loaded?off:playing||d->elapsed_ms==d->cue_ms?on:flash;
  /* pp.47-48: adjusting one loop end flashes that button rapidly and turns the other off. Otherwise
     both are lit while a track plays or loops (owner, stock deck). */
  case XZ_LED_ID_LOOP_IN:case XZ_LED_ID_LOOP_OUT:{
   int self=id==XZ_LED_ID_LOOP_IN?1:2;
   if(!d->loaded)return off;
   if(d->loop.edit)return d->loop.edit==self?flash:off;
   return playing||d->loop.active?on:off;
  }
  case XZ_LED_ID_RELOOP_EXIT:return d->loaded&&(d->loop.active||d->loop.stored)?on:off;
  case XZ_LED_ID_REVERSE:return d->loaded&&d->reverse?on:off;            /* p.42 */
  /* p.58: SLIP lights while on and flashes while playback continues in the background. */
  case XZ_LED_ID_SLIP:return !d->slip?off:d->slip_active?flash:on;
  case XZ_LED_ID_VINYL:return d->vinyl?on:off;                            /* p.43 */
  case XZ_LED_ID_BEAT_SYNC:return d->loaded&&d->sync.requested?on:off;    /* p.64: SYNC on */
  case XZ_LED_ID_MASTER:return d->loaded&&d->master?on:off;
  case XZ_LED_ID_MASTER_TEMPO:return d->master_tempo?on:off;              /* p.42 */
  case XZ_LED_ID_TEMPO_RESET:return d->tempo_reset?on:off;                /* p.35 */
  case XZ_LED_ID_PAD_HOT_CUE:case XZ_LED_ID_PAD_BEAT_LOOP:
  case XZ_LED_ID_PAD_SLIP_LOOP:case XZ_LED_ID_PAD_BEAT_JUMP:
   return d->pad_mode==(int)(id-XZ_LED_ID_PAD_HOT_CUE)||(id==XZ_LED_ID_PAD_HOT_CUE&&d->pad_mode==XZ_EXTRA_PAD_CALL)?on:off;
  default:return -1; /* pads use apply(); jog ring and timers stay native */
 }
}

#ifndef XZ_LED_PORTABLE_TEST
#include "../runtime.h"
#include "../ui_runtime.h"
#if (!defined(__arm__) || defined(__aarch64__)) && !defined(XZ_LED_CALLBACK_TEST)
#error Native LED adapter requires verified 32-bit ARM ABI
#endif
_Static_assert(sizeof(void*)==4,"XZ LED pointer ABI");
typedef void (*xz_stock_led_fn)(void *,void *);
static xz_stock_led_fn original_led;
static int enabled;
/* Bounded diagnostic counters; no input-thread logging or packet retention. */
__attribute__((visibility("default"))) uint32_t xz_mods_led_trace_v1[12] = {1};
#define TRACE(i) __atomic_fetch_add(&xz_mods_led_trace_v1[i],1,__ATOMIC_RELAXED)

/* The LED hook runs on one native input thread, so the snapshot is private to it. */
static struct xz_extra_decks extra;static int extra_side;
static struct xz_led_sync_master_owner sync_master_owner[2];
/* The pointer is borrowed only for this callback. Retained addresses are compared,
   never dereferenced without validating the current packet extent again. */
static void release_sync_master(void *player,void *packet){
 if(!player||!packet)return;
 unsigned channel=((const unsigned char *)player)[0x26];
 if(channel<1||channel>2)return;
 uint16_t count;uint32_t pointer;
 memcpy(&count,(const unsigned char *)packet+8,2);
 memcpy(&pointer,(const unsigned char *)packet+0xc,4);
 int valid=count<=XZ_LED_MAX_RECORDS&&pointer&&!(pointer&3)&&
           pointer<=UINT32_MAX-(uint32_t)count*XZ_LED_RECORD_BYTES;
 xz_native_led_sync_master_release(&sync_master_owner[channel-1],
  valid?(void *)(uintptr_t)pointer:NULL,valid?(size_t)count*XZ_LED_RECORD_BYTES:0,count,channel);
}
static int extra_pad_color(int deck,int pad,uint32_t *rgb,int *lit){
 (void)deck;return xz_extra_pad_led(&extra.deck[extra_side],pad,rgb,lit);
}

static void led_hook(void *player,void *packet) {
 unsigned char *p=packet;uint16_t count;uint32_t pointer,dimmer=0,now_ms=0,innards,mode;unsigned channel;
 /* Release even after stop, before stock has a chance to see our force flag. */
 release_sync_master(player,packet);
 original_led(player,packet);
 TRACE(1);
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE)||!player||!packet)return;
 channel=((const unsigned char*)player)[0x26];
 if(channel<1||channel>2){TRACE(2);return;}
 memcpy(&innards,(const unsigned char*)player+0x138,4);
 if(!innards||(innards&3)||innards>UINT32_MAX-0x84){TRACE(3);return;}
 if(((const unsigned char*)(uintptr_t)innards)[0x26]!=channel){TRACE(4);return;}
 memcpy(&mode,(const unsigned char*)(uintptr_t)innards+0x80,4);
 xz_ui_runtime_pad_native_page((int)channel-1,mode==0?0:(mode>=2&&mode<=4?(int)mode-1:-1));
 memcpy(&dimmer,p+0,4);memcpy(&now_ms,p+4,4);
 memcpy(&count,p+8,2);memcpy(&pointer,p+0xc,4);
 __atomic_store_n(&xz_mods_led_trace_v1[5],count,__ATOMIC_RELAXED);
 __atomic_store_n(&xz_mods_led_trace_v1[6],mode,__ATOMIC_RELAXED);
 if(count>XZ_LED_MAX_RECORDS||!pointer||(pointer&3)||
    pointer>UINT32_MAX-(uint32_t)count*XZ_LED_RECORD_BYTES)return;

 for(unsigned i=0;i<count;i++) {
  const unsigned char *entry=(const unsigned char*)(uintptr_t)pointer+i*XZ_LED_RECORD_BYTES;
  if(word(entry)>=0x12&&word(entry)<=0x19) {
   TRACE(7);
   if(word(entry+4)==channel) {TRACE(8);if(entry[0x18])TRACE(9);}
  }
 }
 xz_native_led_probe((void*)(uintptr_t)pointer,(size_t)count*XZ_LED_RECORD_BYTES,count,channel);
 xz_native_led_pad_probe((void*)(uintptr_t)pointer,(size_t)count*XZ_LED_RECORD_BYTES,count,channel,
                         mode==0?0:(mode>=2&&mode<=4?(int)mode-1:-1));
 xz_native_led_keysync((void*)(uintptr_t)pointer,(size_t)count*XZ_LED_RECORD_BYTES,count,channel,
                      xz_ui_runtime_key_synced((int)channel-1),&xz_led_keysync_mapping);
 xz_led_color_fn color=xz_ui_runtime_pad_color;
 size_t changed=0;
 if(xz_extra_decks_route().route_up){
  int snap_ok=xz_extra_decks_snapshot(&extra);
  int on_extra=snap_ok?(channel==1?extra.left_on_3:extra.right_on_4)
                      :!!(extra.control_revision&(channel==1?1u:2u));
  if(on_extra){
   extra_side=(int)channel-1;color=extra_pad_color;
   if(snap_ok)changed+=xz_native_led_sync_master_apply(&sync_master_owner[extra_side],
     (void *)(uintptr_t)pointer,(size_t)count*XZ_LED_RECORD_BYTES,count,channel,&extra.deck[extra_side]);
   changed+=xz_native_led_extra_deck((void*)(uintptr_t)pointer,
                                     (size_t)count*XZ_LED_RECORD_BYTES,&count,
                                     NULL,0,0,dimmer,now_ms,
                                     channel,&extra.deck[extra_side]);
  }
 }
 changed+=xz_native_led_apply((void*)(uintptr_t)pointer,(size_t)count*XZ_LED_RECORD_BYTES,
                      count,channel,color);
 __atomic_fetch_add(&xz_mods_led_trace_v1[10],(uint32_t)changed,__ATOMIC_RELAXED);
}

int xz_native_led_start(void) {
 static const unsigned char guard[8]={0xec,0x3e,0x9f,0xe5,0xf0,0x4f,0x2d,0xe9};
 unsigned char actual[8];
 if(!original_led) {
  if(xz_read_memory(0x27f074,actual,sizeof(actual))||memcmp(actual,guard,sizeof(guard)))return -1;
  if(xz_hook_slot(0x478d3c,0x27f074,(void*)led_hook,(void**)&original_led))return -1;
 }
 __atomic_store_n(&enabled,1,__ATOMIC_RELEASE);return 0;
}
void xz_native_led_stop(void) {__atomic_store_n(&enabled,0,__ATOMIC_RELEASE);}
#endif
