/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "traffic.h"
#include "../runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
static int refuse=1,stock_result=1;static int (*active_hook)(void*);
static int stock(void *ctx){uint32_t ids[4]={1,2,23,3};memcpy((unsigned char*)ctx+0x108e0,ids,16);return stock_result;}
int xz_hook_arm(uint32_t address,const unsigned char expected[8],void *replacement,void **out){
    assert(address==0x17c58c&&!memcmp(expected,(unsigned char[]){0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1},8));
    if(refuse)return -1;active_hook=replacement;*out=(void*)stock;return 0;
}
int main(void){
    uint32_t ids[4];struct xz_music_key key;
    assert(xz_music_key_parse("9A",&key));struct xz_playing_key master={.key=key,.cents=-13};
    assert(xz_key_traffic_ids(&master,ids)&&ids[0]==17&&ids[1]==18&&ids[2]==15&&ids[3]==19);
    for(int minor=0;minor<=1;minor++)for(int root=0;root<12;root++){
        master=(struct xz_playing_key){.key={root,minor}};assert(xz_key_traffic_ids(&master,ids));
        for(int i=0;i<4;i++){assert(ids[i]>=1&&ids[i]<=24);for(int j=0;j<i;j++)assert(ids[i]!=ids[j]);}
    }
    master.cents=26;assert(xz_key_traffic_ids(&master,ids)&&!ids[0]&&!ids[1]&&!ids[2]&&!ids[3]);
    master.cents=-26;assert(xz_key_traffic_ids(&master,ids)&&!ids[0]);
    master.key.root=-1;assert(!xz_key_traffic_ids(&master,ids));assert(!xz_key_traffic_ids(NULL,ids));
    assert(xz_key_traffic_start()<0);
    void *ctx=mmap((void*)0x31000000,0x11000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(ctx==(void*)0x31000000);
    uint32_t track=123;memcpy((unsigned char*)ctx+0x108d8,&track,4);
    refuse=0;assert(!xz_key_traffic_start()&&active_hook);
    xz_music_key_parse("9A",&master.key);master.cents=-13;xz_key_traffic_master(&master);
    assert(active_hook(ctx)==1);memcpy(ids,(unsigned char*)ctx+0x108e0,16);
    assert(ids[0]==17&&ids[1]==18&&ids[2]==15&&ids[3]==19);
    uint32_t after;memcpy(&after,(unsigned char*)ctx+0x108d8,4);assert(after==track);
    master.cents=26;xz_key_traffic_master(&master);assert(active_hook(ctx)==1);memcpy(ids,(unsigned char*)ctx+0x108e0,16);assert(!ids[0]&&!ids[1]&&!ids[2]&&!ids[3]);
    xz_key_traffic_master(NULL);assert(active_hook(ctx)==1);memcpy(ids,(unsigned char*)ctx+0x108e0,16);assert(ids[0]==1&&ids[1]==2&&ids[2]==23&&ids[3]==3);
    stock_result=0;master.cents=0;xz_key_traffic_master(&master);assert(active_hook(ctx)==0);memcpy(ids,(unsigned char*)ctx+0x108e0,16);assert(ids[0]==1);
    munmap(ctx,0x11000);
    puts("PASS effective-master native traffic IDs, all24keys, relative/adjacent matches and detuned/invalid refusal");
}
