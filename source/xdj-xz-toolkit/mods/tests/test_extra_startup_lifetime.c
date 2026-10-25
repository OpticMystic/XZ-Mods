/* A snapshot scheduled inside a failing connector creation must see no new player. */
#define _GNU_SOURCE
#include <pthread.h>
#include <errno.h>
static int fail_connector(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
#define pthread_create fail_connector
#include "../extra_decks.c"
#undef pthread_create
#define main unused_connector_checks
#include "../test_extra_decks.c"
#undef main
struct xz_xp_players { unsigned marker; };
static unsigned read_calls,destroy_calls;
struct xz_xp_players *xz_xp_create(const struct xz_xp_env *e){(void)e;struct xz_xp_players *p=calloc(1,sizeof *p);assert(p);p->marker=123;return p;}
int xz_xp_start(struct xz_xp_players *p){assert(p->marker==123);return 0;}
void xz_xp_stop(struct xz_xp_players *p){(void)p;}
void xz_xp_destroy(struct xz_xp_players *p){destroy_calls++;free(p);}
void xz_xp_read(struct xz_xp_players *p,struct xz_extra_decks *out){assert(p->marker==123);read_calls++;out->deck[0].loaded=1;}
int xz_xp_read_held(struct xz_xp_players *p,struct xz_extra_decks *out,struct xz_extra_analysis_hold *h){(void)h;xz_xp_read(p,out);return 1;}
void xz_xp_release_analysis(struct xz_extra_decks *o,struct xz_extra_analysis_hold *h){(void)o;memset(h,0,sizeof *h);}
void xz_xp_notice(struct xz_xp_players *p,const char *s){(void)p;(void)s;}
void xz_xp_set_native_sync_clock(struct xz_xp_players *p,int(*f)(struct xz_sync_native_snapshot *)){(void)p;(void)f;}
void xz_xp_set_cue_store(struct xz_xp_players *p,xz_cue_read_fn r,xz_cue_write_fn w){(void)p;(void)r;(void)w;}
int xz_xp_input(struct xz_xp_players *p,const struct xz_extra_deck_event *e){(void)p;(void)e;return 0;}
uint32_t xz_xp_revision(const struct xz_xp_players *p){(void)p;return 0;}
int xz_xp_needs_native_sync(const struct xz_xp_players *p){(void)p;return 0;}
int xz_xp_use_native_master(struct xz_xp_players *p){(void)p;return 0;}
uint32_t xz_xp_jog_content_epoch(int d){(void)d;return 1;}
enum xz_extra_wave_copy_result xz_xp_copy_wave(struct xz_xp_players *p,int d,const struct xz_extra_wave_key *k,uint32_t f,struct xz_extra_wave_chunk *o){(void)p;(void)d;(void)k;(void)f;(void)o;return XZ_EXTRA_WAVE_COPY_ABSENT;}
int xz_cues_worker_start(int(*r)(int,uint32_t,uint64_t,struct xz_extra_track *),void(*n)(const char *),const char *a,const char *b){(void)r;(void)n;(void)a;(void)b;return 0;}
void xz_cues_worker_stop(void){}
int xz_cues_restore(const struct xz_extra_track *t,uint64_t g,int32_t d,struct xz_saved_cues *o){(void)t;(void)g;(void)d;(void)o;return -1;}
int xz_cues_enqueue(int a,const struct xz_extra_track *t,uint64_t g,int32_t d,const struct xz_saved_cues *s,const struct xz_cue_edit *e){(void)a;(void)t;(void)g;(void)d;(void)s;(void)e;return 0;}
static int fail_connector(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *ctx){
 (void)t;(void)a;(void)fn;(void)ctx;struct xz_extra_decks snapshot;
 (void)xz_extra_decks_snapshot(&snapshot);return EAGAIN;
}
int main(void){
 unsetenv("XZ_MODS_FOUR_DECK");assert(xz_extra_decks_start()==-1);
 fprintf(stderr,"startup-failure snapshots=%u destroyed=%u\n",read_calls,destroy_calls);
 assert(!read_calls&&"PARTIAL_STARTUP_VISIBLE");assert(destroy_calls==1&&!players&&!route.started);
 puts("PASS failing connector creation never publishes the private player");return 0;
}
