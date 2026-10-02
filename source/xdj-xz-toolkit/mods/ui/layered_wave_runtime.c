/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "layered_wave_runtime.h"
#include "layered_wave.h"
#include "layered_wave_source.h"
#include "../runtime.h"
#include "../audio/native_reader.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#if defined(__arm__)
#define XZ_WAVE_ABI __attribute__((pcs("aapcs-vfp")))
#else
#define XZ_WAVE_ABI
#endif
#define WIDTH 536
#define EXTENT 52
#define CENTER(deck) (65u+(deck)*136u)
typedef void (XZ_WAVE_ABI *native_column_fn)(unsigned,unsigned,unsigned,unsigned,unsigned,float);
static native_column_fn stock_column;
struct prepared {uint32_t player,file_frames;int mode;char path[XZ126_READER_PATH_MAX];struct xz_wave_load load;struct xz_layered_data data;};
/* Published read-only to the draw thread; the worker is the only writer. A replaced copy
 * is freed only after RETIRE_MS, so a frame that picked it up always finishes with it.
 * No lock on the draw path: a missed lock used to cost a whole stock frame (a flicker). */
static struct prepared *live[2];
#define RETIRE_SLOTS 16
#define RETIRE_MS 1500
static struct {struct prepared *p;long at;} retired[RETIRE_SLOTS];
/* Worker-only bookkeeping per deck. */
static struct {long last_valid,retry_at;char failed[XZ126_READER_PATH_MAX];} track[2];
static int started,mode;
static uint16_t colors[3]={0xf800,0x07e0,0x001f};
static _Thread_local struct {int active;uint16_t *pixels;const struct prepared *deck[2];uint32_t flags[2];int direct[2];} frame;
/* What this thread drew in the current frame; it outlives the lock for the theme pass. */
static _Thread_local struct {struct xz_layered_data look[2];struct xz_layered_col col[2][WIDTH];int any;} drawn;
static _Thread_local uint8_t keep[2][WIDTH][(2*EXTENT+7)/8];
/* [0] version, [1] loads, [2] columns drawn, [3] columns rejected, [4] failed loads, [5] mode, [6] last load ms,
 * [7] frames timed, [8] summed lock-to-unlock us, [9] worst us (stock drawing included, any mode),
 * [10] overview fills seen, [11] overview fills drawn */
__attribute__((visibility("default"))) uint32_t xz_layered_wave_proof_v1[12]={2};
static _Thread_local long frame_began;
static long now_us(void){struct timeval t;gettimeofday(&t,NULL);return t.tv_sec*1000000L+t.tv_usec;}

static int read_mem(void *c,uint32_t a,void *b,size_t n){(void)c;return xz_read_memory(a,b,n);}
static int word(uint32_t a,uint32_t *v){return xz_read_memory(a,v,4);}
static int current_source(unsigned deck,struct xz126_deck_source *s){
 uint32_t engine;return word(XZ126_PLAY_ENGINE_CELL,&engine)||xz126_deck_source_read(read_mem,NULL,engine,deck,s);
}
/* 1 while the deck still holds p's file. The XZ rebinds its reader objects mid-track
 * (loops, scratching), so only a readable, different path counts as a change. */
static int still_current(const struct prepared *p){
 uint32_t reader,impl;char path[XZ126_READER_PATH_MAX];size_t n=strlen(p->path)+1;
 if(word(p->player+0xc8,&reader)||!reader||word(reader+4,&impl)||!impl||xz_read_memory(impl,path,n))return 1;
 if(path[0]!='/')return 1;
 return !memcmp(path,p->path,n);
}
static long now_ms(void){struct timeval t;gettimeofday(&t,NULL);return t.tv_sec*1000L+t.tv_usec/1000;}
static void prepared_free(struct prepared *p){if(p){xz_wave_load_free(&p->load);free(p);}}
static void sweep(long now){
 for(unsigned i=0;i<RETIRE_SLOTS;i++)if(retired[i].p&&now-retired[i].at>=RETIRE_MS){prepared_free(retired[i].p);retired[i].p=NULL;}
}
static void publish(unsigned deck,struct prepared *next){
 struct prepared *old=__atomic_exchange_n(&live[deck],next,__ATOMIC_ACQ_REL);
 if(!old)return;
 for(;;){
  long now=now_ms();sweep(now);
  for(unsigned i=0;i<RETIRE_SLOTS;i++)if(!retired[i].p){retired[i].p=old;retired[i].at=now;return;}
  struct timespec wait={0,100000000};nanosleep(&wait,NULL);
 }
}
static int load(struct prepared *p,unsigned deck,int stems){
 long begun=now_ms();
 int rc=-1;
 if(stems)rc=xz_wave_load_stems(p->path,&p->load);
 if(rc)rc=xz_wave_load_three_band(p->path,&p->load);
 if(rc){__atomic_fetch_add(&xz_layered_wave_proof_v1[4],1,__ATOMIC_RELAXED);return -1;}
 p->data=(struct xz_layered_data){p->load.bands,p->load.count,p->load.normalization,p->load.kind,{0,0,0}};
 long took=now_ms()-begun;__atomic_store_n(&xz_layered_wave_proof_v1[6],(uint32_t)took,__ATOMIC_RELAXED);
 char line[160];
 snprintf(line,sizeof(line),"LAYERED_WAVE: deck=%u %s samples=%u norm=%u load_ms=%ld",deck+1,
  p->load.kind==XZ_LAYERED_STEMS?"stems":"3-band",p->load.count,p->load.normalization,took);
 xz_log(line);__atomic_fetch_add(&xz_layered_wave_proof_v1[1],1,__ATOMIC_RELAXED);
 return 0;
}
static void deck_tick(unsigned i,int wanted,long now){
 struct prepared *cur=__atomic_load_n(&live[i],__ATOMIC_ACQUIRE);
 if(!wanted){if(cur)publish(i,NULL);return;}
 struct xz126_deck_source s;
 /* A failed snapshot is usually the player rebinding; keep drawing for a while. */
 if(current_source(i,&s)){if(cur&&now-track[i].last_valid>5000)publish(i,NULL);return;}
 track[i].last_valid=now;
 int same=cur&&cur->file_frames==s.file_frames&&!strcmp(cur->path,s.path);
 int retry=wanted==XZ_WAVE_STEMS&&same&&cur->mode==wanted&&cur->load.kind!=XZ_LAYERED_STEMS;
 if(same&&cur->mode==wanted&&!retry)return;
 if(now<track[i].retry_at&&!strcmp(track[i].failed,s.path))return;
 struct prepared *next=calloc(1,sizeof(*next));if(!next)return;
 next->player=s.player;next->file_frames=s.file_frames;next->mode=wanted;memcpy(next->path,s.path,sizeof(next->path));
 /* The current copy stays on screen while the next one loads, so a mode switch or a
  * stems retry never blanks the waveform. */
 if(load(next,i,wanted==XZ_WAVE_STEMS)||(retry&&next->load.kind!=XZ_LAYERED_STEMS)){
  prepared_free(next);
  track[i].retry_at=now+(retry?10000:3000);memcpy(track[i].failed,s.path,sizeof(track[i].failed));
  if(!same&&cur)publish(i,NULL);
  return;
 }
 struct xz126_deck_source after;
 if(!current_source(i,&after)&&(after.file_frames!=s.file_frames||strcmp(after.path,s.path))){prepared_free(next);return;}
 track[i].retry_at=0;publish(i,next);
}
static void *worker(void *unused){
 (void)unused;
 for(;;){
  int wanted=__atomic_load_n(&mode,__ATOMIC_ACQUIRE);long now=now_ms();
  sweep(now);
  for(unsigned i=0;i<2;i++)deck_tick(i,wanted,now);
  struct timespec delay={0,250000000};nanosleep(&delay,NULL);
 }
 return NULL;
}
int xz_layered_wave_enabled(void){return __atomic_load_n(&started,__ATOMIC_ACQUIRE);}
int xz_layered_wave_active(void){return xz_layered_wave_enabled()&&__atomic_load_n(&mode,__ATOMIC_ACQUIRE)!=XZ_WAVE_STOCK;}
static uint16_t rgb565(uint32_t rgb){return (uint16_t)(((rgb>>19)&31)<<11|((rgb>>10)&63)<<5|((rgb>>3)&31));}
void xz_layered_wave_configure(int requested,const uint32_t stem_rgb[3]){
 if(requested<0||requested>=XZ_WAVE_MODE_COUNT)requested=XZ_WAVE_STOCK;
 if(stem_rgb)for(unsigned i=0;i<3;i++)__atomic_store_n(&colors[i],rgb565(stem_rgb[i]),__ATOMIC_RELAXED);
 __atomic_store_n(&mode,requested,__ATOMIC_RELEASE);
 __atomic_store_n(&xz_layered_wave_proof_v1[5],(uint32_t)requested,__ATOMIC_RELAXED);
}
void xz_layered_wave_end(void){
 if(frame_began){
  long took=now_us()-frame_began;frame_began=0;
  if(took>=0&&took<1000000){
   __atomic_fetch_add(&xz_layered_wave_proof_v1[7],1,__ATOMIC_RELAXED);
   __atomic_fetch_add(&xz_layered_wave_proof_v1[8],(uint32_t)took,__ATOMIC_RELAXED);
   if((uint32_t)took>xz_layered_wave_proof_v1[9])xz_layered_wave_proof_v1[9]=(uint32_t)took;
  }
 }
 memset(&frame,0,sizeof(frame));
}
void xz_layered_wave_begin(uint16_t *pixels){
 xz_layered_wave_end();
 if(xz_layered_wave_enabled())frame_began=now_us();
 drawn.any=0;memset(drawn.col,0,sizeof(drawn.col));
 if(!xz_layered_wave_active()||!pixels)return;
 frame.active=1;frame.pixels=pixels;
 int wanted=__atomic_load_n(&mode,__ATOMIC_ACQUIRE);
 for(unsigned i=0;i<2;i++){
  uint32_t ref,owner,flags;
  const struct prepared *p=__atomic_load_n(&live[i],__ATOMIC_ACQUIRE);
  if(p&&p->mode==wanted&&still_current(p)&&
    !word(0x1b31ed0+i*32+4,&ref)&&ref&&!word(ref,&owner)&&owner&&owner<UINT32_MAX-0x2ee14&&
    !word(owner+0x2ee10,&flags)&&flags){
   frame.deck[i]=p;frame.flags[i]=flags;
   /* One syscall-checked probe of both ends per frame, then plain loads per column. */
   uint8_t probe;uint32_t last=p->data.count-1;
   frame.direct[i]=last<UINT32_MAX/16&&flags<=UINT32_MAX-last*16-12&&
    !xz_read_memory(flags+12,&probe,1)&&!xz_read_memory(flags+last*16+12,&probe,1);
   drawn.look[i]=p->data;drawn.look[i].bands=NULL;
   for(unsigned k=0;k<3;k++)drawn.look[i].colors[k]=__atomic_load_n(&colors[k],__ATOMIC_RELAXED);
  }
 }
}
static void XZ_WAVE_ABI column(unsigned deck,unsigned x,unsigned first,unsigned end,unsigned bg,float scale){
 stock_column(deck,x,first,end,bg,scale);
 if(!frame.active||deck>=2||!frame.deck[deck]||x>=WIDTH)return;
 const struct prepared *p=frame.deck[deck];
 if(first>=p->data.count||first>UINT32_MAX/16||frame.flags[deck]>UINT32_MAX-first*16-12)return;
 uint8_t flags;
 if(frame.direct[deck])flags=((const volatile uint8_t *)(uintptr_t)frame.flags[deck])[first*16+12];
 else if(xz_read_memory(frame.flags[deck]+first*16+12,&flags,1))return;
 struct xz_layered_data look=p->data;memcpy(look.colors,drawn.look[deck].colors,sizeof(look.colors));
 if(xz_layered_column(frame.pixels,WIDTH,WIDTH,268,x,CENTER(deck),EXTENT,&look,first,end,flags,bg==1,&drawn.col[deck][x])){
  drawn.any=1;__atomic_fetch_add(&xz_layered_wave_proof_v1[2],1,__ATOMIC_RELAXED);
 }else __atomic_fetch_add(&xz_layered_wave_proof_v1[3],1,__ATOMIC_RELAXED);
}
/* Rows d above the centre line, then their mirror below it. */
static unsigned row_of(unsigned deck,unsigned r){return r<EXTENT?CENTER(deck)-r:CENTER(deck)+1+(r-EXTENT);}
void xz_layered_wave_protect(const uint16_t *pixels){
 memset(keep,0,sizeof(keep));
 if(!drawn.any||!pixels)return;
 for(unsigned deck=0;deck<2;deck++)for(unsigned x=0;x<WIDTH;x++){
  const struct xz_layered_col *c=&drawn.col[deck][x];if(!c->valid)continue;
  for(unsigned r=0;r<2*EXTENT;r++){
   int want=xz_layered_pixel(&drawn.look[deck],c,r%EXTENT);
   if(want>=0&&pixels[row_of(deck,r)*WIDTH+x]==(uint16_t)want)keep[deck][x][r/8]|=(uint8_t)(1u<<(r%8));
  }
 }
}
void xz_layered_wave_restore(uint16_t *pixels){
 if(!drawn.any||!pixels)return;
 for(unsigned deck=0;deck<2;deck++)for(unsigned x=0;x<WIDTH;x++){
  const struct xz_layered_col *c=&drawn.col[deck][x];if(!c->valid)continue;
  for(unsigned r=0;r<2*EXTENT;r++)if(keep[deck][x][r/8]&(1u<<(r%8)))
   pixels[row_of(deck,r)*WIDTH+x]=(uint16_t)xz_layered_pixel(&drawn.look[deck],c,r%EXTENT);
 }
}
#define OVERVIEW_W 300
#define OVERVIEW_H 34
static _Thread_local struct {int any;struct xz_layered_data look;struct xz_layered_col col[OVERVIEW_W];} overview;
/* Stock cue marks are full-height 0xf800 columns with black borders; keep them and their borders. */
static int cue_column(const uint16_t *p,unsigned x){
 if(x>=OVERVIEW_W)return 0;
 for(unsigned y=0;y<OVERVIEW_H;y++)if(p[y*OVERVIEW_W+x]!=0xf800)return 0;
 return 1;
}
int xz_layered_overview(uint16_t *pixels,unsigned deck){
 overview.any=0;
 if(!pixels||deck>=2||!xz_layered_wave_active())return 0;
 __atomic_fetch_add(&xz_layered_wave_proof_v1[10],1,__ATOMIC_RELAXED);
 const struct prepared *p=__atomic_load_n(&live[deck],__ATOMIC_ACQUIRE);
 if(p&&p->mode==__atomic_load_n(&mode,__ATOMIC_ACQUIRE)&&still_current(p)){
  overview.look=p->data;for(unsigned k=0;k<3;k++)overview.look.colors[k]=__atomic_load_n(&colors[k],__ATOMIC_RELAXED);
  for(unsigned x=0;x<OVERVIEW_W;x++){
   struct xz_layered_col *c=&overview.col[x];c->valid=0;
   if(cue_column(pixels,x)||cue_column(pixels,x+1)||(x&&cue_column(pixels,x-1)))continue;
   uint32_t first=(uint32_t)((uint64_t)x*p->data.count/OVERVIEW_W),end=(uint32_t)((uint64_t)(x+1)*p->data.count/OVERVIEW_W);
   if(!xz_layered_heights(&p->data,first,end,OVERVIEW_H,c))continue;
   for(unsigned d=0;d<OVERVIEW_H;d++){
    int layer=xz_layered_pixel(&overview.look,c,d);
    pixels[(OVERVIEW_H-1-d)*OVERVIEW_W+x]=layer<0?0:(uint16_t)layer;
   }
   overview.any=1;
  }
  overview.look.bands=NULL;
  if(overview.any)__atomic_fetch_add(&xz_layered_wave_proof_v1[11],1,__ATOMIC_RELAXED);
 }
 return overview.any;
}
void xz_layered_overview_restore(uint16_t *pixels){
 if(!overview.any||!pixels)return;
 for(unsigned x=0;x<OVERVIEW_W;x++){
  const struct xz_layered_col *c=&overview.col[x];if(!c->valid)continue;
  for(unsigned d=0;d<OVERVIEW_H;d++){
   int layer=xz_layered_pixel(&overview.look,c,d);
   if(layer>=0)pixels[(OVERVIEW_H-1-d)*OVERVIEW_W+x]=(uint16_t)layer;
  }
 }
}
int xz_layered_wave_start(void){
 if(xz_layered_wave_enabled())return 0;
 const char *disabled=getenv("XZ_MODS_LAYERED_WAVE");if(disabled&&!strcmp(disabled,"0"))return 0;
 static const unsigned char guard[8]={0xf0,0x4f,0x2d,0xe9,0x80,0x52,0xa0,0xe1};
 if(xz_hook_arm(0x1fab28,guard,(void*)column,(void**)&stock_column))return -1;
 pthread_t thread;if(pthread_create(&thread,NULL,worker,NULL))return -1;
 pthread_detach(thread);__atomic_store_n(&started,1,__ATOMIC_RELEASE);return 0;
}
