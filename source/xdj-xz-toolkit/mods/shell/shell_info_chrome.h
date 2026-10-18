#ifndef XZ_SHELL_INFO_CHROME_H
#define XZ_SHELL_INFO_CHROME_H
/* Chrome shared by the INFO and SOURCE screens: header bar, close button and
   the compact two-deck status strip the flagship keeps under every non-play
   screen (player, time, mini overview, tempo, BPM). Header-only and static so
   each screen file carries its own copy. */
#include "shell_internal.h"
#include <stdio.h>
#include <string.h>

enum {
 SI_HEAD_H=44, SI_STRIP_Y=432, SI_STRIP_H=XZ_SHELL_HEIGHT-SI_STRIP_Y, SI_HALF=XZ_SHELL_WIDTH/2,
 SI_CLOSE_W=48, SI_TILE=0x313131
};

static inline struct xz_shell_rect si_close_rect(void) { return (struct xz_shell_rect){XZ_SHELL_WIDTH-SI_CLOSE_W,0,SI_CLOSE_W,SI_HEAD_H}; }
static inline struct xz_shell_rect si_strip_rect(int deck) { return (struct xz_shell_rect){deck*SI_HALF,SI_STRIP_Y,SI_HALF,SI_STRIP_H}; }
static inline const struct xz_shell_view *si_view(const struct xz_shell *u) { return &u->stack[u->depth-1]; }
static inline size_t si_emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a) {
 if(n<XZ_SHELL_ACTIONS)out[n++]=a;
 return n;
}
/* Appends the navigation a key produces (sh_key writes from out[0]). */
static inline size_t si_key(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 struct xz_shell_action nav[XZ_SHELL_ACTIONS];
 size_t count=sh_key(u,m,k,-1,nav);
 for(size_t i=0;i<count;i++)n=si_emit(out,n,nav[i]);
 return n;
}
static inline const char *si_source_name(enum xz_shell_source s) {
 switch(s){
 case XZ_SHELL_SRC_USB1:return "USB 1";
 case XZ_SHELL_SRC_USB2:return "USB 2";
 case XZ_SHELL_SRC_LINK:return "LINK";
 case XZ_SHELL_SRC_COMPUTER:return "COMPUTER";
 default:return "NONE";
 }
}
static inline uint32_t si_ink_on(uint32_t fill) { return xz_theme_luma(fill)>150?SH_BLACK:SH_WHITE; }

/* Byte length of the longest prefix of s that fits width, broken after a word
   when the whole string does not fit and a space exists. */
static inline size_t si_utf8_step(const char *s) {
 unsigned char ch=(unsigned char)*s;
 size_t n=ch<0xc2?1:ch<0xe0?2:ch<0xf0?3:4,i;
 for(i=1;i<n;i++)if((s[i]&0xc0)!=0x80)return i;
 return n;
}
static inline size_t si_line(struct xz_canvas c, const char *s, int width, enum sh_face face) {
 char buffer[256];
 size_t used=0,word=0;
 while(s[used]&&used<sizeof buffer-5){
  size_t step=si_utf8_step(s+used);
  memcpy(buffer,s,used+step);buffer[used+step]=0;
  if(sh_text_width(c,buffer,face)>width)break;
  used+=step;
  if(s[used]==' ')word=used;
 }
 return s[used]&&word?word:used;
}
/* Up to two lines; the second is cut at the width. Returns the lines drawn. */
static inline int si_text_wrap2(struct xz_canvas c, int x, int y, int width, int leading, const char *s, enum sh_face face, uint32_t color) {
 char buffer[256];
 if(!s||!*s)return 0;
 size_t n=si_line(c,s,width,face);
 if(n>=sizeof buffer)n=sizeof buffer-1;
 memcpy(buffer,s,n);buffer[n]=0;
 sh_text(c,x,y,buffer,face,color);
 s+=n;
 while(*s==' ')s++;
 if(!*s)return 1;
 sh_text_fit(c,x,y+leading,width,s,face,color);
 return 2;
}

static inline void si_header(struct xz_canvas c, const char *title) {
 xz_rect(c,0,0,c.width,SI_HEAD_H,SH_CHROME);
 xz_rect(c,0,SI_HEAD_H-1,c.width,1,SH_BLACK);
 sh_text(c,16,15,title,SH_TEXT,SH_WHITE);
 struct xz_shell_rect r=si_close_rect();
 xz_rect(c,r.x,6,1,SI_HEAD_H-12,SH_RAIL);
 int cx=r.x+r.w/2,cy=SI_HEAD_H/2;
 xz_line16(c,(cx-8)*16,(cy-8)*16,(cx+8)*16,(cy+8)*16,32,SH_WHITE);
 xz_line16(c,(cx+8)*16,(cy-8)*16,(cx-8)*16,(cy+8)*16,32,SH_WHITE);
}

/* ---- compact deck strip ---- */
static inline void si_band_colors(const struct xz_shell_model *m, uint32_t out[3], int scale[3]) {
 enum xz_shell_wave_color mode=sh_setting(m,XZ_SHELL_SET_WAVE_COLOR)?m->settings.wave_color:XZ_SHELL_WAVE_3BAND;
 scale[0]=scale[1]=scale[2]=256;
 switch(mode){
 case XZ_SHELL_WAVE_RGB:out[0]=0xe03030;out[1]=0x30c040;out[2]=0x4080ff;break;
 case XZ_SHELL_WAVE_BLUE:out[0]=out[1]=SH_WAVE_LOW_DEEP;out[2]=SH_WHITE;scale[2]=128;break;
 default:out[0]=SH_WAVE_LOW_DEEP;out[1]=SH_WAVE_MID;out[2]=SH_WHITE;break;
 }
}
static inline void si_mini_overview(const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, struct xz_shell_rect ov, int timed) {
 int ph=timed?ov.x+(int)((int64_t)sh_max(0,sh_min(d->elapsed_ms,d->total_ms))*(ov.w-1)/d->total_ms):-1;
 xz_rect(c,ov.x,ov.y,ov.w,ov.h,SH_BLACK);
 const struct xz_shell_wave *w=&d->overview;
 if(sh_has(d,XZ_SHELL_F_OVERVIEW)&&w->columns&&w->count){
  static const uint32_t played[3]={0x3c3c3c,0x6e6e6e,0xa0a0a0};
  uint32_t color[3];int scale[3];si_band_colors(m,color,scale);
  for(int x=0;x<ov.w;x++){
   uint32_t from=(uint32_t)((int64_t)x*w->count/ov.w),to=(uint32_t)((int64_t)(x+1)*w->count/ov.w);
   int band[3]={0,0,0};
   if(to<=from)to=from+1;
   for(uint32_t i=from;i<to&&i<w->count;i++){
    band[0]=sh_max(band[0],w->columns[i].low);band[1]=sh_max(band[1],w->columns[i].mid);band[2]=sh_max(band[2],w->columns[i].high);
   }
   for(int b=0;b<3;b++){
    int h=band[b]*scale[b]/256*(ov.h-1)/255;
    if(h>0)xz_rect(c,ov.x+x,ov.y+ov.h-h,1,h,ph>=0&&ov.x+x<ph?played[b]:color[b]);
   }
  }
 }else xz_rect(c,ov.x,ov.y+ov.h-1,ov.w,1,SH_RAIL);
 if(ph>=0)xz_rect(c,ph,ov.y,1,ov.h,SH_PLAYHEAD);
}
static inline void si_strip_deck(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 struct xz_shell_rect s=si_strip_rect(deck);
 int y=s.y+1,right=s.x+s.w-6;
 char text[24],frac[8];
 /* deck badge */
 int bx=s.x+6,bw=40;
 xz_rect(c,bx,y+4,bw,39,u->deck==deck?SH_BLUE:SH_RAIL);
 sh_text_center(c,bx+bw/2,y+8,"DECK",SH_SMALL,SH_WHITE);
 snprintf(text,sizeof text,"%d",deck+1);
 sh_text_center(c,bx+bw/2,y+21,text,SH_TEXT,SH_WHITE);
 int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
 if(loaded_known&&!loaded){sh_text_center(c,(bx+bw+right)/2,y+16,"Not Loaded.",SH_TEXT,SH_RED);return;}
 /* time */
 int tx=bx+bw+8,tm_known=1;
 enum xz_shell_time_mode mode=XZ_SHELL_TIME_REMAIN;
 if(sh_has(d,XZ_SHELL_F_TIME_MODE))mode=d->time_mode;
 else if(sh_setting(m,XZ_SHELL_SET_TIME_MODE))mode=m->settings.time_mode;
 else tm_known=0;
 sh_text(c,tx,y+5,mode==XZ_SHELL_TIME_REMAIN?"REMAIN":"TIME",SH_SMALL,tm_known?SH_WHITE:SH_DIM);
 int have_elapsed=loaded&&sh_has(d,XZ_SHELL_F_ELAPSED),have_total=loaded&&sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0;
 int32_t shown=-1;
 if(mode==XZ_SHELL_TIME_REMAIN&&have_elapsed&&have_total)shown=sh_max(0,d->total_ms-d->elapsed_ms);
 else if(mode==XZ_SHELL_TIME_ELAPSED&&have_elapsed)shown=sh_max(0,d->elapsed_ms);
 if(shown>=0){int32_t sec=shown/1000,min=sec/60;if(min>99){min=99;sec=99*60+59;}snprintf(text,sizeof text,"%02d:%02d",(int)min,(int)(sec%60));}
 else snprintf(text,sizeof text,"--:--");
 sh_num(c,tx,y+19,text,SH_NUM_MID,shown>=0?SH_WHITE:SH_DIM);
 /* BPM box at the right edge, tempo left of it, overview filling the rest */
 int boxw=76,boxx=right-boxw;
 int master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 xz_border(c,boxx,y+4,boxw,39,SH_WHITE);
 sh_text(c,boxx+4,y+7,"BPM",SH_SMALL,SH_WHITE);
 if(master)sh_text_right(c,boxx+boxw-4,y+7,"MASTER",SH_SMALL,SH_RED);
 if(loaded&&sh_has(d,XZ_SHELL_F_BPM)){
  int32_t b=sh_max(0,sh_min(d->bpm_x100,99999));
  snprintf(text,sizeof text,"%d",(int)(b/100));snprintf(frac,sizeof frac,".%d",(int)((b/10)%10));
  int fw=sh_num_width(SH_NUM_MID,frac);
  sh_num_right(c,boxx+boxw-4-fw,y+20,text,SH_NUM_MID,master?SH_RED:SH_WHITE);
  sh_num_right(c,boxx+boxw-4,y+20,frac,SH_NUM_MID,master?SH_RED:SH_WHITE);
 }else sh_num_right(c,boxx+boxw-4,y+20,"---.-",SH_NUM_MID,SH_DIM);
 int tempo_right=boxx-8;
 sh_text_right(c,tempo_right,y+5,"TEMPO",SH_SMALL,SH_DIM);
 if(sh_has(d,XZ_SHELL_F_TEMPO)){
  int32_t t=sh_max(-99999,sh_min(d->tempo_x100,99999)),a=t<0?-t:t;
  snprintf(text,sizeof text,"%s%d.%02d",t>0?"+":t<0?"-":"",(int)(a/100),(int)(a%100));
 }else snprintf(text,sizeof text,"--.--");
 int pw=sh_text_width(c,"%",SH_SMALL);
 sh_text_right(c,tempo_right-pw-1,y+21,text,SH_TEXT,sh_has(d,XZ_SHELL_F_TEMPO)?SH_WHITE:SH_DIM);
 sh_text_right(c,tempo_right,y+26,"%",SH_SMALL,SH_DIM);
 int ox=tx+sh_num_width(SH_NUM_MID,"00:00")+10,ow=tempo_right-62-ox;
 int timed=have_elapsed&&have_total;
 if(ow>8)si_mini_overview(m,c,d,(struct xz_shell_rect){ox,y+8,ow,33},timed);
}
static inline void si_strip(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,0,SI_STRIP_Y,c.width,SI_STRIP_H,SH_CHROME);
 xz_rect(c,0,SI_STRIP_Y,c.width,1,SH_BLACK);
 xz_rect(c,SI_HALF-1,SI_STRIP_Y,2,SI_STRIP_H,SH_BLACK);
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++)si_strip_deck(u,m,c,deck);
}
#endif
