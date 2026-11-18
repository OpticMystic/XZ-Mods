#define main retained_player_main
#include "test_extra_player.c"
#undef main
#include "extra_cue_store.h"
#include <fcntl.h>
#include <unistd.h>

static unsigned checks, failures, notices;
static int side, hz;
static uint32_t track_id;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line=%d %s\n", __LINE__, #x); } } while (0)
static int resolved(int source,uint32_t id,struct xz_extra_track *out) {
 int rc=resolve(source,id,out);
 if(!rc) { snprintf(out->anlz_dir,sizeof out->anlz_dir,"%s",anlz_dir);out->track_bpm_x100=12000; }
 return rc;
}
static int checked_resolve(int source,uint32_t id,uint64_t gen,struct xz_extra_track *out) {
 return gen==42?resolved(source,id,out):-1;
}
static void notice16(const char *s) {
 assert(s);__atomic_add_fetch(&notices,1,__ATOMIC_RELEASE);printf("NOTICE %s\n",s);
}
static void store_start(void) { assert(!xz_cues_worker_start(checked_resolve,notice16,fixtures,fixtures)); }
static void load16(void) {
 clock_ms+=XZ_XP_RETIRE_MS+1u;step();
 struct xz_extra_deck_event e=event(side,XZ_EXTRA_IN_LOAD,1);
 e.load.source=1;e.load.track_id=track_id;e.load.media_generation=42;e.load.route_epoch=f.link;
 assert(input(e));step();assert(debug(side).source_rate==(unsigned)hz);
}
static struct xz_saved_cues disk16(void) {
 struct xz_saved_cues s={0};int fd=open(anlz_dir,O_RDONLY|O_DIRECTORY);assert(fd>=0);
 assert(xz_cues_read_at(fd,track_id,2000,&s)==1);close(fd);return s;
}
static void seed(struct xz_saved_cues *s) {
 int fd=open(anlz_dir,O_RDONLY|O_DIRECTORY);assert(fd>=0);
 assert(!xz_cues_write_at(fd,track_id,2000,s));close(fd);
}
static void setup16(const char *dir,int d,int rate) {
 side=d;hz=rate;track_id=rate==44100?1:2;assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",dir);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",dir);build_analysis();
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,resolved,master_bpm,NULL,checked_resolve,0,0};
 store_start();p=xz_xp_create(&env);assert(p);xz_xp_set_cue_store(p,xz_cues_restore,xz_cues_enqueue);
 fake_route(1616);step();load16();
}
static void call_from(int at) { seek(side,at);press(side,XZ_EXTRA_IN_LOOP_DOUBLE); }
static void new_roundtrips(void) {
 seek(side,803);press(side,XZ_EXTRA_IN_CUE);press(side,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();
 struct xz_saved_cues s=disk16();CHECK(s.memory_count==2);int32_t key=s.memory[1].ms;
 printf("new MEMORY serialized=%d source=%d\n",key,hz);CHECK(key==(hz==44100?802:803));
 for(unsigned i=0;i<24;i++) {
  store_start();load16();call_from(700);CHECK(debug(side).cue==ms_frames(side,key));
  CHECK(view.deck[side].memory_cue[1].ms==key);press(side,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();
  s=disk16();CHECK(s.memory_count==2);CHECK(s.memory[1].ms==key);
 }
 store_start();load16();call_from(700);press(side,XZ_EXTRA_IN_MEMORY_DELETE);xz_cues_worker_stop();
 s=disk16();CHECK(s.memory_count==1&&s.memory[0].ms==500);
 store_start();load16();CHECK(view.deck[side].memory_count==1);
}
static void fractional_export(void) {
 /* An independently written, colored export memory point exercises import, not sidecar restore. */
 file_start(&ext);uint32_t q=open_section(&ext,"PCO2",20);u32(&ext,1);u16(&ext,2);u16(&ext,0);
 pcp2(&ext,1,1,300,0x305aff);pcp2(&ext,8,2,1500,0xff8000);close_section(&ext,q);
 q=open_section(&ext,"PCO2",20);u32(&ext,0);u16(&ext,1);u16(&ext,0);pcp2(&ext,0,1,803,0x123456);close_section(&ext,q);
 file_end(&ext);write_file("ANLZ0000.EXT",&ext);load16();CHECK(view.deck[side].memory_count==1);
 CHECK(view.deck[side].memory_cue[0].ms==803);CHECK(view.deck[side].memory_cue[0].rgb==0x123456);
 call_from(700);CHECK(debug(side).cue==ms_frames(side,803));press(side,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();
 struct xz_saved_cues s=disk16();CHECK(s.memory_count==1&&s.memory[0].ms==803&&s.memory[0].rgb==0x123456);
 CHECK(s.hot[0].rgb==0x305aff&&s.hot[7].rgb==0xff8000);
 store_start();load16();call_from(700);press(side,XZ_EXTRA_IN_PLAY);seek(side,1000);press(side,XZ_EXTRA_IN_LOOP_OUT);
 press(side,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();s=disk16();
 CHECK(s.memory_count==1&&s.memory[0].ms==803&&s.memory[0].end_ms==1000&&s.memory[0].rgb==0x123456);
 store_start();load16();call_from(700);CHECK(debug(side).loop_active);CHECK(debug(side).loop_in==ms_frames(side,803));
 CHECK(debug(side).loop_out==ms_frames(side,1000));press(side,XZ_EXTRA_IN_MEMORY_DELETE);xz_cues_worker_stop();CHECK(disk16().memory_count==0);
}
static void adjacent_points(void) {
 struct xz_saved_cues s={0};s.memory_count=3;
 for(unsigned i=0;i<3;i++)s.memory[i]=(struct xz_extra_cue){1,801+(int)i,0x112233+i,0};seed(&s);load16();
 seek(side,804);press(side,XZ_EXTRA_IN_CUE);unsigned before=notices;press(side,XZ_EXTRA_IN_MEMORY_DELETE);xz_cues_worker_stop();
 CHECK(notices==before);CHECK(view.deck[side].memory_count==3);CHECK(strstr(view.notice,"NO MEMORY POINT")!=NULL);
 struct xz_saved_cues after=disk16();CHECK(!memcmp(&s,&after,sizeof s));
 store_start();seek(side,802);press(side,XZ_EXTRA_IN_CUE);press(side,XZ_EXTRA_IN_MEMORY_DELETE);xz_cues_worker_stop();after=disk16();
 CHECK(after.memory_count==2);CHECK(after.memory[0].ms==801&&after.memory[0].rgb==0x112233);
 CHECK(after.memory[1].ms==803&&after.memory[1].rgb==0x112235);
 store_start();load16();CHECK(view.deck[side].memory_count==2);CHECK(view.deck[side].memory_cue[0].ms==801&&view.deck[side].memory_cue[1].ms==803);
}
static void make_loop(void) {
 seek(side,500);press(side,XZ_EXTRA_IN_CUE);press(side,XZ_EXTRA_IN_PLAY);seek(side,1000);press(side,XZ_EXTRA_IN_LOOP_OUT);
 pad_key(side,2,1);pad_key(side,2,0);pad_key(side,2,1);pad_key(side,2,0);
 xz_cues_worker_stop();CHECK(disk16().hot[2].end_ms==1000);store_start();
}
static void finish_loop(int timeout,int readonly) {
 make_loop();struct xz_saved_cues original=disk16();
 if(readonly)assert(!setenv("XZ_MODS_SETTINGS_READONLY","1",1));
 unsigned before=notices;press(side,XZ_EXTRA_IN_LOOP_OUT);CHECK(debug(side).loop_edit==2);
 jog(side,15);step();CHECK(debug(side).loop_out==ms_frames(side,1100));
 if(timeout) { clock_ms+=10001;step(); } else press(side,XZ_EXTRA_IN_LOOP_OUT);
 CHECK(!debug(side).loop_edit);CHECK(view.deck[side].hot_cue[2].end_ms==1100);
 xz_cues_worker_stop();struct xz_saved_cues after=disk16();
 if(readonly) { CHECK(!memcmp(&original,&after,sizeof after));CHECK(notices==before); }
 else { CHECK(after.hot[2].end_ms==1100);CHECK(notices==before+1); }
 store_start();load16();CHECK(view.deck[side].hot_cue[2].end_ms==(readonly?1000:1100));
 /* Entering and completing unchanged adjustment must not enqueue a duplicate save. */
 pad_key(side,2,1);pad_key(side,2,0);before=notices;
 press(side,XZ_EXTRA_IN_LOOP_OUT);press(side,XZ_EXTRA_IN_LOOP_OUT);xz_cues_worker_stop();CHECK(notices==before);
}
static void call_colors(void) {
 struct xz_saved_cues s={0};s.memory_count=2;s.memory[0]=(struct xz_extra_cue){1,803,0xa1b2c3,0};s.memory[1]=(struct xz_extra_cue){1,1301,0xb2c3d4,0};
 s.hot[0]=(struct xz_extra_cue){1,301,0xff4455,0};s.hot[7]=(struct xz_extra_cue){1,1501,0x22ff33,0};seed(&s);load16();
 struct xz_extra_deck_event call=event(side,XZ_EXTRA_IN_PAD_MODE,1);call.hot_cue=XZ_EXTRA_PAD_HOT_CUE;assert(input(call));call.pressed=0;assert(input(call));step();CHECK(view.deck[side].pad_mode==XZ_EXTRA_PAD_CALL);pad_key(side,0,1);pad_key(side,0,0);
 call.pressed=1;assert(input(call));call.pressed=0;assert(input(call));step();CHECK(view.deck[side].pad_mode==XZ_EXTRA_PAD_HOT_CUE);CHECK(view.deck[side].hot_cue[0].set);CHECK(!view.deck[side].hot_cue[7].set);
 call_from(700);press(side,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();struct xz_saved_cues after=disk16();
 CHECK(after.hot[0].set&&after.hot[7].set);CHECK(after.hot[0].rgb==0xff4455&&after.hot[7].rgb==0x22ff33);
 CHECK(after.memory_count==2&&after.memory[0].ms==803&&after.memory[1].ms==1301);
 CHECK(after.memory[0].rgb==0xa1b2c3&&after.memory[1].rgb==0xb2c3d4);
 store_start();load16();CHECK(view.deck[side].hot_cue[7].set);CHECK(view.deck[side].memory_cue[0].ms==803);
}
static void short_fractional_loop(void) {
 struct xz_saved_cues s={0};s.memory_count=1;s.memory[0]=(struct xz_extra_cue){1,803,0x123456,804};seed(&s);load16();
 call_from(700);CHECK(debug(side).loop_active);CHECK(debug(side).loop_in==ms_frames(side,803));CHECK(debug(side).loop_out==ms_frames(side,804));
 printf("before MEMORY disk=803..804 live=%d..%d frames=%llu..%llu\n",view.deck[side].loop.in_ms,view.deck[side].loop.out_ms,(unsigned long long)debug(side).loop_in,(unsigned long long)debug(side).loop_out);
 press(side,XZ_EXTRA_IN_MEMORY);xz_cues_worker_stop();struct xz_saved_cues after=disk16();
 printf("after MEMORY disk=%d..%d RAM=%d..%d\n",after.memory[0].ms,after.memory[0].end_ms,view.deck[side].memory_cue[0].ms,view.deck[side].memory_cue[0].end_ms);
 CHECK(after.memory_count==1&&after.memory[0].ms==803&&after.memory[0].end_ms==804);
 CHECK(view.deck[side].memory_cue[0].ms==803&&view.deck[side].memory_cue[0].end_ms==804);
}
static void export_loop_first_save(void) {
 /* Legal public export loop, no sidecar. Unrelated hot cue edit must still save. */
 file_start(&ext);uint32_t q=open_section(&ext,"PCO2",20);u32(&ext,0);u16(&ext,1);u16(&ext,0);
 uint32_t entry=ext.n;pcp2(&ext,0,2,803,0x123456);set32(&ext,entry+24,804);close_section(&ext,q);file_end(&ext);write_file("ANLZ0000.EXT",&ext);load16();
 CHECK(view.deck[side].memory_count==1);call_from(700);CHECK(debug(side).loop_active);CHECK(debug(side).loop_in==ms_frames(side,803)&&debug(side).loop_out==ms_frames(side,804));
 press(side,XZ_EXTRA_IN_RELOOP);seek(side,1200);pad_key(side,2,1);pad_key(side,2,0);xz_cues_worker_stop();
 CHECK(view.deck[side].hot_cue[2].set);struct xz_saved_cues after={0};int fd=open(anlz_dir,O_RDONLY|O_DIRECTORY);assert(fd>=0);int found=xz_cues_read_at(fd,track_id,2000,&after);close(fd);
 printf("first-save file_found=%d memory_marker=%d..%d new_hot=%d notices=%u\n",found,view.deck[side].memory_cue[0].ms,view.deck[side].memory_cue[0].end_ms,view.deck[side].hot_cue[2].set,notices);
 CHECK(found==1);CHECK(found==1&&after.hot[2].set);CHECK(found==1&&after.memory_count==1);
}
static void export_loop_edited(void) {
 /* Legal public export loop, no sidecar. Unrelated hot cue edit must still save. */
 file_start(&ext);uint32_t q=open_section(&ext,"PCO2",20);u32(&ext,0);u16(&ext,1);u16(&ext,0);
 uint32_t entry=ext.n;pcp2(&ext,0,2,803,0x123456);set32(&ext,entry+24,804);close_section(&ext,q);file_end(&ext);write_file("ANLZ0000.EXT",&ext);load16();
 CHECK(view.deck[side].memory_count==1);call_from(700);CHECK(debug(side).loop_active);CHECK(debug(side).loop_in==ms_frames(side,803)&&debug(side).loop_out==ms_frames(side,804));
 press(side,XZ_EXTRA_IN_LOOP_OUT);jog(side,-1);step();press(side,XZ_EXTRA_IN_LOOP_OUT);press(side,XZ_EXTRA_IN_MEMORY);
 press(side,XZ_EXTRA_IN_RELOOP);seek(side,1200);pad_key(side,2,1);pad_key(side,2,0);xz_cues_worker_stop();
 CHECK(view.deck[side].hot_cue[2].set);struct xz_saved_cues after={0};int fd=open(anlz_dir,O_RDONLY|O_DIRECTORY);assert(fd>=0);int found=xz_cues_read_at(fd,track_id,2000,&after);close(fd);
 printf("first-save file_found=%d memory_marker=%d..%d new_hot=%d notices=%u\n",found,view.deck[side].memory_cue[0].ms,view.deck[side].memory_cue[0].end_ms,view.deck[side].hot_cue[2].set,notices);
 CHECK(found==1);CHECK(found==1&&after.hot[2].set);CHECK(found==1&&after.memory_count==1);
}
int main(int argc,char **argv) {
 setvbuf(stdout,NULL,_IONBF,0);
 assert(argc==5);setup16(argv[1],atoi(argv[3]),atoi(argv[4]));
 if(!strcmp(argv[2],"new-roundtrips"))new_roundtrips();
 else if(!strcmp(argv[2],"fractional-export"))fractional_export();
 else if(!strcmp(argv[2],"adjacent"))adjacent_points();
 else if(!strcmp(argv[2],"loop-explicit"))finish_loop(0,0);
 else if(!strcmp(argv[2],"loop-timeout"))finish_loop(1,0);
 else if(!strcmp(argv[2],"loop-readonly"))finish_loop(0,1);
 else if(!strcmp(argv[2],"call-colors"))call_colors();else if(!strcmp(argv[2],"short-fractional-loop"))short_fractional_loop();else if(!strcmp(argv[2],"export-loop-first-save"))export_loop_first_save();else if(!strcmp(argv[2],"export-loop-edited"))export_loop_edited();else return 2;
 xz_xp_destroy(p);xz_cues_worker_stop();printf("RESULT r16 %s deck=%d Hz=%d checks=%u failures=%u\n",argv[2],side+3,hz,checks,failures);return failures?1:0;
}
