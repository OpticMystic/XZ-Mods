#ifdef NDEBUG
#error Cadence acceptance requires active assertions
#endif
#define _GNU_SOURCE
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
static uint64_t fake_us=1000000;
static unsigned wall_calls,mono_calls,present_calls,render_calls;
static int fixture_wall(struct timeval *v,void *z){(void)z;wall_calls++;v->tv_sec=(time_t)(fake_us/1000000);v->tv_usec=(suseconds_t)(fake_us%1000000);return 0;}
static int fixture_mono(clockid_t c,struct timespec *v){(void)c;mono_calls++;v->tv_sec=(time_t)(fake_us/1000000);v->tv_nsec=(long)(fake_us%1000000)*1000;return 0;}
#define gettimeofday fixture_wall
#define XZ_CLOCK_COMPAT_H
#define xz_clock_gettime_compat fixture_mono
#define xz_shell_render fixture_render
#include "shell_runtime.c"
#undef xz_shell_render
#include "shell/shell_axis.h"
#include <assert.h>
int xz_shell_render(const struct xz_shell *,const struct xz_shell_model *,uint16_t *,size_t);
enum {STRIDE=804,BEATS=129,COLS=32000};
static uint16_t frame[STRIDE*480],reference[STRIDE*480];
static struct xz_shell_column peaks[4][COLS];
static struct xz_shell_beat beats[4][BEATS];
static struct xz_extra_column producer_peaks[2][COLS];
static struct xz_extra_beat producer_beats[2][BEATS];
static struct xz_extra_decks sample;
static int originals[4]={12000,15000,10000,16000};
static int effective=12000,read_cost[2]={4,4},model_cost=4,snapshot_cost;
static int native_ok=1,native_moving=1,extra_moving=1,route_up=1,unloaded;
static int snapshot_fresh=1,coarse_native;
static unsigned checks,failures,frames,exact_frames;
static int max_source_error,max_lane_offset,max_delta_error;
static const char *output;
static void check(int good,const char *what){checks++;if(!good){if(failures++<12)fprintf(stderr,"RED %s time=%llu effective=%d\n",what,(unsigned long long)fake_us,effective);}}
static int source_at(int d,uint32_t at){return (int)(192000000LL/originals[d]+(int64_t)(int32_t)(at-1000)*effective/originals[d]);}
static int rate_at(int d){return (int)(1000000LL*effective/originals[d]);}
static int rgb(unsigned c){return (int)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void attach(struct xz_shell_deck *k,int d){
 k->known|=K(TRACK_BPM)|K(BPM)|K(TEMPO)|K(GRID)|K(WAVE)|K(TOTAL)|K(TITLE);
 k->track_bpm_x100=originals[d];k->bpm_x100=effective;k->tempo_x100=rate_at(d)/100-10000;k->title="CADENCE";
 k->total_ms=COLS;k->grid=beats[d];k->grid_count=BEATS;k->wave=(struct xz_shell_wave){peaks[d],COLS,COLS};
}
static void publish_extra(uint32_t at){
 for(int i=0;i<2;i++){
  int d=i+2;struct xz_extra_deck *k=&sample.deck[i];memset(k,0,sizeof *k);k->loaded=!unloaded;k->playing=extra_moving;
  k->elapsed_ms=source_at(d,at);k->elapsed_at_ms=at;k->total_ms=COLS;k->track_bpm_x100=originals[d];k->bpm_x100=effective;k->tempo_x100=rate_at(d)/100-10000;
  k->known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_ELAPSED)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TIMELINE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_BPM)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TEMPO)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_GRID)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_WAVE)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TITLE);
  snprintf(k->title,sizeof k->title,"CADENCE");k->grid=producer_beats[i];k->grid_count=BEATS;k->wave=(struct xz_extra_wave){producer_peaks[i],COLS,COLS,765};
  k->timeline=(struct xz_extra_timeline){.source_frame=(uint64_t)k->elapsed_ms*48,.source_rate=48000,.at_ms=at,.rate_ppm=extra_moving?rate_at(d):0,.moving=extra_moving,.flags=7,.revision=at,.load_revision=1,.route_epoch=1,.transport_generation=1};
 }
 sample.snapshot_revision++;sample.route_up=route_up;
}
int xz_extra_decks_snapshot(struct xz_extra_decks *out){
 fake_us+=(unsigned)snapshot_cost*1000u;
 if(snapshot_fresh){uint32_t at=(uint32_t)(fake_us/1000u);publish_extra(at-at%20u);}
 *out=sample;return sample.route_up;
}
uint32_t xz_extra_decks_revision(void){return sample.snapshot_revision;}
uint32_t xz_extra_decks_control_revision(void){return sample.control_revision;}
int xz_native_four_mixer_order(void){return 0;}
int xz_native_skin_on_owner_thread(void){return 0;}
void xz_native_skin_request_repaint(void){}
void xz_log(const char *s){(void)s;}
int xz_read_memory(uint32_t a,void *p,size_t n){(void)a;(void)p;(void)n;return -1;}
int xz_arcade_native_clock_read(xz_arcade_memory_read r,void *p,unsigned d,struct xz_arcade_track_clock *c){
 (void)r;(void)p;fake_us+=(unsigned)read_cost[d]*1000u;memset(c,0,sizeof *c);
 uint32_t at=(uint32_t)(fake_us/1000u);if(coarse_native)at=1000+(at-1000)/66*66;
 c->position_ms=(uint32_t)source_at((int)d,at);c->speed=(float)rate_at((int)d)/1e6f;c->playing=native_moving;c->track=d+1;return native_ok;
}
/* Producer seams only. The complete refresh, clock conversion, smoother,
   scheduling, frame-time selection, and renderer are unchanged own C. */
void xz_shell_model_set_input(struct xz_shell_model_storage *s,int d,const struct xz_shell_deck *i,unsigned kind){(void)s;(void)d;(void)i;(void)kind;}
void xz_shell_model_set_wave(struct xz_shell_model_storage *s,int d,const struct xz_shell_column *a,size_t n,uint32_t span,const struct xz_shell_column *b,size_t bn,uint32_t bs){(void)s;(void)d;(void)a;(void)n;(void)span;(void)b;(void)bn;(void)bs;}
void xz_shell_model_set_stems(struct xz_shell_model_storage *s,int d,const struct xz_shell_column *a,size_t n,uint32_t span){(void)s;(void)d;(void)a;(void)n;(void)span;}
unsigned xz_shell_model_refresh(struct xz_shell_model *m,struct xz_shell_model_storage *s,xz_shell_read_fn r,uint32_t at){
 (void)s;(void)r;fake_us+=(unsigned)model_cost*1000u;memset(m,0,sizeof *m);m->clock_ms=at;
 for(int d=0;d<2;d++){
  m->deck[d]=(struct xz_shell_deck){.known=K(LOADED)|K(ELAPSED)|K(PLAYING),.loaded=!unloaded,.playing=native_moving,.elapsed_ms=source_at(d,at),.elapsed_at_ms=at};attach(&m->deck[d],d);
 }
 return 0;
}
uint32_t xz_shell_model_caution(const struct xz_shell_model_storage *s){(void)s;return 0;}
int xz_shell_model_fx_raw(const struct xz_shell_model_storage *s,unsigned char out[12]){(void)s;(void)out;return 0;}
uint32_t xz_shell_model_holds(const struct xz_shell_model_storage *s){(void)s;return 0;}
uint32_t xz_shell_model_fx_failures(const struct xz_shell_model_storage *s){(void)s;return 0;}
void xz_shell_model_diagnostics(const struct xz_shell_model_storage *s,uint32_t out[4]){(void)s;memset(out,0,4*sizeof *out);}
static uint64_t fake_present(void){present_calls++;return fake_us+8000;}
int fixture_render(const struct xz_shell *u,const struct xz_shell_model *m,uint16_t *pixels,size_t stride){
 unsigned before=wall_calls+mono_calls;render_calls++;
 int result=xz_shell_render(u,m,pixels,stride);
 check(wall_calls+mono_calls==before,"no clock syscall from any lane/pixel render");return result;
}
static void reset(void){
 memset(&model,0,sizeof model);memset(&sample,0,sizeof sample);memset(position,0,sizeof position);memset(motion,0,sizeof motion);
 memset(&handoff,0,sizeof handoff);memset(&caution_state,0,sizeof caution_state);memset(&step,0,sizeof step);
 memset(wave_job,0,sizeof wave_job);extra_reset();xz_shell_init(&shell);shell.play_decks=4;shell.zoom=6;
 guards=1u<<G_CLOCK;enabled=interface_modern=1;dirty_flag=1;owns_flag=0;refreshed=0;follow_bits=0;last_drawn_ms=last_refresh_ms=0;
 native_ok=native_moving=extra_moving=route_up=snapshot_fresh=1;unloaded=coarse_native=snapshot_cost=0;fake_us=1000000;next_present=fake_present;
 shown_extra_revision=shown_extra_control=shown_native_input=0;native_input_revision=0;memset(native_input_at,0,sizeof native_input_at);
 period_ms=100;read_cost[0]=4;read_cost[1]=4;model_cost=4;
 memset(peaks,0,sizeof peaks);
 for(int d=0;d<4;d++){
  for(int i=0;i<BEATS;i++)beats[d][i]=(struct xz_shell_beat){(int)(6000000LL*i/originals[d]),(uint8_t)(i%4+1)};
  int peak=beats[d][34].ms;for(int i=peak-4;i<=peak+4;i++)peaks[d][i]=(struct xz_shell_column){255,255,255};
  if(d>=2){memcpy(producer_peaks[d-2],peaks[d],sizeof peaks[d]);for(int i=0;i<BEATS;i++)producer_beats[d-2][i]=(struct xz_extra_beat){beats[d][i].ms,beats[d][i].beat_in_bar,0};}
 }
 publish_extra(1000);
}
static void paint(void){
 if(snapshot_fresh){uint32_t at=(uint32_t)(fake_us/1000u);publish_extra(at-at%20u);}
 for(unsigned i=0;i<sizeof frame/sizeof *frame;i++)frame[i]=0xa55a;
 unsigned prev=present_calls,draws=render_calls;struct xz_shell_runtime_frame_input in={.eligible=1,.theme=XZ_THEME_FLAGSHIP};
 int result=xz_shell_runtime_frame(frame,STRIDE,&in);
 check(result==1,"actual runtime frame is drawn");check(present_calls==prev+1&&render_calls==draws+1,"one frame-global presentation time selected once");
 for(int y=0;y<480;y++)for(int x=800;x<STRIDE;x++)assert(frame[y*STRIDE+x]==0xa55a);
 frames++;
}
static void save(const char *name){
 if(!output)return;char path[1024];snprintf(path,sizeof path,"%s/%s.rgb565",output,name);FILE *f=fopen(path,"wb");assert(f);
 for(int y=0;y<480;y++)assert(fwrite(frame+y*STRIDE,2,800,f)==800);assert(!fclose(f));
}
static int peak_x(const uint16_t *pixels,struct xz_shell_rect r){
 int y=r.y+20+(r.h-20)/2;
 for(int x=110;x<790;x++)if((x<445||x>451)&&pixels[y*STRIDE+x]&&pixels[(y-1)*STRIDE+x]&&pixels[(y+1)*STRIDE+x])return x;
 return -1;
}
static void canvas_check(void){
 struct xz_shell_model expected=model,projected;
 for(int d=0;d<4;d++){
  struct xz_shell_deck *k=&model.deck[d];
  int want=source_at(d,model.clock_ms),got=xz_shell_elapsed_now(&model,d),error=abs(want-got);
  if(error>max_source_error)max_source_error=error;
  check(error<=2,"all native/PCM lanes use source position at the common render instant");
  expected.deck[d]=*k;expected.deck[d].elapsed_ms=want;expected.deck[d].elapsed_at_ms=0;
 }
 unsigned calls=wall_calls+mono_calls;xz_shell_render(&shell,&expected,reference,STRIDE);
 check(calls==wall_calls+mono_calls,"direct full-canvas paths contain no clock calls");
 xz_shell_play_projection(&shell,&model,&projected);
 int exact=1;
 for(int d=0;d<4;d++){
  struct xz_shell_rect r=xz_shell_play_rect(&projected,XZ_SHELL_R_LANE,d);int got=peak_x(frame,r),want=peak_x(reference,r);
  int offset=abs(got-want);if(offset>max_lane_offset)max_lane_offset=offset;
  printf("TRACE effective=%d frame_clock=%u deck=%d source=%d oracle=%d impulse_x=%d oracle_x=%d\n",effective,model.clock_ms,d+1,xz_shell_elapsed_now(&model,d),expected.deck[d].elapsed_ms,got,want);
  check(got>=0&&want>=0&&offset<=1,"actual C impulse offset <=1 pixel from source-time oracle");
  int y=r.y+r.h-1,bad=0;
  for(int x=100;x<796;x++)if(reference[y*STRIDE+x]==rgb(SH_WHITE)){
   int hit=0;for(int n=-1;n<=1;n++)hit|=frame[y*STRIDE+x+n]==rgb(SH_WHITE);bad+=!hit;
  }
  check(!bad,"actual C grid edge <=1 pixel from source-time oracle");
  for(int y0=r.y+20;y0<r.y+r.h;y0++)for(int x=100;x<796;x++)if(frame[y0*STRIDE+x]!=reference[y0*STRIDE+x])exact=0;
 }
 if(exact)exact_frames++;
}
static void temporal_frames(void){
 for(int b=0;b<4;b++){
  effective=(int[]){6000,12000,24000,36000}[b];reset();snapshot_cost=b==3?5:0;
  int last[4]={0},last_want[4]={0};
  for(int i=0;i<18;i++){
   fake_us=1000000+(uint64_t)i*20000;paint();canvas_check();
   for(int d=0;d<4;d++){
    int got=xz_shell_elapsed_now(&model,d),want=source_at(d,model.clock_ms);
    if(i){int off=abs((got-last[d])-(want-last_want[d]));if(off>max_delta_error)max_delta_error=off;check(off<=3,"frame motion delta follows observed source speed");}
    last[d]=got;last_want[d]=want;
   }
   if(i==0&&b==1)save("first-common-frame");
   if(i==8&&b==3)save("fast-motion-frame");
  }
 }
}
static void boundaries(void){
 effective=12000;reset();snapshot_cost=24;paint();
 check(sh_has(&model.deck[2],XZ_SHELL_F_PLAYBACK_RATE),"snapshot published during copy is aged against clocks sampled after copy");
 check(abs(xz_shell_elapsed_now(&model,2)-source_at(2,model.clock_ms))<=1,"post-copy clock pairing preserves new anchor");
 reset();route_up=0;paint();check(sh_deck_count(&model)==2,"route off retains native-only view");
 reset();native_moving=0;paint();int native=xz_shell_elapsed_now(&model,0);model.clock_ms+=30;check(xz_shell_elapsed_now(&model,0)==native,"paused native does not extrapolate");
 reset();extra_moving=0;paint();int extra_pos=xz_shell_elapsed_now(&model,2);model.clock_ms+=30;check(xz_shell_elapsed_now(&model,2)==extra_pos,"paused PCM does not extrapolate");
 reset();unloaded=1;paint();check(!model.deck[0].loaded&&!model.deck[2].loaded,"LOAD/unloaded never projects old displayed audio");
 reset();native_ok=0;paint();check(!engine[0].ok&&model.deck[0].elapsed_ms>=0,"native-reader failure retains honest model fallback");
 reset();native_moving=0;paint();check((xz_shrt_moving_decks(&model)&12)==12,"extra-only motion keeps live cadence");
 reset();extra_moving=0;paint();check((xz_shrt_moving_decks(&model)&3)==3,"native-only motion keeps live cadence");
 reset();snapshot_fresh=0;publish_extra(1100);fake_us=1020000;extra_refresh_locked(1000);
 check(!model.deck[2].elapsed_at_ms&&!sh_has(&model.deck[2],XZ_SHELL_F_PLAYBACK_RATE),"future producer anchor holds without guessed phase");
 reset();snapshot_fresh=0;publish_extra(1000);fake_us=1300000;extra_refresh_locked(1200);
 check(!model.deck[2].elapsed_at_ms&&!sh_has(&model.deck[2],XZ_SHELL_F_PLAYBACK_RATE),"stale producer anchor holds without nominal-rate fallback");
 reset();snapshot_fresh=0;publish_extra(0xfffffff0u);fake_us=((uint64_t)UINT32_MAX+17)*1000;extra_refresh_locked(0xfffffff0u);model.clock_ms=32;
 check(model.deck[2].elapsed_at_ms==0xfffffff0u,"clock-domain conversion preserves a valid wrapped observation");
 /* Renderer boundary behavior is unchanged by the runtime-only fix. */
 struct xz_shell_deck *k=&model.deck[2];k->elapsed_ms=1990;k->elapsed_at_ms=1000;k->playback_rate_ppm=1000000;k->moving=1;k->known|=K(MOTION)|K(PLAYBACK_RATE)|K(LOOP);k->loop=(struct xz_shell_loop){1,1,1,1000,2000};model.clock_ms=1040;
 check(xz_shell_elapsed_now(&model,2)==1030,"real source loop wraps without phase snapping");
 k->elapsed_at_ms=1050;check(xz_shell_elapsed_now(&model,2)==1990,"future render clock holds");
 k->elapsed_at_ms=1000;model.clock_ms=9000;check(xz_shell_elapsed_now(&model,2)==1040,"stale render anchor stops at existing50ms bound");
 k->elapsed_ms=1500;k->elapsed_at_ms=9000;check(xz_shell_elapsed_now(&model,2)==1500,"discontinuity takes the new source coordinate immediately");
}
static void coarse_attribution(void){
 struct xz_shrt_position good={0},unstamped={0};unsigned good_follow=0,bad_follow=0;
 for(uint32_t t=1000;t<1400;t+=16){
  uint32_t observed=1000+(t-1000)/66*66;int32_t ms;uint32_t at;
  good_follow+=(unsigned)xz_shrt_position_step(&good,1,8000+(int32_t)(observed-1000),observed,1000000,1,&ms,&at);
  bad_follow+=(unsigned)xz_shrt_position_step(&unstamped,1,8000+(int32_t)(observed-1000),t,1000000,1,&ms,&at);
 }
 check(!good_follow&&bad_follow>0,"coarse updates with true anchors pass; re-stamped held native values remain attribution gap");
 printf("ATTRIBUTION coarse66ms true_anchor_follow=%u poll_stamp_follow=%u native_producer_anchor_unavailable=1\n",good_follow,bad_follow);
}
int main(int argc,char **argv){
 if(argc>1)output=argv[1];temporal_frames();boundaries();coarse_attribution();
 printf("%s cadence checks=%u failures=%u frames=%u exact_lane_frames=%u max_source_error_ms=%d max_impulse_offset_px=%d max_motion_delta_error_ms=%d\n",failures?"RED":"PASS",checks,failures,frames,exact_frames,max_source_error,max_lane_offset,max_delta_error);
 return !!failures;
}
