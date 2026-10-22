/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Native clock acceptance requires assertions
#endif
#include "native_clock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned char engine[32],players[8],player[4096],reader[16],impl[0x6b0],grid[64],manager[64],tempo[104],units[32],pause_queue[16],ui[84],mixer[0x1a8];
static uint32_t engine_ptr=0x10000,ui_ptr=0x40000;
static const unsigned char guards[][16]={
    {0x0c,0x30,0x90,0xe5,0x10,0x20,0x90,0xe5,0x02,0x20,0x63,0xe0,0x42,0x01,0x51,0xe1},
    {0x9c,0x3e,0x90,0xe5,0x28,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1},
    {0x04,0x00,0x90,0xe5,0x00,0x00,0x50,0xe3,0xa0,0x06,0x90,0x15,0x1e,0xff,0x2f,0xe1},
    {0x66,0x1f,0x81,0xe2,0x00,0x30,0xa0,0xe1,0x07,0x00,0x91,0xe8,0x07,0x00,0x83,0xe8}};
struct memory {uint32_t base;const void *data;size_t size;};
static const struct memory regions[]={
    {0x010e9688,&engine_ptr,4},{0x01c8981c,&ui_ptr,4},
    {0x494ec,guards[0],16},{0x61a28,guards[1],12},{0x3ae58,guards[2],16},{0x25c914,guards[3],16},
    {0x10000,engine,sizeof(engine)},{0x10100,players,sizeof(players)},{0x20000,player,sizeof(player)},
    {0x30000,reader,sizeof(reader)},{0x31000,impl,sizeof(impl)},{0x32000,grid,sizeof(grid)},
    {0x33000,manager,sizeof(manager)},{0x34000,tempo,sizeof(tempo)},{0x35000,units,sizeof(units)},
    {0x36000,pause_queue,sizeof(pause_queue)},{0x40000,ui,sizeof(ui)},{0x41000,mixer,sizeof(mixer)}};
static void put(unsigned char *p,unsigned at,uint32_t v){memcpy(p+at,&v,4);}
static int read_memory(void *context,uint32_t address,void *out,size_t n){
    (void)context;
    for(unsigned i=0;i<sizeof(regions)/sizeof(regions[0]);i++)if(address>=regions[i].base&&address-regions[i].base+n<=regions[i].size){memcpy(out,(const char*)regions[i].data+address-regions[i].base,n);return 0;}
    return -1;
}
struct read_fault {uint32_t address;size_t bytes;};
static int fail_memory_read(void *context,uint32_t address,void *out,size_t n){
    const struct read_fault *fault=context;
    return address==fault->address&&n==fault->bytes?-1:read_memory(NULL,address,out,n);
}
static void test_partial_clock_evidence(void){
    int (*readers[])(xz_arcade_memory_read,void *,unsigned,struct xz_arcade_track_clock *)={
        xz_arcade_native_clock_read,xz_music_native_clock_read};
    const double zero=0,bend=.02;
    for(unsigned i=0;i<sizeof readers/sizeof readers[0];i++){
        struct xz_arcade_track_clock out;
        struct read_fault later[]={{0x35000,8},{0x20ec8,24}};
        memcpy(tempo+0x38,&bend,8);
        for(unsigned j=0;j<sizeof later/sizeof later[0];j++){
            assert(!readers[i](fail_memory_read,&later[j],0,&out));
            assert((out.observed&XZ_ARCADE_CLOCK_MANUAL)&&out.manual);
            assert(!out.phase_valid&&!out.bpm&&!out.speed);
        }
        put(grid,0x24,0);
        assert(!readers[i](read_memory,NULL,0,&out));
        assert((out.observed&XZ_ARCADE_CLOCK_MANUAL)&&out.manual);
        put(grid,0x24,1);memcpy(tempo+0x38,&zero,8);
        player[0xe66]=0;player[0xe77]=0;
        assert(!readers[i](read_memory,NULL,0,&out));
        assert((out.observed&XZ_ARCADE_CLOCK_STATE)&&!out.playing&&!out.manual);
        player[0xea4]=1;
        assert(!readers[i](read_memory,NULL,0,&out));
        assert((out.observed&XZ_ARCADE_CLOCK_STATE)&&out.manual&&!out.playing);
        player[0xe66]=1;player[0xe77]=1;player[0xea4]=0;
        put(manager,0,0);
        assert(!readers[i](read_memory,NULL,0,&out));
        assert(out.observed&XZ_ARCADE_CLOCK_BOUNDARY);
        put(manager,0,0x20000);
        struct read_fault missing[]={{0x20e64,0x44},{0x33000,0x28},{0x35000,8}};
        for(unsigned j=0;j<sizeof missing/sizeof missing[0];j++){
            assert(!readers[i](fail_memory_read,&missing[j],0,&out));
            assert(!(out.observed&XZ_ARCADE_CLOCK_BOUNDARY)&&!out.manual);
            assert(!out.phase_valid&&!out.bpm&&!out.speed);
            if(!j)assert(!out.observed);
        }
        assert(readers[i](read_memory,NULL,0,&out)&&out.phase_valid&&out.playing&&!out.manual);
    }
    puts("PASS partial clock evidence: observed manual/stop/identity survives later rejection; missing reads invent no boundary");
}
int main(void){
    put(engine,12,0x10100);put(engine,16,0x10104);put(players,0,0x20000);
    put(player,0xc8,0x30000);put(reader,4,0x31000);put(impl,0x6a0,0x32000);
    player[0xe66]=1;player[0xe77]=1;put(player,0xe78,60637);put(player,0xe88,0x33000);put(player,0xe9c,0x34000);
    put(player,0xec8,0x36000);put(player,0xed8,4);put(player,0xedc,55125);put(pause_queue,8,5);
    put(manager,0,0x20000);put(manager,4,1);put(manager,0x10,0x35000);
    put(grid,0x24,1);put(grid,0x34,0x35000);put(grid,0x38,0x35020);
    for(unsigned i=0;i<4;i++){units[i*8]=(unsigned char)(i+1);units[i*8+2]=0xe0;units[i*8+3]=0x2e;put(units,i*8+4,1000+i*500);}
    double pitch=.10;memcpy(tempo+0x30,&pitch,8);
    test_partial_clock_evidence();
    assert(xz_arcade_native_clock_valid(read_memory,NULL));struct xz_arcade_track_clock out;
    assert(xz_arcade_native_clock_read(read_memory,NULL,0,&out));assert(fabsf(out.bpm-132)<.001f&&fabsf(out.bar_phase-.75f)<.001f&&out.playing);
    for(unsigned i=0;i<4;i++){units[i*8+2]=0x50;units[i*8+3]=0x46;put(units,i*8+4,1000+i*333);}
    pitch=.5;memcpy(tempo+0x30,&pitch,8);put(player,0xe78,55125);
    assert(!xz_arcade_native_clock_read(read_memory,NULL,0,&out));
    assert(xz_music_native_clock_read(read_memory,NULL,0,&out)&&fabsf(out.bpm-270)<.001f);
    pitch=-.9;memcpy(tempo+0x30,&pitch,8);
    assert(!xz_arcade_native_clock_read(read_memory,NULL,0,&out));
    assert(xz_music_native_clock_read(read_memory,NULL,0,&out)&&out.speed>0&&out.speed<.25f);
    pitch=4.;memcpy(tempo+0x30,&pitch,8);
    assert(!xz_arcade_native_clock_read(read_memory,NULL,0,&out));
    assert(xz_music_native_clock_read(read_memory,NULL,0,&out)&&out.speed==5);
    double invalid[]={NAN,INFINITY,-1.,-2.,1e300};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++){memcpy(tempo+0x30,&invalid[i],8);assert(!xz_music_native_clock_read(read_memory,NULL,0,&out));}
    for(unsigned i=0;i<4;i++){units[i*8+2]=0xe0;units[i*8+3]=0x2e;put(units,i*8+4,1000+i*500);}
    pitch=.1;memcpy(tempo+0x30,&pitch,8);put(player,0xe78,60637);
    player[0xe77]=0;put(pause_queue,8,4);
    assert(xz_arcade_native_clock_read(read_memory,NULL,0,&out));assert(!out.playing&&fabsf(out.bar_phase-.5f)<.001f);
    assert(!xz_arcade_native_clock_read(read_memory,NULL,1,&out));
    put(manager,0x10,0x35001);assert(!xz_arcade_native_clock_read(read_memory,NULL,0,&out));put(manager,0x10,0x35000);
    pitch=NAN;memcpy(tempo+0x30,&pitch,8);assert(!xz_arcade_native_clock_read(read_memory,NULL,0,&out));pitch=.1;memcpy(tempo+0x30,&pitch,8);
    player[0xe66]=0;assert(!xz_arcade_native_clock_read(read_memory,NULL,0,&out));player[0xe66]=1;
    put(player,0xedc,441000);assert(!xz_arcade_native_clock_read(read_memory,NULL,0,&out));
    put(ui,0x50,0x41000);mixer[0x198+10]=0;mixer[0x198+11]=5;
    float bpm;unsigned tapped;
    assert(xz_arcade_mixer_clock_read(read_memory,NULL,&bpm,&tapped)&&bpm==128&&!tapped);
    mixer[0x198+6]=1;assert(xz_arcade_mixer_clock_read(read_memory,NULL,&bpm,&tapped)&&tapped);
    mixer[0x198+5]=1;assert(!xz_arcade_mixer_clock_read(read_memory,NULL,&bpm,&tapped));
    puts("PASS guarded native grid phase/pitch/pause, stale pointers/ranges/NaN rejected; read-only mixer BPM and out-of-range gate");return 0;
}
