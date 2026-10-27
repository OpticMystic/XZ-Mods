/* SPDX-License-Identifier: MIT */
#define main controls_original_main
#include "test_runtime_controls.c"
#undef main
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
#include "../stock_dialog_fixture.h"
#include "../stock_dialog.c"
#ifdef XZ_STOCK_DIALOG_REFERENCE
static void xz_mods_native_frame_v1(const uint16_t*p,uint32_t w,uint32_t h,uint32_t stride){(void)p;(void)w;(void)h;(void)stride;}
#endif
static uint16_t native_frame[800*480],frame[800*480];
static int global_calls,rotate_calls,push_calls;
static int fixture_shell_live,fixture_shell_frames;
int xz_shell_runtime_frame(uint16_t *pixels,size_t stride,const struct xz_shell_runtime_frame_input *in){
 if(!fixture_shell_live||!in->eligible)return 0;fixture_shell_frames++;
 for(unsigned y=0;y<480;y++)for(unsigned x=0;x<800;x++)pixels[y*stride+x]=0x1234;
 return 1;
}
static void global_stock(void *s,const void *i){(void)s;(void)i;global_calls++;}
static int rotate_stock(int d){rotate_calls+=d;return 42;}
static int push_stock(void*a,void*b,void*c,void*d){(void)a;(void)b;(void)c;(void)d;push_calls++;return 43;}
static void native_dialog_fixture(void){
 memset(native_frame,0,sizeof native_frame);
 struct xz_canvas c={native_frame,800,800,480,0};
 xz_rect(c,0,0,800,40,0x203040);
 xz_text(c,18,10,"DECK 1",2,360,0xffffff);xz_text(c,420,10,"DECK 2",2,360,0xffffff);
 xz_rect(c,18,70,764,50,0x101820);xz_rect(c,18,350,764,50,0x101820);
 xz_rect(c,160,170,480,140,0x171717);
 xz_rect(c,160,170,480,3,0xff6030);
 xz_text(c,185,198,"E-8307: UNSUPPORTED DEVICE",2,440,0xffffff);
}
static void image(const char *dir,const char *name){
 if(!dir)return;char p[1024];snprintf(p,sizeof p,"%s/%s.ppm",dir,name);FILE*f=fopen(p,"wb");assert(f);
 fprintf(f,"P6\n800 480\n255\n");
 for(unsigned i=0;i<800*480;i++){uint16_t v=frame[i];unsigned char rgb[3]={(unsigned char)((v>>11)*255/31),(unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}fclose(f);
}
static void yields(const char *dir,const char *name){
 memcpy(frame,native_frame,sizeof frame);
 assert(!xz_mods_render_v1(frame,800,480,800,NULL));
 assert(!memcmp(frame,native_frame,sizeof frame));
 assert(!xz_mods_visible_v1()&&!xz_mods_full_frame_v1()&&!xz_mods_takeover_v1());
 /* The busy model path cannot paint cached badges, notices, keys or shell frames either. */
 pthread_mutex_lock(&ui_mutex);assert(!xz_mods_render_v1(frame,800,480,800,NULL));pthread_mutex_unlock(&ui_mutex);
 assert(!memcmp(frame,native_frame,sizeof frame));image(dir,name);
}
int main(int argc,char **argv){
 const char *dir=argc>1?argv[1]:NULL;
 unsigned char self[32]={0};started=1;settings_readonly=1;
 if(argc>2&&!strcmp(argv[2],"baseline"))goto no_dialog;
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
 native_dialog_fixture();browse=0x1adb0d8;
 touch.visible=1;visible=1;yields(dir,"classic-mods-stock-dialog");
 touch.visible=0;visible=0;dv.open=dv.painted=dv.painted_open=1;yields(dir,"classic-picker-stock-dialog");
 dv.open=0;play_key.open=play_key_visible=1;yields(NULL,NULL);
 model.fb_takeover=fb_takeover_active=network_enabled=network_connected=1;yields(NULL,NULL);
 arcade_present=1;yields(NULL,NULL);arcade_present=0;
 fake_shell_running=fake_owns=fixture_shell_live=1;model.ui_interface=XZ_UI_INTERFACE_MODERN;yields(dir,"modern-stock-alert-fallback");
 assert(!fixture_shell_frames);fake_shell_running=fake_owns=fixture_shell_live=0;
 model.ui_interface=XZ_UI_INTERFACE_CLASSIC;yields(dir,"classic-cable-stock-dialog");
 /* Live native status/counter paths take the same full-screen priority. */
 browse=0;status=0x1adb118;yields(NULL,NULL);status=0;
 browse=0x1adb0d8;touch.visible=1;touch.capture=1;inline_contact=inline_wave_contact=four_contact=1;
 contact(self,300,220,1);assert(stock_calls==1&&last_stock.down&&last_stock.x==300&&last_stock.y==220);
 assert(!touch.capture&&!inline_contact&&!inline_wave_contact&&!four_contact);
 browse=0;contact(self,300,220,0);assert(stock_calls==2&&!last_stock.down);
 stock_global_key=global_stock;stock_encoder_rotate=rotate_stock;stock_encoder_push=push_stock;
 browse=0x1adb0d8;menu_back_key_hook(NULL,NULL);assert(global_calls==1);
 assert(encoder_rotate_hook(3)==42&&rotate_calls==3);assert(encoder_push_hook(NULL,NULL,NULL,NULL)==43&&push_calls==1);
 /* MODERN opens the same MODS panel. A live window takes the screen, touch, MENU and the encoder from it,
    and the panel is still open on the same page when stock closes the window. */
 fake_shell_running=fake_owns=fixture_shell_live=1;model.ui_interface=XZ_UI_INTERFACE_MODERN;
 touch.visible=visible=1;touch.capture=0;ui.page=XZ_UI_PLAY;yields(dir,"modern-mods-stock-dialog");assert(!fixture_shell_frames);
 unsigned char menu_key[16]={0};menu_key[8]=0x0b;menu_key[9]=0x02;
 int stock_keys=global_calls;menu_back_key_hook(NULL,menu_key);menu_key[11]=2;menu_back_key_hook(NULL,menu_key);
 assert(global_calls==stock_keys+2&&touch.visible&&visible);
 int stock_touches=stock_calls;contact(self,770,20,1);contact(self,770,20,0);contact(self,60,120,1);contact(self,60,120,0);
 assert(stock_calls==stock_touches+4&&touch.visible&&visible&&ui.page==XZ_UI_PLAY);
 assert(encoder_rotate_hook(1)==42&&encoder_push_hook(NULL,NULL,NULL,NULL)==43&&touch.visible&&ui.page==XZ_UI_PLAY);
 browse=0;memcpy(frame,native_frame,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));
 assert(memcmp(frame,native_frame,sizeof frame)&&xz_mods_visible_v1()&&touch.visible&&ui.page==XZ_UI_PLAY);
 image(dir,"modern-mods-resumed");
 fake_shell_running=fake_owns=fixture_shell_live=0;fixture_shell_frames=0;model.ui_interface=XZ_UI_INTERFACE_CLASSIC;
 /* Failures and unknown IDs draw the panel instead of handing off. */
 browse=0;memset(&model,0,sizeof model);model.ui_interface=XZ_UI_INTERFACE_CLASSIC;
 fb_takeover_active=network_enabled=network_connected=0;play_key.open=play_key_visible=0;
 touch.visible=visible=1;xz_ui_init(&ui);ui.page=XZ_UI_HOME;
 failed=1;memcpy(frame,native_frame,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));
 assert(memcmp(frame,native_frame,sizeof frame)&&xz_mods_visible_v1());failed=0;
 ids[0]=7777;memcpy(frame,native_frame,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));
 assert(memcmp(frame,native_frame,sizeof frame));ids[0]=0;
 /* Model the native counter draw; presenter input is deliberately unrelated video. */
 memset(native_frame,0,sizeof native_frame);
 struct xz_canvas c={native_frame,800,800,480,0};
 xz_rect(c,0,311,400,169,0x202830);xz_text(c,20,409,"E-8304",2,360,0xffffff);xz_text(c,20,439,"UNSUPPORTED",2,360,0xffffff);
 ids[0]=103;xz_mods_native_frame_v1(native_frame,800,480,800);
 started=0;memcpy(frame,native_frame,sizeof frame);assert(!xz_mods_render_v1(frame,800,480,800,NULL));
 assert(!memcmp(frame,native_frame,sizeof frame));started=1;
 memset(frame,0x55,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));
 for(unsigned y=311;y<480;y++)assert(!memcmp(frame+y*800,native_frame+y*800,400*sizeof *frame));
 assert(frame[80*800+700]!=native_frame[80*800+700]&&xz_mods_visible_v1());image(dir,"classic-mods-counter-E8304");
 int before=stock_calls;contact(self,200,420,1);assert(stock_calls==before+1);
 ids[0]=0;contact(self,200,420,0);assert(stock_calls==before+2);ids[0]=103;
 before=stock_calls;contact(self,700,460,1);contact(self,700,460,0);assert(stock_calls==before);
 touch.visible=visible=1;xz_ui_init(&ui);ui.page=XZ_UI_HOME;
 before=stock_calls;contact(self,700,460,1);contact(self,200,420,1);contact(self,200,420,0);assert(stock_calls==before);
 touch.visible=visible=1;xz_ui_init(&ui);ui.page=XZ_UI_HOME;
 memset(frame,0x33,sizeof frame);pthread_mutex_lock(&ui_mutex);
 assert(xz_mods_render_v1(frame,800,480,800,NULL));pthread_mutex_unlock(&ui_mutex);
 for(unsigned y=311;y<480;y++)assert(!memcmp(frame+y*800,native_frame+y*800,400*sizeof *frame));
 /* Right-deck and simultaneous errors preserve exactly their own regions. */
 ids[0]=0;ids[1]=104;xz_mods_native_frame_v1(native_frame,800,480,800);
 memset(frame,0x33,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));
 for(unsigned y=311;y<480;y++)assert(!memcmp(frame+y*800+400,native_frame+y*800+400,400*sizeof *frame));
 assert(memcmp(frame+311*800,native_frame+311*800,400*sizeof *frame));
 ids[0]=102;xz_mods_native_frame_v1(native_frame,800,480,800);
 memset(frame,0x33,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));
 assert(!memcmp(frame+311*800,native_frame+311*800,169*800*sizeof *frame));
 ids[0]=ids[1]=0;
 no_dialog:
 /* Closing the stock dialog resumes the unchanged panel state and rendering. */
 memset(&model,0,sizeof model);model.ui_interface=XZ_UI_INTERFACE_CLASSIC;model.theme=0;
 touch.visible=visible=1;fb_takeover_active=network_connected=network_enabled=0;
 play_key.open=play_key_visible=0;dv.open=dv.painted=dv.painted_open=0;
 xz_ui_init(&ui);ui.page=XZ_UI_HOME;
 memset(frame,0,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));
 image(dir,"no-dialog-panel");
 touch.visible=visible=0;
 memset(frame,0,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));image(dir,"no-dialog-classic");
 fake_shell_running=fake_owns=fixture_shell_live=1;model.ui_interface=XZ_UI_INTERFACE_MODERN;
 memset(frame,0,sizeof frame);assert(xz_mods_render_v1(frame,800,480,800,NULL));image(dir,"no-dialog-modern");
 fake_shell_running=fake_owns=fixture_shell_live=0;
 puts("PASS real runtime all overlay priority, busy cache, modern native fallback, touch release, stock keys and resume");
 return 0;
}

