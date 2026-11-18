/* SPDX-License-Identifier: MIT */
#include "traffic.h"
#include "../runtime.h"
#include <string.h>
#define TRAFFIC 0x17c58cu
static int (*original)(void *);
static unsigned packed_master;
struct traffic_proof {uint32_t version,calls,overrides,key_id,detuned;};
__attribute__((visibility("default"))) struct traffic_proof xz_mods_key_traffic_proof_v1={.version=1};
int xz_key_traffic_ids(const struct xz_playing_key *key,uint32_t ids[4]){
    if(!key||!ids)return 0;
    char label[8];xz_music_key_format(key->key,XZ_KEY_CAMELOT,label);
    unsigned n=0;size_t i=0;while(label[i]>='0'&&label[i]<='9')n=n*10+(unsigned)(label[i++]-'0');
    if(n<1||n>12||(label[i]!='A'&&label[i]!='B')||label[i+1])return 0;
    if(key->cents < -25 || key->cents > 25){memset(ids,0,16);return 1;}
    /* Stock key IDs are 1A=1,1B=2,...12B=24, proven by the native updater's
       +/-1 relative and +/-2 adjacent-key arithmetic. */
    unsigned id=n*2-(unsigned)(label[i]=='A');
    ids[0]=id;ids[1]=(id&1)?id+1:id-1;ids[2]=id>2?id-2:id+22;ids[3]=id<=22?id+2:id-22;
    return 1;
}
void xz_key_traffic_master(const struct xz_playing_key *key){
    uint32_t ids[4];unsigned packed=0;
    if(xz_key_traffic_ids(key,ids))packed=0x100u|ids[0]|(ids[0]?0:0x200u);
    __atomic_store_n(&packed_master,packed,__ATOMIC_RELEASE);
}
static int hook(void *ctx){
    int result=original(ctx);__atomic_add_fetch(&xz_mods_key_traffic_proof_v1.calls,1,__ATOMIC_RELAXED);
    unsigned packed=__atomic_load_n(&packed_master,__ATOMIC_ACQUIRE);
    uintptr_t base=(uintptr_t)ctx;
    if(result==1&&(packed&0x100u)&&base>=0x10000&&!(base&3)&&base<=UINT32_MAX-0x108f0u){
        uint32_t track;memcpy(&track,(unsigned char*)ctx+0x108d8,4);if(!track)return result;
        uint32_t ids[4]={0};unsigned id=packed&255;
        if(id){ids[0]=id;ids[1]=(id&1)?id+1:id-1;ids[2]=id>2?id-2:id+22;ids[3]=id<=22?id+2:id-22;}
        /* Only these four volatile browse compatibility IDs are replaced. The
           original analyzed key, player key strings and USB database stay intact. */
        memcpy((unsigned char*)ctx+0x108e0,ids,sizeof(ids));
        __atomic_add_fetch(&xz_mods_key_traffic_proof_v1.overrides,1,__ATOMIC_RELAXED);
        xz_mods_key_traffic_proof_v1.key_id=id;xz_mods_key_traffic_proof_v1.detuned=!!(packed&0x200u);
    }
    return result;
}
int xz_key_traffic_start(void){
    static const unsigned char guard[8]={0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    if(original)return 0;
    return xz_hook_arm(TRAFFIC,guard,(void*)hook,(void**)&original);
}
