/* SPDX-License-Identifier: MIT */
/* The interface choice against a live player window, through the real ui_runtime.c and stock_dialog.c
   (owner 2026-10-29 on preview.22.13.2: "it keeps getting stuck in classic mode view" and "the buttons on
   the top of classic view keep flickering").

   The shell below keeps the real shell's ownership contract (shell_runtime.c candidate/set_owns): it takes
   the screen only inside a frame call, only while MODERN is chosen and the frame is eligible, and an
   ineligible frame call takes the screen back from it. The earlier stability test granted ownership up front,
   which hid both faults. */
#define main controls_original_main
#include "test_runtime_controls.c"
#undef main
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
#include "../stock_dialog_fixture.h"
#include "../stock_dialog.c"
static uint16_t frame[800*480];
static unsigned shell_frames;
int xz_shell_runtime_frame(uint16_t *pixels,size_t stride,const struct xz_shell_runtime_frame_input *in){
 if(!fake_shell_running||!in->eligible||in->theme!=XZ_THEME_FLAGSHIP){fake_owns=0;return 0;}
 fake_owns=1;shell_frames++;
 for(unsigned y=0;y<480;y++)for(unsigned x=0;x<800;x++)pixels[y*stride+x]=0x1234;
 return 1;
}
/* One presenter frame over a black stock frame: 1 when XZ Mods drew it. */
static int present(void){memset(frame,0,sizeof frame);return xz_mods_render_v1(frame,800,480,800,NULL)!=0;}
static int shell_on_screen(void){return frame[240*800+400]==0x1234&&xz_mods_visible_v1();}
/* CLASSIC's top row: the MODS button (ui/top_bar.h) is painted in rows 0..17. */
static int top_bar_drawn(void){
 for(unsigned y=0;y<XZ_TOP_BAR_H;y++)for(unsigned x=XZ_TOP_MODS_X;x<800;x++)if(frame[y*800+x])return 1;
 return 0;
}
/* A failed expectation is counted and named, so one run shows every fault; main fails at the end. */
static unsigned failures;
#define EXPECT(what,cond) do{if(!(cond)){failures++;printf("FAIL %s\n",what);}}while(0)
static void boot(int iface){
 struct xz_settings s;xz_settings_default(&s);s.ui_interface=iface;s.theme=xz_settings_look(iface,s.classic_theme);
 touch.visible=0;__atomic_store_n(&visible,0,__ATOMIC_RELEASE);fake_owns=0;shell_frames=0;
 /* Boot applies the saved profile; nothing presses INTERFACE. */
 pthread_mutex_lock(&ui_mutex);profile_apply_locked(&s,"boot-xz");pthread_mutex_unlock(&ui_mutex);
 model.rekordbox_assign[0]=XZ_REKORDBOX_INTERFACE;model.rekordbox_assign[1]=XZ_REKORDBOX_STOCK;
}
int main(void){
 started=1;settings_readonly=1;
 xz_ui_init(&ui);xz_ui_init(&inline_ui);xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;
 stock_uikey_rekordbox=fake_stock_rekordbox;stock_global_key=fake_global;rekordbox_shift_held=fake_shift_held;
 unsigned checks=0;

 /* 1. MODERN boots with a player window already up (a USB warning). The shell has not drawn yet, so it does
    not own the screen. Pre-fix, CLASSIC's yield applied (the flag that says "CLASSIC" was never set from the
    saved profile) and the frame returned before the shell was asked: MODERN never appeared, the stock screen
    stayed, and every key went to the player. */
 status=0x1adb118;caution_text(status_text,"E-8304: USB ACCESS ERROR");
 boot(XZ_UI_INTERFACE_MODERN);
 assert(model.ui_interface==XZ_UI_INTERFACE_MODERN&&fake_shell_running&&!fake_owns);
 for(int i=0;i<3;i++)present();
 EXPECT("MODERN takes the screen at boot over a live window",shell_frames&&fake_owns&&shell_on_screen());checks++;
 /* The same after MODS closes over the window: the shell takes the screen back on the next frame. */
 touch.visible=1;__atomic_store_n(&visible,1,__ATOMIC_RELEASE);assert(present()&&!fake_owns);
 touch.visible=0;__atomic_store_n(&visible,0,__ATOMIC_RELEASE);
 for(int i=0;i<2;i++)present();
 EXPECT("MODERN takes the screen back after MODS closes over a live window",fake_owns&&shell_on_screen());checks++;

 /* 2. CLASSIC with the same window up: the player's window keeps CLASSIC's screen and its other keys, but the
    button above LINK still switches to MODERN, and MODERN then takes the screen over the window. Pre-fix the
    press went to the player's own handler and the interface never changed. */
 boot(XZ_UI_INTERFACE_CLASSIC);
 assert(!present()&&!xz_mods_visible_v1());checks++;            /* CLASSIC yields to the window, as designed */
 int stock_before=fake_stock_calls;
 rekordbox_contact(0,0);
 EXPECT("the button above LINK leaves CLASSIC over a live window",model.ui_interface==XZ_UI_INTERFACE_MODERN&&fake_stock_calls==stock_before);checks++;
 if(model.ui_interface==XZ_UI_INTERFACE_MODERN){
  for(int i=0;i<2;i++)present();
  EXPECT("MODERN then takes the screen over the window",fake_owns&&shell_on_screen());checks++;
  /* And back: MODERN never yields, so the button works there too. */
  rekordbox_contact(0,0);EXPECT("the button above LINK leaves MODERN over a live window",model.ui_interface==XZ_UI_INTERFACE_CLASSIC);checks++;
 }
 /* SHIFT + the button keeps its stock job, which goes to the player's window as before. */
 int global_before=fake_global_calls;rekordbox_contact(1,1);
 assert(model.ui_interface==XZ_UI_INTERFACE_CLASSIC&&fake_global_calls>global_before);checks++;
 /* Another key stays the player's while its window is up in CLASSIC. */
 {unsigned char key[16]={0};key[8]=0x0b;key[9]=0x02;global_before=fake_global_calls;menu_back_key_hook(NULL,key);
  assert(fake_global_calls==global_before+1);}
 status=0;caution_text(status_text,NULL);

 /* 3. The loopback cable's hub warning stays up while the cable is in, and the player redraws its text. A read
    that lands while the text is being rewritten (two reads differ, the buffer is cleared, or only the start
    of the text is there yet) said "ordinary window" pre-fix, so CLASSIC handed the whole screen to the player
    for that frame and took it back on the next: the top buttons flickered and presses on the button above
    LINK went to the player at random. A window keeps the verdict its last clear text gave it. */
 boot(XZ_UI_INTERFACE_CLASSIC);
 status=0x1adb118;caution_text(status_text,"E-8307: HUBS ARE NOT SUPPORTED");
 assert(present()&&top_bar_drawn());
 unsigned dropped=0,stock_presses=0;
 for(unsigned f=0;f<400;f++){
  switch(f%5){
  case 0:torn_at=0x1ada718;break;                                           /* the two reads differ */
  case 1:torn_at=0;caution_text(status_text,NULL);break;                    /* cleared before the copy */
  case 2:caution_text(status_text,"E-8307: HUB");break;                     /* part way through the copy */
  case 3:caution_text(status_text,"E-8307:");break;
  default:caution_text(status_text,"E-8307: HUBS ARE NOT SUPPORTED");break;
  }
  if(!present()||!top_bar_drawn())dropped++;
  if(f%50==7){
   int before=model.ui_interface,global=fake_global_calls,stock_calls_before=fake_stock_calls;
   rekordbox_contact(0,0);
   if(model.ui_interface==before||fake_stock_calls!=stock_calls_before)stock_presses++;
   (void)global;
   rekordbox_contact(0,0);assert(model.ui_interface==before);
  }
 }
 torn_at=0;
 printf("hub window being redrawn: %u of 400 frames without the top bar, %u of 8 presses lost\n",dropped,stock_presses);
 EXPECT("CLASSIC's top row stays while the hub warning is redrawn",!dropped);
 EXPECT("the button above LINK works while the hub warning is redrawn",!stock_presses);checks++;
 /* A different, clear text in the same window is an ordinary window again (a real error replaced the warning). */
 caution_text(status_text,"E-8307: USB ACCESS ERROR");assert(!present());checks++;
 /* A new window whose text cannot be read is still the player's, as before. */
 status=0;caution_text(status_text,NULL);assert(present()&&top_bar_drawn());
 status=0x1adb150;failed_at=0x1ada718;assert(!present());failed_at=0;status=0;

 /* 4. Since preview.22.13.3 our own hooks own both caution-draw entries (ui/native_raw_skin.c removes the hub
    window). The observer took the hooked entry for a firmware mismatch and failed open: no player window and no
    deck counter error was seen at all, so CLASSIC drew over the player's own warnings and MODERN never said them. */
 hooked_entries=1;
 boot(XZ_UI_INTERFACE_CLASSIC);status=0x1adb118;caution_text(status_text,"E-8304: USB ACCESS ERROR");
 EXPECT("a live window is still seen with our caution hooks in place",!present()&&xz_stock_dialog_active());checks++;
 rekordbox_contact(0,0);for(int i=0;i<2;i++)present();
 EXPECT("and the button above LINK still leaves CLASSIC over it",model.ui_interface==XZ_UI_INTERFACE_MODERN&&fake_owns&&shell_on_screen());checks++;
 status=0;caution_text(status_text,NULL);hooked_entries=0;
 if(failures){printf("%u expectation(s) failed\n",failures);return 1;}
 printf("PASS interface vs live player window: MODERN takes the screen over a window at boot and after MODS, "
        "the button above LINK leaves CLASSIC over a window, the hub warning never flickers CLASSIC's top row, %u checks\n",checks);
 return 0;
}
