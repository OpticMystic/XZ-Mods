#include "../overcue_stream.c"
#include <assert.h>
/* Decoder boundary mocks: no files, USB, native engine, or device access. */
static struct xz_oc_assets *mock_assets;
static int16_t reference_sample(int64_t n,unsigned c){uint32_t x=(uint32_t)n*2654435761u+c*2246822519u;x^=x>>16;x*=2246822519u;x^=x>>13;return (int16_t)((int)(x%40001)-20000);}
float xz_oc_mix_gain(const struct xz_oc_assets *a,unsigned m){(void)a;(void)m;return 1;}
struct xz_oc_file *xz_oc_mix_file(struct xz_oc_assets *a,unsigned m){assert(m==3);return &a->combination[0];}
int xz_oc_read(struct xz_oc_file *f,int64_t first,size_t n,int16_t *out){for(size_t i=0;i<n;i++)for(unsigned c=0;c<2;c++)out[i*2+c]=f==&mock_assets->reference?reference_sample(first+(int64_t)i,c):(first+(int64_t)i<500000?4096:12288);return 0;}
int xz_oc_open(const char *p,struct xz_oc_assets *a,char *e,size_t n){(void)p;(void)a;(void)e;(void)n;abort();}
int xz_oc_bundle(const char *p,char *e,size_t n){(void)p;(void)e;(void)n;abort();}
int xz_oc_assets_pin(struct xz_oc_assets *a){(void)a;abort();}
void xz_oc_close(struct xz_oc_assets *a){(void)a;}
size_t xz_oc_assets_ram_bytes(const struct xz_oc_assets *a){(void)a;return 0;}
struct xz_foreign_stems *xz_foreign_open(const char *p,char *e,size_t n){(void)p;(void)e;(void)n;abort();}
void xz_foreign_close(struct xz_foreign_stems *f){(void)f;}
unsigned xz_foreign_stem_count(const struct xz_foreign_stems *f){(void)f;abort();}
uint32_t xz_foreign_rate(const struct xz_foreign_stems *f){(void)f;abort();}
uint64_t xz_foreign_frames(const struct xz_foreign_stems *f){(void)f;abort();}
enum xz_foreign_format xz_foreign_format(const struct xz_foreign_stems *f){(void)f;abort();}
int xz_foreign_read(struct xz_foreign_stems *f,unsigned m,int64_t p,size_t n,float *b){(void)f;(void)m;(void)p;(void)n;(void)b;abort();}
int xz_foreign_reference(struct xz_foreign_stems *f,int64_t p,size_t n,float *b){(void)f;(void)p;(void)n;(void)b;abort();}
#define BLOCK 64
static const float levels[3]={1,1,0};
static void init(struct xz_oc_stream *s){memset(s,0,sizeof(*s));s->rate=s->source_rate=44100;s->role_count=3;s->scale=1;s->capture_state=3;s->state=XZ_OC_READY;s->published=s->loop_published=-1;s->loop_start=-1;memcpy(s->previous_levels,levels,sizeof(levels));float one=1;memcpy(s->previous_levels+3,&one,4);mock_assets=&s->assets;for(int i=0;i<4;i++)for(int r=0;r<2;r++){s->window[i].pcm[r]=calloc(WINDOW*2,sizeof(float));assert(s->window[i].pcm[r]);}}
static void cleanup(struct xz_oc_stream *s){for(int i=0;i<4;i++)for(int r=0;r<2;r++)free(s->window[i].pcm[r]);}
static void window(struct xz_oc_stream *s,int slot,int64_t at,float value){struct window *w=&s->window[slot];w->first=at-9600;w->masks=3;w->valid=1;for(unsigned i=0;i<WINDOW*2;i++)w->pcm[0][i]=value;}
static size_t block(struct xz_oc_stream *s,int64_t at,float pcm[BLOCK*2],uint32_t trace[16]){for(int i=0;i<BLOCK*2;i++)pcm[i]=.75f;return xz_oc_stream_mix_trace(s,pcm,BLOCK,at,levels,trace);}
static int uniform(const float *p,float value){for(int i=0;i<BLOCK*2;i++)if(fabsf(p[i]-value)>1e-6)return 0;return 1;}
static int bug_a(void){struct xz_oc_stream s;init(&s);window(&s,0,20000,.125f);s.published=0;float pcm[BLOCK*2];uint32_t t[16];block(&s,20000,pcm,t);assert(uniform(pcm,.125f));unsigned unity=0,silent=0;for(int n=0;n<8;n++){block(&s,1000000+n*BLOCK,pcm,t);unity+=uniform(pcm,.75f);silent+=uniform(pcm,0);for(unsigned i=0;i<BLOCK;i++)assert(pcm[i*2]>=0&&pcm[i*2]<=.125f);if(!n){assert(fabsf(pcm[0]-.125f*(1-1.0f/BLOCK))<1e-6);assert(pcm[(BLOCK-1)*2]==0);}}
 printf("A uncovered hot-cue: unity_blocks=%u/8 frames=%u simulated_audio_ms=%.3f flags=%u misses=%u silent_blocks=%u/8\n",unity,unity*BLOCK,unity*BLOCK*1000.0/44100,t[3],s.misses,silent);
 window(&s,1,1000000,.375f);s.published=1;block(&s,1000000+8*BLOCK,pcm,t);float recovery=pcm[0];if(!unity){assert(fabsf(recovery-.375f/BLOCK)<1e-6);for(unsigned i=1;i<BLOCK;i++)assert(pcm[i*2]>=pcm[(i-1)*2]);}block(&s,1000000+9*BLOCK,pcm,t);assert(uniform(pcm,.375f));printf("A recovery: first_sample=%.9f steady_sample=%.3f\n",recovery,pcm[0]);cleanup(&s);if(unity){puts("FAIL A: muted stem selection leaked stock/unity after seek");return 1;}puts("PASS A: no unity block after uncovered seek; recovery reaches selected mix");return 0;}
static int bug_b(void){struct xz_oc_stream s;init(&s);float pcm[BLOCK*2];uint32_t t[16];int old=0;window(&s,0,20000,.125f);window(&s,2,20000,.125f);s.published=0;s.loop_published=2;xz_oc_stream_loop_start(&s,20000);for(int i=0;i<3;i++){block(&s,20000+1000,pcm,t);assert(uniform(pcm,.125f));block(&s,20000,pcm,t);assert(uniform(pcm,.125f));}
 xz_oc_stream_loop_start(&s,-1);window(&s,1,1000000,.375f);s.published=1;
 /* New L2 hint, but deliberately keep old L1 pinned; then simulate a stale hint too. */
 xz_oc_stream_loop_start(&s,1000000);for(int i=0;i<100;i++){block(&s,1000000+1000,pcm,t);assert(uniform(pcm,.375f));block(&s,1000000,pcm,t);old+=uniform(pcm,.125f);assert(uniform(pcm,.375f));}
 xz_oc_stream_loop_start(&s,20000);block(&s,1000000,pcm,t);assert(uniform(pcm,.375f));
 printf("B L1 -> exit -> L2 wraps=100 old_region_blocks=%d L2_sample=%.3f stale_hint=%d selected_slot=%u\n",old,pcm[0],s.loop_start,t[4]);
 /* Remove L2 coverage: old L1 windows MUST NOT qualify. */
 s.window[1].valid=0;block(&s,1000000,pcm,t);assert(t[4]==UINT32_MAX);assert(!uniform(pcm,.125f));printf("B uncovered L2: selected_slot=%u output_sample=%.3f (old L1=0.125)\n",t[4],pcm[0]);cleanup(&s);puts("PASS B: old loop cache cannot serve L1 audio for an L2 source position");return 0;}
static int overlapping_b(void){struct xz_oc_stream s;init(&s);float pcm[BLOCK*2];uint32_t t[16];
 /* A single OLD loop window contains multiple distinct absolute-position markers. */
 struct window *w=&s.window[2];w->first=10400;w->valid=1;w->masks=3;
 for(unsigned i=0;i<WINDOW;i++)for(unsigned c=0;c<2;c++)w->pcm[0][i*2+c]=(float)((i/1024)%17+1)/32;
 s.loop_published=2;xz_oc_stream_loop_start(&s,20000);block(&s,20000,pcm,t);
 xz_oc_stream_loop_start(&s,-1);xz_oc_stream_loop_start(&s,60000);
 int wraps=0;for(int64_t at=60000;at<190000;at+=1024){block(&s,at,pcm,t);for(unsigned i=0;i<BLOCK;i++)for(unsigned c=0;c<2;c++){float expected=(float)(((at+i-w->first)/1024)%17+1)/32;assert(pcm[i*2+c]==expected);}wraps++;}
 printf("B overlapping old window: %d L2 positions use correct absolute sample offsets; selected_slot=%u\n",wraps,t[4]);cleanup(&s);puts("PASS B overlap: old window identity does not imply old audio position");return 0;}
static void delay(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static int worker_b(void){struct xz_oc_stream s;init(&s);s.capture_state=2;s.capture_count=2048;s.capture_position=20000;s.previous_position=10000;s.confirmations=1;for(unsigned i=0;i<2048;i++)for(unsigned c=0;c<2;c++)s.capture[i*2+c]=reference_sample(20000+i,c)/32768.0f;s.request_masks=3;s.desired=20000;s.loop_start=20000;s.running=1;assert(!pthread_create(&s.worker,NULL,stream_worker,&s));float pcm[BLOCK*2];uint32_t t[16];
 for(int i=0;i<2000;i++){delay();struct xz_oc_plan p; xz_oc_plan_levels(levels,&p);struct window *w=find_window(&s,&p,20000,63,0);if(w){window_release(w);break;}}
 block(&s,20000,pcm,t);assert(uniform(pcm,.125f));xz_oc_stream_loop_start(&s,-1);xz_oc_stream_position(&s,1000000);xz_oc_stream_loop_start(&s,1000000);
 int ready=0;for(int i=0;i<2000;i++){delay();struct xz_oc_plan p;xz_oc_plan_levels(levels,&p);struct window *w=find_window(&s,&p,1000000,63,0);if(w){window_release(w);ready=1;break;}}
 assert(ready);for(int i=0;i<20;i++){block(&s,1001000,pcm,t);assert(uniform(pcm,.375f));block(&s,1000000,pcm,t);assert(uniform(pcm,.375f));}
 __atomic_store_n(&s.running,0,__ATOMIC_RELEASE);pthread_join(s.worker,NULL);printf("B real worker: new_hint=%d loop_first=%lld slot=%d sample=%.3f\n",s.loop_start,(long long)s.window[s.loop_published].first,s.loop_published,pcm[0]);cleanup(&s);puts("PASS B worker: re-pins L2 and renders L2 after exit/re-entry");return 0;}
static void ramp_block_sizes(void){
 const unsigned sizes[]={1,16,64,128,512,2048};
 for(unsigned n=0;n<sizeof(sizes)/sizeof(*sizes);n++){
  unsigned frames=sizes[n],ramp=frames<STARVE_RAMP_FRAMES?frames:STARVE_RAMP_FRAMES;
  struct xz_oc_stream s;init(&s);window(&s,0,20000,.125f);s.published=0;
  float pcm[4096];for(unsigned i=0;i<4096;i++)pcm[i]=.75f;
  xz_oc_stream_mix(&s,pcm,frames,20000,levels);
  for(unsigned i=0;i<4096;i++)pcm[i]=.75f;
  xz_oc_stream_mix(&s,pcm,frames,1000000,levels);
  for(unsigned i=0;i<frames;i++)for(unsigned c=0;c<2;c++)assert(fabsf(pcm[i*2+c]-(i<ramp?.125f*(1-(float)(i+1)/ramp):0))<1e-6);
  window(&s,1,1000000,.375f);s.published=1;for(unsigned i=0;i<4096;i++)pcm[i]=.75f;
  xz_oc_stream_mix(&s,pcm,frames,1000000,levels);
  for(unsigned i=0;i<frames;i++)for(unsigned c=0;c<2;c++)assert(fabsf(pcm[i*2+c]-(i<ramp?.375f*(float)(i+1)/ramp:.375f))<1e-6);
  cleanup(&s);
 }
 puts("PASS entry/recovery ramp endpoints: 1/16/64/128/512/2048-frame blocks");
}
static int starvation_policy(void){
 ramp_block_sizes();
 const unsigned rates[]={32000,44100,48000,96000};
 for(unsigned ri=0;ri<sizeof(rates)/sizeof(*rates);ri++){unsigned rate=rates[ri];
  struct xz_oc_stream s;init(&s);s.rate=s.source_rate=rate;float pcm[BLOCK*2];uint32_t t[16];
  window(&s,0,20000,.125f);s.published=0;block(&s,20000,pcm,t);
  for(unsigned n=0;n<(rate*STARVE_UNITY_AFTER_MS/1000+BLOCK-1)/BLOCK;n++){
   block(&s,1000000,pcm,t);
   if((n+1)*BLOCK<rate*STARVE_UNITY_AFTER_MS/1000){assert(s.starved==XZ_OC_STARVE_HELD);assert(!uniform(pcm,.75f));}
  }
  assert(s.starved==XZ_OC_STARVE_UNITY);block(&s,1000000,pcm,t);assert(uniform(pcm,.75f));
  struct xz_oc_status status;xz_oc_stream_status(&s,&status);assert(status.starve_runs==1);assert(status.starve_last_ms>=1500);assert(status.starve_last_frames==s.starve_frames);assert(status.starve_max_frames==s.starve_frames);
  uint32_t longest=status.starve_max_frames;
  window(&s,1,1000000,.375f);s.published=1;block(&s,1000000,pcm,t);assert(pcm[0]<.75f&&pcm[0]>.375f);block(&s,1000000,pcm,t);assert(uniform(pcm,.375f));
  xz_oc_stream_status(&s,&status);assert(status.starvation==XZ_OC_STARVE_NONE&&status.starve_fallback==XZ_OC_STARVE_UNITY&&status.starve_last_frames==longest);
  /* A real reader failure must fail open immediately, still using the short ramp. */
  s.state=XZ_OC_ERROR;block(&s,1000000,pcm,t);assert(pcm[0]>.375f&&pcm[0]<.75f);assert(pcm[(BLOCK-1)*2]==.75f);
  block(&s,1000000,pcm,t);assert(uniform(pcm,.75f));xz_oc_stream_status(&s,&status);assert(status.starvation==XZ_OC_STARVE_UNITY&&status.starve_runs==2&&status.starve_last_frames==2*BLOCK&&status.starve_max_frames==longest);
  cleanup(&s);
 }
 /* Attenuated selections hold their native coefficient rather than unity. */
 struct xz_oc_stream s;init(&s);float attenuated[3]={1,.6f,.25f},pcm[BLOCK*2];memcpy(s.previous_levels,attenuated,sizeof(attenuated));
 for(unsigned n=0;n<2;n++){for(unsigned i=0;i<BLOCK*2;i++)pcm[i]=.8f;xz_oc_stream_mix(&s,pcm,BLOCK,1000000,attenuated);assert(uniform(pcm,.2f));}
 cleanup(&s);puts("PASS starvation policy: 32/44.1/48/96 kHz bounded unity, error ramp, recovery, attenuation and per-run metrics");return 0;
}
int main(int argc,char **argv){if(argc!=2)return 2;if(!strcmp(argv[1],"policy"))return starvation_policy();if(!strcmp(argv[1],"A"))return bug_a();if(!strcmp(argv[1],"B"))return bug_b();if(!strcmp(argv[1],"overlap-B"))return overlapping_b();if(!strcmp(argv[1],"worker-B"))return worker_b();return 2;}
