/* Portable integration fixture: no firmware calls, hooks, files or hardware. */
#include "native_skin_runtime.c"
#include <assert.h>
#include <errno.h>
#include <sched.h>

int xz_hook_arm(uint32_t a,const unsigned char b[8],void*c,void**d){(void)a;(void)b;(void)c;(void)d;abort();}
int xz_hook_slot(uint32_t a,uint32_t b,void*c,void**d){(void)a;(void)b;(void)c;(void)d;abort();}
int xz_read_memory(uint32_t a,void*b,size_t c){(void)a;(void)b;(void)c;abort();}
void xz_log(const char *s){(void)s;abort();}
void xz_native_raw_skin_repaint(void){abort();}
int xz_native_window_keys_start(void){abort();}
static int fixture_key_state;static uint16_t fixture_key;
int xz_native_window_key(uintptr_t surface,uint16_t *key){(void)surface;if(key)*key=fixture_key;return fixture_key_state;}

static unsigned repaints;
static void repaint_spy(void){repaints++;}
static pthread_t reclaimer;
static atomic_int reclaim_entered,reclaim_finished;
static unsigned char source_descriptor[68],source_before[68];
static unsigned char *inflight_pack;
static unsigned char fixture_core[32];
static void *reclaim_spy(void *unused){
 (void)unused;assert(!active_map());atomic_store(&reclaim_entered,1);
 release_retired(take_retired());atomic_store(&reclaim_finished,1);return NULL;
}
static uint32_t image_spy(void *core,const void *descriptor,const void *point){
 assert(core==fixture_core&&point==(void*)2);
 assert(descriptor!=source_descriptor);
 assert(xz_asset_u32((const unsigned char*)descriptor+12)==(uint32_t)(uintptr_t)inflight_pack+PACK_COUNT*44);
 assert(memcmp(source_descriptor,source_before,68)==0);
 assert(pthread_mutex_trylock(&cache_mutex)==EBUSY);
 assert(pthread_create(&reclaimer,NULL,reclaim_spy,NULL)==0);
 while(!atomic_load(&reclaim_entered))sched_yield();
 assert(!atomic_load(&reclaim_finished));
 assert(active_snapshot->pack==inflight_pack);
 return 0x7ac1;
}
static uint32_t fallback_spy(void *core,const void *descriptor,const void *point){
 assert(core==fixture_core&&point==(void*)2);assert(descriptor==source_descriptor);return 0x3abc;
}
static uint32_t keyed_spy(void *core,const void *descriptor,const void *point){
 assert(core==fixture_core&&point==(void*)2&&descriptor!=source_descriptor);
 assert(xz_asset_u32((const unsigned char*)descriptor+12)==(uint32_t)(uintptr_t)keyed_image);
 assert(keyed_image[0]==0);return 0x4abc;
}
static uint16_t sidebar_buffer[132*295+2];static int sidebar_unlocks,sidebar_lock_result=1;
static int fill_spy(void *core,const void *rect){(void)core;(void)rect;return 1;}
static int lock_spy(void *core,void **pixels,int *pitch){(void)core;*pixels=sidebar_buffer+1;*pitch=264;return sidebar_lock_result;}
static int unlock_spy(void *core){(void)core;sidebar_unlocks++;return 1;}
static int text_spy(void *core,unsigned font,const char *text,uint32_t color,const int *point){
 (void)core;(void)font;(void)text;(void)color;assert(point[1]==5&&title_ink==1);
 assert((xz_mods_native_surface_color_v1(0x1234,0xffffffff)&0xffffff)==0);return 37;
}
static int expected_title_mode;static uint32_t expected_title_paper,expected_title_ink;
static int semantic_text_spy(void *core,unsigned font,const char *text,uint32_t color,const int *point){
 (void)core;(void)font;(void)text;(void)color;(void)point;
 assert(title_ink==expected_title_mode);
 if(title_ink==1){
  assert(title_background==expected_title_paper&&title_foreground==expected_title_ink);
  uint32_t expected=xz_native_title_rgba(0xffffffff,expected_title_ink,fixture_key_state,fixture_key);
  assert(xz_mods_native_surface_color_v1(0x1234,0xffffffff)==expected);
 }else assert(xz_mods_native_surface_color_v1(0x1234,0xffffffff)==rgba_map_keyed(0xffffffff,active_map(),fixture_key_state,fixture_key));
 return 38;
}
static void init_fixture(void){
 atomic_store(&running,1);atomic_store(&status_code,2);
 owner_thread=pthread_self();atomic_store(&owner_ready,1);
 pack_size=PACK_COUNT*44+PACK_COUNT*2;original_pack=calloc(1,pack_size);assert(original_pack);
 original_base=0x10000000;
 for(unsigned i=0;i<PACK_COUNT;i++){
  unsigned char *e=original_pack+i*44;e[4]=1;e[6]=1;
  xz_asset_put32(e+24,2);xz_asset_put32(e+28,i*44);xz_asset_put32(e+32,PACK_COUNT*44+i*2);
  xz_asset_put32(e+36,(uint32_t)pack_size);original_pack[PACK_COUNT*44+i*2]=(unsigned char)i;
 }
}
static void assert_lut(const uint16_t *lut,int theme){
 assert(lut);for(unsigned p=0;p<65536;p++)assert(lut[p]==(p==0xf81f?p:xz_native_palette_pixel(theme,(uint16_t)p)));
}
static void assert_active_map(int theme){
 assert(active_map()==active_snapshot->lut&&atomic_load(&active_theme)==theme);assert_lut(active_map(),theme);
}
static struct skin_snapshot *request_and_prepare(int theme){
 xz_native_skin_request(theme);return prepare(theme,request_generation);
}
int main(void){
 init_fixture();
 for(int t=0;t<XZ_THEME_COUNT;t++){
  struct skin_snapshot *s=prepare(t,0);assert(s&&(t?s->pack!=NULL:!s->pack&&!s->lut&&!s->wave_lut));
  if(t)assert_lut(s->lut,t);
  if(xz_native_wave_style_has_paper(t)){
   assert(s->wave_lut);
   assert(s->wave_lut[0]==xz_native_wave_style_paper_at(t,268,67,536,134));
   assert(s->wave_lut[65535]==xz_native_wave_style_pixel(t,65535));
  }else if(t!=XZ_THEME_LCARS)assert(!s->wave_lut);
  free_snapshot(s);
 }
 unsigned char *source_copy=malloc(pack_size);assert(source_copy);memcpy(source_copy,original_pack,pack_size);
 struct skin_snapshot *a=request_and_prepare(7);assert(a&&publish_snapshot(a));
 assert(strcmp(xz_native_skin_status(),"Preparing native theme")==0);
 /* An idle native epoch, with no input gesture or message, consumes the request. */
 owner_epoch(repaint_spy);assert(repaints==1&&atomic_load(&active_theme)==7);assert_active_map(7);
 assert(strcmp(xz_native_skin_status(),"Native theme ready")==0);
 owner_epoch(repaint_spy);assert(repaints==1);
 unsigned char *first_a=malloc(pack_size);assert(first_a);memcpy(first_a,active_snapshot->pack,pack_size);
 struct skin_snapshot *b=request_and_prepare(10);assert(b&&publish_snapshot(b));
 assert(atomic_load(&active_theme)==7);owner_epoch(repaint_spy);assert(repaints==2&&atomic_load(&active_theme)==10);
 assert_active_map(10);
 a=request_and_prepare(7);assert(a&&publish_snapshot(a));owner_epoch(repaint_spy);
 assert(repaints==3&&memcmp(first_a,active_snapshot->pack,pack_size)==0);
 assert(memcmp(original_pack,source_copy,pack_size)==0);
 /* Both supersession races: obsolete computation and obsolete published work. */
 b=request_and_prepare(10);assert(b);xz_native_skin_request(8);assert(!publish_snapshot(b));free_snapshot(b);
 b=prepare(8,request_generation);assert(b&&publish_snapshot(b));xz_native_skin_request(7);
 owner_epoch(repaint_spy);assert(repaints==3&&atomic_load(&active_theme)==7&&!pending_snapshot);
 assert(strcmp(xz_native_skin_status(),"Preparing native theme")==0);
 a=prepare(7,request_generation);assert(a&&publish_snapshot(a));owner_epoch(repaint_spy);assert(repaints==4);
 /* Original image draw sees only a cloned descriptor. Cache retirement waits. */
 memset(source_descriptor,0,68);xz_asset_put32(source_descriptor,5);source_descriptor[4]=1;source_descriptor[6]=1;
 source_descriptor[16]=2;xz_asset_put32(source_descriptor+12,original_base+PACK_COUNT*44);
 xz_asset_put32(source_descriptor+44,0x1234);xz_asset_put32(source_descriptor+48,0x5678);
 memcpy(source_before,source_descriptor,68);inflight_pack=active_snapshot->pack;stock_image=image_spy;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x7ac1);
 assert(pthread_join(reclaimer,NULL)==0&&atomic_load(&reclaim_finished));
 assert(active_snapshot->pack==inflight_pack&&memcmp(source_before,source_descriptor,68)==0);
 assert(memcmp(source_copy,original_pack,pack_size)==0);
 /* Unknown type/key inputs are passed untouched, preserving native callbacks. */
 fixture_key_state=1;fixture_key=0;stock_image=keyed_spy;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x4abc);
 for(unsigned alpha=0;alpha<256;alpha++)assert(xz_mods_native_surface_color_v1(0x1234,alpha<<24)==alpha<<24);
 assert(rgba565(rgba_map_keyed(0xffffffff,active_map(),1,0))!=0);
 assert((rgba_map_keyed(0x7fffffff,active_map(),1,0)>>24)==0x7f);
 fixture_key_state=-1;title_ink=1;
 assert(xz_mods_native_surface_color_v1(0x1234,0xffabcdef)==0xffabcdef);
 fixture_key_state=1;fixture_key=0xf81f;
 assert(xz_mods_native_surface_color_v1(0x1234,0xffff00ff)==0xffff00ff);
 title_ink=2;assert(xz_mods_native_surface_color_v1(0x1234,0xffff00ff)==0xffff00ff);title_ink=0;
 fixture_key_state=-1;stock_image=fallback_spy;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x3abc);
 assert(xz_mods_native_surface_color_v1(0x1234,0xff123456)==0xff123456);fixture_key_state=0;
 source_descriptor[0]=3;stock_image=fallback_spy;assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x3abc);
 source_descriptor[0]=5;source_descriptor[24]=1;source_descriptor[28]=0;
 assert(image_hook(fixture_core,source_descriptor,(void*)2)==0x3abc);
 struct skin_snapshot *last=request_and_prepare(XZ_THEME_COUNT-1);assert(last&&publish_snapshot(last));
 owner_epoch(repaint_spy);assert(repaints==5);assert_active_map(XZ_THEME_COUNT-1);
 struct skin_snapshot *lcars=request_and_prepare(XZ_THEME_LCARS);assert(lcars&&publish_snapshot(lcars));owner_epoch(repaint_spy);
 uint32_t sidebar_core[8]={128,295};int sidebar_rect[3]={0,0,128|(295<<16)};
 stock_fill_rect=fill_spy;lcars_lock=lock_spy;lcars_unlock=unlock_spy;
 for(unsigned i=0;i<132*295+2;i++)sidebar_buffer[i]=0x1234;
 assert(fill_rect_hook(sidebar_core,sidebar_rect)==1&&sidebar_unlocks==1);
 assert(sidebar_buffer[0]==0x1234&&sidebar_buffer[132*295+1]==0x1234);
 for(unsigned y=0;y<295;y++)for(unsigned x=128;x<132;x++)assert(sidebar_buffer[1+y*132+x]==0x1234);
 assert(sidebar_buffer[1+80*132+5]==xz_theme_rgb565(XZ_LCARS_NATIVE_AMBER));
 assert(sidebar_buffer[1+230*132+5]==0);
 assert(sidebar_buffer[1+233*132+5]==xz_theme_rgb565(XZ_LCARS_NATIVE_VIOLET));
 sidebar_lock_result=0;assert(fill_rect_hook(sidebar_core,sidebar_rect)==1&&sidebar_unlocks==1);
 uint32_t title_core[8]={400,172};int point[2]={32,5};native_title_record_core(title_core,649);stock_draw_text=text_spy;
 assert(draw_text_hook(title_core,0,"REAL TRACK",0xffffffff,point)==37&&!title_ink);
 /* Real draw scopes restore the prior role and never recolor body text. */
 stock_draw_text=semantic_text_spy;
 const int semantic_themes[]={9,14,13,11,16,17,18};
 for(unsigned i=0;i<sizeof(semantic_themes)/sizeof(*semantic_themes);i++){
  atomic_store(&active_theme,semantic_themes[i]);
  for(int layout=0;layout<2;layout++)for(int deck=0;deck<2;deck++){
   title_core[1]=layout?70:172;
   native_title_record_core(title_core,layout?(deck?726:725):(deck?687:649));
   assert(xz_native_title_colors(semantic_themes[i],deck,&expected_title_paper,&expected_title_ink));
   expected_title_mode=1;point[1]=5;
   assert(draw_text_hook(title_core,0,"REAL TRACK",0xffffffff,point)==38&&!title_ink);
   expected_title_mode=0;point[1]=51;
   assert(draw_text_hook(title_core,0,"120.0",0xffffffff,point)==38&&!title_ink);
  }
 }
 atomic_store(&active_theme,XZ_THEME_LCARS);
 uint32_t unrelated_core[8]={400,172};point[1]=5;expected_title_mode=0;
 assert(draw_text_hook(unrelated_core,0,"UNRELATED",0xffffffff,point)==38&&!title_ink);
 uint32_t wrong_layout[8]={400,70};native_title_record_core(wrong_layout,649);
 assert(native_title_deck_for_core(wrong_layout)==-1);
 struct skin_snapshot *stock=request_and_prepare(0);assert(stock&&!stock->pack&&!stock->lut&&publish_snapshot(stock));owner_epoch(repaint_spy);
 assert(repaints==7&&!active_map()&&xz_mods_native_color_v1(0xab123456)==0xab123456&&xz_mods_native_style_v1()==0);
 assert(memcmp(source_copy,original_pack,pack_size)==0);
 uint32_t cores[20][2];memset(native_title_cores,0,sizeof(native_title_cores));
 for(unsigned i=0;i<20;i++){cores[i][0]=400;cores[i][1]=172;native_title_record_core(cores[i],i&1?687:649);assert(native_title_deck_for_core(cores[i])==(int)(i&1));}
 xz_native_skin_forget_core((uintptr_t)cores[19]);assert(native_title_deck_for_core(cores[19])==-1);
 native_title_record_core(cores[19],649);assert(native_title_deck_for_core(cores[19])==0);
 free(first_a);free(source_copy);release_retired(take_retired());free_snapshot(active_snapshot);
 free(original_pack);puts("native skin integration: PASS (offline ownership, epochs, supersession, source integrity)");return 0;
}
