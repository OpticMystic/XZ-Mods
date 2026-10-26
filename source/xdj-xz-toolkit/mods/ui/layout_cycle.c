/* SPDX-License-Identifier: MIT */
#include "layout_cycle.h"
/* DECK VIEW (settings deck_view) and the DECKS button (deck_view_button), as the saved settings last set them
   or a control changed them since. Atomics: the shell, the classic screen and the UI runtime read these on
   their own threads. */
static int preference=XZ_LAYOUT_TWO,button_shown=1;
static unsigned revision;
int xz_deck_layout_get(void){return __atomic_load_n(&preference,__ATOMIC_ACQUIRE);}
void xz_deck_layout_set(int layout){
 if((unsigned)layout>=XZ_LAYOUT_COUNT)return;
 __atomic_store_n(&preference,layout,__ATOMIC_RELEASE);__atomic_add_fetch(&revision,1u,__ATOMIC_ACQ_REL);
}
void xz_deck_layout_load(int layout){if((unsigned)layout<XZ_LAYOUT_COUNT)__atomic_store_n(&preference,layout,__ATOMIC_RELEASE);}
unsigned xz_deck_layout_revision(void){return __atomic_load_n(&revision,__ATOMIC_ACQUIRE);}
int xz_deck_layout_next(int layout){return (unsigned)layout<XZ_LAYOUT_COUNT?(layout+1)%XZ_LAYOUT_COUNT:XZ_LAYOUT_FOUR;}
int xz_deck_view_button_shown(void){return __atomic_load_n(&button_shown,__ATOMIC_ACQUIRE);}
void xz_deck_view_button_load(int shown){__atomic_store_n(&button_shown,!!shown,__ATOMIC_RELEASE);}
const char *xz_deck_layout_name(int layout){
 return layout==XZ_LAYOUT_FOUR?"4 DECKS, NUMBER ORDER":layout==XZ_LAYOUT_MIXER?"4 DECKS, MIXER ORDER":"2 DECKS";
}
const char *xz_deck_layout_order(int layout){
 return layout==XZ_LAYOUT_FOUR?"1 2 3 4":layout==XZ_LAYOUT_MIXER?"3 1 2 4":"1 2";
}
int xz_classic_layout_hit(int x,int y){return x>=XZ_CLASSIC_LAYOUT_X&&x<XZ_CLASSIC_LAYOUT_X+XZ_CLASSIC_LAYOUT_W&&y>=0&&y<XZ_CLASSIC_LAYOUT_H;}
void xz_deck_layout_draw(struct xz_canvas c,int x,int y,int width,int height,int layout,int classic){
 const char *label=layout==XZ_LAYOUT_TWO?(classic?"2 / NATIVE 1-2":"2 DECKS"):layout==XZ_LAYOUT_MIXER?"4 / 3-1-2-4":"4 / 1-2-3-4";
 xz_rect(c,x,y,width,height,0x253044);xz_border(c,x,y,width,height,0x708098);
 xz_ui_chrome_fit(c,x+5,y+5,width-10,label,1,0xffffff);
 for(int i=0;i<XZ_LAYOUT_COUNT;i++)xz_rect(c,x+width/2-10+i*8,y+height-4,5,2,i==layout?0xffe633:0x708098);
}
