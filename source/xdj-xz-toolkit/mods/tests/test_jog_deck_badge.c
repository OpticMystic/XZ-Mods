#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main
static unsigned checks,failures;
static unsigned link_kind=0xc0,certified;
static void link_original(void *self){
 original(self);uint8_t *f=(uint8_t *)self+0x104;
 f[2]|=(uint8_t)(0x80u|certified);f[0x4e]=(uint8_t)link_kind;
 uint32_t crc=xz_jd_crc(f);memcpy(f+144,&crc,4);
 unsigned side;memcpy(&side,(uint8_t *)self+0x64,4);memcpy(original_frame[side-1],f,148);
}
static void check(int ok,const char *what,unsigned side,unsigned logical){
 checks++;if(!ok){failures++;if(failures<12)printf("RED %s side=%u deck=%u\n",what,side,logical);}
}
int main(void){
 for(unsigned side=0;side<2;side++)for(unsigned extra=0;extra<2;extra++)for(unsigned loaded=0;loaded<2;loaded++){
  reset(0);selection.deck[side]=(int)(side+1+extra*2);selection.epoch[side]++;
  values.deck[side].loaded=loaded;publish();for(unsigned n=0;n<16;n++)tick(side);
  const uint8_t *f=frame(side);unsigned logical=side+1+extra*2;
  check(!!(f[2]&0x80),"LINK presentation",side,logical);
  check(!!(f[2]&0x10)==extra,"alternate side bit",side,logical);
  check((f[0x53]>>4)==logical,"logical number",side,logical);
  check((f[15]>>4)==side+1,"physical command unchanged",side,logical);
  check((f[2]&~0xd0u)==(original_frame[side][2]&~0xd0u),"unowned USB/outer flags unchanged",side,logical);
  check(f[0x0a]==original_frame[side][0x0a]&&f[0x4e]==0x40,"local packet retained with complete LINK stream",side,logical);
  uint32_t crc;memcpy(&crc,f+144,4);check(crc==xz_jd_crc(f),"CRC",side,logical);
  selection.route_up=0;selection.route_epoch++;selection.epoch[side]++;
  for(unsigned n=0;n<24;n++)tick(side);
  check(frame(side)[2]==original_frame[side][2],"standalone restoration",side,logical);
 }
 reset(0);selection.epoch[0]++;reads=0;flip_at=3;flip_kind=1;tick(0);flip_at=0;
 check(!memcmp(frame(0),original_frame[0],148),"rejected choice emits no badge override",0,3);
 for(unsigned side=0;side<2;side++)for(unsigned guard=0;guard<3;guard++){
  reset(0);link_kind=guard==2?0x40:0xc0;certified=guard==1?0x40:0;
  const struct xz_jog_display_test_ops link_ops={link_original,now,control};xz_jog_display_runtime_test_install(&link_ops,&hook);
  publish();for(unsigned n=0;n<20;n++)tick(side);
  if(!guard){uint8_t p[64];packet(side,p);
   check((frame(side)[0x53]>>4)==side+3&&p[1]==0x21&&p[0x3c]==18,"uncertified LINK accepts owned software INFO",side,side+3);
  }else check(!memcmp(frame(side),original_frame[side],148),"certified or USB-owned packet untouched",side,side+3);
 }
 printf("RESULT Jog badge checks=%u failures=%u\n",checks,failures);return !!failures;
}
