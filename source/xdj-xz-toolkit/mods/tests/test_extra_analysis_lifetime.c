/* Offline: real player LOAD/collection and actual shell/Classic consumers. */
#define _GNU_SOURCE
#include <time.h>
#include <stdatomic.h>
static int observed_sleep(const struct timespec *,struct timespec *);
#define nanosleep observed_sleep
#include "../extra_player.c"
#undef nanosleep
#include "../shell_runtime.h"
#include "../ui/extra_lane.h"
#include "../sync_link_runtime.h"
#include <assert.h>
#include <stdio.h>
static const char *media_dir;
static atomic_int destroy_waiting,destroy_finished;
static int observed_sleep(const struct timespec *a,struct timespec *b){atomic_store(&destroy_waiting,1);return nanosleep(a,b);}
static void *destroy_player(void *p){xz_xp_destroy(p);atomic_store(&destroy_finished,1);return NULL;}
static uint32_t test_now(void){return 1000;}
static uint32_t link_id(void *p){(void)p;return 1;}
static xz_usb_result submit_pcm(void *p,uint32_t e,int d,uint32_t g,const float *s,uint32_t n,uint64_t a){(void)p;(void)e;(void)d;(void)g;(void)s;(void)n;(void)a;return XZ_USB_AGAIN;}
static xz_usb_result flush_pcm(void *p,uint32_t e,int d,uint32_t *g){(void)p;(void)e;(void)d;++*g;return XZ_USB_OK;}
static xz_usb_result position_pcm(void *p,uint32_t e,xz_usb_position *o){(void)p;(void)e;memset(o,0,sizeof *o);return XZ_USB_AGAIN;}
static xz_usb_result transition_pcm(void *p,uint32_t e,int d,uint32_t g,const float *s,uint32_t n,uint64_t a,uint32_t *v){(void)p;(void)e;(void)d;(void)g;(void)s;(void)n;(void)a;(void)v;return XZ_USB_AGAIN;}
static int resolve_track(int source,uint32_t id,struct xz_extra_track *out){memset(out,0,sizeof *out);out->source=source;out->track_id=id;out->track_bpm_x100=12000;snprintf(out->path,sizeof out->path,"%s/44100.wav",media_dir);return 0;}
int xz_oc_bundle(const char *s,char *f,size_t n){(void)s;(void)f;(void)n;return -1;}
int main(int argc,char **argv){
 assert(argc==3);media_dir=argv[1];
 struct xz_xp_env env={.sink={NULL,link_id,submit_pcm,flush_pcm,position_pcm,transition_pcm},.now_ms=test_now,.resolve=resolve_track};
 struct xz_xp_players *p=xz_xp_create(&env);assert(p);
 struct xz_extra_deck_event event={.kind=XZ_EXTRA_IN_LOAD,.pressed=1};event.load.source=1;event.load.track_id=1;event.load.route_epoch=1;
 assert(load(p,&p->deck[0],&event,1000,0));publish(p,&p->deck[0],1000);
 struct analysis *a=p->deck[0].analysis;assert(a&&!a->columns&&!a->grid);
 a->count=300;a->span_ms=2000;a->normalization=765;a->columns=calloc(300,sizeof *a->columns);
 a->grid_count=4;a->grid=calloc(4,sizeof *a->grid);assert(a->columns&&a->grid);
 for(unsigned i=0;i<300;i++)a->columns[i]=(struct xz_extra_column){37,41,43};
 for(unsigned i=0;i<4;i++)a->grid[i]=(struct xz_extra_beat){.ms=(int32_t)i*500,.beat_in_bar=(uint8_t)(i+1),.bpm_x100=12000};
 publish(p,&p->deck[0],1000);struct xz_extra_decks snapshot={0};
#ifdef XZ_XP_HAS_ANALYSIS_HOLD
 struct xz_extra_analysis_hold hold={0};assert(xz_xp_read_held(p,&snapshot,&hold));
#else
 xz_xp_read(p,&snapshot);
#endif
 assert(snapshot.deck[0].wave.columns&&snapshot.deck[0].grid);
 event.load.track_id=2;assert(load(p,&p->deck[0],&event,1100,0));publish(p,&p->deck[0],1100);
 collect(&p->deck[0],1100+XZ_XP_RETIRE_MS+10000);
 if(!strcmp(argv[2],"shell")){
  assert(xz_shrt_band_peak((const uint8_t *)snapshot.deck[0].wave.columns,300)==43);
  struct xz_shell_column bins[30];assert(xz_shrt_bin_columns((const uint8_t *)snapshot.deck[0].wave.columns,300,43,bins,30)==30);
 }else if(!strcmp(argv[2],"anchor")){
  struct xz_extra_deck *d=&snapshot.deck[0];d->playing=1;
  d->timeline.source_rate=44100;d->timeline.source_frame=33075;d->timeline.sink_frame=1000;d->timeline.observed_wire_frame=1000;
  d->timeline.route_epoch=1;d->timeline.load_revision=1;d->timeline.transport_generation=1;d->timeline.at_ms=1000;d->timeline.completed_rate=1.;
  d->timeline.flags=XZ_EXTRA_TIMELINE_ANCHOR|XZ_EXTRA_TIMELINE_SINK_FRAME|XZ_EXTRA_TIMELINE_APPLIED_RATE;
  struct xz_sync_link_pcm_anchor anchor;
  assert(xz_sync_link_anchor_from_deck(0,d,1000000,&anchor));
  assert(anchor.beat_in_bar==2&&anchor.beat_source_frame==22050&&anchor.future_beat_ms[0]==500&&anchor.phase_eligible);
 }else{
  uint16_t *pixels=calloc(536*268,sizeof *pixels);assert(pixels);
  struct xz_extra_lane_view view={.now_ms=1000,.column_us=10000,.lit=1};
  assert(xz_extra_lane_draw(pixels,536,536*268,0,3,&snapshot.deck[0],&view,NULL));free(pixels);
 }
#ifdef XZ_XP_HAS_ANALYSIS_HOLD
 assert(p->deck[0].retired_count==1);
 if(!strcmp(argv[2],"destroy")){
  pthread_t owner;assert(!pthread_create(&owner,NULL,destroy_player,p));
  while(!atomic_load(&destroy_waiting)&&!atomic_load(&destroy_finished))sched_yield();
  assert(atomic_load(&destroy_waiting)&&!atomic_load(&destroy_finished));
  assert(snapshot.deck[0].wave.columns[0].low==37&&snapshot.deck[0].grid[1].ms==500);
  xz_xp_release_analysis(&snapshot,&hold);pthread_join(owner,NULL);
  assert(atomic_load(&destroy_finished));puts("PASS teardown waits for the outstanding held frame without freeing on release");return 0;
 }
 xz_xp_release_analysis(&snapshot,&hold);assert(!snapshot.deck[0].wave.columns&&!snapshot.deck[0].grid);
 collect(&p->deck[0],1100+XZ_XP_RETIRE_MS+10000);assert(!p->deck[0].retired_count);
 xz_xp_read(p,&snapshot);assert(!snapshot.deck[0].wave.columns&&!snapshot.deck[0].grid);
#endif
 xz_xp_destroy(p);printf("PASS %s consumes a coherent analysis across real LOAD and delayed collection\n",argv[2]);return 0;
}
