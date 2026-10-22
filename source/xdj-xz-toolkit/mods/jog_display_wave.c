/* SPDX-License-Identifier: MIT */
#include "jog_display_wave.h"
#include <string.h>

int xz_jd_overview_begin(struct xz_jd_overview *s,uint32_t count,uint32_t normalization){
 if(!s||!count||count>1000000u||normalization>765u)return 0;
 memset(s,0,sizeof *s);s->count=count;s->normalization=normalization?normalization:765u;return 1;
}
static unsigned height(const struct xz_extra_column *c){return (unsigned)c->low+c->mid+c->high;}
int xz_jd_overview_add(struct xz_jd_overview *s,uint32_t first,const struct xz_extra_column *in,uint32_t count){
 if(!s||!in||!count||count>XZ_EXTRA_WAVE_CHUNK_COLUMNS||first!=s->next||first>s->count||count>s->count-first)return 0;
 uint32_t end=first+count;
 while(s->cell<XZ_JD_OVERVIEW_COLUMNS){
  uint32_t lo=(uint32_t)((uint64_t)s->cell*s->count/XZ_JD_OVERVIEW_COLUMNS);
  uint32_t hi=(uint32_t)((uint64_t)(s->cell+1u)*s->count/XZ_JD_OVERVIEW_COLUMNS);
  if(hi<=lo)hi=lo+1u;
  if(lo>=end)break;
  uint32_t begin=lo>first?lo:first,stop=hi<end?hi:end;
  struct xz_extra_column *best=&s->column[s->cell];
  unsigned peak=height(best);
  for(uint32_t i=begin;i<stop;i++){
   unsigned value=height(&in[i-first]);
   if(value>peak){*best=in[i-first];peak=value;}
  }
  if(hi>end)break;
  s->cell++;
 }
 s->next=end;return 1;
}
int xz_jd_overview_complete(const struct xz_jd_overview *s){
 return s&&s->count&&s->next==s->count&&s->cell==XZ_JD_OVERVIEW_COLUMNS;
}
void xz_jd_wave_encode_rgb(const uint8_t input[1800],unsigned header_flag,struct xz_jd_wave *out){
 memset(out,0,sizeof *out);out->payload[0]=header_flag?0x80u:0;
 for(unsigned i=0;i<XZ_JD_OVERVIEW_COLUMNS;i++){
  const uint8_t *p=input+i*6u;unsigned h=p[4]|((unsigned)p[5]<<8);
  uint8_t *col=out->payload+1u+i*14u;
  memcpy(col,p,4);col[4]=(uint8_t)(h&63u);col[5]=(uint8_t)((h>>6)&63u);
  memcpy(col+7,col,7);
 }
}
int xz_jd_overview_encode(const struct xz_jd_overview *s,struct xz_jd_wave *out){
 if(!xz_jd_overview_complete(s)||!out)return 0;
 uint8_t input[1800];
 for(unsigned i=0;i<XZ_JD_OVERVIEW_COLUMNS;i++){
  unsigned total=height(&s->column[i]),h=total*63u/s->normalization;
  if(h>63u)h=63u;
  /* A neutral monochrome overview. Both native colour words are white;
     amplitude follows the same summed-band peak/normalization as the main UI.
     Target palette and physical height calibration remain hardware checks. */
  uint8_t *p=input+i*6u;p[0]=p[1]=p[2]=p[3]=0xff;
  unsigned packed=h|(h<<6);p[4]=(uint8_t)packed;p[5]=(uint8_t)(packed>>8);
 }
 xz_jd_wave_encode_rgb(input,0,out);return 1;
}
int xz_jd_wave_packet(const struct xz_jd_wave *s,unsigned side,unsigned index,uint8_t out[64]){
 if(!s||!out||side<1||side>2||index>=XZ_JD_RGB_WAVE_PACKETS)return 0;
 out[0]=(uint8_t)(side<<4);out[1]=0x2c;out[2]=(uint8_t)(index+1u);out[3]=0;
 out[4]=XZ_JD_RGB_WAVE_PACKETS;out[5]=0;memcpy(out+6,s->payload+index*58u,58);return 1;
}
