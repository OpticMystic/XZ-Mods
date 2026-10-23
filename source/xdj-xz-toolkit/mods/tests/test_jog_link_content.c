#define main retained_assets_main
#include "test_jog_assets_runtime.c"
#undef main
static unsigned checks,failures;
static void check(int ok,const char *name,unsigned side){
 checks++;if(!ok){failures++;if(failures<12)printf("RED %s side=%u\n",name,side);}
}
static void packet_pair(unsigned side,unsigned logical){
 const uint8_t *f=frame(side);uint8_t a[64],b[64];
 for(unsigned i=0;i<64;i++){a[i]=f[12+(i^3u)];b[i]=f[80+(i^3u)];}
 check(f[0x4e]==0x40&&(f[0x4f]&1),"LINK block contains a packet, not native status",side);
 check(f[0x4c]==f[8]&&f[0x4d]==f[9],"LINK packet uses accepted emission sequence",side);
 check((a[0]>>4)==side+1&&(b[0]>>4)==logical,"physical/local and logical/LINK addressing",side);
 check((a[0]&15)==(b[0]&15)&&!memcmp(a+1,b+1,63),"both display streams carry the same complete content",side);
 check(!(f[2]&0x40),"certification flag remains native",side);
 uint32_t crc;memcpy(&crc,f+144,4);check(crc==xz_jd_crc(f),"CRC",side);
}
int main(void){
 for(unsigned side=0;side<2;side++){
  reset(1025);complete();unsigned waves=0,infos=0,clears=0;
  for(unsigned n=0;n<180;n++){
   tick(side);packet_pair(side,side+3);uint8_t p[64];packet(side,p);
   if(p[1]==0x21){infos++;check(p[9]&0x20,"INFO retains valid BPM",side);check(p[0x3c]==18,"INFO retains key",side);}
   else if(p[1]==0x2c&&p[4]==73)waves++;else clears++;
  }
  check(waves==73&&infos>0&&clears>0,"complete wave plus responsive INFO and clears",side);
  selection.deck[side]=(int)side+1;selection.epoch[side]++;
  for(unsigned n=0;n<30;n++){tick(side);packet_pair(side,side+1);}
  selection.route_up=0;selection.route_epoch++;selection.epoch[side]++;
  for(unsigned n=0;n<30;n++)tick(side);
  for(unsigned i=0;i<144;i++)if(i!=8&&i!=9)check(frame(side)[i]==original_frame[side][i],"native restoration after route closes",side);
 }
 printf("RESULT LINK content checks=%u failures=%u\n",checks,failures);return !!failures;
}
