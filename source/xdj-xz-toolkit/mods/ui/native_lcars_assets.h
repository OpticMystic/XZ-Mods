#ifndef XZ_NATIVE_LCARS_ASSETS_H
#define XZ_NATIVE_LCARS_ASSETS_H
#include "native_main_theme.h"
#include <stdio.h>

/* Exact 1.26 inventory. These assets replace both artwork and its alpha mask.
   Unclassified images continue through the conservative original-mask path. */
static inline int xz_lcars_asset_owned(unsigned id,int w,int h){
 if(id==639)return w==16&&h==35;
 if(id==815)return w==8&&h==25;
 if(id==727||id==728)return w==8&&h==8;
 if(id==729||id==730||id==731)return w==8&&h==6;
 if(id==732||id==733||id==851||id==852)return w==16&&h==13;
 if(id==849||id==850)return w==16&&h==11;
 if(id==764)return w==16&&h==26;
 if(id>=765&&id<=774)return w==16&&h==18;
 if(id>=816&&id<=825)return w==16&&h==25;
 if(id==837)return w==16&&h==18;
 if(id==826)return w==16&&h==25;
 if(id==969)return w==120&&h==31;
 if(id==970)return w==120&&h==212;
 if(id>=996&&id<=1025)return w==104&&h==22;
 if(id>=1044&&id<=1071)return w==88&&h==27;
 if(id>=734&&id<=753)return w==16&&h==26;
 if(id>=754&&id<=763)return w==16&&h==26;
 if(id>=775&&id<=794)return w==16&&h==23;
 if(id>=795&&id<=814)return w==16&&h==18;
 if(id>=827&&id<=836)return w==16&&h==18;
 if(id>=640&&id<=647)return w==64&&h==14;
 if(id>=972&&id<=995)return w==120&&h==41;
 if(id>=879&&id<=882)return w==64&&h==14;
 if(id>=883&&id<=885)return w==80&&h==45;
 if(id>=887&&id<=902)return w==88&&h==10;
 if(id>=904&&id<=908)return (id==904?w==48:w==40)&&h==10&&id!=906;
 if(id==955)return w==40&&h==10;
 if(id>=956&&id<=959)return w==40&&h==13;
 if(id>=838&&id<=848)return w==24&&h==35;
 if(((id>=651&&id<=667)||(id>=689&&id<=705))&&(id&1))return w==8&&h==138;
 if(id>=1026&&id<=1028)return w==120&&h==41;
 return 0;
}
static inline void xz_lcars_bitmap_mask(unsigned char descriptor[68]){
 descriptor[24]=1;descriptor[25]=descriptor[26]=descriptor[27]=0;
 descriptor[28]=255;descriptor[29]=0;descriptor[30]=255;
}
static inline int xz_lcars_mask_allowed(unsigned image_flags,int key_state,uint16_t key){
 (void)image_flags;(void)key;return key_state>=0;
}
static inline uint16_t xz_lcars_title_pixel(uint16_t pixel,uint32_t background,int keyed,uint16_t key){
 if(keyed&&pixel==key)return pixel;
 uint32_t rgb=xz_theme_rgb888(pixel);unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255,coverage=r>g?r:g;if(b>coverage)coverage=b;
 uint16_t result=xz_theme_rgb565(xz_theme_mix(background,0,coverage*256/255));
 return keyed&&result==key?(uint16_t)(result^1):result;
}
static inline uint16_t xz_lcars_deck_tint(uint16_t pixel,int deck){
 if(deck!=1)return pixel;
 uint32_t rgb=xz_theme_rgb888(pixel);unsigned r=rgb>>16,g=(rgb>>8)&255,b=rgb&255;
 if(r>g&&g>r/4&&g<r*3/4&&b<r/4)return xz_theme_rgb565(xz_theme_mix(0,XZ_LCARS_NATIVE_VIOLET,r*256/255));
 return pixel;
}
static inline void xz_lcars_label(struct xz_theme_surface s,const char *label,int face,int scale,uint32_t fg,uint32_t bg,int bold){
 const struct xz_font_glyph *font=xz_font_glyphs[face];int width=0,top=99,bottom=-99;
 for(const char *c=label;*c;c++){const struct xz_font_glyph *g=&font[(unsigned char)*c-32];width+=g->advance*scale;if(g->y<top)top=g->y;if(g->y+g->height>bottom)bottom=g->y+g->height;}
 if(width>s.width-2&&face){xz_lcars_label(s,label,0,1,fg,bg,bold);return;}
 int x=(s.width-width)/2,y=(s.height-(bottom-top)*scale)/2-top*scale;
 xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},bg);
 for(const char *c=label;*c;c++){
  const struct xz_font_glyph *g=&font[(unsigned char)*c-32];
  for(unsigned row=0;row<g->height;row++)for(unsigned col=0;col<g->width;col++){
   unsigned alpha=xz_font_coverage[g->offset+row*g->width+col];
   if(alpha)xz_theme_blend(s,(struct xz_theme_rect){x+(g->x+(int)col)*scale,y+(g->y+(int)row)*scale,scale+bold,scale},fg,alpha==255?256:alpha);
  }
  x+=g->advance*scale;
 }
}
static inline void xz_lcars_digit(struct xz_theme_surface s,char character,uint32_t color){
 const struct xz_font_glyph *g=&xz_font_glyphs[1][(unsigned char)character-32];
 int h=s.height-4,w=g->height?g->width*h/g->height:1;if(w>(s.width==16?12:s.width-3))w=s.width==16?12:s.width-3;
 int left=(s.width-w)/2,top=(s.height-h)/2;
 xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},0xff00ff);
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  unsigned row=(unsigned)y*g->height/(unsigned)h,col=(unsigned)x*g->width/(unsigned)w;
  unsigned a=xz_font_coverage[g->offset+row*g->width+col];
  /* A half-pixel weight increase keeps numerals solid on the 800x480 panel. */
  if(col){unsigned edge=xz_font_coverage[g->offset+row*g->width+col-1]/2;if(edge>a)a=edge;}
  if(a)s.pixels[(size_t)(top+y)*s.stride+(size_t)(left+x)]=xz_theme_rgb565(xz_theme_mix(0,color,a==255?256:a));
 }
}
static inline void xz_lcars_asset_placement(unsigned id,int width,int height,int x,int y,int *out_x,int *out_y){
 *out_x=x;*out_y=y;
 if(width==128&&height==265&&(id==969||id==970))*out_x=8;

}
static inline void xz_lcars_sidebar(struct xz_theme_surface s){
 for(int deck=0;deck<2;deck++){
  int top=deck?160:17,bottom=deck?295:153;uint32_t color=deck?XZ_LCARS_NATIVE_VIOLET:XZ_LCARS_NATIVE_AMBER;
  xz_main_round(s,0,top,120,bottom-top,18,color);
  xz_main_round(s,13,top+35,111,bottom-top-41,10,0);
  xz_theme_fill(s,(struct xz_theme_rect){0,top+68,13,3},0);
  xz_theme_fill(s,(struct xz_theme_rect){0,deck?273:130,120,16},0);
 }
}
static inline int xz_lcars_source_row(int y){return (y>=58&&y<120)||(y>=201&&y<263);}
static inline int xz_lcars_asset_draw(unsigned id,int theme,struct xz_theme_surface s){
 if(theme!=XZ_THEME_LCARS||!xz_lcars_asset_owned(id,s.width,s.height))return 0;
 uint32_t amber=XZ_LCARS_NATIVE_AMBER,violet=XZ_LCARS_NATIVE_VIOLET;char text[24];
 if(((id>=651&&id<=667)||(id>=689&&id<=705))&&(id&1)){
  /* This is only a redundant 1 px track-color edge, not a cue or status icon. */
  xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},0xff00ff);return 1;
 }
 if(id>=1026&&id<=1028){
  /* The paired DECK asset owns the complete capsule, avoiding two borders. */
  xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},0);return 1;
 }
 if(id>=972&&id<=995){
  unsigned deck=(id-972)/6;uint32_t color=deck&1?violet:amber;unsigned variant=(id-972)%6;
  if(variant==0||variant==2||variant==4)color=xz_theme_mix(0,color,208);
  snprintf(text,sizeof(text),"DECK %u",deck+1);
  xz_lcars_label(s,text,1,1,0,color,1);
  for(int y=0;y<16;y++)for(int x=0;x<s.width;x++)
   if(!xz_theme_round_cov((struct xz_theme_rect){0,0,s.width,s.height},16,x,y))s.pixels[y*s.stride+x]=0;
  /* A black return beneath the label joins the source/key readouts. */
  xz_theme_fill(s,(struct xz_theme_rect){13,s.height-6,s.width-13,6},0);
 }else if(id==969){
  xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},0);
  struct xz_theme_surface label=s;label.pixels+=s.stride*9;label.height=label.clip_h=22;
  xz_lcars_label(label,"BEAT FX",1,1,0,violet,1);
 }else if(id==970){
  xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},violet);
  xz_main_round(s,7,0,106,s.height-7,12,0);
  /* The orange return links the FX rail back to the performance header. */
  xz_theme_fill(s,(struct xz_theme_rect){113,s.height-38,7,3},0);
  xz_theme_fill(s,(struct xz_theme_rect){113,s.height-35,7,28},amber);
  struct xz_theme_surface line=s;line.pixels+=s.stride*38+7;line.width=line.clip_w=106;line.height=line.clip_h=12;
  xz_lcars_label(line,"CHANNEL",0,1,0xddd5ff,0,0);
  line=s;line.pixels+=s.stride*79+7;line.width=line.clip_w=106;line.height=line.clip_h=12;
  xz_lcars_label(line,"PARAMETER",0,1,0xddd5ff,0,0);
 }else if(id>=996&&id<=1025){
  static const char *const names[]={"1","2","3","4","AUX","CF.1","CF.2","CF.A","CF.B","MASTER","MIC","MIC 1","MIC 2","MIC 1&2","SAMPLER"};
  xz_lcars_label(s,names[(id-996)/2],1,1,0,(id&1)?xz_theme_mix(0,violet,180):violet,1);
 }else if(id>=1044&&id<=1071){
  static const char *const names[]={"DELAY","ECHO","FLANGER","SPIRAL","PITCH","REVERB","ROLL","TRANS","PINGPONG","FILTER","PHASER","SLIP ROLL","VINYL BRAKE","HELIX"};
  xz_lcars_label(s,names[(id-1044)/2],1,1,(id&1)?violet:amber,0,1);
 }else if(id==639||id==815||id==727||id==728||id==729||id==730||id==731||id==732||id==733||id==764||id==826||id==837||id==849||id==850||id==851||id==852){
  const char *mark=id==639||id==815?":":id==732||id==851||id==852?"+":id==733||id==764||id==826||id==837?"-":id==849||id==850?"%":".";
  if(id>=727&&id<=731){xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},0xff00ff);xz_theme_fill(s,(struct xz_theme_rect){(s.width-2)/2,(s.height-2)/2,2,2},amber);}
  else xz_lcars_label(s,mark,id==639?1:0,id==639?2:1,amber,0,0);
 }else if(id>=734&&id<=836){
  unsigned number=id<=753?(id-734)/2:id<=763?id-754:id<=774?id-765:id<=794?(id-775)%10:id<=814?(id-795)%10:id<=825?id-816:id-827;
  text[0]=(char)('0'+number);text[1]=0;xz_lcars_digit(s,text[0],amber);
 }else if(id>=640&&id<=647){snprintf(text,sizeof(text),"DECK %u",(id-640)/2+1);xz_lcars_label(s,text,0,1,0xdedee8,0,0);
 }else if(id>=838&&id<=848){text[0]=id==848?'-':(char)('0'+id-838);text[1]=0;xz_lcars_digit(s,text[0],amber);
 }else if(id>=879&&id<=882){xz_lcars_label(s,id<881?"HOT CUE":"AUTO CUE",0,1,(id&1)?amber:0x806020,0,0);
 }else if(id>=883&&id<=885){
  xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},0xff00ff);
  s.width-=4;s.clip_w=s.width;xz_theme_fill(s,(struct xz_theme_rect){0,0,s.width,s.height},0);
  struct xz_theme_surface label=s;label.pixels+=s.stride*33;label.height=12;label.clip_h=12;
  xz_lcars_label(label,id==883?"BPM":"MASTER",0,1,id==883?amber:0,id==883?0:amber,0);
 }else if(id>=887&&id<=902){
  static const char *const values[8]={"","4","2","1","1/16","1/2","1/4","1/8"};
  if((id-887)%8)snprintf(text,sizeof(text),"QUANTIZE %s",values[(id-887)%8]);else snprintf(text,sizeof(text),"QUANTIZE");xz_lcars_label(s,text,0,1,id>=895?amber:0x806020,0,0);
 }else if(id>=956&&id<=959){static const char *const modes[]={"+/-10","+/-16","+/-6","WIDE"};xz_lcars_label(s,modes[id-956],0,1,amber,0,0);
 }else{const char *label=id==904?"REMAIN":id==905?"SINGLE":id==907?"TEMPO":id==908?"TRACK":"BPM";xz_lcars_label(s,label,0,1,0xdedee8,0,0);}
 return 1;
}
#endif
