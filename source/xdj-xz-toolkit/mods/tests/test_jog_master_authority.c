#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main

static struct xz_master_observation observed;
static int observer_ok=1;
static unsigned native_master[2],native_sync[2],checks,failures;
static unsigned observer_calls,observer_revoke_at;
int xz_sync_link_runtime_master_observe(struct xz_master_observation *out){
 if(++observer_calls==observer_revoke_at){observed.deck=XZ_MASTER_UNKNOWN;observed.operator_epoch++;}
 *out=observed;return observer_ok;
}
static void native_indicators(void *self){
 original(self);unsigned channel;memcpy(&channel,(uint8_t *)self+0x64,4);unsigned side=channel-1;
 uint8_t *f=(uint8_t *)self+0x104,p[64];
 for(unsigned i=0;i<64;i++)p[i]=f[12+(i^3u)];
 p[5]=(uint8_t)((p[5]&~0xb0u)|(native_master[side]?0x80u:0u)|(native_sync[side]?0x10u:0u));
 for(unsigned i=0;i<64;i++)f[12+(i^3u)]=p[i];
 f[0x56]=(uint8_t)((f[0x56]&~0x90u)|(native_master[side]?0x80u:0u)|(native_sync[side]?0x10u:0u));
 uint32_t crc=xz_jd_crc(f);memcpy(f+144,&crc,4);memcpy(original_frame[side],f,148);
}
static void check(int value,const char *what,unsigned side,int master){
 checks++;if(value)return;failures++;if(failures<16)printf("RED %s side=%u actual-master=%d\n",what,side,master);
}
static void setup(unsigned side,unsigned extra,int master){
 reset(0);native_master[0]=native_master[1]=1;native_sync[0]=1;native_sync[1]=0;observer_ok=1;
 const struct xz_jog_display_test_ops own_ops={native_indicators,now,control};
 xz_jog_display_runtime_test_install(&own_ops,&hook);
 selection.deck[side]=(int)side+1+(extra?2:0);selection.epoch[side]++;
 observed=(struct xz_master_observation){.deck=master,.revision=5,.operator_epoch=2,.observed_ms=(uint32_t)clock_ms,
  .request_epoch=9,.lease_generation=4,.route_epoch=3,.load_revision=7,.transport_generation=2};
 /* Deliberately old publication: the current observer is the display authority. */
 values.master_actual=observed;values.master_actual.deck=(int)side+2;values.master_actual.operator_epoch=1;
 values.deck[side].sync.requested=1;values.deck[side].sync.locked=0;publish();
}
static void settled(unsigned side){for(unsigned i=0;i<20;i++)tick(side);}
int main(void){
 for(unsigned side=0;side<2;side++)for(unsigned extra=0;extra<2;extra++)for(int master=-2;master<4;master++){
  setup(side,extra,master);settled(side);uint8_t p[64];packet(side,p);
  int expected=master==(int)side+(extra?2:0);
  check(!!(frame(side)[0x56]&0x80)==expected,"outer MASTER",side,master);
  check(p[1]==0x21&&!!(p[5]&0x80)==expected,"INFO MASTER",side,master);
  check(!!(frame(side)[0x56]&0x10)==(extra?1:!!native_sync[side]),"SYNC follows selected deck",side,master);
  check((p[5]&0x30)==(extra?0x20:native_sync[side]?0x10:0),"INFO SYNC state",side,master);
  uint32_t crc;memcpy(&crc,frame(side)+144,4);check(crc==xz_jd_crc(frame(side)),"CRC",side,master);
 }
 for(unsigned side=0;side<2;side++)for(unsigned invalid=0;invalid<7;invalid++){
  setup(side,1,(int)side+2);
  if(invalid==0)observed.deck=XZ_MASTER_UNKNOWN;
  if(invalid==1)observer_ok=0;
  if(invalid==2)observed.observed_ms=(uint32_t)clock_ms-101;
  if(invalid==3)observed.observed_ms=(uint32_t)clock_ms+1;
  if(invalid==4)observed.route_epoch++;
  if(invalid==5)observed.load_revision++;
  if(invalid==6)observed.transport_generation++;
  settled(side);uint8_t p[64];packet(side,p);
  check(!(frame(side)[0x56]&0x80)&&!(p[5]&0x80),"revoked/invalid software MASTER",side,observed.deck);
  check((frame(side)[0x53]>>4)==side+3,"invalid authority keeps track display",side,observed.deck);
 }
 for(unsigned side=0;side<2;side++){
  setup(side,1,(int)side+2);settled(side);
  observed.deck=(int)(side^1u);observed.operator_epoch++;tick(side);
  uint8_t p[64];packet(side,p);check(!(p[5]&0x80)&&!(frame(side)[0x56]&0x80),"handoff without publisher",side,observed.deck);
  selection.deck[side]=(int)side+1;selection.epoch[side]++;observed.deck=(int)side;settled(side);packet(side,p);
  check(!!(p[5]&0x80)&&!!(frame(side)[0x56]&0x80),"return native confirmed MASTER",side,observed.deck);
 }
 for(unsigned side=0;side<2;side++){
  setup(side,1,(int)side+2);settled(side);observer_calls=0;observer_revoke_at=2;tick(side);observer_revoke_at=0;
  uint8_t p[64];packet(side,p);check(!(p[5]&0x80)&&!(frame(side)[0x56]&0x80),"late operator fence",side,observed.deck);
  check((frame(side)[0x53]>>4)==side+3,"revocation keeps INFO",side,observed.deck);
  setup(side,0,XZ_MASTER_UNKNOWN);settled(side);
  for(unsigned i=0;i<148;i++){
   if(i==8||i==9||i>=144)continue;
   check(frame(side)[i]==original_frame[side][i],"unknown authority preserves native frame",side,observed.deck);
  }
  setup(side,0,(int)(side^1u));settled(side);
  for(unsigned i=0;i<148;i++){
   if(i==8||i==9||i>=144||i==0x56||i==12+(5^3))continue;
   check(frame(side)[i]==original_frame[side][i],"native overlay touches only MASTER",side,observed.deck);
  }
 }
 const uint32_t times[]={10000,0,0xffffffffu};
 for(unsigned side=0;side<2;side++)for(unsigned t=0;t<3;t++)for(unsigned age=99;age<=101;age++){
  setup(side,1,(int)side+2);clock_ms=times[t];values.deck[side].timeline.at_ms=(uint32_t)clock_ms;
  observed.observed_ms=(uint32_t)clock_ms-age;values.master_actual.observed_ms=(uint32_t)clock_ms;
  publish();settled(side);uint8_t p[64];packet(side,p);
  check(!!(p[5]&0x80)==(age<=100)&&!!(frame(side)[0x56]&0x80)==(age<=100),"authority age and wrap",side,observed.deck);
 }
 printf("RESULT Jog authority checks=%u failures=%u\n",checks,failures);return !!failures;
}
