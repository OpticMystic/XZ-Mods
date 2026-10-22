#define main retained_main
#include "test_extra_player.c"
#undef main
#include "extra_cue_store.h"
#include <fcntl.h>
#include <unistd.h>
static struct xz_saved_cues saved;static int have_saved,read_only;static unsigned writes,checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
static int read_cues(const struct xz_extra_track *t,uint64_t generation,int32_t duration,struct xz_saved_cues *out){(void)t;(void)generation;(void)duration;if(!have_saved)return 0;*out=saved;return 1;}
static int write_cues(int deck,const struct xz_extra_track *t,uint64_t generation,int32_t duration,const struct xz_saved_cues *base,const struct xz_cue_edit *edit){
 (void)deck;(void)t;(void)generation;(void)duration;if(read_only)return 0;if(!have_saved)saved=*base;
 assert(xz_cues_apply(&saved,edit));have_saved=1;writes++;return 1;
}
static void disk_notice(const char *s){assert(s);__atomic_add_fetch(&writes,1,__ATOMIC_RELEASE);}
static int disk_resolve(int source,uint32_t id,uint64_t generation,struct xz_extra_track *out){return generation==42?resolve(source,id,out):-1;}
static void checked_load(int deck){
 struct xz_extra_deck_event e=event(deck,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=1;e.load.media_generation=42;e.load.route_epoch=f.link;assert(input(e));step();
}
int main(int argc,char **argv){
 assert(argc==3);assert(!pthread_mutex_init(&f.lock,NULL));snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);xz_xp_set_cue_store(p,read_cues,write_cues);fake_route(8123);step();load(0,1);
 struct xz_extra_deck_event call=event(0,XZ_EXTRA_IN_PAD_MODE,1);call.hot_cue=XZ_EXTRA_PAD_HOT_CUE;
 assert(input(call));step();pad_key(0,0,1);pad_key(0,0,0);assert(input(call));step();CHECK(!view.deck[0].hot_cue[7].set);
 seek(0,800);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_MEMORY);CHECK(writes==1&&saved.memory_count==2);
 CHECK(saved.hot[7].set); /* CALL unloads a pad; it must not delete the stored cue. */
 seek(0,900);pad_key(0,2,1);pad_key(0,2,0);CHECK(writes==2&&saved.hot[2].set&&saved.hot[2].ms==900);
 load(0,1);CHECK(view.deck[0].hot_cue[2].set&&view.deck[0].hot_cue[2].ms==900);
 seek(0,700);press(0,XZ_EXTRA_IN_LOOP_DOUBLE);CHECK(debug(0).cue==ms_frames(0,800));
 press(0,XZ_EXTRA_IN_MEMORY_DELETE);CHECK(writes==3&&saved.memory_count==1);
 struct xz_extra_deck_event clear=event(0,XZ_EXTRA_IN_HOT_CUE,1);clear.hot_cue=2;clear.shift=1;assert(input(clear));step();CHECK(writes==4&&!saved.hot[2].set);
 load(0,1);CHECK(!view.deck[0].hot_cue[2].set);read_only=1;
 seek(0,1000);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_MEMORY);CHECK(writes==4);
 CHECK(strstr(view.notice,"SESSION")!=NULL);
 xz_xp_destroy(p);p=NULL;
 /* Actual player -> async writer -> sidecar -> fresh player load, with two decks editing one track. */
 writes=0;assert(!xz_cues_worker_start(disk_resolve,disk_notice,fixtures,fixtures));
 env.resolve_checked=disk_resolve;p=xz_xp_create(&env);assert(p);xz_xp_set_cue_store(p,xz_cues_restore,xz_cues_enqueue);fake_route(8124);step();checked_load(0);checked_load(1);
 seek(0,800);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_MEMORY);
 seek(1,900);pad_key(1,2,1);pad_key(1,2,0);
 xz_cues_worker_stop();CHECK(writes==2);
 assert(!xz_cues_worker_start(disk_resolve,disk_notice,fixtures,fixtures));checked_load(0);
 CHECK(view.deck[0].hot_cue[2].set&&view.deck[0].hot_cue[2].ms==900);
 seek(0,700);press(0,XZ_EXTRA_IN_LOOP_DOUBLE);CHECK(debug(0).cue==ms_frames(0,800));
 xz_xp_destroy(p);p=NULL;xz_cues_worker_stop();
 printf("RESULT cues checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
