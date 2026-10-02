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
    return 0;
}
