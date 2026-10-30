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
int main(void){
 started=1;settings_readonly=1;native_play=native_active=1;
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
 stock_uikey_rekordbox=fake_stock_rekordbox;stock_global_key=fake_global;rekordbox_shift_held=fake_shift_held;

 for(int service=0;service<2;service++){
  /* service 0: the receiver has not published the link yet (NULL); 1: it listens and no PC is linked. */
  if(service)pc(0);else link_now=NULL;
  /* 1. MODERN with the VJ.TOOLS view on and no PC: MODERN takes the screen at boot. */
  boot(XZ_UI_INTERFACE_MODERN);
  for(int i=0;i<3;i++)present();
  EXPECT("MODERN takes the screen at boot with the VJ.TOOLS view on and no PC",fake_owns&&shell_on_screen()&&xz_mods_full_frame_v1());
  EXPECT("no takeover is reported without a PC",!xz_mods_takeover_v1());
  /* 2. The button above LINK (INTERFACE) both ways, three times. */
  for(int round=0;round<3;round++){
   rekordbox_contact(0,0);present();present();
   EXPECT("INTERFACE leaves MODERN for CLASSIC",model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&!fake_owns);
   EXPECT("CLASSIC shows its top row with the VJ.TOOLS view on and no PC",present()&&top_bar_drawn());
   rekordbox_contact(0,0);present();present();
   EXPECT("INTERFACE leaves CLASSIC for MODERN, which takes the screen",model.ui_interface==XZ_UI_INTERFACE_MODERN&&fake_owns&&shell_on_screen());
  }
  /* 3. CLASSIC with the preference on, no PC and ui_mutex busy: the cached top row still draws. */
  boot(XZ_UI_INTERFACE_CLASSIC);
  EXPECT("CLASSIC boots onto its own screen with its top row",present()&&top_bar_drawn());
  unsigned dropped=0;
  for(int i=0;i<20;i++)if(!busy_present()||!top_bar_drawn())dropped++;
  if(dropped)printf("busy CLASSIC frames without the top row: %u of 20\n",dropped);
  EXPECT("CLASSIC's busy frames keep the top row",!dropped);
 }

 /* 4. The PC links and drops while MODERN is chosen: the takeover takes the screen from the shell, then gives it back. */
 boot(XZ_UI_INTERFACE_MODERN);pc(0);for(int i=0;i<2;i++)present();
 EXPECT("MODERN owns before the PC links",fake_owns&&shell_on_screen());
 unsigned repaints=fixture_repaints;
 pc(1);for(int i=0;i<3;i++)present();
 EXPECT("the linked PC's takeover takes the screen from MODERN",xz_mods_takeover_v1()&&!fake_owns&&!xz_mods_full_frame_v1());
 EXPECT("the hand-over repaints the stock frame",fixture_repaints>repaints);
 pc(0);for(int i=0;i<3;i++)present();
 EXPECT("MODERN takes the screen back when the PC drops",!xz_mods_takeover_v1()&&fake_owns&&shell_on_screen());
 /* With the PC linked the button above LINK still switches the interface; MODERN waits behind the takeover. */
 pc(1);present();rekordbox_contact(0,0);present();
 EXPECT("INTERFACE switches under a shown takeover",model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&xz_mods_takeover_v1());
 rekordbox_contact(0,0);present();
 EXPECT("MODERN stays behind a shown takeover",model.ui_interface==XZ_UI_INTERFACE_MODERN&&!fake_owns);
 pc(0);present();present();EXPECT("and owns again once it drops",fake_owns&&shell_on_screen());

 /* 5. The same in CLASSIC: the takeover replaces CLASSIC's screen while linked, the top row returns when it drops. */
 boot(XZ_UI_INTERFACE_CLASSIC);pc(0);
 EXPECT("CLASSIC with no PC draws its top row",present()&&top_bar_drawn()&&!xz_mods_takeover_v1());
 pc(1);present();
 EXPECT("the linked PC's takeover is reported in CLASSIC",xz_mods_takeover_v1());
 pc(0);present();
 EXPECT("CLASSIC's top row returns when the PC drops",present()&&top_bar_drawn()&&!xz_mods_takeover_v1());

 /* 6. Doom keeps the screen while it runs, whatever the link (it forces the preference on). */
 boot(XZ_UI_INTERFACE_MODERN);pc(0);present();
 pthread_mutex_lock(&ui_mutex);doom_phase=1;doom_launched_at=xz_game_milliseconds();doom_previous_takeover=1;model.fb_takeover=1;__atomic_store_n(&fb_takeover_active,1,__ATOMIC_RELEASE);pthread_mutex_unlock(&ui_mutex);
 present();
 EXPECT("Doom keeps MODERN off the screen",!fake_owns&&!xz_mods_full_frame_v1());
 pthread_mutex_lock(&ui_mutex);doom_phase=0;pthread_mutex_unlock(&ui_mutex);present();present();
 EXPECT("MODERN returns after Doom",fake_owns&&shell_on_screen());

 if(failures){printf("%u of %u expectations failed\n",failures,checks);return 1;}
 printf("PASS VJ.TOOLS view preference without a PC: MODERN owns at boot and after INTERFACE presses, CLASSIC's top row "
        "survives busy frames, the takeover hands over both ways in MODERN and CLASSIC, Doom keeps its screen, %u checks\n",checks);
 return 0;
}
