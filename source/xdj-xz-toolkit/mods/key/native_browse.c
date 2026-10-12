/* SPDX-License-Identifier: MIT */
#include "native_browse.h"
#include "../runtime.h"
#include <string.h>
#define MENU 0xdf1e8u
#define MODE_GETTER 0xd9488u
#define MODE_VALUE 0x224e5f4u
#define MENU_GETTER 0xd94a8u
#define MENU_VALUE (MODE_VALUE+0x14u)
static int menu_visible(xz_browse_read read,void *ctx,unsigned *visible){
    static const unsigned char getter[16]={0xf4,0x35,0x0e,0xe3,0x24,0x32,0x40,0xe3,0x14,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
    unsigned char actual[16];
    return !read||read(ctx,MENU_GETTER,actual,16)||memcmp(actual,getter,16)||
        read(ctx,MENU_VALUE,visible,4)||*visible>1?-1:0;
}
int xz_native_browse_state(xz_browse_read read,void *ctx,unsigned *mode){
    static const unsigned char menu[8]={0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char getter[16]={0xf4,0x35,0x0e,0xe3,0x24,0x32,0x40,0xe3,0x00,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
    unsigned char actual[16];unsigned value;
    if(!read||!mode||read(ctx,MENU,actual,8)||memcmp(actual,menu,8)||
        read(ctx,MODE_GETTER,actual,16)||memcmp(actual,getter,16)||read(ctx,MODE_VALUE,&value,4))return -1;
    /* 1 is the play screen; 6 is the separate long-MENU sort screen. */
    if(value<1||value>9)return -1;
    *mode=value;return 0;
}
int xz_native_browse_sort_call(xz_browse_read read,void *ctx,xz_browse_menu_call menu,void *user){
    unsigned mode,visible;
    if(!menu||xz_native_browse_state(read,ctx,&mode)||mode==1)return -1;
    if(mode==6)return 1;
    if(menu_visible(read,ctx,&visible))return -1;
    if(visible)return 1;
    /* The firmware's <=2-count release branch opens BrowseMenuDisp. A count
       of 3 takes the different long-press branch into browse mode 6. */
    uint32_t input[3]={2,0,2};
    if(menu(user,input)<=0)return -1;
    return !menu_visible(read,ctx,&visible)&&visible?1:-1;
}
static int memory(void *ctx,uint32_t address,void *out,size_t n){(void)ctx;return xz_read_memory(address,out,n);}
static int stock(void *ctx,uint32_t input[3]){(void)ctx;return ((int(*)(uint32_t*))(uintptr_t)MENU)(input);}
int xz_native_browse_open_sort(void){return xz_native_browse_sort_call(memory,NULL,stock,NULL);}
unsigned xz_native_browse_mode(void){unsigned mode=0;return xz_native_browse_state(memory,NULL,&mode)?0:mode;}
