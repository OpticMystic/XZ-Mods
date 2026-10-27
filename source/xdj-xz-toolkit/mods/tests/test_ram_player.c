/* Actual software players, synthetic sink, disposable sources. No device access. */
#define main retained_player_acceptance
#include "test_extra_player.c"
#undef main
#include "ram/budget.h"
#include <fcntl.h>
#include <stdatomic.h>
static char ram_path[2][512];
static atomic_uint ram_generation_value=99;
static unsigned saves;
static atomic_uint copy_gate, copy_waiting;
ssize_t __real_pread64(int fd,void *out,size_t bytes,off64_t offset);
ssize_t __wrap_pread64(int fd,void *out,size_t bytes,off64_t offset){
 while(atomic_load(&copy_gate)){
  atomic_store(&copy_waiting,1);struct timespec delay={0,1000000};nanosleep(&delay,NULL);
 }
 return __real_pread64(fd,out,bytes,offset);
}
static uint64_t pcm_hash[2],pcm_frames[2];
static float reference_pcm[2][700000*2];
static uint64_t reference_after[2][6000];
static uint32_t reference_frames[2][6000];
static unsigned block_count[2];
static int record_reference, compare_reference;
static uint64_t ram_generation(void *unused,unsigned source){(void)unused;assert(source==1);return atomic_load(&ram_generation_value);}
static uint64_t cue_generation(unsigned source){return ram_generation(NULL,source);}
static int ram_resolve(int source,uint32_t id,uint64_t generation,struct xz_extra_track *out){
 if(generation!=ram_generation(NULL,(unsigned)source)||id<1||id>2)return -1;
 int rc=resolve(source,1,out);out->track_id=id;
 snprintf(out->path,sizeof out->path,"%s",ram_path[id-1]);return rc;
}
static int cue_write(int deck,const struct xz_extra_track *t,uint64_t gen,int32_t length,const struct xz_saved_cues *s,const struct xz_cue_edit *edit){
 (void)deck;(void)t;(void)gen;(void)length;(void)s;(void)edit;saves++;return 1;
}
static void hash_pcm(int d,const float *pcm,uint32_t frames,uint64_t after){
 if(record_reference||compare_reference){
  assert(pcm_frames[d]+frames<=700000&&block_count[d]<6000);
  float *reference=reference_pcm[d]+pcm_frames[d]*2;unsigned block=block_count[d]++;
  if(record_reference){memcpy(reference,pcm,frames*2*sizeof(float));reference_after[d][block]=after;reference_frames[d][block]=frames;}
  else{assert(reference_after[d][block]==after&&reference_frames[d][block]==frames);assert(!memcmp(reference,pcm,frames*2*sizeof(float)));}
 }
 const unsigned char *b=(const unsigned char *)pcm;
 for(size_t i=0;i<frames*2*sizeof(float);i++)pcm_hash[d]=(pcm_hash[d]^b[i])*UINT64_C(1099511628211);
 pcm_hash[d]=(pcm_hash[d]^after)*UINT64_C(1099511628211);pcm_frames[d]+=frames;
}
static xz_usb_result ram_submit(void *ctx,uint32_t epoch,int d,uint32_t gen,const float *pcm,uint32_t frames,uint64_t after){
 xz_usb_result r=fake_submit(ctx,epoch,d,gen,pcm,frames,after);if(r==XZ_USB_OK)hash_pcm(d,pcm,frames,after);return r;
}
static xz_usb_result ram_transition(void *ctx,uint32_t epoch,int d,uint32_t gen,const float *pcm,uint32_t frames,uint64_t after,uint32_t *next){
 xz_usb_result r=fake_transition(ctx,epoch,d,gen,pcm,frames,after,next);if(r==XZ_USB_OK)hash_pcm(d,pcm,frames,after);return r;
}
static void copy_source(const char *reference,const char *destination){
 FILE *in=fopen(reference,"rb"),*out=fopen(destination,"wb");assert(in&&out);unsigned char b[32768];size_t n;
 while((n=fread(b,1,sizeof b,in)))assert(fwrite(b,1,n,out)==n);
 assert(!ferror(in));fclose(in);assert(!fclose(out));
}
static void lose_sources(void){
 for(unsigned d=0;d<2;d++){int fd=open(ram_path[d],O_WRONLY|O_TRUNC);assert(fd>=0);close(fd);assert(!unlink(ram_path[d]));}
 const char *names[]={"ANLZ0000.DAT","ANLZ0000.EXT","ANLZ0000.2EX"};
 for(unsigned i=0;i<3;i++){char path[512];assert(snprintf(path,sizeof path,"%s/%s",anlz_dir,names[i])<(int)sizeof path);assert(!unlink(path));}
 atomic_store(&ram_generation_value,100);
}
static struct xz_ram_status ram_state(struct xz_ram_extra *s,unsigned d){struct xz_ram_status r;xz_ram_extra_status(s,d,&r);return r;}
static uint64_t ram_wall_ms(void){
 struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));
 return (uint64_t)t.tv_sec*1000u+(unsigned long)t.tv_nsec/1000000u;
}
static void ram_wait_tick(uint64_t deadline){
 assert(ram_wall_ms()<deadline);step();
 struct timespec delay={0,1000000};nanosleep(&delay,NULL);
}
static void wait_ready(struct xz_ram_extra *service){
 uint64_t deadline=ram_wall_ms()+10000;
 for(;;){
  struct xz_ram_status a=ram_state(service,0),b=ram_state(service,1);
  if(a.state==XZ_RAM_READY&&b.state==XZ_RAM_READY)return;
  if(a.state==XZ_RAM_ERROR||b.state==XZ_RAM_ERROR||ram_wall_ms()>=deadline){
   fprintf(stderr,"RAM not ready: d=0 state=%d phase=%d copied=%u/%u reason=%d; d=1 state=%d phase=%d copied=%u/%u reason=%d\n",
    a.state,a.phase,a.copied,a.bytes,a.reason,b.state,b.phase,b.copied,b.bytes,b.reason);
   assert(0);
  }
  ram_wait_tick(deadline);
 }
}
static void run_player(const char *reference,int ram,int destroy,int controls){
 uint64_t before_copy[2]={0};
 build_analysis();
 clock_ms=1000;master=12000;wire_horizon=0;position_interval=position_noise=position_blocked=threaded_clock=hold_flush=0;
 atomic_store(&ram_generation_value,99);saves=0;
 for(unsigned d=0;d<2;d++){copy_source(reference,ram_path[d]);pcm_hash[d]=UINT64_C(1469598103934665603);pcm_frames[d]=0;block_count[d]=0;}
 struct xz_xp_env env={{NULL,fake_link,ram_submit,fake_flush,fake_position,ram_transition},now_ms,resolve,master_bpm,NULL,ram_resolve,0,0};
 p=xz_xp_create(&env);assert(p);fake_route(12345);step();
 struct xz_ram_extra *service=ram?xz_ram_extra_create(NULL,fake_link,ram_generation):NULL;
 if(ram){assert(service);struct xz_ram_player_service ops=xz_ram_extra_player_service(service);xz_xp_set_ram_service(p,&ops);}
 xz_xp_set_source_generation(p,cue_generation);xz_xp_set_cue_store(p,NULL,cue_write);
 for(unsigned d=0;d<2;d++){
  struct xz_extra_deck_event e=event((int)d,XZ_EXTRA_IN_LOAD,1);
  e.load.source=1;e.load.track_id=d+1;e.load.media_generation=99;assert(input(e));step();
 }
 struct xz_extra_analysis_hold analysis_hold;struct xz_extra_decks held;
 assert(xz_xp_read_held(p,&held,&analysis_hold));
 assert(held.deck[0].wave.columns&&held.deck[0].grid_count);
 unsigned char wave_copy[900];memcpy(wave_copy,held.deck[0].wave.columns,sizeof wave_copy);
 if(ram&&controls){
  for(unsigned d=0;d<2;d++){struct xz_ram_status s=ram_state(service,d);assert(xz_ram_extra_command(service,d,XZ_RAM_CANCEL,&s.key)==XZ_RAM_ACCEPTED);}
  for(unsigned i=0;i<10000&&(ram_state(service,0).phase!=XZ_RAM_IDLE||ram_state(service,1).phase!=XZ_RAM_IDLE);i++){
   step();struct timespec delay={0,1000000};nanosleep(&delay,NULL);
  }
 }
 for(int d=0;d<2;d++){press(d,XZ_EXTRA_IN_MASTER_TEMPO);press(d,XZ_EXTRA_IN_PLAY);}
 for(unsigned t=0;t<13000;t+=10){
  if(controls){
   if(t==1000)press(0,XZ_EXTRA_IN_SYNC);
   if(t==1500&&ram){
    atomic_store(&copy_waiting,0);atomic_store(&copy_gate,1);
    for(unsigned d=0;d<2;d++)before_copy[d]=pcm_frames[d];
   }
   if(t==1500&&ram)for(unsigned d=0;d<2;d++){
    struct xz_ram_status s=ram_state(service,d);assert(xz_ram_extra_command(service,d,XZ_RAM_LOAD,&s.key)==XZ_RAM_ACCEPTED);
   }
   if(t==2000){struct xz_extra_deck_event e=event(0,XZ_EXTRA_IN_KEY,1);e.key.kind=XZ_KEY_STEP;e.key.step=1;e.key.source_owner=view.deck[0].key_state.owner;assert(input(e));step();assert(view.deck[0].key_state.desired_cents==100);}
   if(t==3000)hold(0,XZ_EXTRA_IN_REVERSE,1);
   if(t==3500)hold(0,XZ_EXTRA_IN_REVERSE,0);
   if(t==4000)press(1,XZ_EXTRA_IN_LOOP_IN);
   if(t==4500)press(1,XZ_EXTRA_IN_LOOP_OUT);
   if(t==5500)press(1,XZ_EXTRA_IN_RELOOP);
  }
  if(t==3000&&ram){
   if(controls){
    uint64_t deadline=ram_wall_ms()+10000;
    while(!atomic_load(&copy_waiting))ram_wait_tick(deadline);
    for(unsigned d=0;d<2;d++){
     struct xz_ram_status s=ram_state(service,d);
     assert(s.state==XZ_RAM_LOADING&&!s.copied&&pcm_frames[d]>before_copy[d]);
    }
    /* Loading is asynchronous; keep playback running while the copy is delayed. */
    atomic_store(&copy_gate,0);
   }
   /* Pump attachment at a fixed audio time. Bound worker liveness in real time. */
   wait_ready(service);
  }
  if(t==4000&&destroy)lose_sources();
  advance(10);
  if(ram&&t<3000){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
 }
 if(ram){
  for(unsigned d=0;d<2;d++){
   struct xz_ram_status s=ram_state(service,d);assert(s.state==(destroy?XZ_RAM_DETACHED:XZ_RAM_READY));
   fprintf(stderr,"RAM player d=%u controls=%d destroyed=%d state=%d position=%llu playing=%d source=%d\n",d,controls,destroy,debug((int)d).state,(unsigned long long)debug((int)d).position,view.deck[d].playing,view.deck[d].sync.source);
   assert(debug((int)d).state!=XZ_XP_ERROR&&view.deck[d].sync.source!=XZ_EXTRA_SYNC_SOURCE_EMERGENCY);
   if(destroy)assert(view.deck[d].playing);
  }
  assert(!memcmp(wave_copy,held.deck[0].wave.columns,sizeof wave_copy));
  if(destroy){
   unsigned before=saves;struct xz_extra_deck_event e=event(0,XZ_EXTRA_IN_MEMORY,1);assert(input(e));step();assert(saves==before);
   seek(0,13500);advance(100);seek(0,1000);advance(500);assert(view.deck[0].playing&&view.deck[0].sync.source!=XZ_EXTRA_SYNC_SOURCE_EMERGENCY);
   fake_route(0);step();assert(!view.deck[0].playing&&ram_state(service,0).state==XZ_RAM_DETACHED);
   fake_route(12346);step();assert(!view.deck[0].playing&&ram_state(service,0).state==XZ_RAM_DETACHED);
  }
 }else if(destroy){assert(view.deck[0].sync.source==XZ_EXTRA_SYNC_SOURCE_EMERGENCY&&view.deck[1].sync.source==XZ_EXTRA_SYNC_SOURCE_EMERGENCY);}
 xz_xp_release_analysis(&held,&analysis_hold);
 xz_xp_stop(p); /* Runtime stop deliberately need not destroy the player snapshot object. */
 if(service){xz_ram_extra_destroy(service);struct xz_ram_usage u;xz_ram_usage(&u);assert(!u.original_pending&&!u.original_held);}
 xz_xp_destroy(p);p=NULL;
}
int main(int argc,char **argv){
 assert(argc==3);assert(!pthread_mutex_init(&f.lock,NULL));
 snprintf(fixtures,sizeof fixtures,"%s",argv[1]);snprintf(anlz_dir,sizeof anlz_dir,"%s/anlz",argv[1]);build_analysis();
 for(unsigned d=0;d<2;d++)snprintf(ram_path[d],sizeof ram_path[d],"%s/player-usb-%u.wav",argv[1],d);
 record_reference=1;run_player(argv[2],0,0,1);record_reference=0;
 uint64_t hash[2]={pcm_hash[0],pcm_hash[1]},frames[2]={pcm_frames[0],pcm_frames[1]};
 compare_reference=1;run_player(argv[2],1,0,1);compare_reference=0;
 assert(!memcmp(hash,pcm_hash,sizeof hash)&&!memcmp(frames,pcm_frames,sizeof frames));
 printf("PCM frames=%llu,%llu hashes=%016llx,%016llx\n",(unsigned long long)frames[0],(unsigned long long)frames[1],(unsigned long long)hash[0],(unsigned long long)hash[1]);
 puts("PASS byte-identical sink PCM/source tags across live attachment with SYNC, explicit key shift, reverse, MT and loops");
 run_player(argv[2],1,1,0);puts("PASS both software players beyond eight seconds after source destruction, retained analysis/cues, seeks, route loss/reconnect and stop retirement");
 run_player(argv[2],0,1,0);puts("PASS uncached source-destruction control enters emergency audio on both decks");
 pthread_mutex_destroy(&f.lock);
}
