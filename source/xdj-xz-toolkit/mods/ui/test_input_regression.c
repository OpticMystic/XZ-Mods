/* SPDX-License-Identifier: MIT */
#define main existing_ui_tests
#include "test_runtime_controls.c"
#undef main
static uint32_t test_window=2;
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){
 uint32_t value;
 if(n==4&&address==0x3c6fc7c)value=test_window;
 else if(n==4&&address==0x1c888d0)value=0x10000;
 else if(n==1&&address>=0x222e36c&&address<0x222e36c+6*0xbc){*(uint8_t*)out=1;return 0;}
 else return -1;
 memcpy(out,&value,4);return 0;
}
static void reset_input(void){
 memset(&model,0,sizeof(model));memset(&pads,0,sizeof(pads));
 started=audio_available=requested_stems=native_active=native_play=1;
 model.enabled=XZ_UI_STEM;model.stems_overlay=XZ_STEM_LAYOUT_BELOW;
 arcade_present=arcade_touch_owned=0;settings_running=0;settings_readonly=1;
 for(int d=0;d<2;d++){fixture_pc_source[d]=(struct xz_pc_stem_source){.slot=-1,.epoch=1};fixture_pc_valid[d]=1;}
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);
 stock_touch=stock;stock_calls=0;inline_contact=inline_wave_contact=inline_cancelled=0;
 shortcut_contact=-1;head_contact=-1;head_shown=0;test_window=2;refresh();
}
static void scene_release(void){
 for(int layout=1;layout<XZ_STEM_LAYOUT_COUNT;layout++){
  reset_input();model.stems_overlay=layout;unsigned char self[32]={0};
  struct xz_wave_layout geometry;assert(xz_wave_layout_get(layout,&geometry));
  contact(self,240,18+geometry.lane_y[0]+20,1);assert(inline_wave_contact);
  /* Browse, waveform resize, or a modal changes the native scene before up.
     Its cleared-coordinate release must reach stock without an old mapping. */
  native_active=native_play=0;test_window=6;
  contact(self,0,0,0);
  if(last_stock.x||last_stock.y||last_stock.down){
   fprintf(stderr,"FAIL layout=%d: stock release was (%u,%u,%u), expected (0,0,0) after scene change\n",layout,last_stock.x,last_stock.y,last_stock.down);assert(0);
  }
  assert(!inline_contact&&!inline_wave_contact);
  int before=stock_calls;contact(self,715,274,1);contact(self,0,0,0);
  assert(stock_calls==before+2);
 }
 puts("PASS scene-change release and following stock waveform/Browse input across four layouts");
}
static void shortcut_drag(void){
 reset_input();unsigned char self[32]={0};test_window=29;native_play=native_active=0;
 contact(self,510,250,1);assert(shortcut_contact==2);
 contact(self,700,350,1);contact(self,0,0,0);
 if(model.wave_mode!=XZ_WAVE_STOCK){fprintf(stderr,"FAIL Shortcut: dragging out still committed waveform mode %d\n",model.wave_mode);assert(0);}
 test_window=29;contact(self,575,250,1);test_window=2;contact(self,0,0,0);
 assert(model.wave_mode==XZ_WAVE_STOCK&&shortcut_contact<0);
 test_window=29;unsigned before=fixture_repaints;
 contact(self,510,250,1);contact(self,0,0,0);
 assert(model.wave_mode==XZ_WAVE_THREE_BAND&&fixture_repaints==before+1);
 puts("PASS Shortcut drag/screen-change cancellation");
}
static void busy_buttons(void){
 static uint16_t normal[800*480],busy[800*480];
 for(int key=0;key<2;key++){
  reset_input();model.key_feedback=key;
  memset(normal,0,sizeof(normal));assert(xz_mods_render_v1(normal,800,480,800,NULL)==1);
  for(unsigned i=0;i<800*480;i++)busy[i]=0x1234;
  pthread_mutex_lock(&ui_mutex);
  int rendered=xz_mods_render_v1(busy,800,480,800,NULL);
  pthread_mutex_unlock(&ui_mutex);
  if(!rendered){
   fprintf(stderr,"FAIL busy render with key feedback %d dropped top-right STEMS/MODS buttons\n",key);assert(0);
  }
  for(unsigned y=0;y<24;y++)for(unsigned x=676;x<800;x++)
   if(normal[y*800+x])assert(normal[y*800+x]==busy[y*800+x]);
  test_window=29;pthread_mutex_lock(&ui_mutex);
  assert(!xz_mods_render_v1(busy,800,480,800,NULL));pthread_mutex_unlock(&ui_mutex);test_window=2;
  /* Cached buttons never draw over an open mod panel or takeover. */
  __atomic_store_n(&visible,1,__ATOMIC_RELEASE);pthread_mutex_lock(&ui_mutex);
  assert(!xz_mods_render_v1(busy,800,480,800,NULL));pthread_mutex_unlock(&ui_mutex);
  __atomic_store_n(&visible,0,__ATOMIC_RELEASE);__atomic_store_n(&fb_takeover_active,1,__ATOMIC_RELEASE);
  pthread_mutex_lock(&ui_mutex);assert(!xz_mods_render_v1(busy,800,480,800,NULL));pthread_mutex_unlock(&ui_mutex);
  __atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);
 }
 puts("PASS actual busy renderer retains top-right buttons with key feedback on/off and respects panel/takeover");
}
static void paint_only_redraw(void){
 reset_input();unsigned char self[32]={0};
 fixture_pc_metadata_redraws=fixture_repaints=0;
 for(int i=0;i<10;i++){contact(self,700,10,1);contact(self,0,0,0);}
 if(fixture_pc_metadata_redraws){
  fprintf(stderr,"FAIL stem layout repaint requested database/title metadata %u times\n",fixture_pc_metadata_redraws);assert(0);
 }
 assert(fixture_repaints==10);
 puts("PASS ten layout changes request GUI paint only, with zero native database/title requests");
}
static void ram_debug_box(void){
 static uint16_t pixels[832*480],normal[800*480],busy[800*480];
 for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  for(unsigned i=0;i<832*480;i++)pixels[i]=0x1234;
  xz_ui_render_ram_debug(pixels,832,theme,0,1,540);
  for(unsigned i=0;i<832*480;i++)assert(pixels[i]==0x1234);
  xz_ui_render_ram_debug(pixels,832,theme,1,1,540);
  unsigned painted=0;
  for(unsigned y=0;y<480;y++)for(unsigned x=0;x<832;x++){
   if(x>=568&&x<672&&y<24)painted+=pixels[y*832+x]!=0x1234;
   else assert(pixels[y*832+x]==0x1234);
  }
  assert(painted>100);
 }
 reset_input();struct xz_ui_action toggle={.kind=XZ_UI_RAM_DEBUG,.value=1};apply(NULL,&toggle,1);
 assert(model.ram_debug&&xz_mods_ram_debug_v1.enabled&&settings_snapshot().ram_debug);
 unsigned samples=xz_mods_ram_debug_v1.samples;refresh();assert(xz_mods_ram_debug_v1.samples==samples+1&&model.ram_debug_valid);
 refresh();assert(xz_mods_ram_debug_v1.samples==samples+1);
 memset(normal,0,sizeof(normal));assert(xz_mods_render_v1(normal,800,480,800,NULL));
 for(unsigned i=0;i<800*480;i++)busy[i]=0x1234;
 pthread_mutex_lock(&ui_mutex);assert(xz_mods_render_v1(busy,800,480,800,NULL));pthread_mutex_unlock(&ui_mutex);
 for(unsigned y=0;y<24;y++)for(unsigned x=568;x<800;x++)if(normal[y*800+x])assert(normal[y*800+x]==busy[y*800+x]);
 toggle.value=0;apply(NULL,&toggle,1);assert(settings_snapshot().ram_debug);
 struct xz_ui_widget controls[XZ_UI_WIDGETS];ui.page=XZ_UI_RAM_SETUP;size_t count=xz_ui_layout(&ui,&model,controls);
 for(size_t i=0;i<count;i++)assert(controls[i].kind!=XZ_UI_RAM_DEBUG);
 puts("PASS always-available RAM information, old disable ignored, no menu switch and one-second sampling");
}
static unsigned back_stock_calls;
static void back_stock(void *self,const void *input){(void)self;(void)input;back_stock_calls++;}
static void back_input(unsigned operation){unsigned char input[12]={0};uint16_t key=0x4208;memcpy(input+8,&key,2);input[11]=(unsigned char)operation;menu_back_key_hook(NULL,input);}
static void menu_back_navigation(void){
 static uint16_t frame[800*480];
 for(int theme=0;theme<XZ_THEME_COUNT;theme++)for(int page=0;page<XZ_UI_PAGE_COUNT;page++){
  struct xz_ui test;struct xz_ui_model state={0};xz_ui_init(&test);test.page=(enum xz_ui_page)page;state.theme=theme;
  struct xz_ui_widget w[XZ_UI_WIDGETS];size_t n=xz_ui_layout(&test,&state,w);int found=0;
  for(size_t k=0;k<n;k++)if(w[k].kind==XZ_UI_BACK){
   assert(w[k].x==0&&w[k].y==0&&w[k].w>=100&&w[k].h>=40);found=1;
   for(size_t j=0;j<n;j++)if(j!=k)assert(!(w[k].x<w[j].x+w[j].w&&w[j].x<w[k].x+w[k].w&&w[k].y<w[j].y+w[j].h&&w[j].y<w[k].y+w[k].h));
  }
  assert(found&&xz_ui_render(&test,&state,frame,800*480,800));
 }
 reset_input();stock_global_key=back_stock;menu_back_contact=0;back_stock_calls=0;
 ui.page=XZ_UI_SETTINGS;xz_ui_navigate(&ui,XZ_UI_RAM_SETUP);touch.visible=1;__atomic_store_n(&visible,1,__ATOMIC_RELEASE);
 unsigned char self[32]={0};contact(self,62,20,1);contact(self,0,0,0);assert(ui.page==XZ_UI_SETTINGS&&touch.visible);
 xz_ui_navigate(&ui,XZ_UI_KEY_SETUP);xz_ui_navigate(&ui,XZ_UI_BROWSE_SETUP);
 back_input(0);assert(ui.page==XZ_UI_KEY_SETUP&&touch.visible&&!back_stock_calls);back_input(1);assert(ui.page==XZ_UI_KEY_SETUP);back_input(2);
 back_input(0);back_input(3);assert(ui.page==XZ_UI_SETTINGS&&touch.visible);
 back_input(0);assert(!touch.visible&&!__atomic_load_n(&visible,__ATOMIC_ACQUIRE));back_input(1);back_input(3);assert(!back_stock_calls&&!menu_back_contact);
 back_input(0);back_input(2);assert(back_stock_calls==2);
 touch.visible=0;back_input(0);touch.visible=1;back_input(1);back_input(2);assert(back_stock_calls==5&&!menu_back_contact);
 unsigned char play[12]={0};uint16_t play_key=0x4101;memcpy(play+8,&play_key,2);menu_back_key_hook(NULL,play);assert(back_stock_calls==6);
 puts("PASS onscreen Back on every page/theme; previous page/root close; physical press/long/release capture; stock-held and other-key passthrough");
}
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"scene"))scene_release();
 else if(!strcmp(argv[1],"shortcut"))shortcut_drag();
 else if(!strcmp(argv[1],"buttons"))busy_buttons();
 else if(!strcmp(argv[1],"redraw"))paint_only_redraw();
 else if(!strcmp(argv[1],"ram"))ram_debug_box();
 else if(!strcmp(argv[1],"back"))menu_back_navigation();
 else assert(0);
 return 0;
}
