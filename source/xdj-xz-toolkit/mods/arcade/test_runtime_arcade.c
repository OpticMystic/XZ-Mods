/* SPDX-License-Identifier: MIT */
#define main stock_runtime_test
#include "../ui/test_runtime_controls.c"
#undef main
int xz_read_memory(uint32_t a,void *out,size_t n){(void)a;(void)out;(void)n;return -1;}
#if __has_include("../ui/layered_wave_runtime.h")
void xz_layered_wave_configure(int mode,const uint32_t rgb[3]){(void)mode;(void)rgb;}
#endif
int main(void){
    stock_runtime_test();
    unsigned char touch_self[64]={0},deck1[64]={0},deck2[64]={0},event[24]={0};
    deck1[0x26]=1;deck2[0x26]=2;
    model.fb_takeover=1;touch.visible=1;
    struct xz_ui_action open={.kind=XZ_UI_ARCADE_OPEN};apply(NULL,&open,1);
    assert(arcade_present&&touch.visible&&!model.fb_takeover);
    contact(touch_self,700,380,0);assert(!arcade_opening_touch);
    contact(touch_self,500,160,1);contact(touch_self,500,160,0);
    assert(arcade.mode==XZ_ARCADE_PONG_DUEL&&arcade.phase==XZ_ARCADE_COUNT_IN);
    uint16_t key=0x4102;memcpy(event+8,&key,2);
    assert(xz_ui_runtime_arcade_key(deck1,event));
    assert(arcade_present&&touch.visible&&arcade.phase==XZ_ARCADE_COUNT_IN);
    contact(touch_self,710,24,1);contact(touch_self,710,24,0);
    assert(!arcade_present&&!touch.visible&&model.fb_takeover&&arcade.phase==XZ_ARCADE_MUSIC);
    event[11]=2;assert(xz_ui_runtime_arcade_key(deck1,event));
    assert(!xz_ui_runtime_arcade_key(deck1,event));
    key=0x4101;memcpy(event+8,&key,2);event[11]=0;assert(!xz_ui_runtime_arcade_key(deck1,event));
    apply(NULL,&open,1);contact(touch_self,500,160,0);key=0x4102;memcpy(event+8,&key,2);
    assert(xz_ui_runtime_arcade_key(deck1,event));
    assert(arcade_present&&arcade.phase==XZ_ARCADE_MUSIC&&arcade.player[0].ready&&!arcade.player[1].ready);
    event[11]=2;assert(xz_ui_runtime_arcade_key(deck1,event));event[11]=0;
    assert(xz_ui_runtime_arcade_key(deck2,event));assert(arcade_present&&arcade.phase==XZ_ARCADE_COUNT_IN);
    event[11]=2;assert(xz_ui_runtime_arcade_key(deck2,event));
    contact(touch_self,710,24,1);assert(!arcade_present&&!touch.visible&&arcade_touch_owned);
    int before=stock_calls;contact(touch_self,710,24,0);assert(!arcade_touch_owned&&stock_calls==before);
    model.fb_takeover=0;model.connection.connected=0;
    contact(touch_self,700,380,1);contact(touch_self,700,380,0);assert(stock_calls==before+2);
    puts("PASS real runtime arcade open, two-player readiness, CUE strike and onscreen handoff, physical and touch release ownership, stock touch restore");

    static uint16_t screen[800*480];
    unsigned char back[24]={0};uint16_t back_key=0x4208;memcpy(back+8,&back_key,2);
    model.rekordbox_assign[0]=XZ_REKORDBOX_MODS;touch.visible=1;arcade.phase=XZ_ARCADE_MENU;arcade.mode=XZ_ARCADE_BREAK_SOLO;
    apply(NULL,&open,1);assert(arcade_present&&arcade_opening_touch);
    assert(xz_mods_render_v1(screen,800,480,800,NULL)==1&&game_frame.count>=7);
    assert(encoder_rotate_hook(1)==1&&game_nav.visible&&game_nav.shown.at[game_nav.focus].y==208);
    assert(xz_mods_render_v1(screen,800,480,800,NULL)==1&&screen[(208-3)*800+200]==0xffff);
    /* The encoder opened nothing by touch, so the first push must not be eaten by the opening latch. */
    assert(encoder_push_hook(NULL,NULL,NULL,NULL)==1&&arcade.mode==XZ_ARCADE_BREAK_SOLO&&arcade.phase==XZ_ARCADE_COUNT_IN);
    menu_back_key_hook(NULL,back);back[11]=2;menu_back_key_hook(NULL,back);back[11]=0;
    assert(arcade_present&&arcade.phase==XZ_ARCADE_COUNT_IN&&!menu_back_contact);
    assert(uikey_rekordbox_hook(NULL,NULL,NULL,NULL)==1);
    assert(!arcade_present&&touch.visible&&ui.page==XZ_UI_EXTRAS&&arcade.phase==XZ_ARCADE_MUSIC&&model.fb_takeover==0);
    assert(game_locked()==GAME_NONE&&menu_owns_keys_locked()&&encoder_rotate_hook(1)==1);
    apply(NULL,&open,1);assert(xz_mods_render_v1(screen,800,480,800,NULL)==1);
    menu_back_key_hook(NULL,back);back[11]=2;menu_back_key_hook(NULL,back);
    assert(!arcade_present&&!touch.visible&&!arcade.leave);
    touch.visible=1;arcade.phase=XZ_ARCADE_MENU;apply(NULL,&open,1);xz_mods_render_v1(screen,800,480,800,NULL);
    encoder_rotate_hook(1);contact(touch_self,10,470,1);assert(!game_nav.visible);contact(touch_self,10,470,0);
    puts("PASS real runtime arcade encoder focus ring, push past the opening latch, BACK ignored mid-round, REKORDBOX to MODS, BACK on the paused screen, touch hides the ring");
    return 0;
}
