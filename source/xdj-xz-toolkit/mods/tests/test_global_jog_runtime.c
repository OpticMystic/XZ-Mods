#ifdef NDEBUG
#error "Global jog tests require assertions"
#endif
/* Offline integration: production runtime adapter and production player queue. No hardware. */
#include "../runtime.c"
#include <assert.h>
#include <time.h>
extern void test_player_init(void);
extern unsigned test_player_count(int side);
extern int test_player_delta(int side);
extern void test_player_clear(int side);
extern int test_player_kind(int side,unsigned n);
extern int test_player_pressed(int side,unsigned n);
extern int test_player_input(const struct xz_extra_deck_event *e);
static int route_live=1,game_owner,game_calls[3],delivered;
static uint32_t test_clock=1000;
static unsigned native_master_requests,native_input_calls;
static int native_input_side;
static uint32_t native_input_at;
void xz_shell_runtime_native_input(int side,uint32_t at){native_input_calls++;native_input_side=side;native_input_at=at;}
int xz_extra_decks_use_native_master(void){native_master_requests++;return 1;}
struct xz_extra_route xz_extra_decks_route(void){return (struct xz_extra_route){1,route_live};}
int xz_extra_decks_snapshot(struct xz_extra_decks *out){memset(out,0,sizeof *out);return 0;}
void xz_extra_decks_publish_sides(int left,int right){(void)left;(void)right;}
int xz_extra_deck_input(const struct xz_extra_deck_event *e){delivered++;return test_player_input(e);}
uint32_t xz_game_milliseconds(void){return test_clock++;}
int xz_kart_runtime_physical(void *self,const void *input){(void)self;(void)input;game_calls[0]++;return game_owner==1;}
int xz_ui_runtime_arcade_key(void *self,const void *input){(void)self;(void)input;game_calls[1]++;return game_owner==2;}
int xz_doom_physical_key(void *self,const void *input){(void)self;(void)input;game_calls[2]++;return game_owner==3;}
int xz_kart_runtime_input_pending(void){return game_owner==1;}
int xz_ui_runtime_arcade_input_pending(void){return game_owner==2;}
int xz_doom_input_pending(void){return game_owner==3;}
int xz_kart_runtime_observe_key(unsigned channel,const void *input){(void)channel;(void)input;return 0;}
int xz_cue_native_decode(struct xz_cue_native *n,void *self,const void *input,struct xz_cue_event *e){(void)n;(void)self;(void)input;(void)e;return 0;}
int xz_cue_before(struct xz_cue *c,const struct xz_cue_event *e){(void)c;(void)e;return 0;}
void xz_cue_after(struct xz_cue *c,const struct xz_cue_event *e,int result){(void)c;(void)e;(void)result;}
int xz_ui_runtime_pad(const struct xz_cue_event *e,unsigned *flags){(void)e;(void)flags;return 0;}
static int stock_contacts;
static int stock_fixture(void *self,const void *input){(void)self;(void)input;stock_contacts++;return 7;}
static void select_side(int side){
 xz_runtime_deck_global_key(0x4103,side+1,0);xz_runtime_deck_global_key(0x4103,side+1,2);
 xz_runtime_deck_global_key(0x4103,side+1,0);xz_runtime_deck_global_key(0x4103,side+1,2);
 assert(deck_select.side[side].target==XZ_DS_EXTRA);
}
int main(int argc,char **argv){
 int side=argc>1?atoi(argv[1]):0,selected=argc>2?atoi(argv[2]):1;
 unsigned char innards[0x100]={0},input[24]={0};innards[0x26]=(unsigned char)(side+1);
 uint16_t code=0x4305;int32_t delta=-9,absolute=1234;float speed=.75f;
 if(argc>3)delta=atoi(argv[3]);
 memcpy(input+8,&code,2);input[10]=(unsigned char)(side+1);input[11]=4;
 memcpy(input+12,&delta,4);memcpy(input+16,&speed,4);memcpy(input+20,&absolute,4);
 installed=1;observer=0;xz_deck_select_init(&deck_select);xz_deck_select_route(&deck_select,1);test_player_init();
 if(selected)select_side(side);
 int taken=xz_runtime_deck_global_input(NULL,input);
 assert(taken==selected);assert(test_player_count(side)==(unsigned)selected);assert(delivered==selected);
 assert(native_input_calls==(unsigned)!selected);
 if(!selected){assert(native_input_side==side);assert(native_input_at==test_clock-1);}
 if(selected)assert(test_player_delta(side)==delta);
 if(!selected){
  unsigned wake_before=native_input_calls;
  int32_t zero_delta=0;memcpy(input+12,&zero_delta,4);
  (void)xz_runtime_deck_global_input(NULL,input);assert(native_input_calls==wake_before);
  memcpy(input+12,&delta,4);
 }
 printf("%d %u %d\n",taken,test_player_count(side),selected?test_player_delta(side):0);
 if(argc>4&&!strcmp(argv[4],"workflow")){
  fflush(stdout);char command;unsigned raw,operation;stock_key=stock_fixture;
  while(scanf(" %c %x %u",&command,&raw,&operation)==3){
   code=(uint16_t)raw;memcpy(input+8,&code,2);input[11]=(unsigned char)operation;
   int result=command=='F'?physical_key(innards,input):xz_runtime_deck_early_key(code)?xz_runtime_deck_global_input(NULL,input):xz_runtime_deck_global_key(code,(unsigned)side+1,operation);
   printf("%d %u %d %d\n",result,test_player_count(side),stock_contacts,deck_select.side[side].target==XZ_DS_EXTRA);fflush(stdout);
  }
  return 0;
 }
 if(argc>4&&!strcmp(argv[4],"bench")){
  code=argc>5?(uint16_t)strtoul(argv[5],NULL,0):0x4305;memcpy(input+8,&code,2);
  const unsigned loops=200000;struct timespec began,end;double base[7],cost[7];
  for(int round=0;round<7;round++){
   clock_gettime(CLOCK_MONOTONIC,&began);
   for(unsigned i=0;i<loops;i++)test_player_clear(side);
   clock_gettime(CLOCK_MONOTONIC,&end);base[round]=(end.tv_sec-began.tv_sec)*1e9+end.tv_nsec-began.tv_nsec;
   clock_gettime(CLOCK_MONOTONIC,&began);
   for(unsigned i=0;i<loops;i++){test_player_clear(side);input[11]=code==0x4305?4:(i&1?2:0);(void)xz_runtime_deck_global_input(NULL,input);}
   clock_gettime(CLOCK_MONOTONIC,&end);cost[round]=(end.tv_sec-began.tv_sec)*1e9+end.tv_nsec-began.tv_nsec;
  }
  for(int i=0;i<7;i++)for(int j=i+1;j<7;j++){if(base[j]<base[i]){double t=base[i];base[i]=base[j];base[j]=t;}if(cost[j]<cost[i]){double t=cost[i];cost[i]=cost[j];cost[j]=t;}}
  printf("{\"key\":\"0x%x\",\"selected\":%d,\"loops_per_round\":%u,\"rounds\":7,\"baseline_ns\":%.2f,\"adapter_plus_baseline_ns\":%.2f,\"incremental_ns\":%.2f}\n",code,selected,loops,base[3]/loops,cost[3]/loops,(cost[3]-base[3])/loops);return 0;
 }
 if(argc>4)return 0;
 /* Invalid channel/self mismatch fails open without games or player delivery. */
 input[10]=3;assert(!xz_runtime_deck_global_input(innards,input));input[10]=(unsigned char)(2-side);
 assert(!xz_runtime_deck_global_input(innards,input));input[10]=(unsigned char)(side+1);
 observer=1;assert(!xz_runtime_deck_global_input(innards,input));observer=0;
 for(game_owner=1;game_owner<=3;game_owner++){
  memset(game_calls,0,sizeof game_calls);unsigned before=test_player_count(side);
  unsigned game_wake_before=native_input_calls;
  assert(xz_runtime_deck_global_input(innards,input));assert(test_player_count(side)==before);assert(native_input_calls==game_wake_before);
  for(int i=0;i<3;i++)assert(game_calls[i]==(i<game_owner));
 }
 game_owner=0;
 if(deck_select.side[side].target==XZ_DS_NATIVE)assert(xz_runtime_deck_select_toggle(side)==1);
 code=0x4306;memcpy(input+8,&code,2);input[11]=0;
 unsigned before=test_player_count(side);
 assert(xz_runtime_deck_global_input(innards,input));assert(test_player_count(side)==before+1);
 assert(xz_runtime_deck_global_input(innards,input));assert(test_player_count(side)==before+1);
 assert(xz_runtime_deck_select_toggle(side)==1);input[11]=2;game_owner=1;
 /* The old software contact releases to its player even if a game has opened. */
 assert(xz_runtime_deck_global_input(innards,input));assert(test_player_count(side)==before+2);game_owner=0;
 assert(!xz_runtime_deck_global_input(innards,input));assert(test_player_count(side)==before+2);
 /* Native press is recorded only in the fallback, then releases to stock after a side switch. */
 input[11]=0;assert(!xz_runtime_deck_global_input(innards,input));assert(!deck_select.side[side].owner[XZ_DS_NATIVE]);
 assert(!deck_route(innards,input));assert(deck_select.side[side].owner[XZ_DS_NATIVE]);
 assert(xz_runtime_deck_select_toggle(side)==1);input[11]=2;
 game_owner=1;assert(!xz_runtime_deck_global_input(innards,input));assert(deck_select.side[side].owner[XZ_DS_NATIVE]);
 stock_key=stock_fixture;assert(physical_key(innards,input)==7);assert(stock_contacts==1);
 assert(!deck_select.side[side].owner[XZ_DS_NATIVE]);game_owner=0;
 input[11]=0;assert(xz_runtime_deck_global_input(innards,input));before=test_player_count(side);
 route_live=0;input[11]=3;assert(xz_runtime_deck_global_input(innards,input));assert(test_player_count(side)==before+1);
 assert(!xz_runtime_deck_global_input(innards,input));
 route_live=1;xz_deck_select_route(&deck_select,1);assert(xz_runtime_deck_select_toggle(side)==1);
 const uint16_t transport[]={0x4101,0x4102,0x4104,0x4107,0x4108,0x4109,0x410d,0x410e,0x410f,0x4119,0x4324,0x4325};
 const int kinds[]={XZ_EXTRA_IN_PLAY,XZ_EXTRA_IN_CUE,XZ_EXTRA_IN_VINYL,XZ_EXTRA_IN_TEMPO_RANGE,XZ_EXTRA_IN_MASTER_TEMPO,XZ_EXTRA_IN_TEMPO_RESET,XZ_EXTRA_IN_LOOP_IN,XZ_EXTRA_IN_LOOP_OUT,XZ_EXTRA_IN_RELOOP,XZ_EXTRA_IN_HOT_CUE,XZ_EXTRA_IN_LOOP_DOUBLE,XZ_EXTRA_IN_LOOP_HALF};
 for(unsigned i=0;i<sizeof transport/sizeof *transport;i++){
  test_player_clear(side);code=transport[i];memcpy(input+8,&code,2);input[11]=0;
  assert(xz_runtime_deck_global_input(NULL,input));assert(test_player_count(side)==1);
  assert(test_player_kind(side,0)==kinds[i]&&test_player_pressed(side,0));
  assert(xz_runtime_deck_global_input(NULL,input));assert(test_player_count(side)==1);
  input[11]=1;assert(xz_runtime_deck_global_input(NULL,input));
  unsigned held=code==0x410d?1:0;assert(test_player_count(side)==1+held);
  /* A selected side can change while contact is held; release stays with that contact. */
  assert(xz_runtime_deck_select_toggle(side)==1);input[11]=2;game_owner=1;
  assert(xz_runtime_deck_global_input(NULL,input));assert(test_player_count(side)==2+held);
  assert(test_player_kind(side,1+held)==kinds[i]&&!test_player_pressed(side,1+held));game_owner=0;
  assert(!xz_runtime_deck_global_input(NULL,input));assert(xz_runtime_deck_select_toggle(side)==1);
 }
 test_player_clear(side);code=0x4313;memcpy(input+8,&code,2);input[11]=0;
 assert(xz_runtime_deck_global_input(NULL,input));assert(!test_player_count(side));
 assert(xz_runtime_deck_select_toggle(side)==1);game_owner=1;input[11]=2;
 assert(xz_runtime_deck_global_input(NULL,input));assert(test_player_count(side)==1&&test_player_kind(side,0)==XZ_EXTRA_IN_SYNC&&test_player_pressed(side,0));
 game_owner=0;assert(xz_runtime_deck_select_toggle(side)==1);
 test_player_clear(side);unsigned master_before=native_master_requests;assert(xz_runtime_deck_global_key(0x4312,side+1,0));
 assert(test_player_count(side)==1&&test_player_kind(side,0)==XZ_EXTRA_IN_MASTER&&test_player_pressed(side,0));
 assert(xz_runtime_deck_global_key(0x4312,side+1,0));assert(test_player_count(side)==1);
 assert(xz_runtime_deck_select_toggle(side)==1);assert(xz_runtime_deck_global_key(0x4312,side+1,2));assert(test_player_count(side)==2);
 assert(native_master_requests==master_before);
 assert(!xz_runtime_deck_global_key(0x4312,side+1,0));assert(native_master_requests==master_before+1);assert(xz_runtime_deck_select_toggle(side)==1);
 assert(!xz_runtime_deck_global_key(0x4312,side+1,2));assert(test_player_count(side)==2);assert(native_master_requests==master_before+1);
 return 0;
}
