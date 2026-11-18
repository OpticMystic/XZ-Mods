#include "native.h"
#include "../runtime.h"
#include <string.h>
#if !defined(__arm__) || defined(__aarch64__)
#error Native adapter requires verified 32-bit ARM ABI
#endif
_Static_assert(sizeof(void*)==4,"XZ pointer ABI");
_Static_assert(sizeof(long)==4,"XZ long ABI");
static int32_t word(const void *p,unsigned off) { int32_t v; memcpy(&v,(const char*)p+off,4); return v; }
static void put(void *p,unsigned off,int32_t v) { memcpy((char*)p+off,&v,4); }
static int cue_ms(void *ctx,int deck,int kind,int32_t *at) {
 struct xz_cue_native *n=ctx; _Alignas(8) uint32_t out[0x338/4];
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]) return 0;
 ((void (*)(void*,void*,int,int))(uintptr_t)0x42fbc)(out,n->engine_if[deck],deck,kind);
 *at=word(out,0x318); return *at>=0;
}
static int pause_deck(void *ctx,int deck) {
 struct xz_cue_native *n=ctx;
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]) return 0;
 return ((int (*)(void*,int,int,int,int))(uintptr_t)0x3ff20)(n->engine_if[deck],deck,1,1,0);
}
static int seek(void *ctx,int deck,int32_t at) {
 struct xz_cue_native *n=ctx;
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]||at<0) return 0;
 return ((int (*)(void*,int,int32_t))(uintptr_t)0x40210)(n->engine_if[deck],deck,at);
}
static int memory_at(uint32_t base,unsigned offset,void *out,size_t size) {
 if(base<0x10000 || (base&3) || (uint64_t)base+offset+size>UINT32_MAX)return -1;
 return xz_read_memory(base+offset,out,size);
}
static uint32_t cue_player(int deck,uint32_t *reader) {
 uint32_t engine,range[2],player,check;unsigned char loaded;
 if(xz_read_memory(0x10e9688,&engine,4) || memory_at(engine,12,range,sizeof(range)) ||
    range[1]<range[0] || range[1]-range[0]!=8 || memory_at(range[0],(unsigned)deck*4,&player,4) ||
    memory_at(player,0xe66,&loaded,1) || !loaded || memory_at(player,0xc8,reader,4) || !*reader ||
    memory_at(range[0],(unsigned)deck*4,&check,4) || check!=player)return 0;
 return player;
}
static int follow(void *ctx,int deck,int32_t at) {
 struct xz_cue_native *n=ctx; _Alignas(8) uint32_t mem[0x338/4],common[0x338/4];uint32_t reader,check;
 if(!n->verified||deck<0||deck>1||!n->engine_if[deck]||at<0) return 0;
 uint32_t player=cue_player(deck,&reader);if(!player)return 0;
 ((void (*)(void*,void*,int,int))(uintptr_t)0x42fbc)(mem,n->engine_if[deck],deck,0);
 put(mem,0x318,at);
 /* Main cue follows a point; retaining a previous loop end can create a reversed loop. */
 put(mem,0x324,-1); ((unsigned char*)mem)[0x334]=0;
 /* setMemoryCueTime also cancels reservations and ends slip/audible pause.
    Smart Cue only updates the native cue position, in the player's sample units. */
 ((void (*)(void*,const void*))(uintptr_t)0x63a00)(common,mem);
 if(memory_at(player,0xc8,&check,4)||check!=reader)return 0;
 return ((int (*)(void*,int,const void*))(uintptr_t)0x63adc)((void*)(uintptr_t)player,0,common);
}
struct xz_cue_api xz_cue_native_api(struct xz_cue_native *n) {
 struct xz_cue_api a={n,cue_ms,pause_deck,seek,follow}; return a;
}
int xz_cue_native_decode(struct xz_cue_native *n,void *innards,const void *input,struct xz_cue_event *e) {
 uint16_t key; int mode; const unsigned char *p=input; const unsigned char *self=innards;
 if(!e) return 0;
 memset(e,0,sizeof(*e)); e->deck=-1; e->pad=-1; e->pad_page=-1; e->mode_button=-1;
 if(!n||!n->verified||!innards||!input) return 0;
 /* UiObjectManager constructs internal Player channels1 and2. */
 if(self[0x26]!=1 && self[0x26]!=2) return 0;
 e->deck=(int)self[0x26]-1;
 n->input_blocked[e->deck]=self[0x44]||self[0x45];
 memcpy(&n->engine_if[e->deck],self+0x30,4);
 memcpy(&key,p+8,2); e->operation=p[11]&15;
 mode=word(self,0x80);
 e->hotcue_mode=mode==0;
 e->pad_page=mode==0?0:(mode>=2&&mode<=4?mode-1:-1);
 e->shift=self[0x34]!=0;
 e->mode_button=key>=0x4114&&key<=0x4117?(int)key-0x4114:-1;
 e->sync=key==0x4313;
 e->pad=key>=0x4119&&key<=0x4120?(int)key-0x4119:-1;
 e->play=key==0x4101;
 return 1;
}

int xz_cue_native_beat_jump(struct xz_cue_native *n,int deck,int beats) {
 if(!n||!n->verified||deck<0||deck>1||!n->engine_if[deck]||n->input_blocked[deck]||!beats||beats < -256||beats > 256)return 0;
 /* XZ 1.26 uses signed beat counts. The native method retains its own
    track/buffer bounds, beat-grid, quantize and transport-state checks. */
 return ((int (*)(void*,int,int))(uintptr_t)0x444cc)(n->engine_if[deck],deck,beats);
}
