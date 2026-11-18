/* SPDX-License-Identifier: MIT */
#include "native_clock.h"
#include <math.h>
#include <float.h>
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
/* Optional transport evidence. Full 1.26 getters prove each offset; no firmware calls.
   Recheck the player vector and every extra object before exposing partial evidence. */
static int transport_getter(xz_arcade_memory_read read,void *ctx,uint32_t address,uint32_t offset){
    uint32_t expected[11]={0xe590300c,0xe5902010,0xe0632002,0xe1510142,0x2a000003,
        0xe7930101,0xe3500000,0x15d00000|offset,0xe12fff1e,0xe3a00000,0xe12fff1e},got[11];
    return !read(ctx,address,got,sizeof got)&&!memcmp(got,expected,sizeof got);
}
static void transport_read(xz_arcade_memory_read read,void *ctx,unsigned deck,uint32_t engine,
                           const uint32_t range[2],uint32_t player,const unsigned char *state,
                           struct xz_arcade_track_clock *out){
    static const uint32_t ramp_guard[6]={0xe5903ed0,0xe5930018,0xe3500000,0xd3a00000,0xc3a00001,0xe12fff1e};
    uint32_t got[6],ramp=0,work=0,check,range_again[2];unsigned evidence=0;
    unsigned char touch=0,touch_again,play_again[2];
    if(transport_getter(read,ctx,0x494c0,0xe76)&&transport_getter(read,ctx,0x494ec,0xe77)&&
       transport_getter(read,ctx,0x49fa0,0xeb6)&&!block(read,ctx,player,0xeb6,&touch,1)&&
       !block(read,ctx,player,0xe76,play_again,2)&&!memcmp(play_again,state+0x12,2)&&
       !block(read,ctx,player,0xeb6,&touch_again,1)&&touch==touch_again)evidence|=XZ_ARCADE_CLOCK_TRANSPORT;
    if(!read(ctx,0x62b58,got,sizeof got)&&!memcmp(got,ramp_guard,sizeof got)&&
       !word(read,ctx,player,0xed0,&ramp)&&!word(read,ctx,ramp,0x18,&work)&&
       !word(read,ctx,player,0xed0,&check)&&check==ramp)evidence|=XZ_ARCADE_CLOCK_RAMP;
    if(!evidence||read(ctx,0x010e9688,&check,4)||check!=engine||
       block(read,ctx,engine,12,range_again,sizeof range_again)||memcmp(range,range_again,sizeof range_again)||
       word(read,ctx,range[0],deck*4,&check)||check!=player)return;
    if(evidence&XZ_ARCADE_CLOCK_TRANSPORT){out->requested_playing=!!state[0x12];out->effective_playing=!!state[0x13];out->jog_touching=!!touch;}
    if(evidence&XZ_ARCADE_CLOCK_RAMP)out->ramp_active=(int32_t)work>0;
    out->observed|=evidence;
}

enum clock_policy { ARCADE_CLOCK, MUSIC_CLOCK, POSITION_CLOCK };
static int track_clock_read(xz_arcade_memory_read read,void *ctx,unsigned deck,struct xz_arcade_track_clock *out,enum clock_policy music,xz_arcade_observation_clock stamp,void *clock_ctx){
    if(out)memset(out,0,sizeof *out);
    uint32_t engine,range[2],player,reader,impl,info,manager,tempo,check;
    uint64_t state_begin=0,state_end=0,pause_begin=0,pause_end=0;
    unsigned char state[0x44],cache[0x28],grid[0x3c],speed[0x64],unit[8]={0},again[8];
    if(!read||!out||deck>1||read(ctx,0x010e9688,&engine,4)||
       block(read,ctx,engine,12,range,8)||range[1]<range[0]||((range[1]-range[0])&3)||
       range[1]-range[0]>16||deck>=(range[1]-range[0])/4||
       word(read,ctx,range[0],deck*4,&player))return 0;
    if(stamp)state_begin=stamp(clock_ctx);
    if(block(read,ctx,player,0xe64,state,sizeof(state)))return 0;
    if(stamp)state_end=stamp(clock_ctx);
    out->playing=!!state[0x13]&&!state[0x40];out->manual=!!state[0x40];
    out->observed=XZ_ARCADE_CLOCK_STATE;
    transport_read(read,ctx,deck,engine,range,player,state,out);
    if(!state[2])return 0;
    if(word(read,ctx,player,0xc8,&reader)||word(read,ctx,reader,4,&impl)||
       word(read,ctx,impl,0x6a0,&info))return 0;
    out->track=info;out->observed|=XZ_ARCADE_CLOCK_TRACK;
    if(block(read,ctx,info,0,grid,sizeof(grid)))return 0;
    manager=u32(state+0x24);tempo=u32(state+0x38);
    if(block(read,ctx,manager,0,cache,sizeof(cache)))return 0;
    if(u32(cache)!=player){out->observed|=XZ_ARCADE_CLOCK_BOUNDARY;return 0;}
    if((!u32(cache+4)&&music!=POSITION_CLOCK)||block(read,ctx,tempo,0,speed,sizeof(speed)))return 0;
    double bend=f64(speed+0x38);
    if(isfinite(bend)){out->manual=!!state[0x40]||fabs(bend)>1e-6;out->observed|=XZ_ARCADE_CLOCK_MANUAL;}
    int phase_valid=!!u32(grid+0x24);
    if(!music&&!phase_valid)return 0;
    uint32_t begin=u32(grid+0x34),end=u32(grid+0x38),previous=u32(cache+0x10);
    int phase_data=u32(cache+4)&&end>begin&&!((end-begin)%8)&&(end-begin)/8<=1000000&&
        previous>=begin&&previous<=end-8&&!((previous-begin)%8)&&!block(read,ctx,previous,0,unit,8);
    unsigned bar=(unsigned)unit[0]|(unsigned)unit[1]<<8,bpm100=(unsigned)unit[2]|(unsigned)unit[3]<<8;
    if(!phase_data||bpm100<2000||bpm100>40000){
        if(music!=POSITION_CLOCK)return 0;
        phase_valid=0;bpm100=0;
    }
    phase_valid=phase_valid&&bar>=1&&bar<=4;
    uint32_t pause_state[6],current;
    if(stamp)pause_begin=stamp(clock_ctx);
    if(block(read,ctx,player,0xec8,pause_state,sizeof(pause_state)))return 0;
    if(stamp)pause_end=stamp(clock_ctx);
    if(word(read,ctx,pause_state[0],8,&current))return 0;
    int32_t position=(int32_t)(current==pause_state[4]?pause_state[5]:u32(state+(state[0x18]?0x1c:0x14)));
    int32_t offset=(int32_t)u32(grid+0x28);
    if(position<0)return 0;
    if(offset < -3600000||offset>3600000){
        if(music!=POSITION_CLOCK)return 0;
        phase_valid=0;bpm100=0;offset=0;
    }
    if(!isfinite(bend))return 0;
    double multiplier=1+(speed[0x45]||!speed[0x60]?f64(speed+0x30):f64(speed+0x50)+f64(speed+0x58))+(music?0:bend);
    if(!isfinite(multiplier)||(multiplier<0||(multiplier==0&&music!=POSITION_CLOCK))||multiplier>FLT_MAX||
       (!music&&(multiplier<.25||multiplier>4)))return 0;
    double nominal_bpm=bpm100*.01;
    double bpm=nominal_bpm*multiplier;
    double position_ms=position*(1000./44100.);
    double fraction=(position_ms-(double)u32(unit+4)-offset)*bpm100/6000000.;
    /* Cached previous beat can straddle a boundary, but a distant record is
       stale during a seek/load and must never label an invented phase LIVE. */
    phase_valid=phase_valid&&isfinite(fraction)&&fraction>=-.15&&fraction<=1.35;
    if((!music&&!phase_valid)||!isfinite(bpm)||(bpm<0||(bpm==0&&music!=POSITION_CLOCK))||bpm>FLT_MAX||
       (multiplier!=0&&!(float)multiplier)||(bpm!=0&&!(float)bpm)||(!music&&(bpm<40||bpm>240)))return 0;
    if(word(read,ctx,range[0],deck*4,&check))return 0;
    if(check!=player){out->observed|=XZ_ARCADE_CLOCK_BOUNDARY;return 0;}
    if(word(read,ctx,impl,0x6a0,&check))return 0;
    if(check!=info){out->observed|=XZ_ARCADE_CLOCK_BOUNDARY;return 0;}
    if(block(read,ctx,manager,0x10,again,8))return 0;
    if(memcmp(again,cache+0x10,8)){
        if(music==POSITION_CLOCK){phase_valid=0;bpm=nominal_bpm=0;} /* Beat-cache churn does not replace the source. */
        else {out->observed|=XZ_ARCADE_CLOCK_BOUNDARY;return 0;}
    }
    uint64_t observed_begin=current==pause_state[4]?pause_begin:state_begin;
    uint64_t observed_end=current==pause_state[4]?pause_end:state_end;
    if(!observed_begin||observed_end<observed_begin)observed_begin=observed_end=0;
    double phase=phase_valid?fmod(bar-1+fraction+4.,4.):0.;
    *out=(struct xz_arcade_track_clock){.track=info,.position_ms=(uint32_t)position_ms,.position_frames=(uint32_t)position,
                                      .bpm=(float)bpm,.nominal_bpm=(float)nominal_bpm,.bar_phase=(float)phase,.speed=(float)multiplier,
                                      .playing=!!state[0x13]&&!state[0x40],.manual=!!state[0x40]||fabs(bend)>1e-6,.phase_valid=phase_valid,
                                      .position_observed_begin_us=observed_begin,
                                      .position_observed_end_us=observed_end,
                                      .requested_playing=out->requested_playing,.effective_playing=out->effective_playing,
                                      .jog_touching=out->jog_touching,.ramp_active=out->ramp_active,
                                      .observed=out->observed|XZ_ARCADE_CLOCK_TRACK|XZ_ARCADE_CLOCK_STATE|XZ_ARCADE_CLOCK_MANUAL};
    return 1;
}

int xz_arcade_native_clock_read(xz_arcade_memory_read read,void *ctx,unsigned deck,struct xz_arcade_track_clock *out){
    return track_clock_read(read,ctx,deck,out,ARCADE_CLOCK,NULL,NULL);
}
int xz_music_native_clock_read(xz_arcade_memory_read read,void *ctx,unsigned deck,struct xz_arcade_track_clock *out){
    return track_clock_read(read,ctx,deck,out,MUSIC_CLOCK,NULL,NULL);
}

int xz_native_position_clock_read_timed(xz_arcade_memory_read read,void *ctx,unsigned deck,struct xz_arcade_track_clock *out,xz_arcade_observation_clock stamp,void *clock_ctx){
    return track_clock_read(read,ctx,deck,out,POSITION_CLOCK,stamp,clock_ctx);
}
