/* SPDX-License-Identifier: MIT */
#include "shell_model.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Shell model acceptance requires active assertions
#endif
/* Parent adds these declarations to shell_model.h during integration. */
void xz_shell_model_set_input(struct xz_shell_model_storage *,int,const struct xz_shell_deck *,unsigned);
void xz_shell_model_diagnostics(const struct xz_shell_model_storage *,uint32_t out[4]);
int xz_shell_model_hot_cue_out(const struct xz_shell_model_storage *,int,int,int32_t *);
#define K(f) XZ_SHELL_KNOWN(XZ_SHELL_F_##f)
#define S(f) (1u<<XZ_SHELL_SET_##f)
enum { FRAME_SIZE=0x26c, SCROLL_MAX=32768, OVERVIEW_MAX=1024, GRID_MAX=4096 };
struct region {uint32_t address;size_t size;unsigned char data[8192];};
static struct region image[32];static unsigned regions;
static int bump_elapsed; /* each title-record read advances deck 1's elapsed, as playback does */
static uint32_t torn_address,fail_address;static unsigned torn_reads,read_calls,guard_reads[4];
static const uint32_t guard_address[4]={0x249820,0x2508dc,0xd9310,0x18ca44};
static const unsigned char guard_bytes[4][16]={
 {0x0c,0x30,0x90,0xe5,0x00,0x00,0x53,0xe3,0x03,0x00,0xa0,0x01,0x1e,0xff,0x2f,0x01},
 {0x08,0x31,0x9f,0xe5,0x01,0x00,0x51,0xe3,0xf0,0x05,0x2d,0xe9,0x41,0x12,0xa0,0x13},
 {0x4c,0x3d,0x0d,0xe3,0x24,0x32,0x40,0xe3,0x89,0x2f,0xa0,0xe3,0x92,0x30,0x20,0xe0},
 {0x7c,0x3f,0x0f,0xe3,0xea,0x33,0x40,0xe3,0x00,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1}};
static unsigned char *map(uint32_t address,size_t size){
 assert(regions<32&&size<=sizeof(image[0].data));struct region *r=image+regions++;
 r->address=address;r->size=size;memset(r->data,0,sizeof(r->data));return r->data;
}
static unsigned char *at(uint32_t address,size_t size){
 for(unsigned i=0;i<regions;i++)if(address>=image[i].address&&(uint64_t)address+size<=(uint64_t)image[i].address+image[i].size)
  return image[i].data+(address-image[i].address);
 return NULL;
}
static void put16(uint32_t address,unsigned value){unsigned char *p=at(address,2);assert(p);p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);}
static void put32(uint32_t address,uint32_t value){unsigned char *p=at(address,4);assert(p);for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(8*i));}
static int read_memory(uintptr_t address,void *out,size_t size){
 ++read_calls;assert(address<=UINT32_MAX);
 for(unsigned i=0;i<4;i++)if(address==guard_address[i])++guard_reads[i];
 if(fail_address&&address<=fail_address&&(uint64_t)address+size>fail_address)return -1;
 unsigned char *p=at((uint32_t)address,size);if(!p)return -1;
 memcpy(out,p,size);
 if(bump_elapsed&&address==0x224dd4c){unsigned char *e=at(0x3000000+0xc,4);e[0]=(unsigned char)(e[0]+7);}
 if(torn_address&&address<=torn_address&&(uint64_t)address+size>torn_address){
  if(torn_reads++&1)((unsigned char *)out)[torn_address-address]^=1;
 }
 return 0;
}
static uint32_t frame(unsigned deck){return 0x3000000+deck*0x1000;}
static uint32_t total(unsigned deck){return 0x2247cac+0x1610+deck*0x60;}
static uint32_t title_record(unsigned deck){return 0x224dd4c+deck*0x224;}
static void set_title(unsigned deck){
 uint32_t base=title_record(deck);memset(at(base,0x224),0,0x224);
 static const uint16_t units[]={'D','J',' ',0xe9,0x4e2d,0xd83d,0xde00};
 put16(base+8,0x5a);put16(base+0x20,sizeof(units)/sizeof(*units));
 for(unsigned i=0;i<sizeof(units)/sizeof(*units);i++)put16(base+0x22+i*2,units[i]);
}
static void fixture(void){
 regions=0;torn_address=fail_address=torn_reads=read_calls=0;memset(guard_reads,0,sizeof(guard_reads));
 for(unsigned i=0;i<4;i++)memcpy(map(guard_address[i],16),guard_bytes[i],16);
 map(0x1c8981c,4);put32(0x1c8981c,0x3100000);
 map(0x3100000,0x3c);put32(0x3100038,0x3101000);map(0x3101000,0x14);
 for(unsigned deck=0;deck<2;deck++){
  uint32_t f=frame(deck),t=total(deck);map(f,FRAME_SIZE);map(t,0x28);map(title_record(deck),0x224);
  put32(0x310100c+deck*4,f);put32(f+4,2);put32(f+8,240000+deck*1000);put32(f+0xc,32000+deck*1000);
  put32(f+0x14,12500+deck*100);put32(f+0x18,12800);put16(f+0x22,(unsigned)-125);
  *at(f+0x20,1)=2;put16(f+0x24,3);put16(f+0x26,2);
  *at(f+0x2d,1)=0xc4;*at(f+0x2e,1)=0x82;*at(f+0x2f,1)=(unsigned char)(deck?8:4);
  put16(f+0x42,1);put32(f+0x44,2200);put32(f+0x48,4400);
  for(unsigned cue=0;cue<8;cue++){
   unsigned off=0x34*cue;put16(f+0x76+off,cue&1);put32(f+0x78+off,100+cue*1000);
   put32(f+0x7c+off,cue&1?500+cue*1000:0);
   *at(f+0xa4+off,1)=(unsigned char)(0x10+cue);*at(f+0xa5+off,1)=0x20;*at(f+0xa6+off,1)=0x30;
  }
  put32(f+0x268,(uint32_t)-16);put32(t,6);put32(t+8,0x10200);put32(t+0xc,1000+deck);
  put32(t+0x10,21+deck);put32(t+0x24,1);set_title(deck);
 }
 map(0x1c8a404,4);put32(0x1c8a404,0x3200000);map(0x3200000,0x170);
 put32(0x3200164,0x3201000);put32(0x320016c,5);map(0x3201000,0x8c*5);
 static const uint32_t ids[5]={0x62,0x10000003,0x10000004,0x63,0x69};
 static const uint32_t values[5]={0x31,0x101,0x101,1,1};
 for(unsigned i=0;i<5;i++){put32(0x3201000+i*0x8c,ids[i]);put32(0x3201088+i*0x8c,values[i]);}
 map(0x3eaff7c,4);put32(0x3eaff7c,0x00120144);
}
struct guarded_storage {unsigned char *allocation;struct xz_shell_model_storage *storage;size_t size;};
static struct guarded_storage allocate(void){
 size_t size=xz_shell_model_storage_size();assert(size>0&&size%4==0);
 unsigned char *p=calloc(1,size+64);assert(p);memset(p,0xa5,32);memset(p+32+size,0x5a,32);
 return (struct guarded_storage){p,(struct xz_shell_model_storage *)(void *)(p+32),size};
}
static void release(struct guarded_storage *s){
 for(unsigned i=0;i<32;i++){assert(s->allocation[i]==0xa5);assert(s->allocation[32+s->size+i]==0x5a);}
 free(s->allocation);
}
static void assert_native(const struct xz_shell_model *m){
 uint64_t expected=K(LOADED)|K(TITLE)|K(TRACK_NO)|K(BPM)|K(TRACK_BPM)|K(TEMPO)|K(MASTER_TEMPO)|K(MASTER)|K(SYNC)|
  K(ELAPSED)|K(TOTAL)|K(TIME_MODE)|K(QUANTIZE)|K(BEAT_JUMP)|K(LOOP)|K(HOT_CUES)|K(CUE_POINT);
 for(unsigned deck=0;deck<2;deck++){
  const struct xz_shell_deck *d=m->deck+deck;assert(d->known==expected);assert(d->loaded==1);
  assert(!strcmp(d->title,"DJ \xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80"));assert(d->track_no==(int)(21+deck));
  assert(d->bpm_x100==(int32_t)(12500+deck*100)&&d->track_bpm_x100==12800&&d->tempo_x100==-125);
  assert(d->master_tempo==1&&d->master==(int)!deck&&d->sync==(int)deck);
  assert(d->elapsed_ms==(int32_t)(32000+deck*1000)&&d->total_ms==(int32_t)(240000+deck*1000));
  assert(d->time_mode==XZ_SHELL_TIME_REMAIN&&d->quantize_on==1&&d->quantize_num==1&&d->quantize_den==4);
  assert(d->beat_jump_num==-16&&d->beat_jump_den==1);
  assert(d->loop.active&&d->loop.beats_num==3&&d->loop.beats_den==2&&d->loop.in_ms==2200&&d->loop.out_ms==4400&&d->cue_ms==2200);
  for(unsigned cue=0;cue<8;cue++){
   assert(d->hot_cue[cue].set&&d->hot_cue[cue].ms==(int32_t)(100+cue*1000));
   assert(d->hot_cue[cue].rgb==((0x10+cue)<<16|0x2030)&&d->hot_cue[cue].is_loop==(int)(cue&1));
  }
  assert(!d->artist&&!d->artwork&&!d->memory_count&&!d->key&&!d->grid);
 }
 assert(m->settings.known==(S(TIME_MODE)|S(AUTO_CUE)));assert(m->settings.time_mode==XZ_SHELL_TIME_REMAIN&&m->settings.auto_cue);
}
static void test_valid_and_input(void){
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};
 m.capabilities=XZ_SHELL_CAP_BROWSE;m.link_known=1;m.link_connected=1;
 assert(xz_shell_model_refresh(&m,s.storage,read_memory,123)==3);assert_native(&m);assert(m.clock_ms==123);
 assert(m.capabilities==XZ_SHELL_CAP_BROWSE&&m.link_known&&m.link_connected);
 assert(xz_shell_model_caution(s.storage)==0x144);uint32_t diagnostic[4];xz_shell_model_diagnostics(s.storage,diagnostic);
 for(unsigned i=0;i<4;i++)assert(!diagnostic[i]&&guard_reads[i]==1);
 for(int deck=0;deck<2;deck++)for(int cue=0;cue<8;cue++){
  int32_t out=-1;assert(xz_shell_model_hot_cue_out(s.storage,deck,cue,&out));assert(out==(cue&1?500+cue*1000:0));
 }
 assert(xz_shell_model_refresh(&m,s.storage,read_memory,124)==0);
 char key[]="8A";struct xz_shell_beat grid[2]={{0,1},{500,2}};
 struct xz_shell_deck input={.known=K(LOADED)|K(PLAYING)|K(KEY)|K(KEY_SHIFT)|K(GRID)|K(BAR_PHASE)|K(SOURCE)|K(ARTIST),
  .loaded=1,.playing=1,.key=key,.key_matches_master=1,.key_shift=-2,.grid=grid,.grid_count=2,.bar_phase=3};
 xz_shell_model_set_input(s.storage,0,&input,2);xz_shell_model_set_input(s.storage,1,&input,3);
 key[0]='9';grid[0].ms=999;
 assert(!xz_shell_model_refresh(&m,s.storage,read_memory,125));
 for(int deck=0;deck<2;deck++){
  const struct xz_shell_deck *d=m.deck+deck;assert((d->known&input.known)==(input.known&~K(ARTIST)));
  assert(d->loaded&&d->playing&&!strcmp(d->key,"8A")&&d->key_matches_master&&d->key_shift==-2);
  assert(d->grid_count==2&&d->grid[0].ms==0&&d->grid[1].beat_in_bar==2&&d->bar_phase==3);
  assert(d->source==(deck?XZ_SHELL_SRC_USB2:XZ_SHELL_SRC_USB1));
 }
 assert(!xz_shell_model_refresh(&m,s.storage,read_memory,126));assert_native(&m);
 input.grid_count=GRID_MAX+1;input.bar_phase=5;input.key=NULL;
 xz_shell_model_set_input(s.storage,0,&input,99);assert(!xz_shell_model_refresh(&m,s.storage,read_memory,127));
 assert(!(m.deck[0].known&(K(KEY)|K(GRID)|K(BAR_PHASE)|K(SOURCE))));
 struct xz_shell_beat *full=calloc(GRID_MAX,sizeof(*full));assert(full);
 for(unsigned i=0;i<GRID_MAX;i++){full[i].ms=(int32_t)(i*500);full[i].beat_in_bar=(uint8_t)(i%4+1);}
 char long_key[32];memset(long_key,'A',31);long_key[31]=0;
 input.known=K(GRID)|K(KEY);input.grid=full;input.grid_count=GRID_MAX;input.key=long_key;
 xz_shell_model_set_input(s.storage,0,&input,0);free(full);
 xz_shell_model_refresh(&m,s.storage,read_memory,128);
 assert(m.deck[0].grid_count==GRID_MAX&&m.deck[0].grid[GRID_MAX-1].ms==(GRID_MAX-1)*500);
 assert(strlen(m.deck[0].key)==31);
 xz_shell_model_set_input(s.storage,0,NULL,0);release(&s);
 puts("PASS native fields, every supported known bit, UTF-16, copied/consumed input, source mapping");
}
static void test_guards(void){
 for(unsigned family=0;family<4;family++){
  fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};
  *at(guard_address[family],1)^=1;xz_shell_model_refresh(&m,s.storage,read_memory,0);
  uint32_t diagnostic[4];xz_shell_model_diagnostics(s.storage,diagnostic);
  for(unsigned i=0;i<4;i++)assert(diagnostic[i]==(i==family)&&guard_reads[i]==1);
  if(family==0){assert(!(m.deck[0].known&(K(TOTAL)|K(ELAPSED)|K(BPM)|K(LOOP)|K(TRACK_NO))));assert(!(m.settings.known&S(AUTO_CUE)));}
  if(family==1){assert(!(m.deck[0].known&(K(QUANTIZE)|K(TIME_MODE))));assert(!(m.settings.known&S(TIME_MODE)));}
  if(family==2)assert(!(m.deck[0].known&(K(TITLE)|K(LOADED))));
  if(family==3)assert(xz_shell_model_caution(s.storage)==UINT32_MAX);
  xz_shell_model_refresh(&m,s.storage,read_memory,1);xz_shell_model_diagnostics(s.storage,diagnostic);assert(diagnostic[family]==2);
  release(&s);
 }
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};fail_address=guard_address[0];
 xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(TOTAL)));release(&s);
 puts("PASS each guard mismatch/read failure fails its family closed and counts once per refresh");
}
static void test_torn_and_pointers(void){
 const uint32_t cells[]={0x3000014,0x3000022,0x2247cac+0x161c,0x224dd4c+0x22,0x3201088,0x320016c,0x3100038,0x3eaff7c};
 for(unsigned test=0;test<sizeof(cells)/sizeof(*cells);test++){
  fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};torn_address=cells[test];
  xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(read_calls<250&&torn_reads<=12);
  if(test==0||test==1||test==2||test==6)assert(!(m.deck[0].known&(K(TOTAL)|K(BPM)|K(LOOP)|K(TRACK_NO))));
  if(test==3)assert(!(m.deck[0].known&K(TITLE)));
  if(test==4||test==5)assert(!(m.deck[0].known&(K(TIME_MODE)|K(QUANTIZE))));
  if(test==7)assert(xz_shell_model_caution(s.storage)==UINT32_MAX);
  release(&s);
 }
 const uint32_t bad[]={0,1,0x1000,0x10001,0xfffffff0,0x33330000};
 const uint32_t ptrs[]={0x1c8981c,0x3100038,0x310100c};
 for(unsigned p=0;p<3;p++)for(unsigned v=0;v<sizeof(bad)/sizeof(*bad);v++){
  fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};put32(ptrs[p],bad[v]);
  xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&(K(TOTAL)|K(BPM)|K(LOOP)|K(TRACK_NO))));release(&s);
 }
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};put32(0x320016c,513);
 xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(TIME_MODE)));
 put32(0x320016c,5);put32(0x3200164,0xfffffff0);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(QUANTIZE)));
 release(&s);puts("PASS torn frames/title/settings/caution, bounded retries, null/unaligned/overflow/unmapped pointer chains");
}
static void wave(struct xz_shell_model_storage *s){
 struct xz_shell_column cols[2]={{1,2,3},{4,5,6}};xz_shell_model_set_wave(s,0,cols,2,240000,cols,2,240000);
 cols[0].low=99;
}
static void test_identity_and_waves(void){
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};assert(xz_shell_model_refresh(&m,s.storage,read_memory,0)==3);
 wave(s.storage);assert(!xz_shell_model_refresh(&m,s.storage,read_memory,1));
 assert((m.deck[0].known&(K(WAVE)|K(OVERVIEW)))==(K(WAVE)|K(OVERVIEW)));
 assert(m.deck[0].wave.count==2&&m.deck[0].wave.columns[0].low==1&&m.deck[0].wave.span_ms==240000);
 for(unsigned part=0;part<3;part++){
  if(part==0)put16(title_record(0)+0x22,'X');
  if(part==1)put32(frame(0)+8,250000);
  if(part==2)put32(total(0)+0x10,999);
  assert(xz_shell_model_refresh(&m,s.storage,read_memory,part+2)==1);
  assert(!(m.deck[0].known&(K(WAVE)|K(OVERVIEW))));wave(s.storage);
  assert(!xz_shell_model_refresh(&m,s.storage,read_memory,9));assert(m.deck[0].known&K(WAVE));
 }
 /* A torn frame republishes the last complete snapshot (read at 9) for 250 ms, then fails closed. */
 torn_address=frame(0)+0x14;assert(!xz_shell_model_refresh(&m,s.storage,read_memory,10));
 assert((m.deck[0].known&(K(WAVE)|K(LOADED)|K(TITLE)|K(ELAPSED)))==(K(WAVE)|K(LOADED)|K(TITLE)|K(ELAPSED)));
 assert(m.deck[0].elapsed_at_ms==9&&m.deck[0].title&&m.deck[0].title[0]==0x58&&xz_shell_model_holds(s.storage)==1);
 assert(m.deck[1].elapsed_at_ms==10);
 assert(!xz_shell_model_refresh(&m,s.storage,read_memory,9+250));assert(!(m.deck[0].known&(K(WAVE)|K(LOADED))));
 torn_address=0;assert(!xz_shell_model_refresh(&m,s.storage,read_memory,260));assert(m.deck[0].known&K(WAVE));
 /* Playback moves the frame between the two bracket reads: the snapshot stays complete. */
 uint32_t before=(uint32_t)m.deck[0].elapsed_ms;bump_elapsed=1;
 assert(!xz_shell_model_refresh(&m,s.storage,read_memory,261));bump_elapsed=0;
 assert((m.deck[0].known&(K(WAVE)|K(LOADED)|K(TITLE)))==(K(WAVE)|K(LOADED)|K(TITLE))&&m.deck[0].elapsed_ms==(int32_t)before);
 assert(xz_shell_model_holds(s.storage)==1);
 /* Loading suppresses stale title and retained waves; it is not an unload. */
 put32(total(0),7);assert(!xz_shell_model_refresh(&m,s.storage,read_memory,12));assert(!(m.deck[0].known&(K(LOADED)|K(TITLE)|K(WAVE))));
 put32(total(0),6);assert(!xz_shell_model_refresh(&m,s.storage,read_memory,13));assert(m.deck[0].known&K(WAVE));
 put32(total(0)+0xc,0);put32(frame(0)+8,0);put16(title_record(0)+0x20,0);
 assert(xz_shell_model_refresh(&m,s.storage,read_memory,14)==1);assert((m.deck[0].known&K(LOADED))&&!m.deck[0].loaded);
 assert(!(m.deck[0].known&(K(WAVE)|K(OVERVIEW))));
 set_title(0);put32(total(0)+0xc,1000);put32(frame(0)+8,240000);assert(xz_shell_model_refresh(&m,s.storage,read_memory,15)==1);
 size_t count=SCROLL_MAX+19;struct xz_shell_column *many=calloc(count,sizeof(*many));assert(many);many[count-1]=(struct xz_shell_column){255,254,253};
 xz_shell_model_set_wave(s.storage,0,many,count,240000,many,count,240000);free(many);
 assert(!xz_shell_model_refresh(&m,s.storage,read_memory,16));assert(m.deck[0].wave.count==SCROLL_MAX&&m.deck[0].overview.count==OVERVIEW_MAX);
 assert(m.deck[0].wave.columns[SCROLL_MAX-1].low==255&&m.deck[0].overview.columns[OVERVIEW_MAX-1].high==253);
 xz_shell_model_set_wave(s.storage,0,NULL,SIZE_MAX,1,NULL,SIZE_MAX,1);xz_shell_model_refresh(&m,s.storage,read_memory,17);
 assert(!(m.deck[0].known&(K(WAVE)|K(OVERVIEW))));
 struct xz_shell_column tiny={1,2,3};xz_shell_model_set_wave(s.storage,0,&tiny,SIZE_MAX,1,&tiny,SIZE_MAX,1);
 xz_shell_model_set_wave(s.storage,0,&tiny,1000001,1,&tiny,1000001,1);
 xz_shell_model_set_wave(s.storage,-1,&tiny,1,1,&tiny,1,1);xz_shell_model_set_wave(s.storage,2,&tiny,1,1,&tiny,1,1);
 release(&s);puts("PASS title/total/track identity changes, load/unload, wave invalidation, copied/capped whole-span waves, storage guard words");
}
static void test_flag_encodings(void){
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};
 for(unsigned index=0;index<4;index++){
  put32(0x3201088,0x33-index);xz_shell_model_refresh(&m,s.storage,read_memory,0);
  assert((m.deck[0].known&K(QUANTIZE))&&m.deck[0].quantize_den==(1<<(int)index));
 }
 *at(frame(0)+0x2f,1)=2;*at(frame(0)+0x2d,1)=0x80;
 xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!m.deck[0].loop.active&&!m.deck[0].master_tempo&&!m.deck[0].master&&!m.deck[0].sync);
 assert(!(m.deck[0].known&K(CUE_POINT)));
 put16(frame(0)+0x22,0x8000);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(m.deck[0].tempo_x100==-32768);
 put16(frame(0)+0x22,0x7fff);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(m.deck[0].tempo_x100==32767);
 release(&s);puts("PASS quantize encoding, loop inhibit, cue-exist flag, signed tempo extrema");
}
static void test_invalid_data(void){
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};
 put16(title_record(0)+0x20,257);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(TITLE)));
 put16(title_record(0)+0x20,256);for(unsigned i=0;i<256;i++)put16(title_record(0)+0x22+i*2,0x4e2d);
 xz_shell_model_refresh(&m,s.storage,read_memory,0);assert((m.deck[0].known&K(TITLE))&&strlen(m.deck[0].title)==768);
 put16(title_record(0)+0x22,0xdc00);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(TITLE)));
 set_title(0);put16(title_record(0)+0x20,1);put16(title_record(0)+0x22,0xd800);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(TITLE)));
 set_title(0);put32(frame(0)+8,UINT32_MAX);put32(frame(0)+0x14,UINT32_MAX);put16(frame(0)+0x26,0);put32(frame(0)+0x78,UINT32_MAX);
 xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&(K(TOTAL)|K(BPM)|K(LOOP)|K(HOT_CUES))));
 int32_t out=0;assert(!xz_shell_model_hot_cue_out(s.storage,0,0,&out));
 fixture();put32(0x3201088+0x8c*2,0x100);*at(frame(1)+0x2e,1)=0x80;
 xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(m.deck[1].time_mode==XZ_SHELL_TIME_ELAPSED);
 assert(!(m.settings.known&(S(TIME_MODE)|S(AUTO_CUE))));
 put32(total(0)+0x24,0);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(TRACK_NO)));
 put32(0x3201000+0x8c*4,0x62);xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!(m.deck[0].known&K(QUANTIZE)));
 xz_shell_model_refresh(&m,s.storage,NULL,0);assert(!m.deck[0].known&&!m.deck[1].known&&!m.settings.known);
 assert(xz_shell_model_caution(s.storage)==UINT32_MAX);assert(!xz_shell_model_refresh(NULL,s.storage,read_memory,0));
 assert(!xz_shell_model_refresh(&m,NULL,read_memory,0));assert(xz_shell_model_caution(NULL)==UINT32_MAX);
 xz_shell_model_set_input(NULL,0,NULL,0);xz_shell_model_set_wave(NULL,0,NULL,0,0,NULL,0,0);
 release(&s);puts("PASS maximum/malformed UTF-16, invalid numeric cells, settings disagreement/duplicates, unavailable reads");
}
static void test_fx(void){
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};
 xz_shell_model_refresh(&m,s.storage,read_memory,0);assert(!m.fx.known&&xz_shell_model_fx_failures(s.storage)==1);
 static const unsigned char entry[16]={0x66,0x1f,0x81,0xe2,0x00,0x30,0xa0,0xe1,0x07,0x00,0x91,0xe8,0x07,0x00,0x83,0xe8};
 static const struct { uint32_t at; unsigned char code[4]; } loads[5]={
  {0xc9044,{0x06,0x00,0xdd,0xe5}},{0xc91e4,{0x07,0x00,0xdd,0xe5}},{0xc915c,{0x0b,0x00,0xdd,0xe5}},
  {0xc91a0,{0xfc,0x00,0xdd,0xe1}},{0xc90cc,{0xbe,0x00,0xdd,0xe1}}};
 memcpy(map(0x25c914,16),entry,16);for(unsigned i=0;i<5;i++)memcpy(map(loads[i].at,4),loads[i].code,4);
 map(0x3100050,4);put32(0x3100050,0x3300000);
 /* The record the parent read live: SPIRAL, off, beat 1, 500 ms, 102.2 BPM. */
 static const unsigned char live[12]={0x00,0x05,0x03,0x00,0x11,0x01,0x00,0x06,0xf4,0x01,0xfe,0x03};
 memcpy(map(0x3300198,12),live,12);
 const uint32_t base=(1u<<XZ_SHELL_FX_F_ON)|(1u<<XZ_SHELL_FX_F_BPM)|(1u<<XZ_SHELL_FX_F_TAP);
 const uint32_t known=base|(1u<<XZ_SHELL_FX_F_NAME)|(1u<<XZ_SHELL_FX_F_BEAT)|(1u<<XZ_SHELL_FX_F_TIME);
 xz_shell_model_refresh(&m,s.storage,read_memory,1);
 assert(m.fx.known==known&&!m.fx.on&&!strcmp(m.fx.name,"SPIRAL")&&m.fx.beat_num==1&&m.fx.beat_den==1&&m.fx.time_ms==500);
 assert(m.fx.bpm_x10==1022&&!m.fx.tap);
 assert(xz_shell_model_fx_failures(s.storage)==1);
 *at(0x3300198+3,1)=0x11;*at(0x3300198+7,1)=5;*at(0x3300198+2,1)=1;xz_shell_model_refresh(&m,s.storage,read_memory,2);
 assert(m.fx.known==known&&m.fx.on&&!strcmp(m.fx.name,"ECHO")&&m.fx.beat_num==3&&m.fx.beat_den==4);
 /* REVERB and PITCH: percent parameter and a dashed beat, so only ON and the name. */
 *at(0x3300198+2,1)=4;xz_shell_model_refresh(&m,s.storage,read_memory,3);
 assert(m.fx.known==(base|(1u<<XZ_SHELL_FX_F_NAME))&&!strcmp(m.fx.name,"REVERB"));
 *at(0x3300198+2,1)=9;xz_shell_model_refresh(&m,s.storage,read_memory,4);assert(!strcmp(m.fx.name,"PITCH")&&!(m.fx.known&(1u<<XZ_SHELL_FX_F_TIME)));
 *at(0x3300198+2,1)=14;*at(0x3300198+6,1)=1;xz_shell_model_refresh(&m,s.storage,read_memory,5);assert(m.fx.known==base&&m.fx.tap);
 put16(0x3300198+10,0);xz_shell_model_refresh(&m,s.storage,read_memory,5);assert(!(m.fx.known&(1u<<XZ_SHELL_FX_F_BPM)));put16(0x3300198+10,1022);
 *at(0x3300198+2,1)=13;*at(0x3300198+7,1)=13;xz_shell_model_refresh(&m,s.storage,read_memory,6);
 assert(!strcmp(m.fx.name,"HELIX")&&!(m.fx.known&(1u<<XZ_SHELL_FX_F_BEAT))&&(m.fx.known&(1u<<XZ_SHELL_FX_F_TIME)));
 assert(!(m.fx.known&((1u<<XZ_SHELL_FX_F_CHANNEL)|(1u<<XZ_SHELL_FX_F_LEVEL))));
 torn_address=0x3300198+8;xz_shell_model_refresh(&m,s.storage,read_memory,7);assert(!m.fx.known);torn_address=0;
 put32(0x3100050,0x3300001);xz_shell_model_refresh(&m,s.storage,read_memory,8);assert(!m.fx.known);
 release(&s);puts("PASS BEAT FX: live record, names, beat table, REVERB/PITCH blanks, unknown effect, torn record, bad root, cached guard");
}
static void test_settings_bulk(void){
 fixture();struct guarded_storage s=allocate();struct xz_shell_model m={0};
 map(0x3204000,0x8c*40);put32(0x3200164,0x3204000);put32(0x320016c,40);
 for(unsigned i=0;i<40;i++)put32(0x3204000+i*0x8c,0x7000+i);
 put32(0x3204000+30*0x8c,0x10000003);put32(0x3204000+30*0x8c+0x88,0x100);
 put32(0x3204000+31*0x8c,0x10000004);put32(0x3204000+31*0x8c+0x88,0x100);
 put32(0x3204000+35*0x8c,0x62);put32(0x3204000+35*0x8c+0x88,0x30);
 xz_shell_model_refresh(&m,s.storage,read_memory,0);read_calls=0;xz_shell_model_refresh(&m,s.storage,read_memory,1);
 assert((m.deck[0].known&(K(TIME_MODE)|K(QUANTIZE)))==(K(TIME_MODE)|K(QUANTIZE))&&m.deck[0].time_mode==XZ_SHELL_TIME_ELAPSED&&m.deck[0].quantize_den==8);
 assert(m.settings.known&S(TIME_MODE));
 fprintf(stderr,"  reads per refresh with 40 settings: %u\n",read_calls);assert(read_calls<90);
 torn_address=0x3204000+35*0x8c+0x88;xz_shell_model_refresh(&m,s.storage,read_memory,2);assert(!(m.deck[0].known&(K(TIME_MODE)|K(QUANTIZE))));
 release(&s);puts("PASS settings list read in bulk across chunk boundaries, torn chunk fails closed");
}
int main(void){
 test_valid_and_input();test_guards();test_torn_and_pointers();test_identity_and_waves();test_flag_encodings();test_invalid_data();
 test_fx();test_settings_bulk();
 puts("shell_model: all host acceptance tests passed; no hardware/device/network access");return 0;
}
