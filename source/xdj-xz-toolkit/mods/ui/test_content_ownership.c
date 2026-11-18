/* SPDX-License-Identifier: MIT */
/* The VJ.TOOLS view preference against the screen, through the real ui_runtime.c (owner 2026-10-30 on
   preview.22.13.4: still stuck on CLASSIC although MODERN was chosen, and the button above LINK does nothing).
   The owner's USB has fb_takeover=1 with no PC linked. The preference only puts the takeover on screen while
   VJ.Tools is linked (xz_mods_takeover_v1); until then MODERN and CLASSIC must behave as with it off.

   The shell keeps the real shell's ownership contract (shell_runtime.c candidate/set_owns): it takes the screen
   only inside a frame call, only while MODERN is chosen and the frame is eligible; an ineligible call takes it back. */
#define main controls_original_main
#include "test_runtime_controls.c"
#undef main
#include <time.h>
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
/* The stock play scene (0x3c6fc7c) stays the same, so CLASSIC's busy frames may reuse the cached top row. */
int xz_read_memory(uint32_t address,void *out,size_t n){
 uint32_t value;
 if(n==4&&address==0x3c6fc7c)value=2;
 else if(n==4&&address==0x1c888d0)value=0x10000;
 else if(n==1&&address>=0x222e36c&&address<0x222e36c+6*0xbc){*(uint8_t*)out=1;return 0;}
 else return -1;
 memcpy(out,&value,4);return 0;
}
static uint16_t frame[800*480];
static unsigned shell_frames;
int xz_shell_runtime_frame(uint16_t *pixels,size_t stride,const struct xz_shell_runtime_frame_input *in){
 if(!fake_shell_running||!in->eligible||in->theme!=XZ_THEME_FLAGSHIP){fake_owns=0;return 0;}
 fake_owns=1;shell_frames++;
 for(unsigned y=0;y<480;y++)for(unsigned x=0;x<800;x++)pixels[y*stride+x]=0x1234;
 return 1;
}
/* The receiver's connection snapshot: NULL before the link service runs, else listening with or without a PC. */
static struct xz_vj_connection_v1 link_state;
static const struct xz_vj_connection_v1 *link_now;
static void pc(int connected){
 memset(&link_state,0,sizeof link_state);link_state.version=1;link_state.size=sizeof link_state;
 link_state.listening=1;link_state.connected=(uint32_t)connected;link_now=&link_state;
}
static int present(void){memset(frame,0,sizeof frame);return xz_mods_render_v1(frame,800,480,800,link_now)!=0;}
static int busy_present(void){
 memset(frame,0,sizeof frame);pthread_mutex_lock(&ui_mutex);
 int drawn=xz_mods_render_v1(frame,800,480,800,link_now)!=0;pthread_mutex_unlock(&ui_mutex);return drawn;
}
/* Frames for ms of real time, one per 16 ms: the link counts as up or gone only after it has stayed so. */
static int shell_on_screen(void){return frame[240*800+400]==0x1234&&xz_mods_visible_v1();}
static int top_bar_drawn(void){
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)for(unsigned x=XZ_TOP_MODS_X;x<800;x++)if(frame[y*800+x])return 1;
 return 0;
}
static unsigned failures,checks;
#define EXPECT(what,cond) do{checks++;if(!(cond)){failures++;printf("FAIL %s\n",what);}}while(0)
/* Boot as the runtime does: apply the USB's record (MODERN or CLASSIC, the VJ.TOOLS view on, the button above
   LINK on INTERFACE). Nothing presses anything. */
static void boot(int iface){
 struct xz_settings s;xz_settings_default(&s);
 s.ui_interface=iface;s.theme=xz_settings_look(iface,s.classic_theme);s.fb_takeover=1;
 s.rekordbox_press=XZ_REKORDBOX_INTERFACE;s.rekordbox_shift=XZ_REKORDBOX_STOCK;
 touch.visible=0;__atomic_store_n(&visible,0,__ATOMIC_RELEASE);fake_owns=0;shell_frames=0;
 pthread_mutex_lock(&ui_mutex);profile_apply_locked(&s,"boot-usb");pthread_mutex_unlock(&ui_mutex);
 assert(model.fb_takeover&&model.rekordbox_assign[0]==XZ_REKORDBOX_INTERFACE);
}

static int fixture_content;
static int content_now(void){return fixture_content;}
static uint32_t fixture_ms=100;
static uint32_t now_ms(void){return fixture_ms;}
static void at(uint32_t ms){fixture_ms=ms;present();present();}
int main(void){
 started=1;settings_readonly=1;native_play=native_active=1;
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
 receiver_vj_content=content_now;link_clock=now_ms;
 boot(XZ_UI_INTERFACE_MODERN);pc(1);at(100);at(500);
 EXPECT("MODERN remains the actual frame for a settled connected empty stream",!xz_mods_takeover_v1()&&fake_owns&&shell_on_screen());
 at(3500);EXPECT("an incomplete held payload never steals MODERN",!xz_mods_takeover_v1()&&fake_owns&&shell_on_screen());
 for(int i=0;i<10;i++){busy_present();EXPECT("busy model keeps MODERN over an empty stream",!xz_mods_takeover_v1()&&fake_owns);}
 fixture_content=1;at(3510);
 EXPECT("actual content gives VJ ownership",xz_mods_takeover_v1()&&!fake_owns&&!xz_mods_full_frame_v1());
 pc(0);at(3520);at(5000);
 EXPECT("picture survives a short link drop",xz_mods_takeover_v1()&&!fake_owns);
 pc(1);at(5010);at(5500);
 EXPECT("reconnect with content retains VJ",xz_mods_takeover_v1()&&!fake_owns);
 fixture_content=0;at(5510);
 EXPECT("expired content hands back while socket stays connected",!xz_mods_takeover_v1()&&fake_owns&&shell_on_screen());
 fixture_content=1;at(5520);pc(0);at(5530);at(7600);
 EXPECT("real disconnect hands back after existing hold",!xz_mods_takeover_v1()&&fake_owns&&shell_on_screen());
 pc(1);at(7610);at(7900);__atomic_store_n(&fb_takeover_active,0,__ATOMIC_RELEASE);model.fb_takeover=0;at(7910);
 EXPECT("preference OFF keeps MODERN with valid content",!xz_mods_takeover_v1()&&fake_owns&&shell_on_screen());
 boot(XZ_UI_INTERFACE_CLASSIC);fixture_content=0;at(8000);
 EXPECT("CLASSIC empty stream retains its top bar",!xz_mods_takeover_v1()&&top_bar_drawn());
 for(int i=0;i<10;i++)EXPECT("CLASSIC empty stream busy cache retains top bar",busy_present()&&top_bar_drawn()&&!xz_mods_takeover_v1());
 fixture_content=1;at(8010);EXPECT("CLASSIC legitimate content can take over",xz_mods_takeover_v1()&&top_bar_drawn());
 fixture_content=0;at(8020);EXPECT("CLASSIC content expiry retains top bar",!xz_mods_takeover_v1()&&top_bar_drawn());
 receiver_vj_content=NULL;boot(XZ_UI_INTERFACE_MODERN);at(8030);
 EXPECT("older bridge retains legacy link fallback",xz_mods_takeover_v1()&&!fake_owns);
 if(failures){printf("%u of %u expectations failed\n",failures,checks);return 1;}
 printf("PASS actual UI content ownership: %u checks\n",checks);return 0;
}
