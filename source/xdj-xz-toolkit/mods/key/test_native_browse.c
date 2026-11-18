/* SPDX-License-Identifier: MIT */
#include "native_browse.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#undef assert
#define assert(p) ((p)?(void)0:(fprintf(stderr,"FAIL %s line %d\n",#p,__LINE__),exit(1)))
#endif
static unsigned mode=3,calls,visible;static int invalid,opens=1,result=1;
static int read_memory(void *ctx,uint32_t address,void *out,size_t n){
    (void)ctx;
    static const unsigned char menu[]={0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char getter[]={0xf4,0x35,0x0e,0xe3,0x24,0x32,0x40,0xe3,0x00,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
    static const unsigned char menu_getter[]={0xf4,0x35,0x0e,0xe3,0x24,0x32,0x40,0xe3,0x14,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
    if(address==0xdf1e8&&n==8){memcpy(out,menu,n);if(invalid)((unsigned char*)out)[0]^=1;return 0;}
    if(address==0xd9488&&n==16){memcpy(out,getter,n);return 0;}
    if(address==0x224e5f4&&n==4){memcpy(out,&mode,n);return 0;}
    if(address==0xd94a8&&n==16){memcpy(out,menu_getter,n);if(invalid==2)((unsigned char*)out)[0]^=1;return 0;}
    if(address==0x224e608&&n==4){memcpy(out,&visible,n);return 0;}
    return -1;
}
int xz_read_memory(uint32_t address,void *out,size_t n){return read_memory(NULL,address,out,n);}
static int invoke(void *ctx,uint32_t input[3]){(void)ctx;calls++;assert(input[0]==2&&input[1]==0&&input[2]==2);input[2]=0;if(opens)visible=1;return result;}
int main(void){
    unsigned value;assert(!xz_native_browse_state(read_memory,NULL,&value)&&value==3);
    for(mode=2;mode<=9;mode++)if(mode!=6){visible=0;unsigned before=calls;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)==1&&calls==before+1&&visible);}
    mode=6;unsigned before=calls;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)==1&&calls==before);
    mode=1;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)<0&&calls==before);
    mode=3;invalid=1;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)<0&&calls==before);
    invalid=0;mode=0;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)<0&&calls==before);
    mode=3;visible=1;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)==1&&calls==before);
    visible=0;invalid=2;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)<0&&calls==before);
    invalid=0;visible=2;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)<0&&calls==before);
    visible=0;opens=0;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)<0&&calls==before+1);
    result=0;assert(xz_native_browse_sort_call(read_memory,NULL,invoke,NULL)<0&&calls==before+2);
    puts("PASS short MENU release, verified open state, already-open no-op and guard refusal");
}
