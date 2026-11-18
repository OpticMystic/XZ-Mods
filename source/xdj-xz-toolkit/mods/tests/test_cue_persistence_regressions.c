#define main retained_main
#include "test_extra_player.c"
#undef main
#include "extra_cue_store.h"
#include <fcntl.h>
#include <unistd.h>
static unsigned checks,failures,notices;static int generation_ok=1;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
static int disk_resolve(int source,uint32_t id,uint64_t generation,struct xz_extra_track *out){return generation==42&&generation_ok?resolve(source,id,out):-1;}
static void notice(const char *s){assert(s);__atomic_add_fetch(&notices,1,__ATOMIC_RELEASE);printf("NOTICE %s\n",s);}
static void start_store(void){assert(!xz_cues_worker_start(disk_resolve,notice,fixtures,fixtures));}
static void checked_load(int deck){struct xz_extra_deck_event e=event(deck,XZ_EXTRA_IN_LOAD,1);e.load.source=1;e.load.track_id=1;e.load.media_generation=42;e.load.route_epoch=f.link;assert(input(e));step();}
static void initialize(const char *dir){
 assert(!pthread_mutex_init(&f.lock,NULL));snprintf(fixtures,sizeof fixtures,"%s",dir);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",dir);build_analysis();
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolve,master_bpm,NULL,disk_resolve,0,0};
 start_store();p=xz_xp_create(&env);assert(p);xz_xp_set_cue_store(p,xz_cues_restore,xz_cues_enqueue);fake_route(9123);step();checked_load(0);
}
static struct xz_saved_cues disk(void){struct xz_saved_cues s={0};int dir=open(anlz_dir,O_RDONLY|O_DIRECTORY);assert(dir>=0);assert(xz_cues_read_at(dir,1,2000,&s)==1);close(dir);return s;}
static void delete_roundtrip(void){
 seek(0,803);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();
 struct xz_saved_cues before=disk();CHECK(before.memory_count==2);printf("before saved last ms=%d\n",before.memory[before.memory_count-1].ms);
 start_store();checked_load(0);seek(0,700);press(0,XZ_EXTRA_IN_LOOP_DOUBLE);printf("called cue frame=%llu rendered ms=%d\n",(unsigned long long)debug(0).cue,view.deck[0].cue_ms);
 press(0,XZ_EXTRA_IN_MEMORY_DELETE);CHECK(view.deck[0].memory_count==1);xz_cues_worker_stop();
 struct xz_saved_cues after=disk();printf("after DELETE ram count=%u disk count=%u\n",view.deck[0].memory_count,after.memory_count);CHECK(after.memory_count==1);
 start_store();checked_load(0);printf("reloaded memory count=%u\n",view.deck[0].memory_count);CHECK(view.deck[0].memory_count==1);
}
static void readonly_actual(void){
 CHECK(!setenv("XZ_MODS_SETTINGS_READONLY","1",1));seek(0,800);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_MEMORY);seek(0,900);pad_key(0,2,1);pad_key(0,2,0);
 CHECK(view.deck[0].memory_count==2);CHECK(view.deck[0].hot_cue[2].set);CHECK(strstr(view.notice,"SESSION")!=NULL);xz_cues_worker_stop();CHECK(notices==0);
 int dir=open(anlz_dir,O_RDONLY|O_DIRECTORY);assert(dir>=0);struct xz_saved_cues s={0};CHECK(xz_cues_read_at(dir,1,2000,&s)==0);close(dir);CHECK(!unsetenv("XZ_MODS_SETTINGS_READONLY"));
}
static void lifecycle(void){
 for(unsigned i=0;i<4;i++){seek(0,800+(int)i*10);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();start_store();checked_load(0);CHECK(view.deck[0].memory_count==i+2);}
 generation_ok=0;seek(0,950);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();CHECK(disk().memory_count==5);CHECK(view.deck[0].memory_count==6);
}
static void loop_out_edit(void){
 seek(0,500);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_PLAY);seek(0,1000);press(0,XZ_EXTRA_IN_LOOP_OUT);
 CHECK(debug(0).loop_active);CHECK(debug(0).loop_out==ms_frames(0,1000));pad_key(0,2,1);pad_key(0,2,0);pad_key(0,2,1);pad_key(0,2,0);xz_cues_worker_stop();
 CHECK(disk().hot[2].end_ms==1000);start_store();unsigned before=notices;
 press(0,XZ_EXTRA_IN_LOOP_OUT);CHECK(debug(0).loop_edit==2);jog(0,15);step();CHECK(debug(0).loop_out==ms_frames(0,1100));press(0,XZ_EXTRA_IN_LOOP_OUT);CHECK(debug(0).loop_edit==0);
 printf("LOOP OUT completed live end=%d hot end=%d notices=%u\n",view.deck[0].loop.out_ms,view.deck[0].hot_cue[2].end_ms,notices-before);xz_cues_worker_stop();
 struct xz_saved_cues s=disk();printf("LOOP OUT disk end=%d\n",s.hot[2].end_ms);CHECK(s.hot[2].end_ms==1100);
 start_store();checked_load(0);printf("LOOP OUT reloaded hot end=%d\n",view.deck[0].hot_cue[2].end_ms);CHECK(view.deck[0].hot_cue[2].end_ms==1100);
}
static void memory_loop(void){
 seek(0,500);press(0,XZ_EXTRA_IN_CUE);press(0,XZ_EXTRA_IN_PLAY);seek(0,1000);press(0,XZ_EXTRA_IN_LOOP_OUT);
 CHECK(debug(0).loop_active);press(0,XZ_EXTRA_IN_MEMORY);CHECK(view.deck[0].memory_cue[0].end_ms==1000);xz_cues_worker_stop();
 struct xz_saved_cues s=disk();CHECK(s.memory_count==1);CHECK(s.memory[0].ms==500&&s.memory[0].end_ms==1000);
 start_store();checked_load(0);CHECK(view.deck[0].memory_cue[0].ms==500&&view.deck[0].memory_cue[0].end_ms==1000);
 press(0,XZ_EXTRA_IN_MEMORY_DELETE);CHECK(view.deck[0].memory_count==0);xz_cues_worker_stop();CHECK(disk().memory_count==0);
}
int main(int argc,char **argv){assert(argc==3);initialize(argv[1]);
 if(!strcmp(argv[2],"delete-roundtrip"))delete_roundtrip();else if(!strcmp(argv[2],"readonly"))readonly_actual();else if(!strcmp(argv[2],"lifecycle"))lifecycle();else if(!strcmp(argv[2],"loop-out-edit"))loop_out_edit();else if(!strcmp(argv[2],"memory-loop"))memory_loop();else return 2;
 xz_xp_destroy(p);p=NULL;xz_cues_worker_stop();printf("RESULT r14 %s checks=%u failures=%u\n",argv[2],checks,failures);return failures?1:0;
}
