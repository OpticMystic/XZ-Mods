/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Native key acceptance requires active assertions
#endif
#include "native.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct region {uint32_t address;size_t size;unsigned char data[0xf00];};
static struct region regions[12];static int torn;static unsigned key_reads;
static void put32(unsigned char *p,uint32_t v){memcpy(p,&v,4);}
static void put64(unsigned char *p,double v){memcpy(p,&v,8);}
static int memory(void *unused,uint32_t address,void *out,size_t n){
    (void)unused;
    for(unsigned i=0;i<12;i++)if(address>=regions[i].address&&(uint64_t)address+n<=regions[i].address+regions[i].size){
        memcpy(out,regions[i].data+address-regions[i].address,n);
        if(address==0x224e19c&&++key_reads==2&&torn)((unsigned char*)out)[0x22]^=1;
        return 0;
    }
    return -1;
}
int xz_read_memory(uint32_t a,void *out,size_t n){return memory(NULL,a,out,n);}
int xz_audio_output_deck(const void *p,uint32_t *rate,uint32_t *epoch){
    *rate=44100;*epoch=7;return (uintptr_t)p==0x300d0?0:(uintptr_t)p==0x400d0?1:-1;
}
int main(void){
    static const unsigned char guards[3][16]={
        {0x1c,0x31,0x0b,0xe3,0x22,0x32,0x40,0xe3,0x00,0x32,0x83,0xe0,0x15,0x3c,0x83,0xe2},
        {0x9c,0x31,0x0e,0xe3,0x24,0x32,0x40,0xe3,0x89,0x2f,0xa0,0xe3},
        {0x9c,0x3e,0x90,0xe5,0x00,0x00,0x51,0xe3,0x45,0x20,0xd3,0xe5}};
    const uint32_t addresses[12]={0x13aa08,0xd9324,0x61a34,0x10e9688,0x20000,0x21000,0x30000,0x40000,0x50000,0x51000,0x224e19c,0x222c61c};
    for(int i=0;i<12;i++){regions[i].address=addresses[i];regions[i].size=i<3?16:0xf00;}
    for(int i=0;i<3;i++)memcpy(regions[i].data,guards[i],16);
    put32(regions[3].data,0x20000);put32(regions[4].data+12,0x21000);put32(regions[4].data+16,0x21008);
    put32(regions[5].data,0x30000);put32(regions[5].data+4,0x40000);
    for(int d=0;d<2;d++){
        unsigned char *p=regions[6+d].data;p[0xe66]=1;put32(p+0xc8,0x70000+d*0x1000);put32(p+0xe9c,0x50000+d*0x1000);
        put64(regions[8+d].data+0x30,d?.05946309436:0);
        unsigned char *text=regions[10].data+d*0x224;text[0x20]=2;text[0x22]='8';text[0x24]='A';
    }
    put32(regions[11].data,1);
    struct xz_key_snapshot snapshot;assert(xz_key_native_valid(memory,NULL));
    assert(xz_key_native_snapshot(0,&snapshot)&&snapshot.epoch==7&&snapshot.master&&snapshot.original.root==9&&snapshot.original.minor);
    assert(xz_key_native_snapshot(1,&snapshot)&&!snapshot.master&&snapshot.tempo_ratio>1.0594&&snapshot.format==XZ_KEY_CAMELOT);
    regions[7].data[0xea2]=1;assert(xz_key_native_snapshot(1,&snapshot)&&snapshot.master_tempo);
    put32(regions[11].data+16,1);assert(xz_key_native_snapshot(0,&snapshot)&&!snapshot.master);
    put32(regions[11].data+16,0);
    key_reads=0;torn=1;assert(!xz_key_native_snapshot(0,&snapshot));torn=0;
    regions[6].data[0xe66]=0;assert(!xz_key_native_snapshot(0,&snapshot));regions[6].data[0xe66]=1;
    regions[10].data[0x20]=8;assert(!xz_key_native_snapshot(0,&snapshot));regions[10].data[0x20]=2;
    put64(regions[8].data+0x30,9);assert(!xz_key_native_snapshot(0,&snapshot));put64(regions[8].data+0x30,0);
    regions[0].data[0]^=1;assert(!xz_key_native_valid(memory,NULL));
    puts("PASS exact XZ 1.26 metadata guards, two decks, key format, tempo, Master Tempo, master ambiguity, load and torn-read gates");return 0;
}
