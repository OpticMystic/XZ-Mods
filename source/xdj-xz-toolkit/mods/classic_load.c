/* SPDX-License-Identifier: MIT */
#include "classic_load.h"
#include "classic_popup.h"
#include <string.h>

static int expired(uint32_t now_ms, uint32_t until_ms) { return (int32_t)(now_ms - until_ms) >= 0; }

void xz_classic_load_arm(struct xz_classic_load *state, int side, uint32_t now_ms) {
 if(!state || side < 0 || side > 1) return;
 state->side[side].phase = XZ_CL_ARMED;
 state->side[side].until_ms = now_ms + XZ_CLASSIC_LOAD_MS;
}

void xz_classic_load_cancel(struct xz_classic_load *state, int side) {
 if(state && side >= 0 && side <= 1) state->side[side].phase = XZ_CL_IDLE;
}

int xz_classic_load_source(uint32_t id_hi) {
 if(id_hi & 0xffu) return 0;
 switch((id_hi >> 8) & 0xffu) {
 case 2: return 1;
 case 3: return 2;
 default: return 0;
 }
}

enum xz_cl_verdict xz_classic_load_message(struct xz_classic_load *state, const struct xz_cl_message *message,
                                           uint32_t now_ms, struct xz_extra_deck_event *load) {
 if(!state || !message || !load || message->deck < 0 || message->deck > 1) return XZ_CL_PASS;
 enum xz_cl_phase want = message->id == XZ_CL_MSG_LOAD_ROW ? XZ_CL_ARMED :
                         message->id == XZ_CL_MSG_ROW_TRACK ? XZ_CL_ASKED : XZ_CL_IDLE;
 if(want == XZ_CL_IDLE) return XZ_CL_PASS;
 int side = message->deck;
 if(state->side[side].phase != want) return XZ_CL_PASS;
 if(expired(now_ms, state->side[side].until_ms)) { state->side[side].phase = XZ_CL_IDLE; return want==XZ_CL_ARMED?XZ_CL_REFUSE:XZ_CL_PASS; }
 if(want == XZ_CL_ARMED) {
  state->side[side].phase = XZ_CL_ASKED;
  state->side[side].until_ms = now_ms + XZ_CLASSIC_LOAD_MS;
  return XZ_CL_ASK;
 }
 state->side[side].phase = XZ_CL_IDLE;
 int source = xz_classic_load_source(message->id_hi);
 if(!source || !message->id_lo || message->id_lo == UINT32_MAX) return XZ_CL_REFUSE;
 memset(load, 0, sizeof *load);
 load->deck = side;
 load->kind = XZ_EXTRA_IN_LOAD;
 load->pressed = 1;
 load->load.source = source;
 load->load.track_id = message->id_lo;
 return XZ_CL_DELIVER;
}

#ifndef XZ_CLASSIC_LOAD_PORTABLE_TEST
#include "runtime.h"
#include "doom/native_bridge.h"
#include <pthread.h>
#if !defined(__arm__) || defined(__aarch64__)
#error Classic LOAD adapter requires verified 32-bit ARM ABI
#endif
_Static_assert(sizeof(void*)==4,"XZ classic LOAD pointer ABI");

/* Firmware 1.26. Total_MainLoadMessageProc(id, message) and the sender its 0x711 branch uses for 0x3fc,
   Total_MainSendMessage_Arg.constprop.5(id, mailbox, reply mailbox, row, then eight stack words). */
#define TOTAL_LOAD_PROC 0xbe400u
#define TOTAL_SEND_ARG 0xb9858u
#define MBX_DB_CLIENT 0x3eabe90u        /* one mailbox word per native deck */
#define MBX_TOTAL_MAIN 0x2247c94u
#define LITERAL_POOL 0xbf3d8u           /* the 0x711 branch's own references to the two mailboxes */
#define MSG_ROW_REQUEST 0x3fcu

typedef int (*load_proc_fn)(uint32_t, void *);
typedef int (*send_arg_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           uint32_t, uint32_t, uint32_t, uint32_t);
static load_proc_fn stock_proc;
static int installed;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct xz_classic_load state;
static struct xz_cp_state popup_requests;
static struct { uint32_t route_epoch; uint64_t media[2]; } physical_open[2];
#pragma weak xz_extra_source_generation
static int popup_reply(uint32_t token, uint32_t hi, uint32_t lo);
static int popup_start(void);
static uint32_t popup_begin(unsigned extra);
static void popup_notice(const char *text);
/* Bounded counters: 0 asks, 1 deliveries, 2 player refusals, 3 non-USB refusals, 4 failed asks. */
__attribute__((visibility("default"))) uint32_t xz_mods_classic_load_v1[6] = {1};
#define COUNT(i) __atomic_fetch_add(&xz_mods_classic_load_v1[(i)+1],1,__ATOMIC_RELAXED)

static uint32_t field(const void *message, unsigned offset) {
 uint32_t value; memcpy(&value, (const unsigned char *)message + offset, 4); return value;
}

static int ask_row_tagged(int deck, uint32_t row, uint32_t title_flag, uint32_t token) {
 uint32_t mailbox, reply;
 if(xz_read_memory(MBX_DB_CLIENT + 4u * (uint32_t)deck, &mailbox, 4) || xz_read_memory(MBX_TOTAL_MAIN, &reply, 4))
  return 0;
 return ((send_arg_fn)(uintptr_t)TOTAL_SEND_ARG)(MSG_ROW_REQUEST, mailbox, reply, row, token, title_flag,
                                          0, 0, 0, 0, 0, 0) != 0;
}

static int load_proc_hook(uint32_t id, void *message) {
 if(message && id == XZ_CL_MSG_ROW_TRACK) {
  uint32_t token = field(message, 0x54);
  if ((token & 0xffff0000u) == XZ_CP_TOKEN_PREFIX) { popup_reply(token, field(message,0x24), field(message,0x28)); return 1; }
 }
 if(!message || (id != XZ_CL_MSG_LOAD_ROW && id != XZ_CL_MSG_ROW_TRACK)) return stock_proc(id, message);
 struct xz_cl_message view = { .id = id };
 if(id == XZ_CL_MSG_LOAD_ROW) {
  view.deck = (int)field(message, 0x30); view.row = field(message, 0x24); view.title_flag = field(message, 0x34);
 } else {
  view.deck = (int)field(message, 0x54); view.id_hi = field(message, 0x24); view.id_lo = field(message, 0x28);
 }
 struct xz_extra_deck_event load;
 pthread_mutex_lock(&mutex);
 enum xz_cl_verdict verdict = xz_classic_load_message(&state, &view, xz_game_milliseconds(), &load);
 pthread_mutex_unlock(&mutex);
 switch(verdict) {
 case XZ_CL_PASS: return stock_proc(id, message);
 case XZ_CL_ASK: {
  uint32_t token=popup_begin((unsigned)view.deck);
  pthread_mutex_lock(&mutex);xz_classic_load_cancel(&state,view.deck);pthread_mutex_unlock(&mutex);
  if(token&&ask_row_tagged(view.deck,view.row,view.title_flag,token)) { COUNT(0); return 1; }
  /* Still kept from stock: passing it on would load DECK 1/2 with the track meant for DECK 3/4. */
  pthread_mutex_lock(&mutex); xz_classic_load_cancel(&state, view.deck); pthread_mutex_unlock(&mutex);
  COUNT(4); popup_notice("SOURCE OR ROUTE CHANGED / CLASSIC LOAD REFUSED");
  return 1;
 }
 case XZ_CL_DELIVER:
  if(xz_extra_deck_input(&load)) COUNT(1);
  else { COUNT(2); xz_log("Classic LOAD: the DECK 3/4 player refused the track"); }
  return 1;
 case XZ_CL_REFUSE:
  COUNT(3); popup_notice("CLASSIC LOAD REFUSED / SELECT USB TRACK AGAIN");
  return 1;
 }
 return stock_proc(id, message);
}

int xz_classic_load_start(void) {
 static const unsigned char proc_guard[8] = {0x12,0x37,0x00,0xe3,0x03,0x00,0x50,0xe1};
 static const unsigned char send_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x14,0xd0,0x4d,0xe2};
 static const uint32_t pool[2] = {MBX_DB_CLIENT, MBX_TOTAL_MAIN};
 unsigned char actual[8]; uint32_t words[2];
 if(stock_proc) { __atomic_store_n(&installed, 1, __ATOMIC_RELEASE); return 0; }
 if(xz_read_memory(TOTAL_LOAD_PROC, actual, 8) || memcmp(actual, proc_guard, 8)) return -1;
 if(xz_read_memory(TOTAL_SEND_ARG, actual, 8) || memcmp(actual, send_guard, 8)) return -1;
 if(xz_read_memory(LITERAL_POOL, words, 8) || memcmp(words, pool, 8)) return -1;
 if(xz_hook_arm(TOTAL_LOAD_PROC, proc_guard, (void *)load_proc_hook, (void **)&stock_proc)) return -1;
 __atomic_store_n(&installed, 1, __ATOMIC_RELEASE);
 if(popup_start()) xz_log("Classic native popup: extra rows unavailable; native LOAD retained");
 return 0;
}


#define POPUP_SEND 0x20594cu
#define POPUP_SELECT 0x2028a0u
#define POPUP_HEADER 0x171fd0u
#define DB_LOADED_MUSIC 0x21addcu
#define DB_SEND_MBX 0x172264u
#define POPUP_COUNT 0x3fa0cu
#define POPUP_ROWS 0x3fc28u
#define CUE_LIST_DISPLAYING 0x1780acu

typedef int (*popup_send_fn)(void *,void *,void *);
typedef void (*popup_select_fn)(void *,void *,void *);
typedef int (*popup_header_fn)(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
typedef int (*db_send_fn)(void *,void *);
static popup_send_fn stock_popup_send, stock_loaded_music;
static popup_select_fn stock_popup_select;
static popup_header_fn stock_popup_header;
static db_send_fn stock_db_send;
static int popup_active;
static struct { void *self; uint32_t route_epoch, count; uint64_t media[2]; } popup_open;
/* Asynchronous DB refusals have no proven Classic browse toast ABI; synchronous
 * selection refusals are reserialized into the native popup below. */
#pragma weak xz_ui_runtime_notice
extern void xz_ui_runtime_notice(const char *);
static void popup_notice(const char *text) { xz_log(text);if(xz_ui_runtime_notice)xz_ui_runtime_notice(text); }
static _Thread_local struct { void *self, *out, *request; uint32_t route_epoch; uint64_t media[2]; } popup_scope;
static _Thread_local struct { void *out, *request; uint32_t token; } reply_scope;

static uint32_t popup_begin(unsigned extra) {
 if(!__atomic_load_n(&popup_active,__ATOMIC_ACQUIRE)||!xz_extra_decks_route().route_up||!xz_extra_source_generation)return 0;
 uint64_t media[2];uint32_t epoch=xz_extra_decks_epoch();
 pthread_mutex_lock(&mutex);media[0]=physical_open[extra].media[0];media[1]=physical_open[extra].media[1];
 int current=physical_open[extra].route_epoch==epoch;
 uint32_t token=current&&media[0]==xz_extra_source_generation(1)&&media[1]==xz_extra_source_generation(2)?
  xz_cp_begin(&popup_requests,extra,epoch,media,xz_game_milliseconds()):0;
 pthread_mutex_unlock(&mutex);return token;
}
static int popup_reply(uint32_t token,uint32_t hi,uint32_t lo) {
 struct xz_cp_load selected;
 pthread_mutex_lock(&mutex);
 int result=xz_cp_reply(&popup_requests,token,xz_extra_decks_epoch(),hi,lo,xz_game_milliseconds(),&selected);
 pthread_mutex_unlock(&mutex);
 if(result==2) {
  struct xz_extra_deck_event load;memset(&load,0,sizeof load);
  load.kind=XZ_EXTRA_IN_LOAD;load.pressed=1;load.deck=(int)selected.deck;
  load.load.source=(int)selected.source;load.load.track_id=selected.track_id;load.load.media_generation=selected.media_generation;load.load.route_epoch=selected.route_epoch;
  if(!xz_extra_deck_input(&load))popup_notice("EXTRA PLAYER REFUSED USB LOAD");
 } else if(result==1) popup_notice("SOURCE OR ROUTE CHANGED / USB LOAD REFUSED");
 return result;
}
static int popup_send_hook(void *self,void *out,void *request) {
 if(!__atomic_load_n(&popup_active,__ATOMIC_ACQUIRE)||popup_scope.self)return stock_popup_send(self,out,request);
 pthread_mutex_lock(&mutex);memset(&popup_open,0,sizeof popup_open);pthread_mutex_unlock(&mutex);
 popup_scope.self=self;popup_scope.out=out;popup_scope.request=request;
 popup_scope.route_epoch=xz_extra_decks_epoch();
 popup_scope.media[0]=xz_extra_source_generation?xz_extra_source_generation(1):0;
 popup_scope.media[1]=xz_extra_source_generation?xz_extra_source_generation(2):0;
 int result=stock_popup_send(self,out,request);
 memset(&popup_scope,0,sizeof popup_scope);return result;
}
static int popup_header_hook(void *out,uint32_t a,uint32_t total,uint32_t first,uint32_t d,uint32_t e,uint32_t flags) {
 if(popup_scope.self&&out==popup_scope.out&&xz_extra_decks_route().route_up&&popup_scope.route_epoch&&popup_scope.route_epoch==xz_extra_decks_epoch()) {
  uint32_t self=(uint32_t)(uintptr_t)popup_scope.self,mode,count;
  if(!xz_read_memory(self,&mode,4)&&(mode==1||mode==2)&&
     !xz_read_memory(self+POPUP_COUNT,&count,4)&&count==total&&(count==3||count==4)) {
   /* The original constructor just wrote these exact writable slots. */
   struct xz_cp_row *rows=(struct xz_cp_row *)(uintptr_t)(self+POPUP_ROWS);
   if(!xz_cp_extend(rows,(uint32_t *)(uintptr_t)(self+POPUP_COUNT))) {
    total=count+2;pthread_mutex_lock(&mutex);
    popup_open.self=popup_scope.self;popup_open.route_epoch=popup_scope.route_epoch;popup_open.count=total;
    popup_open.media[0]=popup_scope.media[0];popup_open.media[1]=popup_scope.media[1];
    pthread_mutex_unlock(&mutex);
   }
  }
 }
 return stock_popup_header(out,a,total,first,d,e,flags);
}
static int loaded_music_hook(void *self,void *out,void *request) {
 uint32_t token=request?field(request,0x28):0;
 if(!__atomic_load_n(&popup_active,__ATOMIC_ACQUIRE)||reply_scope.out||
    (token&0xffff0000u)!=XZ_CP_TOKEN_PREFIX)return stock_loaded_music(self,out,request);
 reply_scope.out=out;reply_scope.request=request;reply_scope.token=token;
 int result=stock_loaded_music(self,out,request);
 memset(&reply_scope,0,sizeof reply_scope);return result;
}
static int db_send_hook(void *out,void *request) {
 if(reply_scope.out&&reply_scope.out==out&&reply_scope.request==request) {
  /* GetLoadedMusicData's early failure omits this echo. Preserve ownership on every reply. */
  memcpy((unsigned char *)out+0x54,&reply_scope.token,4);
 }
 return stock_db_send(out,request);
}
static int popup_context(uint32_t self,uint32_t *row,uint32_t *title) {
 uint32_t mode,source,depth,values[2];uint64_t record;
 if(xz_read_memory(self,&mode,4))return 0;
 if(mode==2) {
  if(xz_read_memory(self+4,&source,4)||source>5||xz_read_memory(self+0x6d40+source*4,&depth,4)||depth>=12)return 0;
  record=(uint64_t)self+0x2c5cc+(uint64_t)source*0x3150+(uint64_t)depth*0x41c;
 } else if(mode==1) {
  if(xz_read_memory(self+0x6d58,&depth,4)||depth>=12)return 0;
  record=(uint64_t)self+0x3f1c8+(uint64_t)depth*0x41c;
 } else return 0;
 if(record>UINT32_MAX-12||xz_read_memory((uint32_t)record+4,values,sizeof values)||UINT32_MAX-values[0]<values[1])return 0;
 *row=values[0]+values[1];*title=((int(*)(void *))(uintptr_t)CUE_LIST_DISPLAYING)((void *)(uintptr_t)self)==1;
 return 1;
}
static int popup_refuse(void *context,void *message,struct xz_cp_row *rows,uint32_t count,uint32_t index) {
 if(!context||!message||index>=count||count>XZ_CP_CAPACITY)return 0;
 static const char reason[]="VERIFY USB / LOAD REFUSED";
 static const uint16_t title[]={'T','R','A','C','K',' ','M','E','N','U',0};
 memset(rows[index].text,0,sizeof rows[index].text);rows[index].text[0]=0xfffa;
 for(unsigned i=0;i<sizeof(reason)-1;i++)rows[index].text[i+1]=(uint16_t)(unsigned char)reason[i];
 rows[index].text[sizeof(reason)]=0xfffb;
 stock_popup_header(context,0,count,0,0,0,0x80);
 typedef int (*add_fn)(void*,uint32_t,uint32_t,const void*,const uint16_t*,uint32_t);
 add_fn add=(add_fn)(uintptr_t)0x172018;
 add(context,0x63,0,NULL,title,0xff);
 for(uint32_t i=0;i<count;i++)add(context,rows[i].text_tag,0,NULL,rows[i].text,0xff);
 stock_db_send(context,message);return 1;
}
static void popup_select_hook(void *self,void *context,void *message) {
 if(!__atomic_load_n(&popup_active,__ATOMIC_ACQUIRE)||!self||!message){stock_popup_select(self,context,message);return;}
 uint32_t count,address=(uint32_t)(uintptr_t)self;uint16_t index;
 if(xz_read_memory(address+POPUP_COUNT,&count,4)||count>XZ_CP_CAPACITY||count<3){stock_popup_select(self,context,message);return;}
 memcpy(&index,(unsigned char *)message+0x78,2);
 struct xz_cp_row *rows=(struct xz_cp_row *)(uintptr_t)(address+POPUP_ROWS);
 int target=xz_cp_target(rows,count,index);
 if(target<2) {
  if(target==0||target==1){pthread_mutex_lock(&mutex);xz_classic_load_cancel(&state,target);popup_requests.pending[target].token=0;pthread_mutex_unlock(&mutex);}
  stock_popup_select(self,context,message);return;
 }
 uint32_t row,title,epoch=xz_extra_decks_epoch(),token=0;
 uint64_t media[2];int current;
 pthread_mutex_lock(&mutex);
 media[0]=popup_open.media[0];media[1]=popup_open.media[1];
 current=popup_open.self==self&&popup_open.count==count&&popup_open.route_epoch==epoch;
 pthread_mutex_unlock(&mutex);
 current=current&&epoch&&xz_extra_decks_route().route_up&&xz_extra_source_generation&&
  media[0]==xz_extra_source_generation(1)&&media[1]==xz_extra_source_generation(2);
 if(current&&popup_context(address,&row,&title)) {
  pthread_mutex_lock(&mutex);token=xz_cp_begin(&popup_requests,(unsigned)target-2,epoch,media,xz_game_milliseconds());pthread_mutex_unlock(&mutex);
 }
 if(token) {
  /* Native DB client is selected by a real channel, while the echoed word is our unique token. */
  if(!ask_row_tagged(target-2,row,title,token)){popup_notice("BROWSER REQUEST UNAVAILABLE / LOAD REFUSED");if(popup_refuse(context,message,rows,count,index))return;}
 } else {popup_notice("SOURCE OR ROUTE CHANGED / VERIFY USB THEN LOAD");if(popup_refuse(context,message,rows,count,index))return;}
 /* Stock CANCEL owns closing/back navigation. Do not mutate the shared native table. */
 uint16_t cancel=UINT16_MAX;
 for(uint16_t i=0;i<count;i++)if(rows[i].kind==4){cancel=i;break;}
 if(cancel!=UINT16_MAX){unsigned char shadow[0x80];memcpy(shadow,message,sizeof shadow);memcpy(shadow+0x78,&cancel,2);stock_popup_select(self,context,shadow);}
}
static int popup_start(void) {
 struct hook {uint32_t address;unsigned char guard[8];void *replacement;void **original;} hooks[]={
  {DB_SEND_MBX,{0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1},(void*)db_send_hook,(void**)&stock_db_send},
  {DB_LOADED_MUSIC,{0xf0,0x4f,0x2d,0xe9,0x01,0x50,0xa0,0xe1},(void*)loaded_music_hook,(void**)&stock_loaded_music},
  {POPUP_HEADER,{0x00,0xc0,0xa0,0xe1,0x04,0x40,0x2d,0xe5},(void*)popup_header_hook,(void**)&stock_popup_header},
  {POPUP_SEND,{0xf0,0x4f,0x2d,0xe9,0xfe,0x4b,0x80,0xe2},(void*)popup_send_hook,(void**)&stock_popup_send},
  {POPUP_SELECT,{0x00,0x30,0x90,0xe5,0xf0,0x4f,0x2d,0xe9},(void*)popup_select_hook,(void**)&stock_popup_select}
 };
 unsigned char actual[8];
 static const unsigned char add_guard[8]={0xf0,0x47,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
 if(xz_read_memory(0x172018,actual,8)||memcmp(actual,add_guard,8))return -1;
 static const unsigned char cue_guard[8]={0xc8,0x3d,0x06,0xe3,0x03,0x00,0xd0,0xe7};
 if(xz_read_memory(CUE_LIST_DISPLAYING,actual,8)||memcmp(actual,cue_guard,8))return -1;
 for(unsigned i=0;i<sizeof hooks/sizeof hooks[0];i++)if(xz_read_memory(hooks[i].address,actual,8)||memcmp(actual,hooks[i].guard,8))return -1;
 for(unsigned i=0;i<sizeof hooks/sizeof hooks[0];i++)if(xz_hook_arm(hooks[i].address,hooks[i].guard,hooks[i].replacement,hooks[i].original))return -1;
 __atomic_store_n(&popup_active,1,__ATOMIC_RELEASE);return 0;
}

int xz_classic_load_divert_arm(int side) {
 if(!__atomic_load_n(&installed, __ATOMIC_ACQUIRE) || !__atomic_load_n(&popup_active,__ATOMIC_ACQUIRE) || side < 0 || side > 1 || !xz_extra_decks_route().route_up)
  return 0;
 pthread_mutex_lock(&mutex);
 physical_open[side].route_epoch=xz_extra_decks_epoch();
 physical_open[side].media[0]=xz_extra_source_generation?xz_extra_source_generation(1):0;
 physical_open[side].media[1]=xz_extra_source_generation?xz_extra_source_generation(2):0;
 xz_classic_load_arm(&state, side, xz_game_milliseconds());
 pthread_mutex_unlock(&mutex);
 return 1;
}
#endif
