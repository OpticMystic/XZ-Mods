/* SPDX-License-Identifier: MIT */
#include "input.h"
#include <string.h>
#include <math.h>
int xz_arcade_input_event(struct xz_arcade_input *s,struct xz_arcade *g,int active,int deck,const void *input,uint32_t now){
    if(!input||deck<0||deck>1)return 0;
    const unsigned char *p=input;uint16_t key;memcpy(&key,p+8,2);unsigned op=p[11]&15;
    if(key==0x4305&&op==4&&active){
        int32_t delta;float speed;memcpy(&delta,p+12,4);memcpy(&speed,p+16,4);
        /* Preserve signed pulse magnitude; +20 is never a direction source. */
        float gain=xz_arcade_jog_gain(g);
        float movement=delta?delta*gain:isfinite(speed)?speed*gain:0;
        xz_arcade_move(g,deck,movement,now);return 1;
    }
    /* PLAY/PAUSE, track search and every other transport key retain stock
       presses, repeats and releases. Jog touch must stay with the game too,
       otherwise touching the platter pauses/scratches the playing track. */
    if(key==0x4101)return 0;
    int index=key==0x4102?XZ_ARCADE_CUE_KEY:key==0x4306?10:
        key>=0x4119&&key<=0x4120?(int)key-0x4119+XZ_ARCADE_PAD_A:-1;
    if(index<0)return 0;
    uint32_t bit=1u<<(unsigned)index;
    if(op==2||op==3){int owned=!!(s->owned[deck]&bit);s->owned[deck]&=~bit;return owned;}
    if(active&&op==0){if(!(s->owned[deck]&bit)){s->owned[deck]|=bit;if(index<10)xz_arcade_key(g,deck,(enum xz_arcade_key)index,now);}return 1;}
    return active||!!(s->owned[deck]&bit);
}
