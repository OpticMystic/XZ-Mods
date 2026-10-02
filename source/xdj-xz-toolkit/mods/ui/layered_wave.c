/* SPDX-License-Identifier: MIT */
#include "layered_wave.h"
static uint16_t rgb(unsigned r,unsigned g,unsigned b){return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
/* CDJ-3000 3-band colours: blue lows outside, amber mids, white highs at the core. */
#define LOW_BODY rgb(32,83,217)
#define MID_BODY rgb(242,170,60)
#define HIGH_CORE rgb(255,255,255)
uint64_t xz_layered_path_hash(const char *path){
 uint64_t h=UINT64_C(14695981039346656037);
 for(;*path;path++){unsigned char c=(unsigned char)*path;if(c>='A'&&c<='Z')c+=32;h^=c;h*=UINT64_C(1099511628211);}
 return h;
}
int xz_layered_pixel(const struct xz_layered_data *data,const struct xz_layered_col *col,unsigned d){
 if(!data||!col||!col->valid)return -1;
 /* Stacked, outermost first: h[0] is the total, h[1] the inner two layers, h[2] the core. */
 const uint8_t *h=col->h;
 int layer=d<h[2]?2:d<h[1]?1:d<h[0]?0:-1;
 if(layer<0)return -1;
 if(data->kind==XZ_LAYERED_STEMS){
  /* Each stem dims gently (up to 16%) toward its outer edge, so every boundary has a soft
   * brightness step. Similar-brightness neighbours (harmonics beside vocals) then separate
   * as clearly as drums beside harmonics, without an outline. */
  uint16_t c=data->colors[layer];
  unsigned below=layer==2?0:h[layer+1],span=h[layer]>below?h[layer]-below:1;
  unsigned f=256u-41u*(d-below)/span;
  /* Vocals and harmonics are the closest pair in brightness: ease the vocal core a little
   * darker over its last two rows where it meets harmonics (35%, then 50%). */
  if(layer==2&&h[1]>h[2]&&h[2]>=4&&d+2>=h[2])f=d+1==h[2]?128u:166u;
  return (uint16_t)((((c>>11)&31)*f>>8)<<11|(((c>>5)&63)*f>>8)<<5|((c&31)*f>>8));
 }
 return layer==2?HIGH_CORE:layer==1?MID_BODY:LOW_BODY;
}
int xz_layered_heights(const struct xz_layered_data *data,uint32_t first,uint32_t end,unsigned extent,struct xz_layered_col *out){
 out->valid=0;
 if(!data||!data->bands||!data->normalization||!extent||extent>255||first>=data->count||end>data->count||end<first)return 0;
 if(end==first)end=first+1;
 /* Rekordbox's own overall waveform height follows low+mid+high at equal weight, so the
  * bands stack: each layer is as thick as its band. Zoomed out, the loudest sample wins. */
 const uint8_t *b=data->bands+(size_t)first*3;unsigned best=0;
 for(uint32_t i=first;i<end;i++){
  const uint8_t *s=data->bands+(size_t)i*3;unsigned total=s[0]+s[1]+s[2];
  if(total>best){best=total;b=s;}
 }
 /* Bytes are outermost-first for stems (drums, harmonics, vocals) and low/mid/high for 3-band. */
 unsigned core=b[2],inner=b[1]+core,total=b[0]+inner;
 unsigned v[3]={total,inner,core};
 for(unsigned i=0;i<3;i++){unsigned px=v[i]*extent/data->normalization;out->h[i]=(uint8_t)(px>extent?extent:px);}
 out->valid=1;return 1;
}
int xz_layered_column(uint16_t *pixels,size_t stride,unsigned width,unsigned height,
 unsigned x,unsigned center,unsigned extent,const struct xz_layered_data *data,
 uint32_t first,uint32_t end,unsigned flags,unsigned mono,struct xz_layered_col *out){
 if(out)out->valid=0;
 if(!pixels||!data||!data->bands||!data->normalization||data->normalization>1020||
    !width||stride<width||x>=width||center>=height||!extent||extent>center+1||extent>52||
    first>=data->count||end>data->count||end<first||end-first>4096)return 0;
 if(end==first)end=first+1;
 struct xz_layered_col col;
 if(!xz_layered_heights(data,first,end,extent,&col))return 0;
 for(unsigned d=0;d<extent;d++){
  unsigned v=extent-d;uint16_t c=mono?rgb(v,v,v):rgb(0,v,2*v);
  if(flags&4)c=0x3186;if(flags&10)c=0xb300;
  int layer=xz_layered_pixel(data,&col,d);if(layer>=0)c=(uint16_t)layer;
  pixels[(center-d)*stride+x]=c;
 }
 if(out)*out=col;
 return 1;
}
