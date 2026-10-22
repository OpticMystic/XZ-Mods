#ifdef NDEBUG
#error Axis tests require assertions
#endif
#include "shell_axis.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int64_t old_px(int pps,int32_t ms){int64_t n=(int64_t)ms*pps+500;int64_t q=n/1000;return q-(n<0&&n%1000);}
static uint16_t before[800*80],after[800*80];
int main(void){
 struct xz_shell_deck d={0};
 int rates[]={0,1,7,123,5000,10000,10667,12345,20000,INT32_MAX};
 int32_t positions[]={INT32_MIN,-100000,-501,-1,0,1,469,500,123456,INT32_MAX};
 int pps_values[]={32,48,64,96,128,192,256};
 for(unsigned z=0;z<7;z++){
  int pps=pps_values[z];
  d.known=0;
  struct sh_wave_axis unknown=sh_axis(pps,&d);assert(unknown.rate==10000);
  d.known=XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO);d.tempo_x100=-10000;assert(sh_axis(pps,&d).rate==10000);
  for(unsigned r=0;r<sizeof rates/sizeof rates[0];r++){
   d.tempo_x100=rates[r]-10000;struct sh_wave_axis a=sh_axis(pps,&d);assert(a.rate==10000u);
   for(unsigned i=0;i<sizeof positions/sizeof positions[0];i++){
    int32_t ms=positions[i];int64_t px=sh_axis_px(a,ms);
    if(a.rate==10000)assert(px==old_px(pps,ms));
    int32_t back=sh_axis_ms(a,px);int64_t error=(int64_t)back-ms;if(error<0)error=-error;
    assert(error<=(int64_t)a.rate*1000/(pps*10000)+2);
    for(int delta=-800;delta<=800;delta+=400)(void)sh_axis_ms(a,px+delta);
   }
  }
 }
 /* Fractional multiplier uses the full rational scale, with no SYNC input.
   120->128 is approximated by the published nominal fader precision10667. */
 struct sh_wave_axis a={96,10667},b={96,10000};
 assert(sh_axis_px(a,500)==45&&sh_axis_px(b,469)==45);
 int64_t sa=sh_axis_px(a,26000)-sh_axis_px(a,10000),sb=sh_axis_px(b,24375)-sh_axis_px(b,9375);assert(sa==sb);
 assert(sh_axis_px((struct sh_wave_axis){96,12345},100000)!=old_px(78,100000));
 /* Different beat phase remains different, and unrelated100BPM spacing is57.6px. */
 assert(sh_axis_px(b,250)!=sh_axis_px(b,0));assert(sh_axis_px(b,600)==58);
 /* Cue and loop geometry use the same forward/inverse mapping. */
 int32_t cue=123456,loop_in=120000,loop_out=124000;
 assert(sh_axis_px(a,loop_in)<sh_axis_px(a,cue)&&sh_axis_px(a,cue)<sh_axis_px(a,loop_out));
 assert(sh_axis_ms(a,sh_axis_px(a,cue))>=cue-6&&sh_axis_ms(a,sh_axis_px(a,cue))<=cue+6);
 /* Existing preview API and normalized1x entry have identical source sampling. */
 struct xz_shell_column columns[1000];memset(columns,0,sizeof columns);for(int i=0;i<1000;i++)columns[i].low=(uint8_t)(i%256);
 d=(struct xz_shell_deck){.known=XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE),.wave={.columns=columns,.count=1000,.span_ms=10000}};
 struct xz_shell u={0};struct xz_shell_model m={0};struct xz_shell_rect rect={0,0,800,80};
 for(int mode=0;mode<3;mode++){
  u.display.wave_color=mode;memset(before,0,sizeof before);memset(after,0,sizeof after);
  sh_deck_lane((struct xz_canvas){.p=before,.stride=800,.width=800,.height=80},rect,39,&u,&m,&d,0,96);
  sh_deck_lane_axis((struct xz_canvas){.p=after,.stride=800,.width=800,.height=80},rect,39,&u,&m,&d,0,(struct sh_wave_axis){96,10000});
  assert(!memcmp(before,after,sizeof before));
 }
 /* A real source impulse and its marker share the scaled axis (within the
    existing column quantization), including the stem rendering path. */
 memset(columns,0,sizeof columns);columns[50].low=columns[50].high=255;
 for(int stem=0;stem<2;stem++){
  u.display.wave_color=stem?XZ_SHELL_WAVE_STEMS:XZ_SHELL_WAVE_3BAND;
  d.known|=XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE);d.stems=d.wave;
  memset(after,0,sizeof after);
  sh_deck_lane_axis((struct xz_canvas){.p=after,.stride=800,.width=800,.height=80},rect,39,&u,&m,&d,0,(struct sh_wave_axis){96,20000});
  int first=-1,last=-1;for(int x=0;x<800;x++)for(int y=0;y<80;y++)if(after[y*800+x]){if(first<0)first=x;last=x;break;}
  assert(first>=23&&first<=25&&last<=26);
 }
 /* Slow positive tempo with sparse columns must not quantize columns/pixel
    up to1/65536 and draw a completely different source region. */
 d.wave.span_ms=600000;d.stems=d.wave;u.display.wave_color=XZ_SHELL_WAVE_3BAND;
 memset(after,0,sizeof after);struct sh_wave_axis slow={96,1};
 int64_t marker=sh_axis_px(slow,30000);
 sh_deck_lane_axis((struct xz_canvas){.p=after,.stride=800,.width=800,.height=80},rect,39,&u,&m,&d,marker-400,slow);
 assert(after[40*800+400]!=0||after[40*800+401]!=0);
 puts("PASS fixed source-time axis at every tempo; rational sampler bounds, cue/loop/seek and source sampling");return 0;
}
