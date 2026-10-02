/* SPDX-License-Identifier: MIT */
#include "native_clock.h"
#include <math.h>
#include <string.h>

static int block(xz_arcade_memory_read read,void *ctx,uint32_t base,unsigned offset,void *out,size_t n){
    if(!base||(base&3)||base<0x10000||(uint64_t)base+offset+n>UINT32_MAX)return -1;
    return read(ctx,base+offset,out,n);
}
static int word(xz_arcade_memory_read read,void *ctx,uint32_t base,unsigned offset,uint32_t *out){return block(read,ctx,base,offset,out,4);}
static uint32_t u32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static double f64(const unsigned char *p){double v;memcpy(&v,p,8);return v;}
int xz_arcade_native_clock_valid(xz_arcade_memory_read read,void *ctx){
    /* Exact 1.26 getters establish vector layout, play flag, grid and tempo
       pointers. The adapter reads snapshots; it never calls firmware methods. */
    static const unsigned char a[16]={0x0c,0x30,0x90,0xe5,0x10,0x20,0x90,0xe5,0x02,0x20,0x63,0xe0,0x42,0x01,0x51,0xe1};
    static const unsigned char b[12]={0x9c,0x3e,0x90,0xe5,0x28,0x00,0x93,0xe5,0x1e,0xff,0x2f,0xe1};
    static const unsigned char c[16]={0x04,0x00,0x90,0xe5,0x00,0x00,0x50,0xe3,0xa0,0x06,0x90,0x15,0x1e,0xff,0x2f,0xe1};
    unsigned char data[16];
    return read&&!read(ctx,0x494ec,data,16)&&!memcmp(data,a,16)&&
        !read(ctx,0x61a28,data,12)&&!memcmp(data,b,12)&&
        !read(ctx,0x3ae58,data,16)&&!memcmp(data,c,16);
}
int xz_arcade_mixer_clock_read(xz_arcade_memory_read read,void *ctx,float *bpm,unsigned *tap_mode){
    static const unsigned char guard[16]={0x66,0x1f,0x81,0xe2,0x00,0x30,0xa0,0xe1,0x07,0x00,0x91,0xe8,0x07,0x00,0x83,0xe8};
    uint32_t ui,mixer,again;unsigned char data[16],state[12],check[12];
    if(!read||!bpm||!tap_mode||read(ctx,0x25c914,data,16)||memcmp(data,guard,16)||
       read(ctx,0x01c8981c,&ui,4)||word(read,ctx,ui,0x50,&mixer)||
       block(read,ctx,mixer,0x198,state,12)||block(read,ctx,mixer,0x198,check,12)||memcmp(state,check,12)||
       word(read,ctx,ui,0x50,&again)||again!=mixer)return 0;
    unsigned raw=(unsigned)state[10]|(unsigned)state[11]<<8;
    if(state[5]||raw<400||raw>2400)return 0;
    *bpm=raw*.1f;*tap_mode=!!state[6];return 1;
}
int xz_arcade_native_clock_read(xz_arcade_memory_read read,void *ctx,unsigned deck,struct xz_arcade_track_clock *out){
    uint32_t engine,range[2],player,reader,impl,info,manager,tempo,check;
    unsigned char state[0x44],cache[0x28],grid[0x3c],speed[0x64],unit[8],again[8];
    if(!read||!out||deck>1||read(ctx,0x010e9688,&engine,4)||
       block(read,ctx,engine,12,range,8)||range[1]<range[0]||((range[1]-range[0])&3)||
       range[1]-range[0]>16||deck>=(range[1]-range[0])/4||
       word(read,ctx,range[0],deck*4,&player)||block(read,ctx,player,0xe64,state,sizeof(state))||
       !state[2]||word(read,ctx,player,0xc8,&reader)||word(read,ctx,reader,4,&impl)||
       word(read,ctx,impl,0x6a0,&info)||block(read,ctx,info,0,grid,sizeof(grid)))return 0;
    manager=u32(state+0x24);tempo=u32(state+0x38);
    if(block(read,ctx,manager,0,cache,sizeof(cache))||u32(cache)!=player||!u32(cache+4)||
       block(read,ctx,tempo,0,speed,sizeof(speed))||!u32(grid+0x24))return 0;
    uint32_t begin=u32(grid+0x34),end=u32(grid+0x38),previous=u32(cache+0x10);
    if(end<=begin||(end-begin)%8||(end-begin)/8>1000000||previous<begin||previous>end-8||(previous-begin)%8||
       block(read,ctx,previous,0,unit,8))return 0;
    unsigned bar=(unsigned)unit[0]|(unsigned)unit[1]<<8,bpm100=(unsigned)unit[2]|(unsigned)unit[3]<<8;
    if(bar<1||bar>4||bpm100<2000||bpm100>40000)return 0;
    uint32_t pause_state[6],current;
    if(block(read,ctx,player,0xec8,pause_state,sizeof(pause_state))||word(read,ctx,pause_state[0],8,&current))return 0;
    int32_t position=(int32_t)(current==pause_state[4]?pause_state[5]:u32(state+(state[0x18]?0x1c:0x14)));
    int32_t offset=(int32_t)u32(grid+0x28);
    if(position<0||offset < -3600000||offset>3600000)return 0;
    double multiplier=1+(speed[0x45]||!speed[0x60]?f64(speed+0x30):f64(speed+0x50)+f64(speed+0x58))+f64(speed+0x38);
    if(!isfinite(multiplier)||multiplier<.25||multiplier>4)return 0;
    double bpm=bpm100*.01*multiplier;
    double position_ms=position*(1000./44100.);
    double fraction=(position_ms-(double)u32(unit+4)-offset)*bpm100/6000000.;
    /* Cached previous beat can straddle a boundary, but a distant record is
       stale during a seek/load and must never label an invented phase LIVE. */
    if(!isfinite(fraction)||fraction < -.15||fraction>1.35||bpm<40||bpm>240)return 0;
    if(word(read,ctx,range[0],deck*4,&check)||check!=player||
       word(read,ctx,impl,0x6a0,&check)||check!=info||block(read,ctx,manager,0x10,again,8)||memcmp(again,cache+0x10,8))return 0;
    double phase=fmod(bar-1+fraction+4.,4.);
    *out=(struct xz_arcade_track_clock){.track=info,.position_ms=(uint32_t)position_ms,.bpm=(float)bpm,.bar_phase=(float)phase,
                                      .playing=!!state[0x13]&&!state[0x40]};
    return 1;
}
