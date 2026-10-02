/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "xz_controls.h"
#include "vendor/doomkeys.h"
#include "../ui/pixel_font.h"
#include <string.h>

struct button { int x,y,w,h,key; const char *label; };
static const struct button buttons[] = {
    {0,0,80,80,KEY_ESCAPE,"MENU"}, {0,80,80,80,KEY_ENTER,"ENTER"},
    {0,160,80,80,KEY_FIRE,"FIRE"}, {0,240,80,80,KEY_USE,"USE"},
    {0,320,80,80,KEY_TAB,"MAP"}, {720,0,80,80,KEY_UPARROW,"UP"},
    {720,80,80,80,KEY_LEFTARROW,"LEFT"}, {720,160,80,80,KEY_RIGHTARROW,"RIGHT"},
    {720,240,80,80,KEY_DOWNARROW,"DOWN"}, {720,320,80,80,KEY_RSHIFT,"RUN"},
    {640,408,152,64,0,"EXIT"}
};

void xz_doom_touch(struct xz_doom_controls *c,int down,int x,int y) {
    if(c->held) { c->desired[c->held]=0; c->held=0; }
    if(!down || x<0 || x>=800 || y<0 || y>=480) return;
    for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);i++) {
        const struct button *b=&buttons[i];
        if(x<b->x || x>=b->x+b->w || y<b->y || y>=b->y+b->h) continue;
        if(!b->key) c->quit=1;
        else if(b->key==KEY_FIRE) c->desired[KEY_FIRE]=(unsigned char)(c->fire=!c->fire);
        else { c->held=b->key; c->desired[b->key]=1; }
        return;
    }
}
int xz_doom_key(struct xz_doom_controls *c,int *pressed,unsigned char *key) {
    for(unsigned i=0;i<256;i++) if(c->sent[i]!=(c->desired[i]|c->physical[i])) {
        *pressed=c->desired[i]|c->physical[i]; *key=(unsigned char)i; c->sent[i]=(unsigned char)*pressed; return 1;
    }
    return 0;
}
void xz_doom_physical(struct xz_doom_controls *c,uint32_t buttons,int turn) {
    memset(c->physical,0,sizeof(c->physical));
    c->physical[KEY_UPARROW]=!!(buttons&((1u<<0)|(1u<<2)));
    c->physical[KEY_DOWNARROW]=!!(buttons&(1u<<3));
    c->physical[KEY_FIRE]=!!(buttons&((1u<<1)|(1u<<4)));
    c->physical[KEY_USE]=!!(buttons&(1u<<5));
    c->physical[',']=!!(buttons&(1u<<6)); c->physical['.']=!!(buttons&(1u<<7));
    c->physical[KEY_ENTER]=!!(buttons&(1u<<8)); c->physical[KEY_ESCAPE]=!!(buttons&(1u<<9));
    c->physical[KEY_LEFTARROW]=turn<0; c->physical[KEY_RIGHTARROW]=turn>0;
}
static void text(uint16_t *out,int x,int y,const char *s,uint16_t color) {
    for(;*s;s++,x+=12) for(int row=0;row<7;row++) {
        unsigned bits=xz_pixel_font_row((unsigned char)*s,(unsigned)row);
        for(int col=0;col<5;col++) if(bits&(16u>>col))
            for(int dy=0;dy<2;dy++) for(int dx=0;dx<2;dx++)
                out[(y+row*2+dy)*800+x+col*2+dx]=color;
    }
}
void xz_doom_compose(uint16_t *out,const uint32_t *game,const struct xz_doom_controls *c) {
    memset(out,0,800*480*sizeof(*out));
    for(unsigned y=0;y<200;y++) for(unsigned x=0;x<320;x++) {
        uint32_t p=game[y*320+x];
        uint16_t rgb=(uint16_t)(((p>>8)&0xf800)|((p>>5)&0x7e0)|((p>>3)&31));
        unsigned at=y*1600+80+x*2;
        out[at]=out[at+1]=out[at+800]=out[at+801]=rgb;
    }
    for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);i++) {
        const struct button *b=&buttons[i];
        uint16_t bg=b->key && c->desired[b->key]?0x9460:0x2124;
        for(int y=b->y+2;y<b->y+b->h-2;y++) for(int x=b->x+2;x<b->x+b->w-2;x++) out[y*800+x]=bg;
        int x=b->x+(b->w-(int)strlen(b->label)*12)/2;
        text(out,x,b->y+b->h/2-7,b->label,0xffff);
    }
    text(out,160,419,"DOOM / NATIVE XZ",0xfd20);
    text(out,160,443,"FIRE TOGGLES / USE OPENS",0xffff);
}
