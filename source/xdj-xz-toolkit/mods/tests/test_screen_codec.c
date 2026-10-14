#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../vendor/tools/xz_runtime/xz_screen_codec.h"
#define PIXELS (800u*480u)
#define BYTES (PIXELS*2u)
static uint16_t word(const unsigned char *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static int unpack(const unsigned char *data,size_t bytes,unsigned char *out,size_t pixels,const unsigned char *previous){
    size_t offset=0,at=0;
    while(offset<bytes){
        if(bytes-offset<2)return 0;
        uint16_t command=word(data+offset);offset+=2;
        size_t count=(command&32767u)+1;
        if(count>pixels-at)return 0;
        size_t needed=(command&32768u)?2:count*2;
        if(bytes-offset<needed)return 0;
        for(size_t i=0;i<count;i++){
            uint16_t value=word(data+offset+((command&32768u)?0:i*2));
            if(previous)value^=word(previous+(at+i)*2);
            out[(at+i)*2]=(unsigned char)value;out[(at+i)*2+1]=(unsigned char)(value>>8);
        }
        offset+=needed;at+=count;
    }
    return at==pixels;
}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void roundtrip(unsigned char *packed,unsigned char *decoded,const unsigned char *source,const unsigned char *previous,size_t count){
    size_t size=xz_screen_rle16(packed,BYTES+32,source,previous,count);
    assert(size && unpack(packed,size,decoded,count,previous));
    assert(!memcmp(source,decoded,count*2));
    assert(!unpack(packed,size-1,decoded,count,previous));
}
int main(int argc,char **argv){
    unsigned char *source=calloc(1,BYTES),*previous=calloc(1,BYTES),*decoded=malloc(BYTES),*packed=malloc(BYTES+64);
    assert(source&&previous&&decoded&&packed);
    roundtrip(packed,decoded,source,NULL,PIXELS);
    assert(xz_screen_rle16(packed,BYTES,source,NULL,PIXELS)==48);
    uint32_t seed=42;
    for(size_t i=0;i<BYTES;i++){seed=1664525u*seed+1013904223u;source[i]=(unsigned char)(seed>>24);previous[i]=(unsigned char)i;}
    roundtrip(packed,decoded,source,NULL,PIXELS);
    roundtrip(packed,decoded,source,previous,PIXELS);
    for(size_t count=1;count<300;count++)roundtrip(packed,decoded,source,previous,count);
    for(size_t cap=0;cap<64;cap++){
        memset(packed,0xab,BYTES+64);xz_screen_rle16(packed,cap,source,previous,PIXELS);
        for(size_t i=cap;i<cap+16;i++)assert(packed[i]==0xab);
    }
    assert(!unpack((const unsigned char*)"\xff\xff\0\0",4,decoded,1,NULL));
    puts("PASS lossless full/delta RLE, long runs, literals, short buffers and malformed decoding");
    memset(previous,0,BYTES);
    for(int frame=1;frame<argc;frame++){
        unsigned char header[66];FILE *file=fopen(argv[frame],"rb");assert(file);
        assert(fread(header,1,66,file)==66 && header[0]=='B' && header[1]=='M' && header[10]==66 && header[28]==16);
        assert(fread(source,1,BYTES,file)==BYTES);fclose(file);
        size_t full=xz_screen_rle16(packed,BYTES,source,NULL,PIXELS);
        if(full)assert(unpack(packed,full,decoded,PIXELS,NULL)&&!memcmp(source,decoded,BYTES));
        size_t delta=xz_screen_rle16(packed,BYTES,source,previous,PIXELS);
        if(delta)assert(unpack(packed,delta,decoded,PIXELS,previous)&&!memcmp(source,decoded,BYTES));
        double start=seconds();
        for(int repeat=0;repeat<100;repeat++)xz_screen_rle16(packed,BYTES,source,previous,PIXELS);
        double ms=(seconds()-start)*10;
        printf("{\"frame\":%d,\"full_bytes\":%zu,\"delta_bytes\":%zu,\"host_encode_ms\":%.4f}\n",frame-1,full?full:BYTES,delta?delta:BYTES,ms);
        memcpy(previous,source,BYTES);
    }
    free(source);free(previous);free(decoded);free(packed);return 0;
}
