/* SPDX-License-Identifier: MIT */
/* Host test of the runtime's native reads (tracklist_runtime.c, included to reach its static functions) against a
   fake player memory: the ON AIR guard, pointer chain and channel order, the DECK 1/2 load record, and the DECK 3/4
   route rule. The addresses and guard bytes are the firmware 1.26 values the runtime uses. */
#include "tracklist_runtime.c"
#include <stdio.h>

static int failures, checks;
#define CHECK(c) do{checks++;if(!(c)){failures++;fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);}}while(0)

/* ---- fake memory: a few regions ---- */
struct region { uint32_t at, n; unsigned char *bytes; };
static struct region mem[16];static unsigned regions;
static int flip_root;                                 /* the root changes between two reads: unstable */
static void map(uint32_t at,uint32_t n){mem[regions].at=at;mem[regions].n=n;mem[regions].bytes=calloc(1,n);regions++;}
static unsigned char *at_ptr(uint32_t a,size_t n){
 for(unsigned i=0;i<regions;i++)if(a>=mem[i].at&&(uint64_t)a+n<=(uint64_t)mem[i].at+mem[i].n)return mem[i].bytes+(a-mem[i].at);
 return NULL;
}
static void put(uint32_t a,const void *p,size_t n){unsigned char *d=at_ptr(a,n);if(d)memcpy(d,p,n);}
static void put32(uint32_t a,uint32_t v){put(a,&v,4);}
static unsigned root_reads;
void xz_log(const char *m){(void)m;}
int xz_read_memory(uint32_t a,void *out,size_t n){
 unsigned char *s=at_ptr(a,n);if(!s)return -1;
 memcpy(out,s,n);
 if(a==0x1c8981cu&&flip_root&&++root_reads%2==0){uint32_t v=0x3200000u;memcpy(out,&v,4);}
 return 0;
}
/* The other natives the runtime reaches are weak; the ones this test needs are faked here. */
static int fake_playing[2]={1,0};
int xz_arcade_native_clock_valid(xz_arcade_memory_read r,void *c){(void)r;(void)c;return 1;}
int xz_native_position_clock_read_timed(xz_arcade_memory_read r,void *c,unsigned deck,struct xz_arcade_track_clock *o,
 xz_arcade_observation_clock s,void *sc){(void)r;(void)c;(void)s;(void)sc;memset(o,0,sizeof *o);
 o->observed=XZ_ARCADE_CLOCK_STATE;o->playing=(unsigned)fake_playing[deck];return 0;}
static struct xz_extra_decks fake_snap;static int fake_route;
int xz_extra_decks_snapshot(struct xz_extra_decks *out){*out=fake_snap;return fake_route;}

static void setup(void){
 static const unsigned char get_state[16]={0x94,0x31,0x90,0xe5,0x00,0x00,0xa0,0xe3,0x08,0xd0,0x4d,0xe2,0x53,0x14,0xe7,0xe7};
 map(0x25c8e0,16);put(0x25c8e0,get_state,16);
 const uint32_t loads[4]={0xc8838,0xc884c,0xc8874,0xc8860};
 for(int i=0;i<4;i++){unsigned char b[4]={(unsigned char)(0x0b-i),0x00,0xdd,0xe5};map(loads[i],4);put(loads[i],b,4);}
 map(0x1c8981c,4);put32(0x1c8981c,0x3100000u);       /* mgr */
 map(0x3100000,0x60);put32(0x3100050,0x3101000u);     /* mixer */
 map(0x3101000,0x1a4);
 map(0x3200000,0x60);put32(0x3200050,0x3101000u);
 map(0x2247cac+0x1610,0xc0);                          /* stTotalWk load records, decks 0 and 1 */
}

static void test_air(void){
 int air[4];
 /* Bytes 3..0 of the word are CH1..CH4. CH1 and CH3 on air; a byte other than 1 is off, as UiGetMixerOnAir4ch. */
 unsigned char word[4]={0x00,0x01,0x02,0x01};
 put(0x3101194,word,4);
 read_air(air);
 CHECK(air[0]==1&&air[1]==0&&air[2]==1&&air[3]==0);
 CHECK(P.air_guard==1&&P.air_raw==0x01020100u);
 /* The root changes between reads: no value. */
 flip_root=1;read_air(air);flip_root=0;
 CHECK(air[0]==-1&&air[1]==-1&&air[2]==-1&&air[3]==-1&&P.air_failures==1);
}
static void test_air_guard(void){
 int air[4];
 unsigned char wrong[4]={0x0a,0x00,0xdd,0xe5};put(0xc8838,wrong,4);   /* another firmware maps CH1 elsewhere */
 guard=0;read_air(air);
 CHECK(air[0]==-1&&P.air_guard==0);
 unsigned char right[4]={0x0b,0x00,0xdd,0xe5};put(0xc8838,right,4);
 read_air(air);                                       /* the refusal stands for the process */
 CHECK(air[0]==-1);
}
static void test_native(void){
 struct xz_tl_deck_in d;
 /* Deck 1: USB2 (kind 3) track id 4711, settled. Deck 2: loading (state 7). */
 unsigned char rec[16]={0};uint32_t state=1,id=4711;memcpy(rec,&state,4);rec[9]=3;memcpy(rec+12,&id,4);
 put(0x2247cac+0x1610,rec,16);
 state=7;id=12;memcpy(rec,&state,4);rec[9]=2;memcpy(rec+12,&id,4);put(0x2247cac+0x1610+0x60,rec,16);
 memset(&d,0,sizeof d);d.playing=-1;native_deck(0,1000,&d);
 CHECK(d.known&&d.loaded&&d.track_key==((2ull<<32)|4711u)&&d.playing==1);
 memset(&d,0,sizeof d);d.playing=-1;native_deck(1,1000,&d);
 CHECK(d.known&&!d.loaded);
 /* A kind other than USB1/USB2 (a linked player): loaded, no key. */
 state=1;id=99;memcpy(rec,&state,4);rec[9]=5;memcpy(rec+12,&id,4);put(0x2247cac+0x1610+0x60,rec,16);
 memset(&d,0,sizeof d);d.playing=-1;native_deck(1,1000,&d);
 CHECK(d.known&&d.loaded&&d.track_key==0&&d.playing==0);
}
static void test_extra(void){
 struct xz_tl_deck_in d[2];int air[4]={0,0,1,-1};
 memset(&fake_snap,0,sizeof fake_snap);
 struct xz_extra_deck *x=&fake_snap.deck[0];
 x->loaded=1;x->source=1;x->track_id=55;x->jog_content_epoch=9;x->playing=1;
 x->known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_ARTIST)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING);
 snprintf(x->title,sizeof x->title,"T");snprintf(x->artist,sizeof x->artist,"A");
 fake_snap.deck[1].loaded=1;fake_snap.deck[1].known=0;
 fake_route=1;memset(d,0,sizeof d);extra_decks(air,d);
 CHECK(d[0].known&&d[0].loaded&&d[0].track_key==((1ull<<32)|55u)&&d[0].load_id==9&&d[0].playing==1&&d[0].on_air==1);
 CHECK(!strcmp(d[0].title,"T")&&!strcmp(d[0].artist,"A")&&d[0].text_known);
 CHECK(d[1].known&&d[1].loaded&&d[1].playing==-1&&d[1].on_air==-1&&!d[1].text_known);
 /* Route down: CH3/CH4 do not carry DECK 3/4, whatever the mixer says. */
 fake_route=0;memset(d,0,sizeof d);extra_decks(air,d);
 CHECK(d[0].on_air==0&&d[1].on_air==0);
}
/* The switch: the MODS setting before and after start, the test variable overriding it, and the status JSON. An
   observer runtime may not write, so no thread starts here; the decisions are what is checked. */
static void reset_switch(void){armed=0;env_mode=-1;wanted=0;enabled=0;skip_next=0;unsetenv("XZ_MODS_TRACKLIST");}
static void test_switch(void){
 setenv("XZ_MODS_OBSERVER","1",1);
 char json[2048];
 reset_switch();
 xz_tracklist_set_enabled(1);CHECK(wanted==1&&!enabled);          /* the UI applies settings before start */
 CHECK(xz_tracklist_start()==0&&armed&&enabled&&!created);           /* may not write: nothing launched */
 xz_tracklist_set_enabled(0);CHECK(!enabled);
 xz_tracklist_set_enabled(1);CHECK(enabled&&!created);
 size_t n=xz_tracklist_status_json(json,sizeof json);
 CHECK(n>0&&json[0]=='{'&&json[n-1]=='}'&&strstr(json,"\"started\":false")&&strstr(json,"\"enabled\":true")&&strstr(json,"\"env\":null"));
 CHECK(xz_tracklist_status_json(json,16)==0&&json[0]==0);
 reset_switch();setenv("XZ_MODS_TRACKLIST","0",1);
 xz_tracklist_set_enabled(1);CHECK(xz_tracklist_start()==0&&!enabled&&env_mode==MODE_NONE);
 xz_tracklist_set_enabled(1);CHECK(!enabled);                        /* the variable wins over the setting */
 CHECK(xz_tracklist_status_json(json,sizeof json)&&strstr(json,"\"env\":\"0\""));
 reset_switch();setenv("XZ_MODS_TRACKLIST","live",1);
 CHECK(xz_tracklist_start()==0&&enabled&&env_mode==MODE_LIVE);
 xz_tracklist_set_enabled(0);CHECK(enabled);
 reset_switch();setenv("XZ_MODS_TRACKLIST","1",1);
 CHECK(xz_tracklist_start()==0&&enabled&&env_mode==MODE_AT_END);
 reset_switch();setenv("XZ_MODS_TRACKLIST","yes",1);              /* anything else: the setting decides */
 CHECK(xz_tracklist_start()==0&&!enabled&&env_mode==-1);
 reset_switch();unsetenv("XZ_MODS_OBSERVER");
}

int main(void){
 setup();test_air();test_native();test_extra();test_air_guard();test_switch();
 printf("%s: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}
