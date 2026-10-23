#define main retained_main
#include "test_extra_player.c"
#undef main
static unsigned checks,failed;
#define CHECK(x) do{checks++;if(!(x)){failed++;if(failed<12)printf("FAIL line=%d %s\n",__LINE__,#x);}}while(0)
static int mode_bpm,variable,injected_bpm=-1;
static unsigned grid_ms[40],grid_bpms[40];
static int own_resolve(int source,uint32_t id,struct xz_extra_track *out){
 int rc=sync_resolve(source,id,out);out->track_bpm_x100=12500;return rc;
}
static void grid_file(void){
 file_start(&dat);uint32_t s=open_section(&dat,"PQTZ",24);
 u32(&dat,0);u32(&dat,0x80000);u32(&dat,40);
 double at=100.;
 for(unsigned i=0;i<40;i++){
  unsigned bpm=variable&&at>=800.?12600:(unsigned)mode_bpm;
  grid_ms[i]=(unsigned)llround(at);grid_bpms[i]=bpm;
  u16(&dat,i%4+1);u16(&dat,injected_bpm>=0?(unsigned)injected_bpm:bpm);u32(&dat,(uint32_t)llround(at));at+=6000000./bpm;
 }
 close_section(&dat,s);file_end(&dat);write_file("ANLZ0000.DAT",&dat);
}
static void one(int bpm,int var,int quant,int sync){
 mode_bpm=bpm;variable=var;build_analysis();grid_file();clock_ms=1000;wire_horizon=882;
 position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,own_resolve,master_bpm,NULL,NULL,0,quant};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,copy_sim_native);fake_route(6000+checks);step();
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;
 sim_native.bpm=bpm/100.;sim_native.source_revision=28;sim_native.track_identity=0x280001;
 sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
 for(int d=0;d<2;d++){sync_loop_track(d,d+1,350);if(!view.deck[d].playing)press(d,XZ_EXTRA_IN_PLAY);if(sync==1)press(d,XZ_EXTRA_IN_SYNC);}
 if(sync==2){press(0,XZ_EXTRA_IN_MASTER);advance(40);press(1,XZ_EXTRA_IN_SYNC);}
 int tmin[2]={99999,99999},tmax[2]={-99999,-99999},bmin[2]={99999,99999},bmax[2]={0,0};
 double smin[2]={99,99},smax[2]={0,0},cmin[2]={99,99},cmax[2]={0,0};unsigned trims=0,completed[2]={0,0};
 for(unsigned i=0;i<400;i++){
  advance(10);
  for(int d=0;d<2;d++){
   int t=view.deck[d].tempo_x100,b=view.deck[d].bpm_x100;double speed_now=debug(d).speed;
   if(t<tmin[d])tmin[d]=t;if(t>tmax[d])tmax[d]=t;if(b<bmin[d])bmin[d]=b;if(b>bmax[d])bmax[d]=b;
   if(speed_now<smin[d])smin[d]=speed_now;if(speed_now>smax[d])smax[d]=speed_now;
   if(view.deck[d].timeline.flags&XZ_EXTRA_TIMELINE_APPLIED_RATE){
    double cr=view.deck[d].timeline.completed_rate;completed[d]++;
    if(cr<cmin[d])cmin[d]=cr;if(cr>cmax[d])cmax[d]=cr;
    if(!var&&!quant&&i>40)CHECK(fabs(cr-1.)<1e-6);
   }
   if(view.deck[d].sync.correction_ppm)trims++;
   if(!var){CHECK(t==0);CHECK(b==bpm);CHECK(view.deck[d].track_bpm_x100==bpm);if(!quant)CHECK(fabs(speed_now-1.)<1e-6);}
   if(var){
    unsigned index=0;while(index+1<40&&grid_ms[index+1]<=(unsigned)view.deck[d].elapsed_ms)index++;
    int expected=grid_bpms[index];CHECK(view.deck[d].track_bpm_x100==expected);
    if(!sync)CHECK(b==expected);
    if(sync&&i>30){CHECK(abs(t-(int)llround((12000./expected-1.)*10000.))<=1);CHECK(abs(b-12000)<=1);}
   }
  }
 }
 for(int d=0;d<2;d++)printf("CASE bpm=%d variable=%d quantize=%d sync=%d deck=%d tempo=[%d,%d] bpm=[%d,%d] source_speed=[%.9f,%.9f] completed_rate=[%.9f,%.9f] completed_samples=%u trim_samples=%u reason=%d\n",bpm,var,quant,sync,d+3,tmin[d],tmax[d],bmin[d],bmax[d],smin[d],smax[d],cmin[d],cmax[d],completed[d],trims,view.deck[d].sync.reason);
 for(int d=0;d<2;d++)CHECK(completed[d]>100);
 xz_xp_destroy(p);p=NULL;
}

static void parser_fallback(void){
 build_analysis();file_start(&dat);uint32_t sec=open_section(&dat,"PQTZ",24);
 unsigned values[]={0,1,1999,60001,65535,17200};
 u32(&dat,0);u32(&dat,0x80000);u32(&dat,6);
 for(unsigned i=0;i<6;i++){u16(&dat,i%4+1);u16(&dat,values[i]);u32(&dat,100+500*i);}
 close_section(&dat,sec);file_end(&dat);
 struct xz_extra_beat *grid=NULL;uint32_t count=0;int32_t bpm=99;
 CHECK(xz_anlz_beat_grid(dat.b,dat.n,&grid,&count,&bpm)==0);CHECK(count==6);CHECK(bpm==0);
 for(unsigned i=0;i<6;i++){CHECK(grid[i].ms==(int)(100+500*i));CHECK(grid[i].bpm_x100==(i==5?17200:0));}
 free(grid);
 CHECK(xz_anlz_beat_grid(dat.b,12,&grid,&count,&bpm)==-1);CHECK(!grid&&!count&&!bpm);
 puts("PASS parser missing/malformed per-beat BPM initialized to zero, valid tempo retained, truncated file refused");
}
static int wait_mode;
static int wait_clock(struct xz_sync_native_snapshot *out){
 int rc=copy_sim_native(out);
 if(wait_mode==1){out->reason=XZ_SYNC_NATIVE_CLOCK_UNAVAILABLE;out->available=0;return 0;}
 if(wait_mode==2)out->phase_available=0;
 return rc;
}
static void wait_paths(void){
 build_analysis();clock_ms=1000;wire_horizon=882;
 struct xz_xp_env env={{NULL,fake_link,fake_submit,fake_flush,fake_position,fake_transition},now_ms,sync_resolve,master_bpm,NULL,NULL,0,1};
 p=xz_xp_create(&env);assert(p);xz_xp_set_native_sync_clock(p,wait_clock);fake_route(9900);step();
 memset(&sim_native,0,sizeof sim_native);sim_native.active=sim_native.playing=1;sim_native.bpm=120.;sim_native.source_revision=28;sim_native.track_identity=0x280001;sim_native.anchor_us=(uint64_t)clock_ms*1000u;sim_native.anchor_phase=.25;
 wait_mode=1;
 for(int d=0;d<2;d++){sync_loop_track(d,d+1,350);if(!view.deck[d].playing)press(d,XZ_EXTRA_IN_PLAY);press(d,XZ_EXTRA_IN_SYNC);}
 advance(100);
 for(int d=0;d<2;d++){CHECK(view.deck[d].sync.requested);CHECK(!view.deck[d].sync.locked);CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER);}
 wait_mode=2;advance(300);
 for(int d=0;d<2;d++){CHECK(!view.deck[d].sync.locked);CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_NATIVE_PHASE_UNAVAILABLE);CHECK(view.deck[d].tempo_x100==0);}
 wait_mode=0;sim_native.manual=1;advance(100);
 for(int d=0;d<2;d++)CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_MANUAL);
 sim_native.manual=0;wait_mode=1;
 for(int d=0;d<2;d++)press(d,XZ_EXTRA_IN_PLAY);
 advance(100);
 for(int d=0;d<2;d++){CHECK(!view.deck[d].playing);CHECK(view.deck[d].sync.requested);CHECK(!view.deck[d].sync.locked);CHECK(view.deck[d].sync.reason==XZ_EXTRA_SYNC_WAIT_MASTER);}
 xz_xp_destroy(p);p=NULL;
 puts("PASS own-C WAIT_MASTER on unavailable clock, NATIVE_PHASE_UNAVAILABLE on tempo-only clock, MANUAL, paused requested unlocked WAIT_MASTER");
}
int main(int argc,char **argv){
 assert(argc==3);setvbuf(stdout,NULL,_IONBF,0);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);
 printf("LAYOUT size=%zu align=%zu ms=%zu beat=%zu bpm=%zu\n",sizeof(struct xz_extra_beat),_Alignof(struct xz_extra_beat),offsetof(struct xz_extra_beat,ms),offsetof(struct xz_extra_beat,beat_in_bar),offsetof(struct xz_extra_beat,bpm_x100));
 CHECK(sizeof(struct xz_extra_beat)==8);CHECK(offsetof(struct xz_extra_beat,ms)==0);CHECK(offsetof(struct xz_extra_beat,beat_in_bar)==4);CHECK(offsetof(struct xz_extra_beat,bpm_x100)==6);
 parser_fallback();wait_paths();
 const int bpms[]={12000,17200,12345};
 for(unsigned i=0;i<3;i++){one(bpms[i],0,0,0);one(bpms[i],0,0,1);one(bpms[i],0,1,1);one(bpms[i],0,1,2);}
 injected_bpm=0;one(12000,0,0,1);injected_bpm=1;one(12000,0,0,1);injected_bpm=65535;one(12000,0,0,1);injected_bpm=-1;
 one(12000,1,0,0);one(12000,1,0,1);
 printf("RESULT checks=%u failures=%u\n",checks,failed);return failed?1:0;
}
