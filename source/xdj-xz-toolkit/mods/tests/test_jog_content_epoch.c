/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
static pthread_mutex_t *watched_lock;
static atomic_int entered_publication;
static int observed_lock(pthread_mutex_t *);
#define pthread_mutex_lock observed_lock
#include "../extra_player.c"
#undef pthread_mutex_lock
#include <assert.h>
#include <stdio.h>
static int observed_lock(pthread_mutex_t *lock){
 if(lock==watched_lock)atomic_store(&entered_publication,1);
 return pthread_mutex_lock(lock);
}

static const char *fixture_dir;
static uint32_t at=1000,route_id=1;
static uint32_t test_now(void){return at;}
static uint32_t test_link(void *p){(void)p;return route_id;}
static xz_usb_result submit(void *p,uint32_t e,int d,uint32_t g,const float *s,uint32_t f,uint64_t a){
 (void)p;(void)e;(void)d;(void)g;(void)s;(void)f;(void)a;return XZ_USB_AGAIN;
}
static xz_usb_result flush_sink(void *p,uint32_t e,int d,uint32_t *g){(void)p;(void)e;(void)d;++*g;return XZ_USB_OK;}
static xz_usb_result position(void *p,uint32_t e,xz_usb_position *out){(void)p;(void)e;memset(out,0,sizeof *out);return XZ_USB_AGAIN;}
static xz_usb_result transition(void *p,uint32_t e,int d,uint32_t g,const float *s,uint32_t f,uint64_t a,uint32_t *n){
 (void)p;(void)e;(void)d;(void)g;(void)s;(void)f;(void)a;(void)n;return XZ_USB_AGAIN;
}
static int resolve_track(int source,uint32_t id,struct xz_extra_track *out){
 if(id==999)return -1;
 memset(out,0,sizeof *out);out->source=source;out->track_id=id;out->track_bpm_x100=12000;
 snprintf(out->path,sizeof out->path,"%s/44100.wav",fixture_dir);return 0;
}
int xz_oc_bundle(const char *s,char *f,size_t n){(void)s;(void)f;(void)n;return -1;}
static struct xz_xp_players *p;
static struct xz_extra_deck_event request;
static atomic_int load_started;
static void *do_load(void *unused){
 (void)unused;atomic_store(&load_started,1);assert(load(p,&p->deck[0],&request,at,0));return NULL;
}
int main(int argc,char **argv){
 assert(argc==2);fixture_dir=argv[1];
 struct xz_xp_env env={{NULL,test_link,submit,flush_sink,position,transition},test_now,resolve_track,NULL,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);
 uint32_t initial=xz_xp_jog_content_epoch(0),right=xz_xp_jog_content_epoch(1);
 assert(initial&&right&&!xz_xp_jog_content_epoch(-1)&&!xz_xp_jog_content_epoch(2));
 struct xz_extra_decks old={0},current={0};xz_xp_read(p,&old);
 assert(old.deck[0].jog_content_epoch==initial);
 request=(struct xz_extra_deck_event){.kind=XZ_EXTRA_IN_LOAD,.pressed=1};
 request.load.source=1;request.load.track_id=1;request.load.route_epoch=1;
 /* A copier owning the publication lock prevents accepted LOAD publication
    from advancing the content epoch before it can unpublish the old view. */
 pthread_mutex_lock(&p->publish_lock);watched_lock=&p->publish_lock;pthread_t thread;
 assert(!pthread_create(&thread,NULL,do_load,NULL));
 while(!atomic_load(&entered_publication))sched_yield();
 assert(xz_xp_jog_content_epoch(0)==initial);
 pthread_mutex_unlock(&p->publish_lock);pthread_join(thread,NULL);
 watched_lock=NULL;
 publish(p,&p->deck[0],at);xz_xp_read(p,&current);
 uint32_t loaded=xz_xp_jog_content_epoch(0);
 assert(loaded==initial+1&&current.deck[0].jog_content_epoch==loaded);
 assert(old.deck[0].jog_content_epoch==initial&&initial!=loaded); /* delayed old view remains old */
 assert(xz_xp_jog_content_epoch(1)==right);
 assert(load(p,&p->deck[0],&request,at,0));publish(p,&p->deck[0],at);
 assert(xz_xp_jog_content_epoch(0)==loaded+1);loaded++;
 /* Two LOAD commands can be drained in one worker pass, with no final loaded
    publication between them. Each accepted transition still advances once. */
 assert(load(p,&p->deck[0],&request,at,0));
 assert(load(p,&p->deck[0],&request,at,0));
 assert(xz_xp_jog_content_epoch(0)==loaded+2);loaded+=2;publish(p,&p->deck[0],at);
 request.load.track_id=999;assert(!load(p,&p->deck[0],&request,at,0));
 assert(xz_xp_jog_content_epoch(0)==loaded);
 request.load.track_id=1;request.load.route_epoch=2;assert(!load(p,&p->deck[0],&request,at,0));
 assert(xz_xp_jog_content_epoch(0)==loaded);
 for(int i=0;i<20;i++){at++;publish(p,&p->deck[0],at);}
 assert(xz_xp_jog_content_epoch(0)==loaded);
 p->deck[0].state=XZ_XP_LOADING;p->deck[0].jog_load_pending=1;publish(p,&p->deck[0],at);loaded++;
 publish(p,&p->deck[0],at);assert(xz_xp_jog_content_epoch(0)==loaded);
 xz_xp_destroy(p);p=xz_xp_create(&env);assert(p);
 assert(xz_xp_jog_content_epoch(0)==loaded+1&&p->view[0].jog_content_epoch==loaded+1);
 atomic_store(&jog_content_epoch[0],UINT32_MAX);p->view[0].jog_content_epoch=UINT32_MAX;
 p->deck[0].state=XZ_XP_LOADING;p->deck[0].jog_load_pending=1;publish(p,&p->deck[0],at);
 assert(!xz_xp_jog_content_epoch(0)&&!p->view[0].jog_content_epoch);
 xz_xp_destroy(p);p=xz_xp_create(&env);assert(p);
 assert(!xz_xp_jog_content_epoch(0)&&!p->view[0].jog_content_epoch);
 p->deck[0].state=XZ_XP_LOADING;publish(p,&p->deck[0],at);
 assert(!xz_xp_jog_content_epoch(0));xz_xp_destroy(p);
 puts("PASS accepted LOAD epoch: current-view lock, delayed snapshot, same-ID load, refusal, repeated publish, restart, sticky exhaustion");
 return 0;
}
