#define main retained_main
#include "test_extra_player.c"
#undef main
static unsigned failures,checks;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
static int extension_resolve(int source,uint32_t id,struct xz_extra_track *out){
 if(id!=6)return resolve(source,id,out);
 int rc=resolve(source,1,out);out->track_id=6;out->anlz_dir[0]=0;
 snprintf(out->path,sizeof out->path,"%s/long44100.wav",fixtures);return rc;
}
static void begin_extra(void){
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,extension_resolve,master_bpm,NULL,NULL,0,0};
 p=xz_xp_create(&env);assert(p);fake_route(810);step();
}
static void hot_button(void){
 struct xz_extra_deck_event e=event(0,XZ_EXTRA_IN_PAD_MODE,1);e.hot_cue=XZ_EXTRA_PAD_HOT_CUE;
 assert(input(e));e.pressed=0;assert(input(e));step();
}
int main(int argc,char **argv){
 assert(argc==3);assert(!pthread_mutex_init(&f.lock,NULL));snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);
 build_analysis();begin_extra();load(0,1);seek(0,700);
 unsigned gen=debug(0).generation;uint64_t pos=debug(0).position;
 hot_button();pad_key(0,0,1);pad_key(0,0,0);
 CHECK(debug(0).generation==gen&&debug(0).position==pos&&debug(0).state==XZ_XP_CUED);
 hot_button();
 CHECK(view.deck[0].hot_cue[0].set&&!view.deck[0].hot_cue[7].set);
 pad_key(0,0,1);pad_key(0,0,0);CHECK(debug(0).state==XZ_XP_PLAYING);
 xz_xp_destroy(p);p=NULL;
 begin_extra();load(0,6);seek(0,500);
 struct xz_extra_deck_event loop=event(0,XZ_EXTRA_IN_LOOP_4_8,1);assert(input(loop));step();
 CHECK(debug(0).loop_active&&debug(0).state==XZ_XP_PLAYING&&debug(0).loop_out-debug(0).loop_in==ms_frames(0,2000));
 clock_ms+=1100;loop.held=1;assert(input(loop));step();CHECK(debug(0).loop_out-debug(0).loop_in==ms_frames(0,4000));
 loop.pressed=0;assert(input(loop));step();loop.pressed=1;loop.held=0;assert(input(loop));step();CHECK(debug(0).loop_out-debug(0).loop_in==ms_frames(0,2000));
 loop.pressed=0;assert(input(loop));step();loop.pressed=1;loop.shift=1;assert(input(loop));step();CHECK(debug(0).loop_out-debug(0).loop_in==ms_frames(0,4000));
 xz_xp_destroy(p);p=NULL;
 begin_extra();load(0,6);press(0,XZ_EXTRA_IN_PLAY);advance(2500);
 char broken[1100];snprintf(broken,sizeof broken,"%s/long44100.wav",fixtures);FILE *gone=fopen(broken,"wb");assert(gone);fclose(gone);
 advance(800);CHECK(debug(0).state==XZ_XP_PLAYING&&debug(0).loop_active);CHECK(strstr(view.notice,"EMERGENCY LOOP")!=NULL);
 f.capture[0]=1;advance(10);CHECK(f.captured_frames[0]>0);float peak=0;for(unsigned i=0;i<f.captured_frames[0]*2;i++)peak=fmaxf(peak,fabsf(f.captured[0][i]));CHECK(peak>.1f);
 xz_xp_destroy(p);p=NULL;
 build_analysis();
 uint32_t list=open_section(&dat,"PCOB",24);u32(&dat,0);u16(&dat,0);u16(&dat,1);u32(&dat,0xffffffffu);
 uint32_t entry=dat.n;pcpt(&dat,0,2,600);set32(&dat,entry+16,4);set32(&dat,entry+36,1100);close_section(&dat,list);file_end(&dat);write_file("ANLZ0000.DAT",&dat);
 begin_extra();load(0,1);seek(0,100);press(0,XZ_EXTRA_IN_PLAY);advance(1400);
 CHECK(debug(0).loop_active);CHECK(debug(0).loop_in==ms_frames(0,600));CHECK(debug(0).loop_out==ms_frames(0,1100));
 CHECK(completed_tag(0)>=ms_frames(0,600)&&completed_tag(0)<ms_frames(0,1100));
 press(0,XZ_EXTRA_IN_RELOOP);advance(300);CHECK(!debug(0).loop_active);
 xz_xp_destroy(p);p=NULL;build_analysis();
 begin_extra();load(0,1);load(1,2);seek(0,700);press(0,XZ_EXTRA_IN_PLAY);advance(300);
 struct xz_extra_deck_event duplicate=event(1,(enum xz_extra_input)30,1);
 CHECK(input(duplicate));step();
 CHECK(!strcmp(view.deck[1].title,view.deck[0].title)&&view.deck[1].playing);
 CHECK(fabs((double)view.deck[0].elapsed_ms-view.deck[1].elapsed_ms)<20);
 xz_xp_destroy(p);p=NULL;
 printf("RESULT extensions checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
