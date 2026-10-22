#ifdef NDEBUG
#error LED acceptance requires assertions
#endif
#include "native_led.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls;static int bank;
static int color(int deck,int pad,uint32_t *rgb,int *enabled) {
 assert(deck==0);assert(pad>=0&&pad<8);calls++;
 if(pad<bank*4||pad>=bank*4+4)return 0;
 *rgb=0x123456;*enabled=pad!=bank*4+1;return 1;
}
static void put(unsigned char *p,uint32_t v) {memcpy(p,&v,4);}
static uint32_t word(const unsigned char *p) {uint32_t v;memcpy(&v,p,4);return v;}
static int stem_color(int deck,int pad,uint32_t *rgb,int *enabled) {
 assert(deck==0);
 if(pad>=4)return 0;
 *rgb=xz_stem_hardware_color(pad<3?2-pad:3);*enabled=1;return 1;
}
static int tint(int deck,int pad,uint32_t *rgb,int *enabled){(void)deck;(void)pad;*rgb=xz_jump_page2_color(pad);*enabled=1;return XZ_LED_HUE;}
static struct xz_extra_deck extra;
static int extra_color(int deck,int pad,uint32_t *rgb,int *enabled){assert(deck==0);return xz_extra_pad_led(&extra,pad,rgb,enabled);}
int main(void) {
 unsigned char packet[8*XZ_LED_RECORD_BYTES],saved[sizeof(packet)];
 memset(packet,0,sizeof(packet));
 for(unsigned i=0;i<8;i++) {
  unsigned char *p=packet+i*XZ_LED_RECORD_BYTES;
  put(p,0x12+i);put(p+4,1);put(p+0x10,2);put(p+0x14,1);
  put(p+0x1c,500);p[0x28]=0xee;
 }
 /* C is stock-forced; inactive E-H and channel2 must stay native. */
 packet[2*XZ_LED_RECORD_BYTES+0x18]=1;
 memcpy(saved,packet,sizeof(packet));
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,color)==3&&calls==7);
 assert(word(packet+0x10)==1&&word(packet+0x14)==0&&word(packet+0x1c)==0);
 assert(packet[0x28]==0x12&&packet[0x29]==0x34&&packet[0x2a]==0x56&&!packet[0x18]);
 assert(word(packet+XZ_LED_RECORD_BYTES+0x10)==1);
 assert(word(packet+XZ_LED_RECORD_BYTES+0x14)==1);
 assert(packet[XZ_LED_RECORD_BYTES+0x28]==0x12&&packet[XZ_LED_RECORD_BYTES+0x29]==0x34&&packet[XZ_LED_RECORD_BYTES+0x2a]==0x56);
 assert(!memcmp(packet+2*XZ_LED_RECORD_BYTES,saved+2*XZ_LED_RECORD_BYTES,XZ_LED_RECORD_BYTES));
 assert(!memcmp(packet+4*XZ_LED_RECORD_BYTES,saved+4*XZ_LED_RECORD_BYTES,4*XZ_LED_RECORD_BYTES));
 memcpy(packet,saved,sizeof(packet));bank=1;
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,color)==4);
 assert(!memcmp(packet,saved,4*XZ_LED_RECORD_BYTES));
 for(unsigned i=4;i<8;i++)assert(word(packet+i*XZ_LED_RECORD_BYTES+0x14)==(i==5?1u:0u));
 memcpy(packet,saved,sizeof(packet));calls=0;
 assert(!xz_native_led_apply(packet,sizeof(packet),8,2,color)&&!calls);
 assert(!memcmp(packet,saved,sizeof(packet)));
 assert(!xz_native_led_apply(packet,sizeof(packet)-1,8,1,color));
 assert(!xz_native_led_apply(packet,sizeof(packet),257,1,color));
 assert(!xz_native_led_apply(packet,sizeof(packet),8,0,color));
 assert(!xz_native_led_apply(NULL,sizeof(packet),8,1,color));
 assert(!xz_native_led_apply(packet,sizeof(packet),8,1,NULL));
 assert(!memcmp(packet,saved,sizeof(packet)));
 /* Firmware PadColorCode palette entries 21, 1, 42, 64 at 0x47b740.
    Secondary channels must stay zero on the three colored physical pads. */
 const unsigned char native_rgb[4][3]={{0,255,0},{255,0,0},{0,0,255},{255,255,255}};
 packet[2*XZ_LED_RECORD_BYTES+0x18]=0;
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,stem_color)==4);
 for(unsigned i=0;i<4;i++) {
  assert(!memcmp(packet+i*XZ_LED_RECORD_BYTES+0x28,native_rgb[i],3));
  assert(word(packet+i*XZ_LED_RECORD_BYTES+0x14)==0);
 }
 memcpy(packet,saved,sizeof(packet));
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,tint)==7);
 for(unsigned i=0;i<8;i++){
  unsigned char *p=packet+i*XZ_LED_RECORD_BYTES,*old=saved+i*XZ_LED_RECORD_BYTES;
  assert(!memcmp(p,old,0x28));assert(p[0x2b]==old[0x2b]);
  if(i==2)assert(!memcmp(p,old,XZ_LED_RECORD_BYTES));
  else{
   uint32_t expected=i<2||i>=6?0xff3000:0xffa300;
   for(unsigned k=0;k<3;k++)assert(p[0x28+k]==((((expected>>(16-8*k))&255u)*old[0x28]+127u)/255u));
   assert(!p[0x2a]);
  }
 }
 memset(packet+0x28,0,3);assert(xz_native_led_apply(packet,XZ_LED_RECORD_BYTES,1,1,tint)==1);
 assert(!packet[0x28]&&!packet[0x29]&&!packet[0x2a]);
 /* DECK 3 on the left: stored cues lit in their colour (black falls back to green), empty pads and
    unknown cues off, the stock-forced pad C untouched, channel 2 untouched. */
 memcpy(packet,saved,sizeof(packet));
 extra.loaded=1;extra.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_HOT_CUES);
 extra.hot_cue[0]=(struct xz_extra_cue){1,1000,0xff0000};
 extra.hot_cue[3]=(struct xz_extra_cue){1,2000,0};
 extra.hot_cue[2]=(struct xz_extra_cue){1,3000,0x0000ff};
 extra.hot_cue[7]=(struct xz_extra_cue){1,4000,0x123456};
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,extra_color)==7);
 const uint32_t expect_rgb[8]={0xff0000,0,0,0x00ff00,0,0,0,0x123456};
 for(unsigned i=0;i<8;i++){
  unsigned char *p=packet+i*XZ_LED_RECORD_BYTES;
  if(i==2){assert(!memcmp(p,saved+i*XZ_LED_RECORD_BYTES,XZ_LED_RECORD_BYTES));continue;}
  int lit=expect_rgb[i]!=0;
  assert(word(p+0x10)==1&&word(p+0x14)==(lit?0u:1u)&&word(p+0x1c)==0);
  assert(p[0x28]==(expect_rgb[i]>>16&255)&&p[0x29]==(expect_rgb[i]>>8&255)&&p[0x2a]==(expect_rgb[i]&255));
 }
 memcpy(packet,saved,sizeof(packet));
 assert(!xz_native_led_apply(packet,sizeof(packet),8,2,extra_color));
 assert(!memcmp(packet,saved,sizeof(packet)));
 extra.known=0;
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,extra_color)==7);
 for(unsigned i=0;i<8;i++)if(i!=2)assert(word(packet+i*XZ_LED_RECORD_BYTES+0x14)==1&&!packet[i*XZ_LED_RECORD_BYTES+0x28]);
 extra.known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_HOT_CUES);extra.loaded=0;memcpy(packet,saved,sizeof(packet));
 assert(xz_native_led_apply(packet,sizeof(packet),8,1,extra_color)==7);
 assert(word(packet+0x14)==1);
 uint32_t rgb=1;int lit=1;
 assert(xz_extra_pad_led(&extra,8,&rgb,&lit)==XZ_LED_STOCK&&xz_extra_pad_led(NULL,0,&rgb,&lit)==XZ_LED_STOCK);
 /* Hypothetical status id and encoding only, never a device mapping. */
 unsigned char status[3*XZ_LED_RECORD_BYTES]={0},before[sizeof status];
 for(unsigned i=0;i<3;i++){put(status+i*XZ_LED_RECORD_BYTES,0x70);put(status+i*XZ_LED_RECORD_BYTES+4,i==1?2:1);}
 status[2*XZ_LED_RECORD_BYTES+0x18]=1;memcpy(before,status,sizeof status);
 struct xz_led_keysync_spec spec={.enabled=1,.id=0x70,.mask={0xff,0xff},.blink={3,7}};
 assert(!xz_native_led_keysync(status,sizeof status,3,1,1,&xz_led_keysync_mapping));
 assert(!memcmp(status,before,sizeof status));
 assert(!xz_native_led_keysync(status,sizeof status,3,1,0,&spec));
 assert(xz_native_led_keysync(status,sizeof status,3,1,1,&spec)==1);
 assert(word(status+8)==3&&word(status+12)==7);
 assert(!memcmp(status+XZ_LED_RECORD_BYTES,before+XZ_LED_RECORD_BYTES,2*XZ_LED_RECORD_BYTES));
 assert(!xz_native_led_keysync(status,sizeof status-1,3,1,1,&spec));
 xz_native_led_probe(status,sizeof status,3,1);xz_native_led_probe(status,sizeof status,3,2);
 assert(xz_mods_led_probe_v1.entry[0][0].id==0x70&&xz_mods_led_probe_v1.entry[0][0].seen==2);
 assert(xz_mods_led_probe_v1.entry[1][0].seen==1);
 assert(!(xz_mods_led_probe_v1.entry[0][0].sequence&1));
 for(unsigned i=0;i<XZ_LED_PROBE_IDS+1;i++){put(status,0x100+i);xz_native_led_probe(status,XZ_LED_RECORD_BYTES,1,1);}
 assert(xz_mods_led_probe_v1.overflow[0]==2);
 /* Existing-record scope: no append, index write, timer, loop or ring write. */
 {
  unsigned char existing[4*XZ_LED_RECORD_BYTES+64],before[sizeof existing];
  uint16_t idx[120],cnt=4;
  memset(existing,0xa5,sizeof existing);memset(idx,0x5a,sizeof idx);
  put(existing,XZ_LED_ID_PLAY_PAUSE);put(existing+4,1);put(existing+0x10,0);existing[0x18]=0;
  put(existing+44,XZ_LED_ID_LOOP_IN);put(existing+48,1);
  put(existing+88,XZ_LED_ID_PLAY_PAUSE);put(existing+92,2);
  put(existing+132,XZ_LED_ID_CUE);put(existing+136,1);
  existing[132+0x18]=1;
  memcpy(before,existing,sizeof existing);
  struct xz_extra_deck d={0};d.loaded=d.playing=1;d.loop.active=1;
  assert(xz_native_led_extra_deck(existing,sizeof existing,&cnt,idx,65535,65535,
                                  0,1000,1,&d)==1);
  assert(cnt==4&&word(existing+0x10)==1);
  assert(!memcmp(existing+44,before+44,sizeof existing-44));
  for(unsigned i=0;i<120;i++)assert(idx[i]==0x5a5a);
  cnt=0;memcpy(existing,before,sizeof existing);
  assert(!xz_native_led_extra_deck(existing,sizeof existing,&cnt,idx,0,0,
                                   0,1000,1,&d));
  assert(!memcmp(existing,before,sizeof existing));
  cnt=65535;
  assert(!xz_native_led_extra_deck(existing,sizeof existing,&cnt,idx,65535,65535,
                                   0,1000,1,&d));
  assert(!memcmp(existing,before,sizeof existing));
 }
 puts("PASS native LED packet bounds, colors, mute, DECK 3/4 hot cues, existing-record-only transport, stock ownership and channel isolation");
}
