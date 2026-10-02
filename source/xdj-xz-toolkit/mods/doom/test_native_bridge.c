/* SPDX-License-Identifier: MIT */
#include "native_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct xz_game_input state={.magic=XZ_GAME_INPUT_MAGIC,.version=1,.owner=42,.heartbeat=1000};
static struct xz_game_native bridge={.input=&state};
static unsigned char input[24];
static void event(unsigned key,unsigned op,int pulse) {
    memset(input,0,sizeof(input)); uint16_t k=(uint16_t)key;
    memcpy(input+8,&k,2); input[11]=(unsigned char)op; memcpy(input+12,&pulse,4);
}
int main(void) {
    event(0x4101,0,0); assert(xz_game_decode(&bridge,0,input,1100)==1); assert(state.buttons[0]==1);
    event(0x4102,0,0); assert(xz_game_decode(&bridge,1,input,1100)==1); assert(state.buttons[1]==2);
    event(0x411a,0,0); assert(xz_game_decode(&bridge,0,input,1100)==1); assert(state.buttons[0]==9);
    event(0x4305,4,-8); assert(xz_game_decode(&bridge,0,input,1100)==1); assert((int32_t)state.jog[0]==-1);
    event(0x4305,4,3); assert(xz_game_decode(&bridge,0,input,1100)==1); assert((int32_t)state.jog[0]==1);
    event(0x4305,4,0); float speed=-.5f; memcpy(input+16,&speed,4);
    assert(xz_game_decode(&bridge,1,input,1100)==1); assert((int32_t)state.jog[1]==-1);
    state.owner=0;
    event(0x4101,2,0); assert(xz_game_decode(&bridge,0,input,1900)==1); assert(state.buttons[0]==8);
    event(0x4102,3,0); assert(xz_game_decode(&bridge,1,input,1900)==1); assert(state.buttons[1]==0);
    event(0x4101,0,0); assert(xz_game_decode(&bridge,0,input,1900)==0);
    event(0x4305,4,9); assert(xz_game_decode(&bridge,0,input,1900)==0);
    event(0x4119,2,0); assert(xz_game_decode(&bridge,0,input,1900)==0);
    state.owner=42;
    event(0x4101,0,0); assert(xz_game_decode(&bridge,0,input,1800)==0);
    assert(xz_game_decode(&bridge,2,input,1100)==0);
    puts("Game key ownership, both decks, jog pulses, crash expiry and release drain passed");
}
