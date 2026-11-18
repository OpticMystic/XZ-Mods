/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xz_controls.h"
#include "vendor/doomkeys.h"
#include "native_bridge.h"
#include <assert.h>
#include <stdio.h>
static struct xz_doom_controls controls;
static uint32_t game[320*200];
static uint16_t frame[800*480+1];
int main(void) {
    int pressed; unsigned char key;
    xz_doom_touch(&controls,1,760,40);
    assert(xz_doom_key(&controls,&pressed,&key) && pressed && key==KEY_UPARROW);
    xz_doom_touch(&controls,0,0,0);
    assert(xz_doom_key(&controls,&pressed,&key) && !pressed && key==KEY_UPARROW);
    xz_doom_touch(&controls,1,40,200);
    assert(xz_doom_key(&controls,&pressed,&key) && pressed && key==KEY_FIRE);
    xz_doom_touch(&controls,0,40,200);
    assert(!xz_doom_key(&controls,&pressed,&key));
    xz_doom_touch(&controls,1,760,120);
    assert(xz_doom_key(&controls,&pressed,&key) && pressed && key==KEY_LEFTARROW);
    xz_doom_touch(&controls,0,760,120);
    assert(xz_doom_key(&controls,&pressed,&key) && !pressed && key==KEY_LEFTARROW);
    xz_doom_touch(&controls,1,40,200);
    assert(xz_doom_key(&controls,&pressed,&key) && !pressed && key==KEY_FIRE);
    xz_doom_touch(&controls,0,40,200);
    xz_doom_touch(&controls,1,-1,40); assert(!controls.quit);
    xz_doom_touch(&controls,1,800,40); assert(!controls.quit);
    xz_doom_touch(&controls,1,740,440); assert(controls.quit);
    game[0]=0x00ff0000; game[319]=0x0000ff00; game[199*320]=0x000000ff;
    frame[800*480]=0x1234;
    xz_doom_compose(frame,game,&controls);
    assert(frame[80]==0xf800 && frame[81]==0xf800 && frame[880]==0xf800);
    assert(frame[718]==0x07e0 && frame[398*800+80]==0x001f);
    assert(frame[800*480]==0x1234);
    controls=(struct xz_doom_controls){0};
    xz_doom_physical(&controls,(1u<<0)|(1u<<2),0);
    assert(xz_doom_key(&controls,&pressed,&key)&&pressed&&key==KEY_UPARROW);
    xz_doom_physical(&controls,1u<<2,0); assert(!xz_doom_key(&controls,&pressed,&key));
    xz_doom_physical(&controls,0,0); assert(xz_doom_key(&controls,&pressed,&key)&&!pressed&&key==KEY_UPARROW);
    xz_doom_physical(&controls,(1u<<1)|(1u<<4),0);
    assert(xz_doom_key(&controls,&pressed,&key)&&pressed&&key==KEY_FIRE);
    xz_doom_physical(&controls,1u<<4,0); assert(!xz_doom_key(&controls,&pressed,&key));
    xz_doom_physical(&controls,0,0); assert(xz_doom_key(&controls,&pressed,&key)&&!pressed&&key==KEY_FIRE);
    controls=(struct xz_doom_controls){0};
    assert(xz_doom_nav_tap(&controls,XZ_GAME_NAV_DOWN));
    assert(!xz_doom_nav_tap(&controls,XZ_GAME_NAV_DOWN));
    assert(xz_doom_key(&controls,&pressed,&key)&&pressed&&key==KEY_DOWNARROW);
    assert(!xz_doom_nav_tap(&controls,XZ_GAME_NAV_DOWN));
    assert(xz_doom_key(&controls,&pressed,&key)&&!pressed&&key==KEY_DOWNARROW);
    assert(!xz_doom_key(&controls,&pressed,&key));
    assert(xz_doom_nav_tap(&controls,XZ_GAME_NAV_DOWN));
    assert(xz_doom_key(&controls,&pressed,&key)&&pressed&&key==KEY_DOWNARROW);
    assert(xz_doom_key(&controls,&pressed,&key)&&!pressed&&key==KEY_DOWNARROW);
    static const struct { unsigned nav; int key; } taps[]={{XZ_GAME_NAV_UP,KEY_UPARROW},{XZ_GAME_NAV_ENTER,KEY_ENTER},{XZ_GAME_NAV_BACK,KEY_ESCAPE}};
    for(unsigned i=0;i<3;i++){
        assert(xz_doom_nav_tap(&controls,taps[i].nav));
        assert(xz_doom_key(&controls,&pressed,&key)&&pressed&&key==taps[i].key);
        assert(xz_doom_key(&controls,&pressed,&key)&&!pressed&&key==taps[i].key);
    }
    xz_doom_physical(&controls,1u<<9,0);
    assert(xz_doom_key(&controls,&pressed,&key)&&pressed&&key==KEY_ESCAPE);
    assert(!xz_doom_nav_tap(&controls,XZ_GAME_NAV_BACK));
    xz_doom_physical(&controls,0,0);
    assert(xz_doom_key(&controls,&pressed,&key)&&!pressed&&key==KEY_ESCAPE);
    assert(xz_doom_nav_tap(&controls,XZ_GAME_NAV_BACK));
    assert(xz_doom_key(&controls,&pressed,&key)&&pressed&&key==KEY_ESCAPE);
    assert(xz_doom_key(&controls,&pressed,&key)&&!pressed&&key==KEY_ESCAPE);
    assert(!controls.quit&&xz_doom_nav_tap(&controls,XZ_GAME_NAV_QUIT)&&controls.quit);
    puts("Native Doom controls, latched fire, encoder taps, releases and RGB565 bounds passed");
}
