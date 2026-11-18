/* SPDX-License-Identifier: MIT */
#include "stems.h"
#include "../runtime.h"
#include <string.h>

#define CERT 0x1c8a034u
#define NOTE 0x2be2f8u
static int word(uint32_t address,uint32_t *out){return xz_read_memory(address,out,4);}
static int guarded(void){
 static const uint8_t note[8]={0x00,0x00,0x53,0xe3,0x90,0x10,0x81,0xe3};
 static const uint8_t send[8]={0x00,0x30,0x90,0xe5,0x70,0x40,0x2d,0xe9};
 uint8_t actual[8];
 return !xz_read_memory(NOTE,actual,8)&&!memcmp(note,actual,8)&&
        !xz_read_memory(0x2bde14,actual,8)&&!memcmp(send,actual,8);
}
static uint32_t sender(void){
 uint8_t certified=0;uint32_t midi=0,device=0,vtable=0;
 if(!guarded()||xz_read_memory(CERT+12,&certified,1)||!certified||
    word(CERT+4,&midi)||!midi||midi>UINT32_MAX-4||word(midi,&device)||!device||
    word(device,&vtable)||vtable!=0x487168)return 0;
 return midi;
}
int xz_pc_stems_native_ready(void){return sender()!=0;}
int xz_pc_stems_native_redraw(void){
 static const uint8_t guard[8]={0x08,0x40,0x2d,0xe9,0x00,0x10,0xa0,0xe1};
 uint8_t actual[8];uint32_t scene=0,flag=0;
 if(xz_read_memory(0xf3d04,actual,8)||memcmp(actual,guard,8)||word(0x3c6fc7c,&scene)||scene!=2||
    word(0x2247c6c,&flag)||!flag||flag>65535)return 0;
#if defined(__arm__) && !defined(__aarch64__)
 ((void(*)(unsigned))(uintptr_t)0xf3d04)(0x00c00000u);
 return 1;
#else
 return 0;
#endif
}
static int note(void *context,unsigned channel,unsigned key,int pressed){
 (void)context;
 if(channel<12||channel>15||key<0x70||key>0x73)return 0;
 uint32_t midi=sender();if(!midi)return 0;
#if defined(__arm__) && !defined(__aarch64__)
 ((void (*)(void*,int,int,int))(uintptr_t)NOTE)((void*)(uintptr_t)midi,(int)channel,(int)key,!!pressed);
 return 1; /* Sent via stock output, not a claim that rekordbox acted. */
#else
 (void)pressed;
 return 0;
#endif
}
struct xz_pc_stem_sink xz_pc_stems_native_sink(void){return (struct xz_pc_stem_sink){NULL,note};}
int xz_pc_stems_native_feedback(struct xz_pc_stems *s,const void *message){
 if(!message)return 0;
 uint32_t address=(uint32_t)(uintptr_t)message,buffer=0,size=0;uint8_t bytes[3];
 if(address>UINT32_MAX-16||word(address+8,&buffer)||word(address+12,&size)||size!=3||!buffer||
    buffer>UINT32_MAX-3||xz_read_memory(buffer,bytes,3))return 0;
 return xz_pc_stems_feedback(s,bytes,3);
}
static uint32_t epoch[4]={1,1,1,1};
static uint32_t wave_epoch[4]={1,1,1,1},wave_serial[4],wave_writers[4];
static int (*stock_receive)(const uint8_t *,int);
static int (*stock_clear)(unsigned);
static int (*stock_discard)(unsigned,unsigned,unsigned);
static int observing;
static void invalidate_begin(unsigned bank){
 if(bank>=4)return;
 __atomic_add_fetch(&wave_writers[bank],1,__ATOMIC_ACQ_REL);
 __atomic_add_fetch(&wave_serial[bank],1,__ATOMIC_RELEASE);
 __atomic_add_fetch(&wave_epoch[bank],1,__ATOMIC_RELEASE);
 __atomic_add_fetch(&epoch[bank],1,__ATOMIC_RELEASE);
}
static void invalidate_end(unsigned bank){
 if(bank>=4)return;
 __atomic_add_fetch(&wave_serial[bank],1,__ATOMIC_RELEASE);
 __atomic_sub_fetch(&wave_writers[bank],1,__ATOMIC_RELEASE);
}
static int clear(unsigned bank){
 invalidate_begin(bank);
 int result=stock_clear?stock_clear(bank):0;
 invalidate_end(bank);
 return result;
}
static int discard(unsigned bank,unsigned extent,unsigned total){
 invalidate_begin(bank);
 int result=stock_discard?stock_discard(bank,extent,total):0;
 invalidate_end(bank);
 return result;
}
static int receive(const uint8_t *packet,int bytes){
 unsigned bank=4,type=0,extent=0;
 if(packet&&bytes>=2){bank=(packet[0]>>4)-1u;type=packet[1];}
 if(bank<4&&bytes>=6){extent=(unsigned)packet[2]|(unsigned)packet[3]<<8;}
 int wave=bank<4&&(type==0x22||type==0x2e||type==0x35||type==0x36||type==0x26);
 if(wave){__atomic_add_fetch(&wave_writers[bank],1,__ATOMIC_ACQ_REL);__atomic_add_fetch(&wave_serial[bank],1,__ATOMIC_RELEASE);}
 if(bank<4&&type==0x22&&extent==1){__atomic_add_fetch(&epoch[bank],1,__ATOMIC_RELEASE);__atomic_add_fetch(&wave_epoch[bank],1,__ATOMIC_RELEASE);}
 else if(bank<4&&(type==0x35||(type==0x2e&&extent==1))){__atomic_add_fetch(&wave_epoch[bank],1,__ATOMIC_RELEASE);__atomic_add_fetch(&epoch[bank],1,__ATOMIC_RELEASE);}
 uint8_t before=0,after=0;
 if(bank<4&&type==0x21)xz_read_memory(0x11ecd58u+bank*80u+7u,&before,1);
 int result=stock_receive?stock_receive(packet,bytes):0;
 if(bank<4&&type==0x21&&!xz_read_memory(0x11ecd58u+bank*80u+7u,&after,1)&&
    !(before&1)&&(after&1)){__atomic_add_fetch(&epoch[bank],1,__ATOMIC_RELEASE);__atomic_add_fetch(&wave_epoch[bank],1,__ATOMIC_RELEASE);}
 if(wave){__atomic_add_fetch(&wave_serial[bank],1,__ATOMIC_RELEASE);__atomic_sub_fetch(&wave_writers[bank],1,__ATOMIC_RELEASE);}
 return result;
}
int xz_pc_stems_native_observe(void){
 static const uint8_t guard[8]={0xf0,0x4f,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
 static const uint8_t clear_guard[8]={0x03,0x00,0x50,0xe3,0xf0,0x41,0x2d,0xe9};
 static const uint8_t discard_guard[8]={0x03,0x00,0x50,0xe3,0xf0,0x47,0x2d,0xe9};
 if(observing)return 0;
 uint8_t actual[8];
 if(xz_read_memory(0xe52d0,actual,8)||memcmp(actual,guard,8)||
    xz_read_memory(0xe8194,actual,8)||memcmp(actual,clear_guard,8)||
    xz_read_memory(0xe8604,actual,8)||memcmp(actual,discard_guard,8))return -1;
 if((!stock_clear&&xz_hook_arm(0xe8194,clear_guard,(void*)clear,(void**)&stock_clear))||
    (!stock_discard&&xz_hook_arm(0xe8604,discard_guard,(void*)discard,(void**)&stock_discard))||
    (!stock_receive&&xz_hook_arm(0xe52d0,guard,(void*)receive,(void**)&stock_receive)))return -1;
 observing=1;
 return 0;
}
int xz_pc_stems_native_source(unsigned physical,struct xz_pc_stem_source *out){
 if(!out||physical>=2)return 0;
 memset(out,0,sizeof(*out));out->slot=-1;
 uint32_t slot=0,on=0;
 if(word(0x224e5f4u+0xd4u+physical*8u,&slot)||word(0x224e5f4u+0xd8u+physical*8u,&on))return 0;
 if(!on)return 1;
 out->pc=1;out->linked=xz_pc_stems_native_ready();
 if(slot>=4||(slot&1)!=physical)return 1;
 out->slot=(int)slot;out->epoch=__atomic_load_n(&epoch[slot],__ATOMIC_ACQUIRE);
 /* These certified clients do not populate the legacy AppName/time-enable
  * records. Mirror cert_is_certified: PioneerDJ+FF is rekordbox; Serato+1 is
  * another vendor. CERT byte13 is an activation latch, not the level. */
 uint8_t before[48],after[48],certified=0,certified_after=0;uint32_t state=0,name_ptr=0,length=0;char name[33]={0};
 if(xz_read_memory(0x1c8aaa8,before,sizeof(before))||xz_read_memory(CERT+12,&certified,1)||!certified)return 1;
 memcpy(&state,before,4);memcpy(&name_ptr,before+12,4);memcpy(&length,before+16,4);
 if(state!=3||!name_ptr||length<1||length>32||name_ptr>UINT32_MAX-length||xz_read_memory(name_ptr,name,length)||
    xz_read_memory(0x1c8aaa8,after,sizeof(after))||xz_read_memory(CERT+12,&certified_after,1)||
    certified_after!=certified||memcmp(before,after,sizeof(before)))return 1;
 out->rekordbox=before[44]==0xff&&!strcmp(name,"PioneerDJ");
 uint32_t header[3],after_header[3],after_slot=0,after_on=0;
 uint32_t address=0x11ed0c8u+slot*0x107ad4u;
 if(!xz_read_memory(address,header,sizeof(header))&&!xz_read_memory(address,after_header,sizeof(after_header))&&
    !memcmp(header,after_header,sizeof(header))&&header[0]==1&&header[2]>0&&header[2]<=540000)
     out->loaded=1; /* Waveform available; companion binding proves its content. */
 if(word(0x224e5f4u+0xd4u+physical*8u,&after_slot)||word(0x224e5f4u+0xd8u+physical*8u,&after_on)||after_slot!=slot||after_on!=on)return 0;
 return 1;
}
int xz_pc_wave_native_stamp(unsigned slot,struct xz_pc_wave_stamp *out){
 if(!out||slot>=4||!observing)return 0;
 memset(out,0,sizeof(*out));
 out->epoch=__atomic_load_n(&wave_epoch[slot],__ATOMIC_ACQUIRE);
 out->serial=__atomic_load_n(&wave_serial[slot],__ATOMIC_ACQUIRE);
 out->busy=__atomic_load_n(&wave_writers[slot],__ATOMIC_ACQUIRE)!=0;
 uint32_t head[3];
 if(xz_read_memory(0x11ed0c8u+slot*0x107ad4u,head,sizeof(head)))return 0;
 out->complete=head[0]==1;out->count=head[2];
 uint32_t final_epoch=__atomic_load_n(&wave_epoch[slot],__ATOMIC_ACQUIRE);
 uint32_t final_serial=__atomic_load_n(&wave_serial[slot],__ATOMIC_ACQUIRE);
 if(final_epoch!=out->epoch||final_serial!=out->serial||
    __atomic_load_n(&wave_writers[slot],__ATOMIC_ACQUIRE))return 0;
 return 1;
}
