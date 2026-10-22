/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>
#define XZ_CLOCK_COMPAT_H
static uint32_t clock_ms;
static int fixture_clock(clockid_t id,struct timespec *t){
 (void)id;t->tv_sec=clock_ms/1000;t->tv_nsec=(long)(clock_ms%1000)*1000000;return 0;
}
#define xz_clock_gettime_compat fixture_clock
#include "extra_decks.c"
#include "shell_runtime.h"
#include <stdio.h>

static struct xz_extra_decks supplied,published;
static struct xz_master_observation actual;
static int copied,order;
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);}}while(0)
void xz_xp_read(struct xz_xp_players *p,struct xz_extra_decks *out){(void)p;order=order*10+1;*out=supplied;}
void xz_sync_link_runtime_poll(const struct xz_extra_decks *s){(void)s;order=order*10+2;}
int xz_sync_link_runtime_master_observe(struct xz_master_observation *out){order=order*10+3;*out=actual;return copied;}
void xz_jog_display_runtime_publish(const struct xz_extra_decks *s,uint32_t now){(void)now;order=order*10+4;published=*s;}

static void reset(int deck){
 clock_ms=1000;order=0;copied=1;memset(&supplied,0,sizeof supplied);memset(&published,0,sizeof published);
 supplied.master_intent=(struct xz_master_intent){.deck=deck,.epoch=11,.explicit_choice=1,.operator_epoch=7};
 actual=(struct xz_master_observation){.deck=deck,.revision=3,.observed_ms=1000,.operator_epoch=7,.request_epoch=11,.lease_generation=4,.route_epoch=5,.load_revision=6,.transport_generation=8};
 for(int i=0;i<2;i++){
  supplied.deck[i].known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER);
  supplied.deck[i].loaded=1;supplied.deck[i].master=1;supplied.deck[i].master_scope=12;
  supplied.deck[i].timeline.route_epoch=5;supplied.deck[i].timeline.load_revision=6;supplied.deck[i].timeline.transport_generation=8;
 }
 players=(struct xz_xp_players *)(uintptr_t)1;
 control_state=CONTROL_AVAILABLE|CONTROL_ROUTE;
}
static void run(int expected,int known){
 struct xz_extra_decks out;CHECK(xz_extra_decks_snapshot(&out));
 CHECK(order==1234);CHECK(!memcmp(&out.master_actual,&published.master_actual,sizeof actual));
 CHECK(out.master_actual.deck==expected);
 for(int i=0;i<2;i++){
  CHECK(out.deck[i].master==(expected==i+2));
  CHECK(!!(out.deck[i].known&XZ_EXTRA_KNOWN(XZ_EXTRA_F_MASTER))==known);
  CHECK(published.deck[i].master==out.deck[i].master);
 }
}
static void ui_projection(int deck,int fresh,int route_up){
 reset(deck);supplied.route_up=route_up;supplied.master_actual=actual;
 if(!fresh)supplied.master_actual.observed_ms=clock_ms-XZ_MASTER_MAX_AGE_MS-1;
 struct xz_shell_model m={0};struct xz_shrt_extra_parts parts[2]={{0}};
 for(int i=0;i<4;i++){m.deck[i].master=1;m.deck[i].known=XZ_SHELL_KNOWN(XZ_SHELL_F_MASTER);}
 xz_shrt_extra_map(&m,&supplied,parts,clock_ms,clock_ms);
 int eligible=fresh&&(route_up||deck<2);
 for(int i=0;i<4;i++)CHECK(m.deck[i].master==(eligible&&deck==i));
}
int main(void){
 for(int d=0;d<4;d++)for(int fresh=0;fresh<2;fresh++)for(int route_up=0;route_up<2;route_up++)
  ui_projection(d,fresh,route_up);
 for(int d=0;d<4;d++){
  reset(d);run(d,1);
  reset(d);clock_ms+=XZ_MASTER_MAX_AGE_MS;run(d,1);
  reset(d);clock_ms+=XZ_MASTER_MAX_AGE_MS+1;run(XZ_MASTER_UNKNOWN,0);
  reset(d);actual.observed_ms=clock_ms+1;run(XZ_MASTER_UNKNOWN,0);
  reset(d);actual.revision=0;run(XZ_MASTER_UNKNOWN,0);
  reset(d);actual.operator_epoch=0;run(XZ_MASTER_UNKNOWN,0);
  reset(d);copied=0;run(XZ_MASTER_UNKNOWN,0);
  reset(d);clock_ms=20;actual.observed_ms=UINT32_MAX-20;run(d,1);
 }
 for(int d=2;d<4;d++)for(int bad=0;bad<7;bad++){
  reset(d);
  switch(bad){case 0:actual.route_epoch++;break;case 1:actual.load_revision++;break;case 2:actual.transport_generation++;break;
   case 3:actual.request_epoch++;break;case 4:actual.lease_generation=0;break;case 5:supplied.deck[d-2].loaded=0;break;case 6:actual.request_epoch=0;break;}
  run(XZ_MASTER_UNKNOWN,0);
 }
 reset(2);supplied.master_intent.deck=3;supplied.master_intent.epoch++;run(2,1); /* old actual while other promotion is pending */
 reset(2);actual=(struct xz_master_observation){.deck=XZ_MASTER_UNKNOWN,.operator_epoch=8};run(XZ_MASTER_UNKNOWN,0);
 reset(2);actual.deck=XZ_MASTER_EXTERNAL;run(XZ_MASTER_EXTERNAL,1);
 reset(2);actual.deck=XZ_MASTER_NONE;run(XZ_MASTER_NONE,1);
 reset(2);actual.deck=99;run(XZ_MASTER_UNKNOWN,0);
 players=NULL;struct xz_extra_decks empty;CHECK(!xz_extra_decks_snapshot(&empty));CHECK(empty.master_actual.deck==XZ_MASTER_UNKNOWN);
 printf("RESULT checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
