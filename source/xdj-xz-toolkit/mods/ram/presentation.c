/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "presentation.h"
#include "abi.h"
#include "../runtime.h"
#include <string.h>
/* Bounded read-only diagnostics for the five native data-retention boundaries. */
__attribute__((visibility("default"),used)) struct {
    uint32_t version;
    struct {uint32_t calls,deck,caller,arg,kept;} event[5];
} xz_mods_ram_view_trace_v1={.version=1};
static void trace_view(unsigned kind,unsigned deck,uintptr_t caller,unsigned arg,int kept){
    __atomic_store_n(&xz_mods_ram_view_trace_v1.event[kind].deck,deck,__ATOMIC_RELAXED);
    __atomic_store_n(&xz_mods_ram_view_trace_v1.event[kind].caller,(uint32_t)caller,__ATOMIC_RELAXED);
    __atomic_store_n(&xz_mods_ram_view_trace_v1.event[kind].arg,arg,__ATOMIC_RELAXED);
    __atomic_store_n(&xz_mods_ram_view_trace_v1.event[kind].kept,!!kept,__ATOMIC_RELAXED);
    __atomic_add_fetch(&xz_mods_ram_view_trace_v1.event[kind].calls,1,__ATOMIC_RELEASE);
}
static void (*stock_info_to_deck)(unsigned);
static int (*stock_ex_wave_write)(unsigned,const void *,unsigned);
static void (*stock_title_set)(const void *,unsigned,unsigned);
static void (*stock_wave_clear)(unsigned,unsigned),(*stock_ex_wave_clear)(unsigned,unsigned);
static void (*stock_music_get)(unsigned,void *);
static uint32_t (*stock_max_time)(unsigned);
static unsigned (*stock_source_select)(unsigned);
static unsigned (*stock_node_get)(unsigned),(*stock_device_get)(unsigned),(*stock_browse_get)(unsigned);
static int retained_identity(unsigned deck,uint32_t record[7]){
    if(deck>=2)return 0;
    stock_music_get(deck,record);
    if(record[0]||record[1])return 0;
    return xz_ram_music_retained(deck,record);
}
static int same_id(const void *a,const void *b){
    return !memcmp(a,b,3)&&!memcmp((const unsigned char *)a+4,(const unsigned char *)b+4,4);
}
static int retained_current(unsigned deck,uint32_t record[7]){
    uint32_t current[7],after[7],epoch,after_epoch;
    if(deck>=2||!xz_ram_music_owned_epoch(deck,record,&epoch))return 0;
    stock_music_get(deck,current);
    if(!xz_ram_music_owned_epoch(deck,after,&after_epoch)||epoch!=after_epoch||memcmp(record,after,sizeof(after)))return 0;
    return !(current[0]||current[1])||same_id(current,record);
}
static void title_set_for_caller(const void *rows,unsigned count,unsigned deck,uintptr_t caller){
    uint32_t record[7];
    int keep=!count&&caller==0xf56a8&&retained_current(deck,record);
    trace_view(0,deck,caller,count,keep);if(keep)return;
    stock_title_set(rows,count,deck);
}
static void title_set_hook(const void *rows,unsigned count,unsigned deck){
    title_set_for_caller(rows,count,deck,(uintptr_t)__builtin_return_address(0));
}
static void info_to_deck_for_caller(unsigned deck,uintptr_t caller){
    uint32_t record[7];unsigned char rows[9*0x224];int empty=1;
    if(caller==0xf5be0&&retained_current(deck,record)&&!xz_read_memory(0x224e7f0,rows,sizeof(rows))){
        for(unsigned row=0;row<9;row++){
            uint16_t tag;memcpy(&tag,rows+row*0x224+8,2);
            /* Tag0 is a placeholder even when its localized text is nonempty. */
            if(tag){empty=0;break;}
        }
        if(empty){trace_view(1,deck,caller,0,1);return;}
    }
    trace_view(1,deck,caller,0,0);stock_info_to_deck(deck);
}
static void info_to_deck_hook(unsigned deck){info_to_deck_for_caller(deck,(uintptr_t)__builtin_return_address(0));}
static uint32_t max_time_for_caller(unsigned deck,uintptr_t caller){
    uint32_t value=stock_max_time(deck),record[7],manager,watcher,frame,total;
    int display=caller==0xec440||caller==0xec48c||caller==0xec52c||caller==0xeec2c||caller==0x1425e0;
    if(value||!display||!retained_current(deck,record))return value;
    if(xz_read_memory(0x1c8981c,&manager,4)||manager<0x10000||
       xz_read_memory(manager+0x38,&watcher,4)||watcher<0x10000||
       xz_read_memory(watcher+0xc+deck*4,&frame,4)||frame<0x10000||
       xz_read_memory(frame+8,&total,4)||!total||total>=0x7fffffff)return value;
    return total;
}
static uint32_t max_time_hook(unsigned deck){
    return max_time_for_caller(deck,(uintptr_t)__builtin_return_address(0));
}
static int owned_wave(unsigned deck,int detailed){
    uint32_t record[7];if(!retained_current(deck,record))return 0;
    if(detailed){
        uint32_t valid,head[7],asset_valid,pointer,count;unsigned char id[8];
        if(xz_read_memory(0x227080c+deck*0x1e898,&valid,4)||valid!=1||
           xz_read_memory(0x2251f78+deck*0x1e898,head,sizeof(head))||!same_id(head+1,record)||head[4]<0x10000||
           xz_read_memory(head[4]+0x2c,id,8)||!same_id(id,record))return 0;
        unsigned format=head[3];if(format>1)return 0;
        if(xz_read_memory(head[4]+(format?0x24:0x10),&asset_valid,4)||asset_valid!=1||
           xz_read_memory(head[4]+(format?0xe44:0xb10),&pointer,4)||pointer<0x10000||
           xz_read_memory(head[4]+(format?0xe48:0xb14),&count,4)||!count)return 0;
        return 1;
    }
    uint32_t dto[4];
    if(!xz_read_memory(0x2335048+deck*0x394,dto,sizeof(dto))&&dto[1]==1&&same_id(dto+2,record))return 1;
    return !xz_read_memory(0x2335770+deck*0x718,dto,sizeof(dto))&&dto[1]==1&&same_id(dto+2,record);
}
static int ex_wave_write_for_caller(unsigned deck,const void *id,unsigned format,uintptr_t caller){
    uint32_t record[7],head[7],valid,pointer,count,staging;unsigned char cached_id[8];
    if(caller==0xa4404&&format<=1&&id&&retained_current(deck,record)&&same_id(id,record)&&
       !xz_read_memory(0x2251f78+deck*0x1e898,head,sizeof(head))&&same_id(head+1,record)&&head[4]>=0x10000&&
       !xz_read_memory(head[4]+0x2c,cached_id,8)&&same_id(cached_id,id)&&
       !xz_read_memory(0x2205d20+deck*0x114d8+(format?0x10cf4:0x10ce4),&staging,4)&&!staging&&
       !xz_read_memory(head[4]+(format?0x24:0x10),&valid,4)&&valid==1&&
       !xz_read_memory(head[4]+(format?0xe44:0xb10),&pointer,4)&&pointer>=0x10000&&
       !xz_read_memory(head[4]+(format?0xe48:0xb14),&count,4)&&count){trace_view(4,deck,caller,format,1);return 1;}
    trace_view(4,deck,caller,format,0);
    return stock_ex_wave_write(deck,id,format);
}
static int ex_wave_write_hook(unsigned deck,const void *id,unsigned format){
    return ex_wave_write_for_caller(deck,id,format,(uintptr_t)__builtin_return_address(0));
}
static void wave_clear_for_caller(unsigned deck,unsigned request,uintptr_t caller){
    int keep=request==1&&caller==0xb2c48&&owned_wave(deck,0);
    trace_view(2,deck,caller,request,keep);if(keep)return;
    stock_wave_clear(deck,request);
}
static void wave_clear_hook(unsigned deck,unsigned request){
    wave_clear_for_caller(deck,request,(uintptr_t)__builtin_return_address(0));
}
static void ex_wave_clear_for_caller(unsigned deck,unsigned request,uintptr_t caller){
    int query=caller==0xa24c8||caller==0xa33e4||caller==0xa4744||caller==0xa4af0;
    int keep=request==1&&query&&owned_wave(deck,1);
    trace_view(3,deck,caller,request,keep);if(keep)return;
    stock_ex_wave_clear(deck,request);
}
static void ex_wave_clear_hook(unsigned deck,unsigned request){
    ex_wave_clear_for_caller(deck,request,(uintptr_t)__builtin_return_address(0));
}
/* The native play-group and cue controls read this independent source selector.
 * USB STOP clears its cell even while the complete RAM track remains loaded.
 * Keep nonempty native replacements authoritative and never rewrite mount state. */
static unsigned source_select_for_caller(unsigned deck,uintptr_t caller){
    unsigned current=stock_source_select(deck);uint32_t record[7];
    int presentation=caller==0xefef8||caller==0xeec14||caller==0xec728||caller==0xf11f4||caller==0x1425c8;
    if(presentation&&!current&&retained_identity(deck,record)){
        const unsigned char *id=(const unsigned char *)record;
        if(!id[0]&&id[1]==2)return 3;
        if(!id[0]&&id[1]==3)return 2;
    }
    return current;
}
static unsigned source_select_hook(unsigned deck){
    return source_select_for_caller(deck,(uintptr_t)__builtin_return_address(0));
}
static unsigned scalar_get(unsigned deck,unsigned (*stock)(unsigned),unsigned byte){
    unsigned current=stock(deck);uint32_t record[7];
    if(!current&&retained_identity(deck,record))return ((const unsigned char *)record)[byte];
    return current;
}
static unsigned node_get_hook(unsigned deck){return scalar_get(deck,stock_node_get,0);}
static unsigned device_get_hook(unsigned deck){return scalar_get(deck,stock_device_get,1);}
static unsigned browse_get_hook(unsigned deck){return scalar_get(deck,stock_browse_get,2);}
static void music_get_hook(unsigned deck,void *output){
    stock_music_get(deck,output);
    if(!output||deck>=2)return;
    uint32_t current[7];memcpy(current,output,sizeof(current));
    if(current[0]||current[1])return;
    if(xz_ram_music_retained(deck,current))memcpy(output,current,sizeof(current));
}
void xz_ram_presentation_snapshot(int deck,uint32_t epoch){
    if(!stock_music_get||deck<0||deck>=2)return;
    uint32_t record[7]={0};stock_music_get((unsigned)deck,record);
    xz_ram_music_snapshot(deck,epoch,record);
}
int xz_ram_presentation_start(void){
    return (!stock_info_to_deck&&xz_hook_arm(0xd1d3c,xz_ram_hook_guard(0xd1d3c),(void *)info_to_deck_hook,(void **)&stock_info_to_deck))||
        (!stock_ex_wave_write&&xz_hook_arm(0xa761c,xz_ram_hook_guard(0xa761c),(void *)ex_wave_write_hook,(void **)&stock_ex_wave_write))||
        (!stock_max_time&&xz_hook_arm(0xa904c,xz_ram_hook_guard(0xa904c),(void *)max_time_hook,(void **)&stock_max_time))||
        (!stock_title_set&&xz_hook_arm(0xd174c,xz_ram_hook_guard(0xd174c),(void *)title_set_hook,(void **)&stock_title_set))||
        (!stock_wave_clear&&xz_hook_arm(0x13a470,xz_ram_hook_guard(0x13a470),(void *)wave_clear_hook,(void **)&stock_wave_clear))||
        (!stock_ex_wave_clear&&xz_hook_arm(0x13a7f4,xz_ram_hook_guard(0x13a7f4),(void *)ex_wave_clear_hook,(void **)&stock_ex_wave_clear))||
        (!stock_source_select&&xz_hook_arm(0xa9374,xz_ram_hook_guard(0xa9374),(void *)source_select_hook,(void **)&stock_source_select))||
        (!stock_music_get&&xz_hook_arm(0x13951c,xz_ram_hook_guard(0x13951c),(void *)music_get_hook,(void **)&stock_music_get))||
        (!stock_node_get&&xz_hook_arm(0x139bf0,xz_ram_hook_guard(0x139bf0),(void *)node_get_hook,(void **)&stock_node_get))||
        (!stock_device_get&&xz_hook_arm(0x139c0c,xz_ram_hook_guard(0x139c0c),(void *)device_get_hook,(void **)&stock_device_get))||
        (!stock_browse_get&&xz_hook_arm(0x139c28,xz_ram_hook_guard(0x139c28),(void *)browse_get_hook,(void **)&stock_browse_get))?-1:0;
}
