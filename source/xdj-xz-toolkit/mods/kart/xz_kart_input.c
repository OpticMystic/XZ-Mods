/* SPDX-License-Identifier: MIT */
#include "xz_kart_input.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#define LOAD(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define STORE(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
_Static_assert(sizeof(uint32_t)==4 && sizeof(struct xz_kart_wire)==896,"wire ABI");
_Static_assert(sizeof(float)==4,"firmware float ABI");
static void begin(uint32_t *seq) { __atomic_add_fetch(seq,1,__ATOMIC_SEQ_CST); }
static void end(uint32_t *seq) { __atomic_add_fetch(seq,1,__ATOMIC_SEQ_CST); }
static void fader_mode(struct xz_kart_wire *);
static int valid(const struct xz_kart_wire *w) {
 return w && LOAD(&w->magic)==XZ_KART_INPUT_MAGIC && LOAD(&w->version)==XZ_KART_INPUT_VERSION && LOAD(&w->bytes)==sizeof(*w);
}
static int live(const struct xz_kart_wire *w,uint32_t now) {
 return LOAD(&w->owner) && LOAD(&w->client_epoch)==LOAD(&w->epoch)
  && (uint32_t)(now-LOAD(&w->client_ms))<XZ_KART_LEASE_MS
  && (uint32_t)(now-LOAD(&w->host_ms))<XZ_KART_LEASE_MS;
}
static void clear_payload(struct xz_kart_host *h) {
 struct xz_kart_wire *w=h->wire;
 begin(&w->payload_seq);
 STORE(&w->armed_mask,0);
 STORE(&w->requested_fader_mask,0);STORE(&w->fader_mask,0);STORE(&w->fader_ready_mask,0);
 for(unsigned d=0;d<2;d++) {
  h->blocked[d]=h->owned[d];
  STORE(&w->buttons[d],0);STORE(&w->jog_delta[d],0);STORE(&w->jog_ms[d],0);
  STORE(&w->event_seq[d],0);
  STORE(&w->fader_value[d],0);
  STORE(&w->requested_mode_seq[d],0);STORE(&w->acknowledged_mode_seq[d],0);
 }
 STORE(&w->payload_epoch,LOAD(&w->epoch));end(&w->payload_seq);
}
void xz_kart_host_init(struct xz_kart_host *h,struct xz_kart_wire *w) {
 memset(h,0,sizeof(*h));memset(w,0,sizeof(*w));h->wire=w;
 h->display_ready=1;
 w->version=XZ_KART_INPUT_VERSION;w->bytes=sizeof(*w);STORE(&w->magic,XZ_KART_INPUT_MAGIC);
}
int xz_kart_host_grant(struct xz_kart_host *h,uint32_t owner,uint32_t epoch,unsigned mask,uint32_t now) {
 if(!h || !valid(h->wire) || !owner || !epoch || !mask || (mask&~3u))return -1;
 struct xz_kart_wire *w=h->wire;
 if(LOAD(&w->owner) || epoch==LOAD(&w->epoch))return -1;
 begin(&w->grant_seq);
 STORE(&w->epoch,epoch);STORE(&w->deck_mask,mask);STORE(&w->host_ms,now);
 STORE(&w->client_ms,now);STORE(&w->client_epoch,0);STORE(&w->owner,owner);
 STORE(&w->requested_owner,0);STORE(&w->requested_epoch,0);
 clear_payload(h);end(&w->grant_seq);return 0;
}
int xz_kart_host_adopt(struct xz_kart_host *h,uint32_t launcher,uint32_t child,uint32_t epoch,uint32_t now) {
 struct xz_kart_wire *w=h->wire;
 if(!child || LOAD(&w->owner)!=launcher || LOAD(&w->epoch)!=epoch || LOAD(&w->client_epoch))return -1;
 begin(&w->grant_seq);STORE(&w->owner,child);STORE(&w->client_ms,now);STORE(&w->host_ms,now);
 STORE(&w->requested_owner,0);STORE(&w->requested_epoch,0);end(&w->grant_seq);return 0;
}
void xz_kart_host_revoke(struct xz_kart_host *h) {
 struct xz_kart_wire *w=h->wire;
 begin(&w->grant_seq);STORE(&w->owner,0);STORE(&w->deck_mask,0);
 clear_payload(h);end(&w->grant_seq);
}
void xz_kart_host_tick(struct xz_kart_host *h,uint32_t now) {
 struct xz_kart_wire *w=h->wire;
 uint32_t timeout=LOAD(&w->client_epoch)==0?XZ_KART_STARTUP_MS:XZ_KART_LEASE_MS;
 if(LOAD(&w->owner) && (uint32_t)(now-LOAD(&w->client_ms))>=timeout) {
  xz_kart_host_revoke(h);return;
 }
 STORE(&w->host_ms,now);
 if(h->display_ready && live(w,now)) {
  fader_mode(w);
  uint32_t armed=LOAD(&w->armed_mask),mask=LOAD(&w->deck_mask);
  for(unsigned deck=0;deck<2;deck++)
   if((mask&(1u<<deck)) && !h->physical_down[deck] && !h->owned[deck])armed|=1u<<deck;
  STORE(&w->armed_mask,armed);
 }
}
void xz_kart_host_display(struct xz_kart_host *h,int ready) {
 h->display_ready=!!ready;
 if(!ready && LOAD(&h->wire->armed_mask))xz_kart_host_revoke(h);
}
static uint32_t u32(const unsigned char *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int32_t signed32(uint32_t n) { return n<=INT32_MAX?(int32_t)n:-(int32_t)(~n)-1; }
static void fader_mode(struct xz_kart_wire *w) {
 for(unsigned d=0;d<2;d++) {
  uint32_t bit=1u<<d,seq=LOAD(&w->requested_mode_seq[d]);
  if(seq&1u)continue;
  uint32_t requested=LOAD(&w->requested_fader_mask)&LOAD(&w->deck_mask)&bit;
  if(seq!=LOAD(&w->requested_mode_seq[d]))continue;
  uint32_t old=LOAD(&w->fader_mask);
  if((old&bit)==requested && seq==LOAD(&w->acknowledged_mode_seq[d]))continue;
  begin(&w->payload_seq);
  STORE(&w->fader_ready_mask,LOAD(&w->fader_ready_mask)&~bit);STORE(&w->fader_value[d],0);
  STORE(&w->fader_mask,(old&~bit)|requested);STORE(&w->acknowledged_mode_seq[d],seq);
  end(&w->payload_seq);
 }
}
static void buttons_changed(struct xz_kart_wire *w,unsigned deck,uint32_t value,uint32_t now) {
 if(LOAD(&w->buttons[deck])==value)return;
 begin(&w->payload_seq);
 uint32_t seq=LOAD(&w->event_seq[deck])+1;
 struct xz_kart_transition *e=&w->events[deck][seq%XZ_KART_HISTORY];
 STORE(&e->buttons,value);STORE(&e->when,now);STORE(&e->sequence,seq);
 STORE(&w->buttons[deck],value);STORE(&w->event_seq[deck],seq);end(&w->payload_seq);
}
int xz_kart_host_event(struct xz_kart_host *h,unsigned deck,const void *event,size_t bytes,uint32_t now) {
 if(!h || !valid(h->wire) || !event || bytes<16 || deck>1)return 0;
 const unsigned char *p=event;
 unsigned key=(unsigned)p[8]|((unsigned)p[9]<<8),op=p[11]&15u;
 struct xz_kart_wire *w=h->wire;
 int active=h->display_ready && live(w,now) && (LOAD(&w->armed_mask)&(1u<<deck));
 if(active)fader_mode(w);
 if(key==0x410a && op==5) {
  if(!active || !(LOAD(&w->fader_mask)&(1u<<deck)) || bytes<24)return 0;
  uint32_t bits=u32(p+16);float position;memcpy(&position,&bits,sizeof(position));
  const float deadband=0.08f; /* Includes the measured -0.0661 center offset. */
  uint32_t ready=LOAD(&w->fader_ready_mask),bit=1u<<deck;int value=0;
  if(!isfinite(position) || position < -1.0f || position > 1.0f)ready&=~bit;
  else if(position>=-deadband && position<=deadband)ready|=bit;
  else if(ready&bit) {
   /* Physical up is negative native tempo; center deadband is rescaled. */
   float drive=-position;
   value=(int)((drive>0?(drive-deadband):(drive+deadband))*50.0f/(1.0f-deadband));
   if(value>50)value=50;
   if(value < -50)value=-50;
  }
  begin(&w->payload_seq);STORE(&w->fader_value[deck],(uint32_t)value);
  STORE(&w->fader_ready_mask,ready);end(&w->payload_seq);return 1;
 }
 if(key==0x4305 && op==4) {
  if(!active)return 0;
  int32_t delta=signed32(u32(p+12));
  if(delta>50)delta=50;
  if(delta< -50)delta=-50;
  begin(&w->payload_seq);STORE(&w->jog_delta[deck],(uint32_t)delta);
  STORE(&w->jog_ms[deck],now);end(&w->payload_seq);return 1;
 }
 /* PLAY/CUE/search/mixer and unused pad E always reach stock handling. */
 unsigned bit=key==0x4306?XZ_KART_TOUCH:key>=0x4119 && key<=0x4120?1u<<(key-0x4119):0;
 if(!(bit&(XZ_KART_ACCEL|XZ_KART_DRIFT|XZ_KART_ITEM|XZ_KART_BRAKE|XZ_KART_MODE|XZ_KART_MENU|XZ_KART_EXIT|XZ_KART_TOUCH)))return 0;
 if(op==0 || op==1)h->physical_down[deck]|=bit;
 else if(op==2 || op==3)h->physical_down[deck]&=~bit;
 if(op==2 || op==3) {
  if(!(h->owned[deck]&bit))return 0;
  h->owned[deck]&=~bit;h->blocked[deck]&=~bit;
  buttons_changed(w,deck,LOAD(&w->buttons[deck])&~bit,now);return 1;
 }
 if(op==0 && active) {
  if(h->blocked[deck]&bit)return 1;
  h->owned[deck]|=bit;
  buttons_changed(w,deck,LOAD(&w->buttons[deck])|bit,now);return 1;
 }
 return (op==0 || op==1) && (h->owned[deck]&bit)?1:0;
}
int xz_kart_host_player_event(struct xz_kart_host *h,const void *self,size_t self_bytes,
 const void *event,size_t event_bytes,uint32_t now) {
 if(!self || self_bytes<=0x26)return 0;
 unsigned channel=((const unsigned char *)self)[0x26];
 return channel==1 || channel==2?xz_kart_host_event(h,channel-1,event,event_bytes,now):0;
}
int xz_kart_read(struct xz_kart_reader *r,struct xz_kart_wire *w,unsigned deck,uint32_t now,struct xz_kart_sample *out) {
 memset(out,0,sizeof(*out));
 if(!r || !valid(w) || deck>1)return 0;
 uint32_t grant=LOAD(&w->grant_seq),mask=LOAD(&w->deck_mask);
 if((grant&1u) || LOAD(&w->owner)!=r->owner || LOAD(&w->epoch)!=r->epoch
  || !(mask&(1u<<deck)) || (uint32_t)(now-LOAD(&w->host_ms))>=XZ_KART_LEASE_MS)return 0;
 uint32_t timeout=LOAD(&w->client_epoch)==0?XZ_KART_STARTUP_MS:XZ_KART_LEASE_MS;
 if((uint32_t)(now-LOAD(&w->client_ms))>=timeout)return 0;
 STORE(&w->client_ms,now);STORE(&w->client_epoch,r->epoch);
 uint32_t seq=LOAD(&w->payload_seq);
 if((seq&1u) || LOAD(&w->payload_epoch)!=r->epoch)return 0;
 uint32_t mode_seq=LOAD(&w->requested_mode_seq[deck]);
 if((mode_seq&1u) || mode_seq!=LOAD(&w->acknowledged_mode_seq[deck]) ||
  ((LOAD(&w->requested_fader_mask)^LOAD(&w->fader_mask))&(1u<<deck)))return 0;
 uint32_t physical=LOAD(&w->buttons[deck]),at=LOAD(&w->jog_ms[deck]);
 uint32_t latest=LOAD(&w->event_seq[deck]),cursor=r->cursor[deck],buttons=r->delivered[deck],changed=0;
 unsigned overflow=r->overflow[deck] || (uint32_t)(latest-cursor)>XZ_KART_HISTORY;
 if(overflow) {cursor=latest;buttons=0;} else {
  while(cursor!=latest) {
   uint32_t next=cursor+1;
   const struct xz_kart_transition *e=&w->events[deck][next%XZ_KART_HISTORY];
   if(LOAD(&e->sequence)!=next)return 0;
   uint32_t value=LOAD(&e->buttons),difference=buttons^value;
   if(difference&changed)break; /* Preserve down/up as separate commands. */
   changed|=difference;buttons=value;cursor=next;
  }
 }
 int32_t delta=signed32(LOAD(&w->jog_delta[deck]));
 unsigned fader=!!(LOAD(&w->fader_mask)&(1u<<deck)),ready=!!(LOAD(&w->fader_ready_mask)&(1u<<deck));
 int32_t throttle=signed32(LOAD(&w->fader_value[deck]));
 if(seq!=LOAD(&w->payload_seq) || grant!=LOAD(&w->grant_seq) || mode_seq!=LOAD(&w->requested_mode_seq[deck]))return 0;
 r->mask=mask;r->cursor[deck]=cursor;r->delivered[deck]=buttons;
 r->overflow[deck]=overflow && physical!=0;
 if(r->overflow[deck])return 0;
 out->buttons=buttons;
 out->fader_mode=fader;out->fader_ready=ready;
 if(fader && ready && throttle>=-50 && throttle<=50)out->throttle=(int8_t)throttle;
 if(delta>50)delta=50;
 if(delta< -50)delta=-50;
 /* Clockwise physical delta steers right; native right turn is negative. */
 if((uint32_t)(now-at)<XZ_KART_JOG_MS)out->turn=(int16_t)(-delta*16);
 return 1;
}
void xz_kart_command(struct xz_kart_reader *r,unsigned seat,const struct xz_kart_sample *s,
 int active,uint32_t now,struct xz_kart_command *out) {
 memset(out,0,sizeof(*out));if(seat>1)return;
 if(!active) { r->exit_armed[seat]=0;return; }
 out->turn=s->turn;
 if(s->fader_mode) {
  out->buttons|=XZ_KART_FADER_BUTTON;
  out->forward=s->fader_ready?s->throttle:0;
  if(s->buttons&XZ_KART_BRAKE)out->forward=0;
  out->buttons|=out->forward>0?1:8;
 } else {
  if(s->buttons&XZ_KART_ACCEL){out->buttons|=1;out->forward=50;}
  if(s->buttons&XZ_KART_BRAKE){out->buttons|=8;out->forward-=25;}
 }
 if(s->buttons&XZ_KART_DRIFT)out->buttons|=4;
 if(s->buttons&XZ_KART_ITEM)out->buttons|=16;
 out->menu_pressed=!!((s->buttons&XZ_KART_MENU) && !(r->previous[seat]&XZ_KART_MENU));
 out->mode_pressed=!!((s->buttons&XZ_KART_MODE) && !(r->previous[seat]&XZ_KART_MODE));
 if(s->buttons&XZ_KART_EXIT) {
  if(!r->exit_armed[seat]){r->exit_armed[seat]=1;r->exit_started[seat]=now;}
  out->exit_requested=(uint32_t)(now-r->exit_started[seat])>=XZ_KART_EXIT_MS;
 } else r->exit_armed[seat]=0;
 r->previous[seat]=s->buttons;
}
