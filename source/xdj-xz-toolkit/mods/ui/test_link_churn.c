/* SPDX-License-Identifier: MIT */
/* The VJ.Tools takeover against a link that drops and comes back, through the real ui_runtime.c in real time
   (2026-10-30: the VJ.Tools Library reconnected to the player about every 4 s; each time the receiver's single
   client slot read 0 for a moment and the takeover switched off and on). The receiver side, which keeps the last
   picture through such a gap and never accepts a header-less probe, is test_receiver_link.c.

   Runs about 100 s: the churn is real reconnects every 2 to 4 s for a minute in MODERN and half a minute in
   CLASSIC, rendered at 60 frames a second. */
#define main controls_original_main
#include "test_runtime_controls.c"
#undef main
#include <time.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
int xz_read_memory(uint32_t address,void *out,size_t n){
 uint32_t value;
 if(n==4&&address==0x3c6fc7c)value=2;
 else if(n==4&&address==0x1c888d0)value=0x10000;
 else if(n==1&&address>=0x222e36c&&address<0x222e36c+6*0xbc){*(uint8_t*)out=1;return 0;}
 else return -1;
 memcpy(out,&value,4);return 0;
}
static uint16_t frame[800*480];
int xz_shell_runtime_frame(uint16_t *pixels,size_t stride,const struct xz_shell_runtime_frame_input *in){
 if(!fake_shell_running||!in->eligible||in->theme!=XZ_THEME_FLAGSHIP){fake_owns=0;return 0;}
 fake_owns=1;
 for(unsigned y=0;y<480;y++)for(unsigned x=0;x<800;x++)pixels[y*stride+x]=0x1234;
 return 1;
}
static struct xz_vj_connection_v1 link_state;
static void pc(int connected){
 memset(&link_state,0,sizeof link_state);link_state.version=1;link_state.size=sizeof link_state;
 link_state.listening=1;link_state.connected=(uint32_t)connected;
}
static int top_bar_drawn(void){
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)for(unsigned x=XZ_TOP_MODS_X;x<800;x++)if(frame[y*800+x])return 1;
 return 0;
}
/* What one presenter frame shows, and the counts a run collects. */
struct watch { unsigned frames, toggles, shell_frames, no_top_row; int shown; };
static void frame_once(struct watch *w){
 memset(frame,0,sizeof frame);
 int drawn=xz_mods_render_v1(frame,800,480,800,&link_state)!=0;
 int shown=xz_mods_takeover_v1();
 if(w->frames&&shown!=w->shown)w->toggles++;
 w->shown=shown;w->frames++;
 if(fake_owns)w->shell_frames++;
 if(model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&(!drawn||!top_bar_drawn()))w->no_top_row++;
}
static void run_for(struct watch *w,uint32_t ms){
 uint32_t t0=xz_game_milliseconds();
 while(xz_game_milliseconds()-t0<ms){frame_once(w);nanosleep(&(struct timespec){0,16000000L},NULL);}
}
static unsigned failures,checks;
#define EXPECT(what,cond) do{checks++;if(!(cond)){failures++;printf("FAIL %s\n",what);}}while(0)
static void boot(int iface){
 struct xz_settings s;xz_settings_default(&s);
 s.ui_interface=iface;s.theme=xz_settings_look(iface,s.classic_theme);s.fb_takeover=1;
 touch.visible=0;__atomic_store_n(&visible,0,__ATOMIC_RELEASE);fake_owns=0;
 pthread_mutex_lock(&ui_mutex);profile_apply_locked(&s,"boot-usb");pthread_mutex_unlock(&ui_mutex);
}
/* Reconnects for ms: up 2 to 4 s, then down for a short gap (a reconnect, or a second connection that evicted the
   session and closed), from a fixed sequence so every run is the same. */
static unsigned churn(struct watch *w,uint32_t ms){
 static const uint32_t up[]={2000,3100,2600,4000,2200,3500,2900,3800};
 static const uint32_t gap[]={40,120,300,600,90,450,200,800};
 uint32_t t0=xz_game_milliseconds();unsigned n=0;
 while(xz_game_milliseconds()-t0<ms){
  pc(1);run_for(w,up[n%8]);pc(0);run_for(w,gap[n%8]);n++;
 }
 pc(1);return n;
}
int main(void){
 started=1;settings_readonly=1;native_play=native_active=1;
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;

 /* A real connect in MODERN: the takeover shows and MODERN steps aside. */
 boot(XZ_UI_INTERFACE_MODERN);struct watch w={0};pc(0);run_for(&w,300);
 EXPECT("MODERN owns before the PC connects",fake_owns&&!xz_mods_takeover_v1());
 unsigned repaints=fixture_repaints;
 pc(1);run_for(&w,500);
 EXPECT("a real connect shows the takeover",xz_mods_takeover_v1()&&!fake_owns&&fixture_repaints>repaints);

 /* A minute of reconnects every 2 to 4 s: the takeover never switches and MODERN never draws. */
 memset(&w,0,sizeof w);unsigned n=churn(&w,60000);run_for(&w,300);
 printf("MODERN churn: %u reconnects, %u frames, %u takeover toggles, %u frames drawn by MODERN\n",n,w.frames,w.toggles,w.shell_frames);
 EXPECT("reconnects never toggle the takeover in MODERN",w.toggles==0&&w.shown);
 EXPECT("MODERN never flashes through a reconnect",w.shell_frames==0);

 /* A probe or a second short connection while linked. Header-less probes never reach the slot (the receiver
    closes them unaccepted), so the link reads up throughout; a second connection that sends a header evicts the
    session and closes, which reads as a short gap until the client reconnects. */
 memset(&w,0,sizeof w);w.shown=1;w.frames=1;
 pc(1);run_for(&w,400);pc(0);run_for(&w,150);pc(1);run_for(&w,400);
 EXPECT("a probe or a short second connection never toggles the takeover",w.toggles==0&&xz_mods_takeover_v1());

 /* A real disconnect: the takeover holds through the hold, then MODERN takes the screen back. */
 repaints=fixture_repaints;pc(0);memset(&w,0,sizeof w);run_for(&w,1500);
 EXPECT("the takeover holds 1.5 s into a disconnect",xz_mods_takeover_v1()&&w.shell_frames==0);
 run_for(&w,900);
 EXPECT("MODERN takes the screen back after the hold",!xz_mods_takeover_v1()&&fake_owns&&fixture_repaints>repaints);

 /* CLASSIC: half a minute of reconnects, no toggle and the top row on every frame. */
 boot(XZ_UI_INTERFACE_CLASSIC);pc(1);memset(&w,0,sizeof w);run_for(&w,500);
 EXPECT("a real connect shows the takeover in CLASSIC",xz_mods_takeover_v1());
 memset(&w,0,sizeof w);n=churn(&w,30000);run_for(&w,300);
 printf("CLASSIC churn: %u reconnects, %u frames, %u takeover toggles, %u frames without the top row\n",n,w.frames,w.toggles,w.no_top_row);
 EXPECT("reconnects never toggle the takeover in CLASSIC",w.toggles==0&&w.shown);
 EXPECT("CLASSIC draws its top row on every frame",w.no_top_row==0);
 pc(0);run_for(&w,2400);
 EXPECT("CLASSIC's own screen returns after a real disconnect",!xz_mods_takeover_v1()&&top_bar_drawn());

 if(failures){printf("%u of %u expectations failed\n",failures,checks);return 1;}
 printf("PASS link churn: a minute of reconnects in MODERN and half a minute in CLASSIC with no takeover toggle, no MODERN "
        "flash and the top row on every frame; probes ignored; real connect shows, real disconnect hands back after the hold, %u checks\n",checks);
 return 0;
}
