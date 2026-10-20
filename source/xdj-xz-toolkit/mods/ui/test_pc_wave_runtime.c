/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error PC renderer acceptance requires live assertions
#endif
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "layered_wave_runtime.c"
static struct xz_pc_stem_source source={1,0,1,1,1,1};
static struct xz_pc_wave_stamp stamp={7,12,256,1,0};
static unsigned loads,usb_reads,usb_loads,frees;
static int load_failed;
int xz_pc_stems_native_source(unsigned physical,struct xz_pc_stem_source *out){*out=source;return physical<2;}
int xz_pc_wave_native_stamp(unsigned slot,struct xz_pc_wave_stamp *out){*out=stamp;return slot==(unsigned)source.slot;}
int xz_pc_wave_current(unsigned physical,unsigned slot,uint32_t epoch,uint32_t serial,uint32_t count){return physical<2&&source.pc&&source.linked&&source.rekordbox&&source.loaded&&slot==(unsigned)source.slot&&!stamp.busy&&stamp.complete&&count==stamp.count&&epoch==stamp.epoch&&serial==stamp.serial;}
int xz_pc_wave_load(unsigned physical,struct xz_wave_load *out,unsigned *slot,uint32_t *epoch,uint32_t *serial){
 (void)physical;loads++;if(load_failed)return -1;
 out->bands=calloc(stamp.count,3);assert(out->bands);out->count=stamp.count;out->normalization=1;out->kind=0;
 *slot=(unsigned)source.slot;*epoch=stamp.epoch;*serial=stamp.serial;return 0;
}
int xz_pc_wave_start(void){return 0;}
void xz_pc_wave_state(void){}
int xz_read_memory(uint32_t address,void *out,size_t n){(void)address;(void)out;(void)n;usb_reads++;return -1;}
int xz126_deck_source_read(xz126_read_fn r,void *c,uint32_t e,unsigned d,struct xz126_deck_source *s){(void)r;(void)c;(void)e;(void)d;(void)s;usb_reads++;return -1;}
int xz_wave_load_three_band(const char *path,struct xz_wave_load *out){(void)path;(void)out;usb_loads++;return -1;}
int xz_wave_load_stems(const char *path,struct xz_wave_load *out){(void)path;(void)out;usb_loads++;return -1;}
void xz_wave_load_free(struct xz_wave_load *out){if(out->bands){free(out->bands);frees++;}memset(out,0,sizeof(*out));}
void xz_log(const char *line){(void)line;}
int xz_hook_arm(uint32_t a,const unsigned char guard[8],void *fn,void **old){(void)a;(void)guard;(void)fn;(void)old;return -1;}
static void fixture_column(unsigned d,unsigned x,unsigned f,unsigned e,unsigned bg,float zoom){(void)d;(void)x;(void)f;(void)e;(void)bg;(void)zoom;}
int main(void){
 struct prepared *local=calloc(1,sizeof(*local));assert(local);local->physical=0;local->mode=XZ_WAVE_THREE_BAND;strcpy(local->path,"/media/usb1/Contents/local.wav");live[0]=local;
 assert(!still_current(local));source.linked=0;deck_tick(0,XZ_WAVE_THREE_BAND,0);assert(!live[0]&&loads==0&&usb_reads==0&&usb_loads==0);
 source.linked=1;deck_tick(0,XZ_WAVE_THREE_BAND,1);assert(live[0]&&live[0]->pc&&loads==1);assert(still_current(live[0]));
 struct prepared *first=live[0];deck_tick(0,XZ_WAVE_THREE_BAND,500);assert(live[0]==first&&loads==1);
 deck_tick(0,XZ_WAVE_STEMS,501);assert(live[0]&&live[0]->mode==XZ_WAVE_STEMS&&live[0]->data.kind==XZ_LAYERED_THREE_BAND&&loads==2);
 stamp.count=0;assert(!still_current(live[0]));deck_tick(0,XZ_WAVE_STEMS,502);assert(!live[0]);stamp.count=256;
 source.slot=2;stamp.epoch=8;stamp.serial=14;deck_tick(0,XZ_WAVE_THREE_BAND,503);assert(live[0]&&live[0]->slot==2&&loads==3);
 stamp.serial++;assert(!still_current(live[0]));deck_tick(0,XZ_WAVE_THREE_BAND,504);assert(live[0]&&live[0]->serial==15&&loads==4);
 source.loaded=0;assert(!still_current(live[0]));deck_tick(0,XZ_WAVE_THREE_BAND,505);assert(!live[0]);source.loaded=1;
 stamp.epoch++;load_failed=1;deck_tick(0,XZ_WAVE_THREE_BAND,506);assert(loads==5&&!live[0]);deck_tick(0,XZ_WAVE_THREE_BAND,750);assert(loads==5);deck_tick(0,XZ_WAVE_THREE_BAND,1506);assert(loads==6);
 load_failed=0;deck_tick(0,XZ_WAVE_THREE_BAND,2506);assert(live[0]&&loads==7);
 stamp.busy=1;assert(!still_current(live[0]));stamp.busy=0;
 unsigned before=loads;deck_tick(0,XZ_WAVE_STOCK,2507);assert(!live[0]&&loads==before);
 assert(usb_reads==0&&usb_loads==0);sweep(now_ms()+RETIRE_MS+1);assert(frees==5);
 /* Native zoom for the four-deck lanes: the longer side of the playhead wins, so a clamped track
    start or end cannot shrink it; a frame missing a probe column changes nothing. */
 stock_column=fixture_column;uint32_t us;unsigned bg;
 xz_layered_wave_view(&us,&bg);assert(us==0&&bg==1);
 column(0,0,0,1,1,1.0f);column(0,268,268,269,1,1.0f);column(0,535,535,536,1,1.0f);xz_layered_wave_end();
 xz_layered_wave_view(&us,&bg);assert(us==6666&&bg==1);
 column(1,535,1070,1072,0,1.0f);column(1,268,536,538,0,1.0f);column(1,0,100,101,0,1.0f);xz_layered_wave_end();
 xz_layered_wave_view(&us,&bg);assert(us==13333&&bg==0);
 column(0,0,5,6,1,1.0f);column(0,535,9,10,1,1.0f);xz_layered_wave_end();
 xz_layered_wave_view(&us,NULL);assert(us==13333);
 puts("PASS PC renderer source isolation, stock toggle, mode reload, native invalidation and retry acceptance");return 0;
}
