/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error PC waveform acceptance requires live assertions
#endif
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(_WIN32)
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
/* Match POSIX replacement semantics in this disposable Windows fixture. */
static int replace_file(const char *from,const char *to){remove(to);return rename(from,to);}
#define rename replace_file
#endif
#define XZ_PC_WAVE_DIRECTORY "pc-wave-fixture"
#include "wave.c"

static struct xz_pc_stem_source native={1,0,1,1,1,9};
static struct xz_pc_wave_stamp stamp={7,12,256,1,0};
static unsigned stamp_calls,change_on_call;
static uint8_t canonical[512];
int xz_pc_stems_native_observe(void){return 0;}
int xz_pc_stems_native_source(unsigned physical,struct xz_pc_stem_source *out){*out=native;return physical==0;}
int xz_pc_wave_native_stamp(unsigned slot,struct xz_pc_wave_stamp *out){
 stamp_calls++;*out=stamp;if(change_on_call==stamp_calls)out->epoch++;
 return slot==0;
}
int xz_read_memory(uint32_t address,void *out,size_t n){
 if(address!=0x11ed0c8u+12u||n!=sizeof(canonical))return -1;
 memcpy(out,canonical,n);return 0;
}
int xz_oc_bundle(const char *path,char *out,size_t n){(void)path;(void)out;(void)n;return -1;}
static void put32(uint8_t *out,uint32_t value){for(unsigned i=0;i<4;i++)out[i]=(uint8_t)(value>>(8*i));}
static void put64(uint8_t *out,uint64_t value){for(unsigned i=0;i<8;i++)out[i]=(uint8_t)(value>>(8*i));}
static uint8_t *make_packet(size_t *size,uint8_t hash[32]){
 static const uint16_t palette[8]={0x0600,0x0a80,0x0f80,0x1780,0x5780,0x7780,0x7780,0xdf80};
 *size=96+256*5;uint8_t *b=calloc(1,*size);assert(b);
 for(unsigned i=0;i<256;i++){uint16_t value=palette[i>>5]|((i&31)<<2);canonical[i*2]=(uint8_t)value;canonical[i*2+1]=(uint8_t)(value>>8);}
 digest(canonical,sizeof(canonical),hash);
 memcpy(b,"PCW1",4);put32(b+4,1);put32(b+8,0);put32(b+12,7);put32(b+16,256);put32(b+20,257);put64(b+24,session);
 memcpy(b+32,hash,32);memcpy(b+96,canonical,512);
 for(unsigned i=0;i<256;i++){b[608+i*3]=25;b[609+i*3]=9;b[610+i*3]=17;}
 digest(b+96,*size-96,b+64);return b;
}
static void invalid(const uint8_t *b,size_t n,unsigned slot,uint64_t nonce,uint32_t epoch,uint32_t count,const uint8_t *hash){
 struct xz_wave_load out={0};assert(xz_pc_wave_packet(b,n,slot,nonce,epoch,count,hash,&out));assert(!out.bands&&!out.count);
}
int main(int argc,char **argv){
 session=UINT64_C(0x1020304050607080);size_t size;uint8_t hash[32];uint8_t *b=make_packet(&size,hash);
 uint8_t mode_bytes[25]={0};memcpy(mode_bytes,"PCM1",4);put32(mode_bytes+4,1);put64(mode_bytes+8,session);put32(mode_bytes+16,62500);
 unsigned roles=0;
 put32(mode_bytes+20,3);assert(!xz_pc_mode_packet(mode_bytes,24,session,&roles)&&roles==11);
 put32(mode_bytes+20,4);assert(!xz_pc_mode_packet(mode_bytes,24,session,&roles)&&roles==15);
 put32(mode_bytes+20,0);assert(!xz_pc_mode_packet(mode_bytes,24,session,&roles)&&roles==0);
 put32(mode_bytes+20,5);assert(xz_pc_mode_packet(mode_bytes,24,session,&roles)&&!roles);
 put32(mode_bytes+20,3);assert(xz_pc_mode_packet(mode_bytes,24,session+1,&roles)&&!roles);
 assert(xz_pc_mode_packet(mode_bytes,23,session,&roles)&&xz_pc_mode_packet(mode_bytes,25,session,&roles));
 put32(mode_bytes+16,0);assert(xz_pc_mode_packet(mode_bytes,24,session,&roles));put32(mode_bytes+16,62500);
 struct xz_wave_load out={0};assert(!xz_pc_wave_packet(b,size,0,session,7,256,hash,&out));
 assert(out.count==256&&out.kind==0&&out.normalization==51&&out.bands[0]==25&&out.bands[1]==9&&out.bands[2]==17);xz_wave_load_free(&out);
 invalid(NULL,size,0,session,7,256,hash);invalid(b,95,0,session,7,256,hash);invalid(b,size-1,0,session,7,256,hash);invalid(b,size+1,0,session,7,256,hash);
 invalid(b,size,1,session,7,256,hash);invalid(b,size,4,session,7,256,hash);invalid(b,size,0,0,7,256,hash);invalid(b,size,0,session+1,7,256,hash);
 invalid(b,size,0,session,8,256,hash);invalid(b,size,0,session,7,0,hash);invalid(b,size,0,session,7,540001,hash);invalid(b,size,0,session,7,255,hash);
 uint8_t wrong[32]={0};invalid(b,size,0,session,7,256,wrong);
 for(unsigned offset=0;offset<96;offset++){if(offset==20)continue;b[offset]^=1;invalid(b,size,0,session,7,256,hash);b[offset]^=1;}
 b[size-1]^=1;invalid(b,size,0,session,7,256,hash);b[size-1]^=1;
 put32(b+20,256);assert(!xz_pc_wave_packet(b,size,0,session,7,256,hash,&out));xz_wave_load_free(&out);
 put32(b+20,255);invalid(b,size,0,session,7,256,hash);put32(b+20,258);invalid(b,size,0,session,7,256,hash);put32(b+20,257);
 b[96]|=1;digest(b+96,512,wrong);memcpy(b+32,wrong,32);digest(b+96,size-96,b+64);invalid(b,size,0,session,7,256,wrong);free(b);b=make_packet(&size,hash);
 assert(xz_pc_wave_packet(b,size,0,session,7,256,hash,NULL));
 (void)mkdir(XZ_PC_WAVE_DIRECTORY,0700);FILE *f=fopen(XZ_PC_WAVE_DIRECTORY "/slot0.pcw","wb");assert(f);assert(fwrite(b,1,size,f)==size);assert(!fclose(f));
 f=fopen(XZ_PC_WAVE_DIRECTORY "/mode.bin","wb");assert(f);assert(fwrite(mode_bytes,1,24,f)==24);fclose(f);
 xz_pc_wave_state();assert(xz_pc_wave_available_roles()==11);f=fopen(XZ_PC_WAVE_DIRECTORY "/state.json","rb");assert(f);char json[1024]={0};assert(fread(json,1,sizeof(json)-1,f)>0);fclose(f);assert(strstr(json,"\"generation\":1")&&strstr(json,"\"ready\":true")&&strstr(json,"\"samples\":256"));
 xz_pc_wave_state();f=fopen(XZ_PC_WAVE_DIRECTORY "/state.json","rb");assert(f);memset(json,0,sizeof(json));assert(fread(json,1,sizeof(json)-1,f)>0);fclose(f);assert(strstr(json,"\"generation\":2"));
 unsigned slot;uint32_t epoch,serial;assert(!xz_pc_wave_load(0,&out,&slot,&epoch,&serial));assert(slot==0&&epoch==7&&serial==12);xz_wave_load_free(&out);
 assert(xz_pc_wave_current(0,0,7,12,256));assert(!xz_pc_wave_current(0,0,7,12,255));assert(!xz_pc_wave_current(0,4,7,12,256));
 stamp.count=0;assert(!xz_pc_wave_current(0,0,7,12,256));stamp.count=256;
 stamp.busy=1;assert(!xz_pc_wave_current(0,0,7,12,256));stamp.busy=0;
 stamp_calls=0;change_on_call=2;assert(xz_pc_wave_load(0,&out,&slot,&epoch,&serial));assert(!out.bands);change_on_call=0;
 native.slot=2;assert(!xz_pc_wave_current(0,0,7,12,256));native.slot=0;native.rekordbox=0;assert(xz_pc_wave_load(0,&out,&slot,&epoch,&serial));native.rekordbox=1;
 if(argc>1){
  for(int i=1;i<argc;i++){f=fopen(argv[i],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>96);rewind(f);uint8_t *real=malloc((size_t)n);assert(real);assert(fread(real,1,(size_t)n,f)==(size_t)n);fclose(f);
   assert(!xz_pc_wave_packet(real,(size_t)n,le32(real+8),le64(real+24),le32(real+12),le32(real+16),real+32,&out));printf("PASS private packet %u samples\n",out.count);xz_wave_load_free(&out);free(real);
  }
 }
 free(b);remove(XZ_PC_WAVE_DIRECTORY "/slot0.pcw");remove(XZ_PC_WAVE_DIRECTORY "/state.json");remove(XZ_PC_WAVE_DIRECTORY "/mode.bin");rmdir(XZ_PC_WAVE_DIRECTORY);puts("PASS PCW1 boundary, native binding, worker heartbeat, three/four role mode and stale-load acceptance");return 0;
}
