#ifndef XZ_NATIVE_FLAGSHIP_ASSETS_H
#define XZ_NATIVE_FLAGSHIP_ASSETS_H
#include "native_lcars_assets.h"

/* Flagship restyle of the same exact 1.26 sprite inventory LCARS owns. Owned
   sprites replace artwork and alpha mask together; transparent pixels are
   0xf81f, keyed through the explicit mask descriptor (xz_lcars_bitmap_mask). */
enum xz_flagship_sprite_kind {
 XZ_FS_EDGE,XZ_FS_DECK_TAG,XZ_FS_DECK_LABEL,XZ_FS_DECK_CAPTION,XZ_FS_FX_HEADER,XZ_FS_FX_PANEL,
 XZ_FS_SOURCE,XZ_FS_FX_NAME,XZ_FS_DIGIT,XZ_FS_GLYPH,XZ_FS_MARK,XZ_FS_DOT,XZ_FS_CUE_MODE,
 XZ_FS_BPM_BOX,XZ_FS_QUANTIZE,XZ_FS_RANGE,XZ_FS_WORD
};
/* arg: DIGIT = ids per numeral; GLYPH/MARK = character; WORD = word index. */
struct xz_flagship_sprite {uint16_t first,last,width,height;uint8_t kind,arg;};
static inline const struct xz_flagship_sprite *xz_flagship_sprite(unsigned id,int w,int h){
 static const struct xz_flagship_sprite table[]={
  {651,667,8,138,XZ_FS_EDGE,0},{689,705,8,138,XZ_FS_EDGE,0},
  {1026,1028,120,41,XZ_FS_DECK_TAG,0},{972,995,120,41,XZ_FS_DECK_LABEL,0},{640,647,64,14,XZ_FS_DECK_CAPTION,0},
  {969,969,120,31,XZ_FS_FX_HEADER,0},{970,970,120,212,XZ_FS_FX_PANEL,0},
  {996,1025,104,22,XZ_FS_SOURCE,0},{1044,1071,88,27,XZ_FS_FX_NAME,0},
  {734,753,16,26,XZ_FS_DIGIT,2},{754,763,16,26,XZ_FS_DIGIT,1},{765,774,16,18,XZ_FS_DIGIT,1},
  {775,794,16,23,XZ_FS_DIGIT,1},{795,814,16,18,XZ_FS_DIGIT,1},{816,825,16,25,XZ_FS_DIGIT,1},
  {827,836,16,18,XZ_FS_DIGIT,1},{838,847,24,35,XZ_FS_DIGIT,1},{848,848,24,35,XZ_FS_GLYPH,'-'},
  {639,639,16,35,XZ_FS_MARK,':'},{815,815,8,25,XZ_FS_MARK,':'},
  {732,732,16,13,XZ_FS_MARK,'+'},{851,852,16,13,XZ_FS_MARK,'+'},{733,733,16,13,XZ_FS_MARK,'-'},
  {764,764,16,26,XZ_FS_MARK,'-'},{826,826,16,25,XZ_FS_MARK,'-'},{837,837,16,18,XZ_FS_MARK,'-'},
  {849,850,16,11,XZ_FS_MARK,'%'},{727,728,8,8,XZ_FS_DOT,0},{729,731,8,6,XZ_FS_DOT,0},
  {879,882,64,14,XZ_FS_CUE_MODE,0},{883,885,80,45,XZ_FS_BPM_BOX,0},{887,902,88,10,XZ_FS_QUANTIZE,0},
  {956,959,40,13,XZ_FS_RANGE,0},{904,904,48,10,XZ_FS_WORD,0},{905,905,40,10,XZ_FS_WORD,1},
  {907,907,40,10,XZ_FS_WORD,2},{908,908,40,10,XZ_FS_WORD,3},{955,955,40,10,XZ_FS_WORD,4},
 };
 for(size_t i=0;i<sizeof(table)/sizeof(table[0]);i++){
  const struct xz_flagship_sprite *e=&table[i];
  if(id>=e->first&&id<=e->last&&w==e->width&&h==e->height&&(e->kind!=XZ_FS_EDGE||(id&1)))return e;
 }
 return NULL;
}
static inline int xz_flagship_asset_owned(unsigned id,int w,int h){return xz_flagship_sprite(id,w,h)!=NULL;}
/* The destination still holds the LUT-mapped stock sprite. Its brightest
   pixel carries the native state: white, grey when inactive, warm on master. */
static inline uint32_t xz_flagship_sprite_ink(struct xz_theme_surface s){
 unsigned best=0;uint32_t color=0;
 for(int y=0;y<s.height;y++)for(int x=0;x<s.width;x++){
  uint16_t p=s.pixels[(size_t)y*s.stride+(size_t)x];if(p==0xf81f)continue;
  uint32_t c=xz_theme_rgb888(p);unsigned r=c>>16,g=(c>>8)&255,b=c&255,m=r>g?r:g;if(b>m)m=b;
  if(m>best){best=m;color=c;}
 }
 if(best<40)return 0xffffff;
 unsigned r=color>>16,g=(color>>8)&255,b=color&255,lo=r<g?r:g;if(b<lo)lo=b;
 if(best-lo>=64)return r>=g&&r>=b?XZ_FLAGSHIP_RED:g>=b?XZ_FLAGSHIP_GREEN:XZ_FLAGSHIP_BLUE;
 return best>=176?0xffffff:XZ_FLAGSHIP_DIM;
}
/* Antialiased text over a transparent ground: edges darken toward black, then
   the untouched black ground becomes the key so the panel behind shows. */
static inline void xz_flagship_keyed_label(struct xz_theme_surface s,const char *text,int face,int scale,uint32_t ink){
 xz_lcars_label(s,text,face,scale,ink,0,0);
 for(int y=0;y<s.height;y++)for(int x=0;x<s.width;x++){uint16_t *p=&s.pixels[(size_t)y*s.stride+(size_t)x];if(!*p)*p=0xf81f;}
}
static inline void xz_flagship_boxed_label(struct xz_theme_surface s,const char *text,uint32_t ink,uint32_t edge,uint32_t fill){
 xz_lcars_label(s,text,0,1,ink,fill,0);
 xz_theme_edge(s,(struct xz_theme_rect){0,0,s.width,s.height},0,edge);
}
static inline struct xz_theme_surface xz_flagship_band(struct xz_theme_surface s,int x,int y,int w,int h){
 s.pixels+=(size_t)y*s.stride+(size_t)x;s.width=s.clip_w=w;s.height=s.clip_h=h;s.clip_x=s.clip_y=0;return s;
}
static inline int xz_flagship_asset_draw(unsigned id,int theme,struct xz_theme_surface s){
 const struct xz_flagship_sprite *e=theme==XZ_THEME_FLAGSHIP?xz_flagship_sprite(id,s.width,s.height):NULL;
 if(!e)return 0;
 uint32_t ink=xz_flagship_sprite_ink(s),white=0xffffff;char text[24];
 struct xz_theme_rect whole={0,0,s.width,s.height};
 switch((enum xz_flagship_sprite_kind)e->kind){
 case XZ_FS_EDGE:xz_theme_fill(s,whole,0xff00ff);break;
 case XZ_FS_DECK_TAG:xz_flagship_deck_tag(s,id);break;
 case XZ_FS_DECK_LABEL:
  /* Drawn over the paired 1026..1028 tag, which owns the fill and edge. */
  snprintf(text,sizeof(text),"DECK %u",(id-972)/6+1);xz_flagship_keyed_label(s,text,1,1,white);break;
 case XZ_FS_DECK_CAPTION:snprintf(text,sizeof(text),"DECK %u",(id-640)/2+1);xz_flagship_boxed_label(s,text,white,white,0);break;
 case XZ_FS_FX_HEADER:xz_flagship_fx_header(s);break;
 case XZ_FS_FX_PANEL:
  xz_theme_fill(s,whole,XZ_FLAGSHIP_CHROME);xz_theme_edge(s,whole,0,XZ_FLAGSHIP_RAIL);
  xz_theme_fill(s,(struct xz_theme_rect){6,34,s.width-12,1},XZ_FLAGSHIP_RAIL);
  xz_theme_fill(s,(struct xz_theme_rect){6,75,s.width-12,1},XZ_FLAGSHIP_RAIL);
  xz_lcars_label(xz_flagship_band(s,7,38,106,12),"CH SELECT",0,1,XZ_FLAGSHIP_DIM,XZ_FLAGSHIP_CHROME,0);
  xz_lcars_label(xz_flagship_band(s,7,79,106,12),"PARAMETER",0,1,XZ_FLAGSHIP_DIM,XZ_FLAGSHIP_CHROME,0);
  break;
 case XZ_FS_SOURCE:{
  static const char *const names[]={"1","2","3","4","AUX","CF.1","CF.2","CF.A","CF.B","MASTER","MIC","MIC 1","MIC 2","MIC 1&2","SAMPLER"};
  xz_lcars_label(s,names[(id-996)/2],1,1,ink,XZ_FLAGSHIP_RAIL,0);break;}
 case XZ_FS_FX_NAME:{
  static const char *const names[]={"DELAY","ECHO","FLANGER","SPIRAL","PITCH","REVERB","ROLL","TRANS","PINGPONG","FILTER","PHASER","SLIP ROLL","VINYL BRAKE","HELIX"};
  xz_lcars_label(s,names[(id-1044)/2],1,1,ink,XZ_FLAGSHIP_CHROME,0);break;}
 case XZ_FS_DIGIT:xz_lcars_digit(s,(char)('0'+((id-e->first)/e->arg)%10),ink);break;
 case XZ_FS_GLYPH:xz_lcars_digit(s,(char)e->arg,ink);break;
 case XZ_FS_MARK:text[0]=(char)e->arg;text[1]=0;xz_flagship_keyed_label(s,text,s.height>=35,s.height>=35?2:1,ink);break;
 case XZ_FS_DOT:xz_theme_fill(s,whole,0xff00ff);xz_theme_fill(s,(struct xz_theme_rect){(s.width-2)/2,(s.height-2)/2,2,2},ink);break;
 case XZ_FS_CUE_MODE:{
  int on=id&1;xz_flagship_boxed_label(s,id<881?"A.HOT CUE":"A.CUE",on?white:XZ_FLAGSHIP_DIM,on?white:XZ_FLAGSHIP_RAIL,0);break;}
 case XZ_FS_BPM_BOX:{
  /* The stock box leaves its last 4 columns transparent. */
  xz_theme_fill(s,whole,0xff00ff);
  struct xz_theme_surface box=xz_flagship_band(s,0,0,s.width-4,s.height);int master=id!=883;
  xz_theme_fill(box,(struct xz_theme_rect){0,0,box.width,box.height},0);
  xz_theme_edge(box,(struct xz_theme_rect){0,0,box.width,box.height},0,master?XZ_FLAGSHIP_RED:white);
  xz_lcars_label(xz_flagship_band(box,1,33,box.width-2,11),master?"MASTER":"BPM",0,1,white,master?XZ_FLAGSHIP_RED:0,0);
  break;}
 case XZ_FS_QUANTIZE:{
  static const char *const values[8]={"","4","2","1","1/16","1/2","1/4","1/8"};unsigned v=(id-887)%8;
  snprintf(text,sizeof(text),"QUANTIZE%s%s",v?" ":"",values[v]);
  xz_flagship_keyed_label(s,text,0,1,id>=895?XZ_FLAGSHIP_RED:XZ_FLAGSHIP_DIM);break;}
 case XZ_FS_RANGE:{
  static const char *const modes[]={"\xb1" "10","\xb1" "16","\xb1" "6","WIDE"};
  xz_lcars_label(s,modes[id-956],0,1,white,XZ_FLAGSHIP_RED,0);break;}
 case XZ_FS_WORD:{
  static const char *const words[]={"REMAIN","SINGLE","TEMPO","TRACK","BPM"};
  xz_flagship_keyed_label(s,words[e->arg],0,1,ink);break;}
 }
 return 1;
}
#endif
