/* SPDX-License-Identifier: MIT */
#include "native.h"
#include "../runtime.h"
#include "../audio/runtime.h"
#include <math.h>
#include <string.h>

static int block(xz_key_memory_read read,void *ctx,uint32_t base,unsigned offset,void *out,size_t n){
    if(base<0x10000||(base&3)||(uint64_t)base+offset+n>UINT32_MAX)return -1;
    return read(ctx,base+offset,out,n);
}
static int word(xz_key_memory_read read,void *ctx,uint32_t base,unsigned offset,uint32_t *out){return block(read,ctx,base,offset,out,4);}
static uint32_t u32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint16_t u16(const unsigned char *p){uint16_t v;memcpy(&v,p,2);return v;}
static double f64(const unsigned char *p){double v;memcpy(&v,p,8);return v;}
int xz_key_native_valid(xz_key_memory_read read,void *ctx){
    static const unsigned char master[16]={0x1c,0x31,0x0b,0xe3,0x22,0x32,0x40,0xe3,0x00,0x32,0x83,0xe0,0x15,0x3c,0x83,0xe2};
    static const unsigned char key[12]={0x9c,0x31,0x0e,0xe3,0x24,0x32,0x40,0xe3,0x89,0x2f,0xa0,0xe3};
    static const unsigned char tempo[12]={0x9c,0x3e,0x90,0xe5,0x00,0x00,0x51,0xe3,0x45,0x20,0xd3,0xe5};
    unsigned char data[16];
    return read&&!read(ctx,0x13aa08,data,16)&&!memcmp(data,master,16)&&
        !read(ctx,0xd9324,data,12)&&!memcmp(data,key,12)&&
        !read(ctx,0x61a34,data,12)&&!memcmp(data,tempo,12);
}
int xz_key_native_read(xz_key_memory_read read,void *ctx,unsigned deck,struct xz_key_snapshot *out){
    uint32_t engine,range[2],player,reader,tempo,check,masters[8],after[8];
    unsigned char state[0x44],speed[0x64],speed_after[0x64],key[66],key_after[66],state_after[0x44];
    if(!read||!out||deck>1||read(ctx,0x10e9688,&engine,4)||block(read,ctx,engine,12,range,8)||
        range[1]<range[0]||range[1]-range[0]!=8||word(read,ctx,range[0],deck*4,&player)||
        block(read,ctx,player,0xe64,state,sizeof(state))||!state[2]||
        word(read,ctx,player,0xc8,&reader)||!reader)return 0;
    tempo=u32(state+0x38);
    if(block(read,ctx,tempo,0,speed,sizeof(speed))||
        read(ctx,0x0224e19c+deck*0x224,key,sizeof(key))||
        read(ctx,0x0222c61c,masters,sizeof(masters)))return 0;
    size_t n=u16(key+0x20);char text[8];
    if(!n||n>7)return 0;
    for(size_t i=0;i<n;i++){
        uint16_t ch=u16(key+0x22+i*2);
        if(ch==0x266f)ch='#';else if(ch==0x266d)ch='b';
        if(ch>127||!ch)return 0;text[i]=(char)ch;
    }
    text[n]=0;struct xz_music_key original;
    if(!xz_music_key_parse(text,&original))return 0;
    double multiplier=1+(speed[0x45]||!speed[0x60]?f64(speed+0x30):f64(speed+0x50)+f64(speed+0x58))+f64(speed+0x38);
    if(!isfinite(multiplier)||multiplier<.25||multiplier>4||state[0x3e]>1)return 0;
    if(word(read,ctx,range[0],deck*4,&check)||check!=player||word(read,ctx,player,0xc8,&check)||check!=reader||
        block(read,ctx,player,0xe64,state_after,sizeof(state_after))||
        state_after[2]!=state[2]||state_after[0x3e]!=state[0x3e]||u32(state_after+0x38)!=tempo||
        block(read,ctx,tempo,0,speed_after,sizeof(speed_after))||memcmp(speed,speed_after,sizeof(speed))||
        read(ctx,0x0224e19c+deck*0x224,key_after,sizeof(key_after))||memcmp(key,key_after,sizeof(key))||
        read(ctx,0x0222c61c,after,sizeof(after))||memcmp(masters,after,sizeof(masters)))return 0;
    *out=(struct xz_key_snapshot){.original=original,.player=player,.reader=reader,.tempo_ratio=multiplier,
        .loaded=1,.master=masters[deck*4]!=0&&masters[(1-deck)*4]==0,
        .master_tempo=state[0x3e],.format=text[0]>='1'&&text[0]<='9'?XZ_KEY_CAMELOT:XZ_KEY_CLASSIC};
    return 1;
}
static int native_read(void *unused,uint32_t address,void *out,size_t n){(void)unused;return xz_read_memory(address,out,n);}
int xz_key_native_snapshot(unsigned deck,struct xz_key_snapshot *out){
    if(!xz_key_native_valid(native_read,NULL)||!xz_key_native_read(native_read,NULL,deck,out))return 0;
    uint32_t rate,epoch;
    if(xz_audio_output_deck((void *)(uintptr_t)(out->player+0xd0),&rate,&epoch)!=(int)deck||rate!=44100)return 0;
    out->epoch=epoch;return 1;
}
