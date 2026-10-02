/* SPDX-License-Identifier: MIT */
#include "wave_rider_source.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Wave Rider source acceptance requires active assertions
#endif

enum { PLAYER=0x10000, OWNER=0x12000, END=0x13000 };
struct memory {uint8_t data[END-PLAYER];uint32_t fail,mutate;unsigned visits;};
static void put(struct memory *m,uint32_t address,uint32_t value){memcpy(m->data+address-PLAYER,&value,4);}
static int read_memory(void *context,uint32_t address,void *out,size_t n){
    struct memory *m=context;
    if(address<PLAYER||address>=END||n>END-address||address==m->fail)return -1;
    if(address==m->mutate&&++m->visits==2)return -1;
    memcpy(out,m->data+address-PLAYER,n);return 0;
}
static void stock_position(void){
    struct memory m={0};struct xz_wave_rider_position p;
    put(&m,PLAYER+0xec8,OWNER);put(&m,PLAYER+0xed8,0x12500);put(&m,OWNER+8,0x12600);
    put(&m,PLAYER+0xe78,441000);put(&m,PLAYER+0xedc,88200);m.data[0xe77]=1;
    assert(!xz_wave_rider_position_read(read_memory,&m,PLAYER,&p));
    assert(p.ticks==441000&&p.playing&&!p.audible_pause);
    put(&m,OWNER+8,0x12500);m.data[0xe77]=0;
    assert(!xz_wave_rider_position_read(read_memory,&m,PLAYER,&p));
    assert(p.ticks==88200&&!p.playing&&p.audible_pause);
    put(&m,PLAYER+0xedc,(uint32_t)-4410);
    assert(!xz_wave_rider_position_read(read_memory,&m,PLAYER,&p)&&p.ticks== -4410);
    m.fail=PLAYER+0xedc;assert(xz_wave_rider_position_read(read_memory,&m,PLAYER,&p)<0&&!p.ticks);
    m.fail=0;m.mutate=PLAYER+0xec8;
    assert(xz_wave_rider_position_read(read_memory,&m,PLAYER,&p)<0);
    m.mutate=0;m.data[0xe77]=7;
    assert(xz_wave_rider_position_read(read_memory,&m,PLAYER,&p)<0);
    assert(xz_wave_rider_position_read(read_memory,&m,UINT32_MAX,&p)<0);
    puts("PASS stock 44100 Hz source ticks, audible pause, seek, invalid/changing native state");
}
static void real_sample_window(void){
    enum {COUNT=4000};uint8_t bands[COUNT*3];struct xz_wave_rider_snapshot a={0},b={0};
    for(unsigned i=0;i<COUNT;i++)for(unsigned k=0;k<3;k++)bands[i*3+k]=(uint8_t)((i*7+k*31)%251);
    assert(!xz_wave_rider_window(&a,bands,COUNT,1500.5));
    assert(a.valid&&a.position==1500.5&&a.first==1200&&a.count==1800&&a.total==COUNT);
    assert(!memcmp(a.bands,bands+1200*3,1800*3));
    assert(!xz_wave_rider_window(&b,bands,COUNT,1500.5)&&!memcmp(a.bands,b.bands,a.count*3));
    bands[(1500+100)*3+1]^=127;
    assert(!xz_wave_rider_window(&b,bands,COUNT,1500.5));
    assert(b.bands[(1500+100-1200)*3+1]!=a.bands[(1500+100-1200)*3+1]);
    assert(!xz_wave_rider_window(&a,bands,COUNT,300)&&a.first==0);
    assert(!xz_wave_rider_window(&a,bands,COUNT, -150)&&a.first==0&&a.position==0);
    assert(!xz_wave_rider_window(&a,bands,COUNT,COUNT+100)&&a.position==COUNT&&a.first==COUNT-300&&a.count==300);
    assert(!xz_wave_rider_window(&a,bands,23,10)&&a.count==23);
    assert(xz_wave_rider_window(&a,bands,COUNT,NAN)<0&&!a.valid&&!a.count);
    assert(xz_wave_rider_window(&a,NULL,COUNT,100)<0&&!a.valid);
    assert(xz_wave_rider_window(&a,bands,0,100)<0&&!a.valid);
    puts("PASS exact bounded sample copying, fractional position, seek/end bounds and waveform causality");
}
int main(void){stock_position();real_sample_window();return 0;}
