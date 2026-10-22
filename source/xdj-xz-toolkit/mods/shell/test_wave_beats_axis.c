#ifdef NDEBUG
#error Beat axis acceptance requires assertions
#endif
#include "shell_axis.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct xz_shell_beat beats[65537];
static unsigned checks;
#define CHECK(e) do {assert(e);checks++;}while(0)
static int64_t magnitude(int64_t x){return x<0?-x:x;}
static void properties(void){
 const int zooms[]={32,48,64,96,128,192,256};
 for(int variable=0;variable<2;variable++)for(int original=0;original<3;original++){
  int bpm=(int[]){12000,15000,12837}[original];
  for(int i=0;i<65537;i++){
   int64_t ms=123+(int64_t)i*6000000/bpm;
   if(variable&&i>32)ms=123+32LL*6000000/bpm+(i-32LL)*350;
   beats[i]=(struct xz_shell_beat){(int32_t)ms,(uint8_t)(1+i%4)};
  }
  struct xz_shell_deck d={.known=XZ_SHELL_KNOWN(XZ_SHELL_F_GRID)|XZ_SHELL_KNOWN(XZ_SHELL_F_TRACK_BPM),.grid=beats,.grid_count=65537,.track_bpm_x100=bpm};
  for(int z=0;z<7;z++)for(int where=0;where<4;where++){
   int index=(int[]){0,30,32481,65535}[where];int32_t head=beats[index].ms;
   struct sh_wave_axis a=sh_axis_window(sh_axis(zooms[z],&d),head,700);
   CHECK(a.grid&&a.hi>a.lo&&a.hi-a.lo<=112);
   for(uint32_t i=a.lo;i<a.hi;i++){
    int ppb=zooms[z]/2;
    CHECK(sh_axis_px(a,beats[i].ms)==(int64_t)i*ppb);
    CHECK(sh_axis_ms(a,(int64_t)i*ppb)==beats[i].ms);
    for(int q=0;q<4;q++){
     int32_t ms=beats[i].ms+(beats[i+1].ms-beats[i].ms)*q/4;
     int64_t error=magnitude((int64_t)sh_axis_ms(a,sh_axis_px(a,ms))-ms);
     CHECK(error<=(beats[i+1].ms-beats[i].ms)/ppb+2);
    }
   }
   for(int i=0;i<5;i++){
    int32_t ms=(int32_t[]){INT32_MIN,-4000,0,123,INT32_MAX}[i];
    int64_t px=sh_axis_px(a,ms);CHECK(px>=sh_axis_px(a,INT32_MIN)&&px<=sh_axis_px(a,INT32_MAX));
    CHECK(sh_axis_ms(a,INT64_MIN)==INT32_MIN&&sh_axis_ms(a,INT64_MAX)==INT32_MAX);
   }
   struct sh_wave_axis base=a;
   d.known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO)|XZ_SHELL_KNOWN(XZ_SHELL_F_BPM)|XZ_SHELL_KNOWN(XZ_SHELL_F_TOTAL)|XZ_SHELL_KNOWN(XZ_SHELL_F_SYNC)|XZ_SHELL_KNOWN(XZ_SHELL_F_SYNC_STATUS);
   for(int mode=0;mode<5;mode++){
    d.sync=mode&1;d.sync_status=(enum xz_shell_sync_status)mode;d.bpm_x100=9000+1000*mode;d.tempo_x100=mode*1000-1000;d.total_ms=INT32_MAX-mode;
    a=sh_axis_window(sh_axis(zooms[z],&d),head,700);
    CHECK(sh_axis_px(a,head+123)==sh_axis_px(base,head+123));
   }
  }
 }
}
static void invalid_and_missing(void){
 for(int fault=0;fault<8;fault++){
  for(int i=0;i<200;i++)beats[i]=(struct xz_shell_beat){100+i*400,(uint8_t)(1+i%4)};
  struct xz_shell_deck d={.known=XZ_SHELL_KNOWN(XZ_SHELL_F_GRID)|XZ_SHELL_KNOWN(XZ_SHELL_F_TRACK_BPM),.grid=beats,.grid_count=200,.track_bpm_x100=15000};
  if(fault==0)beats[18].ms=beats[17].ms;
  if(fault==1)beats[18].ms=beats[17].ms-1;
  if(fault==2)beats[18].beat_in_bar=0;
  if(fault==3)beats[18].beat_in_bar=4;
  if(fault==4)beats[0].ms=-1;
  if(fault==5)d.grid_count=1;
  if(fault==6)d.grid=NULL;
  if(fault==7)d.known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_GRID);
  struct sh_wave_axis a=sh_axis_window(sh_axis(96,&d),6500,700);
  CHECK(!a.grid);CHECK(sh_axis_px(a,400)==48);CHECK(sh_axis_ms(a,48)==400);
  d.known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_TRACK_BPM);
  a=sh_axis_window(sh_axis(96,&d),6500,700);CHECK(!a.grid&&sh_axis_px(a,500)==48);
 }
 struct xz_shell_deck d={0};
 for(int i=0;i<10000;i++){
  int32_t ms=i*1931-500000;struct sh_wave_axis a=sh_axis(96,&d);
  int64_t n=(int64_t)ms*96+500;CHECK(sh_axis_px(a,ms)==sh_axis_floor(n,1000));
 }
}
int main(void){properties();invalid_and_missing();printf("PASS beat axis checks=%u bounded_window_intervals<=112 inverse/source_edges/fractional/malformed/no_grid\n",checks);return 0;}
