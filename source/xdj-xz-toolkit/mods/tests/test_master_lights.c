/* SPDX-License-Identifier: MIT */
/* MASTER and BEAT SYNC lights with DECK 3/4 (master_lights.h). A model of the stock refresh, firmware 1.26:
   LedManager::refStates clears the list, ui::Player::checkLedStat appends SYNC (engine flag) and MASTER
   (TotalMainIf+0x34 == channel) for each channel, then the mods' LED hook runs (ui/native_led.c). The model
   drives the real native_led.c and master_lights.c code; only the stock parts are stand-ins.
   Each scenario runs twice: "today" (no UI master number, no inserted records) reproduces the owner's report,
   "fixed" must show the lights the table in master-lights/REPORT.md asks for. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../master_lights.h"
#include "../ui/native_led.h"

/* ---- stock stand-ins ---- */
struct stat { uint16_t count; unsigned char records[XZ_ML_STAT_CAPACITY*XZ_ML_RECORD_BYTES]; uint16_t index[XZ_ML_STAT_IDS*XZ_ML_STAT_SLOTS]; };
static uint32_t rd(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static void wr(unsigned char *p,uint32_t v){memcpy(p,&v,4);}
/* LedStat::setLedState append path (0x27bf3c): record at count, index slot = count + 1. */
static void stock_set(struct stat *s,unsigned id,unsigned ch,unsigned state){
 unsigned slot=id*XZ_ML_STAT_SLOTS+ch-1;
 unsigned char *r;
 if(s->index[slot])r=s->records+(size_t)(s->index[slot]-1)*XZ_ML_RECORD_BYTES;
 else{
  assert(s->count<XZ_ML_STAT_CAPACITY);
  r=s->records+(size_t)s->count*XZ_ML_RECORD_BYTES;memset(r,0,XZ_ML_RECORD_BYTES);
  wr(r,id);wr(r+4,ch);wr(r+0x14,1);
  s->index[slot]=++s->count;
 }
 wr(r+0x10,state);
}
static void append(void *ctx,unsigned id,unsigned ch){stock_set(ctx,id,ch,1);}
static struct xz_ml_stat view(struct stat *s){
 return (struct xz_ml_stat){&s->count,s->records,XZ_ML_STAT_IDS,XZ_ML_STAT_SLOTS,s->index};
}

/* ---- the unit ---- */
struct unit {
 int32_t ui;                 /* TotalMainIf+0x34 */
 int32_t sentinel;           /* the sentinel task's own master number (writes ui only when it changes) */
 int32_t engine;             /* the engine's master as the bridge observes it (enum xz_master_deck) */
 int stock_sync[2],stock_flash[2];
 struct xz_extra_deck soft[2];
 int side_extra[2];          /* left on DECK 3, right on DECK 4 */
 struct xz_ml_ui ml;
 struct xz_led_sync_master_owner owner[2];
 struct stat led;
 uint32_t now;
 int fixed;
};
static void unit_init(struct unit *u,int fixed){
 memset(u,0,sizeof *u);u->fixed=fixed;u->ui=u->sentinel=-1;u->engine=XZ_MASTER_NONE;
 for(int i=0;i<2;i++)u->soft[i].loaded=1;
}
/* The software decks' published master follows the engine (extra_player.c read_view, observer mode). */
static void project(struct unit *u){
 for(int i=0;i<2;i++)u->soft[i].master=u->soft[i].loaded&&u->engine==XZ_MASTER_DECK3+i;
}
/* A stock MASTER press on deck ch (1/2) passing to stock: the sentinel (TotalSentinel_DeckN_MasterKey) and
   onKey_Master -> DjEngineIF::setSyncMaster. The sentinel writes the UI number only when its master changes. */
static void press_stock_master(struct unit *u,int ch){
 if(u->sentinel!=ch){u->sentinel=ch;u->ui=ch;}
 u->engine=ch==1?XZ_MASTER_DECK1:XZ_MASTER_DECK2;project(u);
}
/* MASTER on a side showing DECK 3/4: the mods' bridge, SyncMaster::setLinkMaster(3/4). Stock UI untouched. */
static void press_soft_master(struct unit *u,int deck){u->engine=deck==3?XZ_MASTER_DECK3:XZ_MASTER_DECK4;project(u);}
static void engine_none(struct unit *u){u->engine=XZ_MASTER_NONE;project(u);}

/* One refresh: the UI tick on the channel-1 callback, then per channel stock's records and the mods' hook. */
static void refresh(struct unit *u){
 u->now+=50;
 if(u->fixed){int32_t w;if(xz_ml_ui_step(&u->ml,u->engine,u->ui,&w))u->ui=w;}
 memset(&u->led,0,sizeof u->led);
 for(unsigned ch=1;ch<=2;ch++){
  if(u->stock_sync[ch-1])stock_set(&u->led,XZ_ML_ID_BEAT_SYNC,ch,u->stock_flash[ch-1]?2:1);
  if(u->ui==(int32_t)ch)stock_set(&u->led,XZ_ML_ID_MASTER,ch,1);
  if(!u->side_extra[ch-1])continue;
  const struct xz_extra_deck *d=&u->soft[ch-1];
  if(u->fixed){struct xz_ml_stat v=view(&u->led);assert(xz_ml_insert_missing(&v,ch,d,append,&u->led)>=0);}
  xz_native_led_sync_master_apply_at(&u->owner[ch-1],u->led.records,(size_t)u->led.count*XZ_ML_RECORD_BYTES,
                                     u->led.count,ch,d,u->now);
 }
}
/* A lamp as the panel gets it: no record is off; otherwise the state word (blinking alternates). */
static int lamp(const struct unit *u,unsigned id,unsigned ch){
 unsigned slot=id*XZ_ML_STAT_SLOTS+ch-1,n=u->led.index[slot];
 return n?(int)rd(u->led.records+(size_t)(n-1)*XZ_ML_RECORD_BYTES+0x10):0;
}
/* L-MASTER, L-SYNC, R-MASTER, R-SYNC as 4 digits (0 off, 1 lit). */
static int lights(struct unit *u){
 refresh(u);
 return (lamp(u,5,1)!=0)*1000+(lamp(u,4,1)!=0)*100+(lamp(u,5,2)!=0)*10+(lamp(u,4,2)!=0);
}
#define EXPECT(u,today,fixed_) do{int got_=lights(u);int want_=(u)->fixed?(fixed_):(today);\
 if(got_!=want_){fprintf(stderr,"%s:%d %s: got %04d want %04d\n",__FILE__,__LINE__,(u)->fixed?"fixed":"today",got_,want_);assert(0);}}while(0)

/* Owner's report: DECK 1 master, MASTER pressed on DECK 4. Left on DECK 1, right on DECK 4. */
static void owners_sequence(int fixed){
 struct unit u;unit_init(&u,fixed);
 u.side_extra[1]=1;
 press_stock_master(&u,1);
 EXPECT(&u,1000,1000);
 /* DECK 4 already follows DECK 1 with SYNC; stock deck 2 (hidden under the right side) has SYNC off. */
 u.soft[1].sync.requested=1;
 EXPECT(&u,1000,1001);             /* today R-SYNC dark: no stock record to rewrite */
 press_soft_master(&u,4);
 EXPECT(&u,1000,11);               /* today: DECK 1 MASTER stays lit, DECK 4 MASTER and SYNC dark */
 /* DECK 1 gets SYNC: it follows DECK 4. */
 u.stock_sync[0]=1;
 EXPECT(&u,1100,111);
 /* The left side to DECK 3 (no SYNC) and back. */
 u.side_extra[0]=1;
 EXPECT(&u,0,11);                  /* today the left MASTER goes dark only because DECK 3 is not master */
 u.side_extra[0]=0;
 EXPECT(&u,1100,111);
}
/* Every from/to pair among the four decks, both sides on the deck that matters, no SYNC. */
static void every_pair(int fixed){
 for(int from=1;from<=4;from++)for(int to=1;to<=4;to++){
  if(from==to)continue;
  for(int sides=0;sides<4;sides++){
   struct unit u;unit_init(&u,fixed);
   u.side_extra[0]=!!(sides&1);u.side_extra[1]=!!(sides&2);
   if(from<=2)press_stock_master(&u,from);else press_soft_master(&u,from);
   (void)lights(&u);
   if(to<=2)press_stock_master(&u,to);else press_soft_master(&u,to);
   int got=lights(&u);
   /* What a DJ should see: one MASTER light, on the side that shows the master deck. */
   int left_shows=u.side_extra[0]?3:1,right_shows=u.side_extra[1]?4:2;
   int want=(to==left_shows)*1000+(to==right_shows)*10;
   if(fixed&&got!=want){fprintf(stderr,"pair %d->%d sides %d: got %04d want %04d\n",from,to,sides,got,want);assert(0);}
   /* Once more after the sides swap back and forth, which today exposes the stale stock number. */
   u.side_extra[0]=!u.side_extra[0];u.side_extra[1]=!u.side_extra[1];
   got=lights(&u);left_shows=u.side_extra[0]?3:1;right_shows=u.side_extra[1]?4:2;
   want=(to==left_shows)*1000+(to==right_shows)*10;
   if(fixed&&got!=want){fprintf(stderr,"pair %d->%d swapped %d: got %04d want %04d\n",from,to,sides,got,want);assert(0);}
  }
 }
}
/* Master back from a software deck to a stock deck, both ways the sentinel can see it. */
static void back_to_stock(int fixed){
 struct unit u;unit_init(&u,fixed);
 press_stock_master(&u,1);press_soft_master(&u,4);
 EXPECT(&u,1000,0);
 /* DECK 1 MASTER again: the sentinel still holds DECK 1, so it writes nothing; the engine moves. */
 press_stock_master(&u,1);
 EXPECT(&u,1000,1000);
 assert(!fixed||u.ui==1);
 /* DECK 4 again, then DECK 2: the sentinel writes 2 itself and stock owns the number again. */
 press_soft_master(&u,4);
 EXPECT(&u,1000,0);
 press_stock_master(&u,2);
 EXPECT(&u,10,10);
 assert(!fixed||(!u.ml.owned&&u.ui==2));
}
/* DECK 3 to DECK 4, and a release to no master (DECK 4 unloaded or its lease returned with no master). */
static void software_to_software(int fixed){
 struct unit u;unit_init(&u,fixed);
 u.side_extra[0]=u.side_extra[1]=1;
 press_stock_master(&u,2);
 EXPECT(&u,0,0);                   /* DECK 2 master, both sides on software decks */
 press_soft_master(&u,3);
 EXPECT(&u,0,1000);
 press_soft_master(&u,4);
 EXPECT(&u,10,10);                 /* today right only because DECK 2 was the stock master */
 u.side_extra[1]=0;
 EXPECT(&u,10,0);                  /* today DECK 2 shows MASTER it does not have */
 engine_none(&u);
 EXPECT(&u,10,0);
 assert(!fixed||u.ui==-1);
}
/* SYNC lamps on a software side follow the software deck only. */
static void sync_lamps(int fixed){
 struct unit u;unit_init(&u,fixed);
 u.side_extra[0]=1;press_stock_master(&u,2);   /* right side on DECK 2, the master: R-MASTER lit throughout */
 u.soft[0].sync.requested=1;
 EXPECT(&u,10,110);                /* DECK 3 synced, deck 1 not: today dark */
 u.stock_sync[0]=1;
 EXPECT(&u,110,110);
 u.soft[0].sync.requested=0;
 EXPECT(&u,10,10);                 /* deck 1 synced, DECK 3 not: dark either way */
 u.soft[0].sync.requested=1;u.soft[0].sync.offset=1;u.stock_sync[0]=0;
 int on=0,off=0;
 for(int i=0;i<8;i++){int l=lights(&u);on+=l==110;off+=l==10;}
 if(fixed)assert(on&&off&&on+off==8);  /* flashing with a kept offset */
 else assert(off==8);
 u.soft[0].loaded=0;
 EXPECT(&u,10,10);                 /* nothing loaded: no lamp */
}
/* External masters and unknown observations never take the number; the sentinel's value is left alone. */
static void ui_step_rules(void){
 struct xz_ml_ui s={0};int32_t w=0;
 assert(!xz_ml_ui_step(&s,XZ_MASTER_DECK1,2,&w)&&!s.owned);
 assert(!xz_ml_ui_step(&s,XZ_MASTER_EXTERNAL,1,&w)&&!s.owned);
 assert(!xz_ml_ui_step(&s,XZ_MASTER_UNKNOWN,1,&w)&&!s.owned);
 assert(xz_ml_ui_step(&s,XZ_MASTER_DECK4,1,&w)&&w==4&&s.owned);
 assert(!xz_ml_ui_step(&s,XZ_MASTER_DECK4,4,&w));
 assert(!xz_ml_ui_step(&s,XZ_MASTER_UNKNOWN,4,&w)&&s.owned);   /* a stale sample changes nothing */
 assert(!xz_ml_ui_step(&s,XZ_MASTER_EXTERNAL,4,&w)&&s.owned);
 assert(xz_ml_ui_step(&s,XZ_MASTER_NONE,4,&w)&&w==-1);
 assert(xz_ml_ui_step(&s,XZ_MASTER_DECK3,-1,&w)&&w==3);
 assert(xz_ml_ui_step(&s,XZ_MASTER_DECK3,7,&w)&&w==3&&s.owned);  /* someone wrote 7 while DECK 3 leads: retaken */
 struct xz_ml_ui t={0};
 assert(xz_ml_ui_step(&t,XZ_MASTER_DECK3,1,&w)&&w==3);
 assert(!xz_ml_ui_step(&t,XZ_MASTER_DECK2,2,&w)&&!t.owned);     /* the sentinel wrote 2 */
 assert(!xz_ml_ui_step(&t,XZ_MASTER_DECK1,2,&w)&&!t.owned);     /* stock's own business now */
 assert(xz_ml_ui_number(XZ_MASTER_EXTERNAL)==XZ_ML_NO_TARGET&&xz_ml_ui_number(XZ_MASTER_UNKNOWN)==XZ_ML_NO_TARGET);
}
/* The list's shape, its index and its spare room are checked before stock's writer is called. */
static int appended;
static void count_append(void *ctx,unsigned id,unsigned ch){appended++;stock_set(ctx,id,ch,1);}
static void bad_append(void *ctx,unsigned id,unsigned ch){(void)id;(void)ch;((struct stat *)ctx)->count++;}
static void insertion_guards(void){
 static struct stat s;struct xz_extra_deck d;memset(&d,0,sizeof d);d.loaded=1;d.master=1;d.sync.requested=1;
 struct xz_ml_stat v=view(&s);
 memset(&s,0,sizeof s);appended=0;
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==2&&appended==2);
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==0&&appended==2);   /* present: nothing */
 assert(xz_ml_insert_missing(&v,2,&d,count_append,&s)==2);
 d.master=d.sync.requested=0;
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==0);
 d.master=1;d.loaded=0;
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==0);
 d.loaded=1;
 /* Full list: stop short of the last records. */
 memset(&s,0,sizeof s);s.count=XZ_ML_STAT_CAPACITY-XZ_ML_STAT_HEADROOM;appended=0;
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==0&&!appended);
 s.count=XZ_ML_STAT_CAPACITY+1;
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==-1);
 /* Wrong dimensions, an index past the count, a stray unindexed record, a writer that did not index. */
 memset(&s,0,sizeof s);struct xz_ml_stat w=v;w.slots=3;
 assert(xz_ml_insert_missing(&w,1,&d,count_append,&s)==-1);
 s.index[XZ_ML_ID_MASTER*2]=5;s.count=1;
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==-1);
 d.sync.requested=1;
 memset(&s,0,sizeof s);s.count=1;wr(s.records,XZ_ML_ID_BEAT_SYNC);wr(s.records+4,1);
 assert(xz_ml_insert_missing(&v,1,&d,count_append,&s)==-1);
 memset(&s,0,sizeof s);
 assert(xz_ml_insert_missing(&v,1,&d,bad_append,&s)==-1);
 assert(xz_ml_insert_missing(&v,3,&d,count_append,&s)==0&&xz_ml_insert_missing(&v,0,&d,count_append,&s)==0);
}
/* "table": the four lamps today and fixed for every from/to pair and side choice, SYNC on the other decks
   (REPORT.md tables are printed from this). Columns: L-MASTER L-SYNC R-MASTER R-SYNC. */
static void print_tables(void){
 static const char *name[4]={"DECK 1","DECK 2","DECK 3","DECK 4"};
 puts("from    -> to      sides(L,R)  sync on        today  fixed");
 for(int from=1;from<=4;from++)for(int to=1;to<=4;to++){
  if(from==to)continue;
  for(int sides=0;sides<4;sides++)for(int sync=0;sync<2;sync++){
   int out[2];
   for(int fixed=0;fixed<2;fixed++){
    struct unit u;unit_init(&u,fixed);
    u.side_extra[0]=!!(sides&1);u.side_extra[1]=!!(sides&2);
    if(from<=2)press_stock_master(&u,from);else press_soft_master(&u,from);
    /* sync: every deck but the new master has SYNC on */
    for(int k=1;k<=4&&sync;k++){if(k==to)continue;if(k<=2)u.stock_sync[k-1]=1;else u.soft[k-3].sync.requested=1;}
    (void)lights(&u);
    if(to<=2)press_stock_master(&u,to);else press_soft_master(&u,to);
    out[fixed]=lights(&u);
   }
   printf("%s -> %s  %s,%s   %-13s  %04d   %04d%s%c",name[from-1],name[to-1],sides&1?"3":"1",sides&2?"4":"2",
          sync?"all others":"none",out[0],out[1],out[0]!=out[1]?"  changed":"",10);
  }
 }
}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"table")){print_tables();return 0;}
 ui_step_rules();
 insertion_guards();
 for(int fixed=0;fixed<2;fixed++){
  owners_sequence(fixed);
  every_pair(fixed);
  back_to_stock(fixed);
  software_to_software(fixed);
  sync_lamps(fixed);
 }
 puts("master lights: ok");
 return 0;
}
