/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "wave.h"
#include "stems.h"
#include "../runtime.h"
#include "../audio/vendor/sha256/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_SAMPLES 540000u
#ifndef XZ_PC_WAVE_DIRECTORY
#define XZ_PC_WAVE_DIRECTORY "/dev/shm/xz-pc-wave"
#endif
#define DIRECTORY XZ_PC_WAVE_DIRECTORY
static uint64_t session;
static uint32_t state_generation;
static unsigned available_roles;
static struct {uint32_t epoch,serial,count;uint8_t hash[32];int valid;} snapshots[4];
static uint32_t le32(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static uint64_t le64(const uint8_t *b){return le32(b)|(uint64_t)le32(b+4)<<32;}
int xz_pc_mode_packet(const unsigned char *b,size_t n,uint64_t nonce,unsigned *out){
 if(!out)return -1;*out=0;
 if(!b||n!=24||!nonce||memcmp(b,"PCM1",4)||le32(b+4)!=1||le64(b+8)!=nonce||!le32(b+16))return -1;
 unsigned count=le32(b+20);if(count!=0&&count!=3&&count!=4)return -1;
 *out=count==3?11u:count==4?15u:0u;return 0;
}
unsigned xz_pc_wave_available_roles(void){return __atomic_load_n(&available_roles,__ATOMIC_ACQUIRE);}
static void mode_tick(void){
 unsigned roles=0;uint8_t bytes[25];FILE *file=fopen(DIRECTORY "/mode.bin","rb");
 if(file){size_t n=fread(bytes,1,sizeof(bytes),file);fclose(file);xz_pc_mode_packet(bytes,n,session,&roles);}
 __atomic_store_n(&available_roles,roles,__ATOMIC_RELEASE);
}
static void digest(const uint8_t *b,size_t n,uint8_t out[32]){SHA256_CTX c;sha256_init(&c);sha256_update(&c,b,n);sha256_final(&c,out);}
int xz_pc_wave_packet(const uint8_t *b,size_t n,unsigned slot,uint64_t nonce,uint32_t epoch,uint32_t count,const uint8_t hash[32],struct xz_wave_load *out){
 if(!out)return -1;
 memset(out,0,sizeof(*out));
 if(!b||!hash||slot>=4||!nonce||n<96||memcmp(b,"PCW1",4)||le32(b+4)!=1||le32(b+8)!=slot||le32(b+12)!=epoch||
    le32(b+16)!=count||!count||count>MAX_SAMPLES||le64(b+24)!=nonce||memcmp(b+32,hash,32)||
    le32(b+20)<count||le32(b+20)>count+1||n!=96+(size_t)count*5)return -1;
 uint8_t actual[32];digest(b+96,(size_t)count*5,actual);if(memcmp(actual,b+64,32))return -1;
 for(uint32_t i=0;i<count;i++)if(b[96+(size_t)i*2]&3)return -1;
 digest(b+96,(size_t)count*2,actual);if(memcmp(actual,hash,32))return -1;
 out->bands=malloc((size_t)count*3);if(!out->bands)return -1;
 memcpy(out->bands,b+96+(size_t)count*2,(size_t)count*3);out->count=count;out->kind=0;
 out->normalization=xz_wave_normalization(out->bands,count,out->kind);return 0;
}
int xz_pc_wave_start(void){
 if(session)return 0;
 if(xz_pc_stems_native_observe())return -1;
 FILE *random=fopen("/dev/urandom","rb");if(!random)return -1;
 int ok=fread(&session,1,sizeof(session),random)==sizeof(session);fclose(random);
 if(!ok||!session){session=0;return -1;}
 if(mkdir(DIRECTORY,0700)&&access(DIRECTORY,W_OK)){session=0;return -1;}
 return 0;
}
static int snapshot(unsigned slot){
 struct xz_pc_wave_stamp before,after;
 if(slot>=4)return 0;
 if(!session||!xz_pc_wave_native_stamp(slot,&before)||before.busy||!before.complete||!before.count||before.count>MAX_SAMPLES){snapshots[slot].valid=0;return 0;}
 if(snapshots[slot].valid&&snapshots[slot].epoch==before.epoch&&snapshots[slot].serial==before.serial&&snapshots[slot].count==before.count)return 1;
 size_t n=(size_t)before.count*2;uint8_t *b=malloc(n);if(!b)return 0;
 if(xz_read_memory(0x11ed0c8u+slot*0x107ad4u+12u,b,n)){free(b);return 0;}
 for(size_t i=0;i<n;i+=2)b[i]&=0xfc;
 uint8_t hash[32];digest(b,n,hash);free(b);
 if(!xz_pc_wave_native_stamp(slot,&after)||after.busy||!after.complete||after.epoch!=before.epoch||after.serial!=before.serial||after.count!=before.count)return 0;
 snapshots[slot].epoch=before.epoch;snapshots[slot].serial=before.serial;snapshots[slot].count=before.count;
 memcpy(snapshots[slot].hash,hash,32);snapshots[slot].valid=1;return 1;
}
void xz_pc_wave_state(void){
 if(!session&&xz_pc_wave_start())return;
 mode_tick();
 FILE *file=fopen(DIRECTORY "/state.tmp","w");if(!file)return;
 fprintf(file,"{\"schema\":\"xz-pc-wave/1\",\"session\":\"%016llx\",\"generation\":%u,\"pid\":%ld,\"decks\":[",(unsigned long long)session,++state_generation,(long)getpid());
 for(unsigned d=0;d<2;d++){
  struct xz_pc_stem_source source;int valid=xz_pc_stems_native_source(d,&source)&&source.pc&&source.linked&&source.rekordbox&&source.loaded&&source.slot>=0;
  valid=valid&&source.slot<4;
  unsigned slot=valid?(unsigned)source.slot:0;int ready=valid&&snapshot(slot);
  fprintf(file,"%s{\"physical\":%u,\"pc_slot\":%d,\"ready\":%s",d?",":"",d,valid?source.slot:-1,ready?"true":"false");
  if(ready){fprintf(file,",\"epoch\":%u,\"serial\":%u,\"samples\":%u,\"hash\":\"",snapshots[slot].epoch,snapshots[slot].serial,snapshots[slot].count);
   for(unsigned i=0;i<32;i++)fprintf(file,"%02x",snapshots[slot].hash[i]);fprintf(file,"\"");}
  fprintf(file,"}");
 }
 fprintf(file,"]}\n");int failed=ferror(file);if(fclose(file))failed=1;
 if(!failed)rename(DIRECTORY "/state.tmp",DIRECTORY "/state.json");
}
int xz_pc_wave_current(unsigned physical,unsigned slot,uint32_t epoch,uint32_t serial,uint32_t count){
 struct xz_pc_stem_source source;struct xz_pc_wave_stamp stamp;
 return physical<2&&slot<4&&count&&count<=MAX_SAMPLES&&xz_pc_stems_native_source(physical,&source)&&source.pc&&source.linked&&source.rekordbox&&source.loaded&&source.slot==(int)slot&&
        xz_pc_wave_native_stamp(slot,&stamp)&&!stamp.busy&&stamp.complete&&stamp.count==count&&stamp.epoch==epoch&&stamp.serial==serial;
}
int xz_pc_wave_load(unsigned physical,struct xz_wave_load *out,unsigned *slot,uint32_t *epoch,uint32_t *serial){
 struct xz_pc_stem_source source;
 if(!out||!slot||!epoch||!serial)return -1;
 memset(out,0,sizeof(*out));
 if(physical>=2||!session||!xz_pc_stems_native_source(physical,&source)||!source.pc||!source.linked||!source.rekordbox||!source.loaded||source.slot<0||source.slot>=4)return -1;
 unsigned target=(unsigned)source.slot;if(!snapshot(target))return -1;
 uint32_t expected_epoch=snapshots[target].epoch,expected_serial=snapshots[target].serial,expected_count=snapshots[target].count;
 char path[120];snprintf(path,sizeof(path),DIRECTORY "/slot%u.pcw",target);
 FILE *file=fopen(path,"rb");if(!file)return -1;
 size_t size=96+(size_t)snapshots[target].count*5;uint8_t *b=malloc(size+1);
 if(!b){fclose(file);return -1;}size_t n=fread(b,1,size+1,file);int failed=ferror(file);fclose(file);
 int result=failed?-1:xz_pc_wave_packet(b,n,target,session,snapshots[target].epoch,snapshots[target].count,snapshots[target].hash,out);free(b);
 if(result)return -1;
 if(!xz_pc_wave_current(physical,target,expected_epoch,expected_serial,expected_count)){xz_wave_load_free(out);return -1;}
 *slot=target;*epoch=expected_epoch;*serial=expected_serial;return 0;
}
