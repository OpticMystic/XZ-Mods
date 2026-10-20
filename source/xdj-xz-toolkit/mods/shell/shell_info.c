/* Track INFO screen, after the flagship player's track detail page: deck tabs,
   large artwork with rating, colour and source beside it, the title over a grid
   of captioned values, and the compact deck strip at the bottom. Artist, album,
   genre, key and BPM are information-jump links when the runtime can open
   BROWSE at them. */
#include "shell_info.h"
#include "shell_info_chrome.h"

enum {
 TAB_X=104, TAB_W=104, TAB_GAP=6,
 LEFT_X=20, ART_Y=54, ART_SIDE=188, LEFT_W=200,
 RATING_Y=248, COLOR_Y=300, SOURCE_Y=344, DATE_Y=388, LEFT_ROW_H=44,
 STAR_W=40, STAR_R=13,
 RIGHT_X=244, RIGHT_W=XZ_SHELL_WIDTH-8-RIGHT_X,
 TITLE_Y=48, TITLE_H=54, ROW_H=46, ARTIST_Y=TITLE_Y+TITLE_H, ALBUM_Y=ARTIST_Y+ROW_H,
 STAT_Y=ALBUM_Y+ROW_H+6, STAT_H=62, STAT_GAP=8, STAT_W=(RIGHT_W-3*STAT_GAP)/4,
 GENRE_Y=STAT_Y+STAT_H+6, COMMENT_Y=GENRE_Y+ROW_H, FILE_Y=COMMENT_Y+ROW_H,
 TAB_HIGHLIGHT=3, TAB_LINKED=4, TABS_MAX=3+XZ_SHELL_INFO_LINKED
};
static const struct { const char *name; uint32_t rgb; } color_tags[XZ_SHELL_COLOR_COUNT]={
 {"NO COLOR",0},{"PINK",0xff64c8},{"RED",0xff3c3c},{"ORANGE",0xff8c1a},{"YELLOW",0xffe020},
 {"GREEN",0x28e028},{"AQUA",0x20c8ff},{"BLUE",0x3c64ff},{"PURPLE",0x9b3cff}
};

struct xz_shell_rect xz_shell_info_rect(enum xz_shell_info_region r, int i) {
 switch(r){
 case XZ_SHELL_INFO_R_TAB:
  if(i<0||i>=TABS_MAX)break;
  return (struct xz_shell_rect){TAB_X+i*(TAB_W+TAB_GAP),0,TAB_W,SI_HEAD_H};
 case XZ_SHELL_INFO_R_CLOSE:return si_close_rect();
 case XZ_SHELL_INFO_R_ARTWORK:return (struct xz_shell_rect){LEFT_X,ART_Y,ART_SIDE,ART_SIDE};
 case XZ_SHELL_INFO_R_TITLE:return (struct xz_shell_rect){RIGHT_X,TITLE_Y,RIGHT_W,TITLE_H};
 case XZ_SHELL_INFO_R_STAR:
  if(i<0||i>=XZ_SHELL_RATING_MAX)break;
  return (struct xz_shell_rect){LEFT_X-4+i*STAR_W,RATING_Y+8,STAR_W,LEFT_ROW_H};
 case XZ_SHELL_INFO_R_STRIP:
  if(i<0||i>=XZ_SHELL_INFO_DECKS)break;
  return si_strip_rect(i);
 case XZ_SHELL_INFO_R_VALUE:
  switch(i){
  case XZ_SHELL_I_ARTIST:return (struct xz_shell_rect){RIGHT_X,ARTIST_Y,RIGHT_W,ROW_H};
  case XZ_SHELL_I_ALBUM:return (struct xz_shell_rect){RIGHT_X,ALBUM_Y,RIGHT_W,ROW_H};
  case XZ_SHELL_I_KEY:return (struct xz_shell_rect){RIGHT_X,STAT_Y,STAT_W,STAT_H};
  case XZ_SHELL_I_BPM:return (struct xz_shell_rect){RIGHT_X+(STAT_W+STAT_GAP),STAT_Y,STAT_W,STAT_H};
  case XZ_SHELL_I_DURATION:return (struct xz_shell_rect){RIGHT_X+2*(STAT_W+STAT_GAP),STAT_Y,STAT_W,STAT_H};
  case XZ_SHELL_I_BITRATE:return (struct xz_shell_rect){RIGHT_X+3*(STAT_W+STAT_GAP),STAT_Y,RIGHT_W-3*(STAT_W+STAT_GAP),STAT_H};
  case XZ_SHELL_I_GENRE:return (struct xz_shell_rect){RIGHT_X,GENRE_Y,RIGHT_W/2-4,ROW_H};
  case XZ_SHELL_I_LABEL:return (struct xz_shell_rect){RIGHT_X+RIGHT_W/2+4,GENRE_Y,RIGHT_W-RIGHT_W/2-4,ROW_H};
  case XZ_SHELL_I_COMMENT:return (struct xz_shell_rect){RIGHT_X,COMMENT_Y,RIGHT_W,ROW_H};
  case XZ_SHELL_I_FILE_NAME:return (struct xz_shell_rect){RIGHT_X,FILE_Y,RIGHT_W,ROW_H};
  case XZ_SHELL_I_RATING:return (struct xz_shell_rect){LEFT_X-4,RATING_Y,LEFT_W+8,LEFT_ROW_H+8};
  case XZ_SHELL_I_COLOR:return (struct xz_shell_rect){LEFT_X-4,COLOR_Y,LEFT_W+8,LEFT_ROW_H};
  case XZ_SHELL_I_SOURCE:return (struct xz_shell_rect){LEFT_X-4,SOURCE_Y,LEFT_W+8,LEFT_ROW_H};
  case XZ_SHELL_I_DATE_ADDED:return (struct xz_shell_rect){LEFT_X-4,DATE_Y,LEFT_W+8,LEFT_ROW_H};
  case XZ_SHELL_I_TITLE:return xz_shell_info_rect(XZ_SHELL_INFO_R_TITLE,0);
  case XZ_SHELL_I_ARTWORK:return xz_shell_info_rect(XZ_SHELL_INFO_R_ARTWORK,0);
  default:break;
  }
  break;
 }
 return (struct xz_shell_rect){0,0,0,0};
}

/* ---- what is shown ---- */
static int has(const struct xz_shell_info_track *t, enum xz_shell_info_field f) { return (t->known>>f)&1; }
static const char *text_of(const struct xz_shell_info_track *t, enum xz_shell_info_field f, const char *s) { return has(t,f)?s:NULL; }
/* 1..2 = DECK 1..2, TAB_HIGHLIGHT = the highlighted list track, TAB_LINKED + i = linked player i. */
static int linked_count(const struct xz_shell_info_model *im) { return sh_max(0,sh_min(im->linked_count,XZ_SHELL_INFO_LINKED)); }
static int shown_tab(const struct xz_shell *u, const struct xz_shell_info_model *im) {
 int f=si_view(u)->focus;
 if(f==TAB_HIGHLIGHT&&im->has_highlight)return TAB_HIGHLIGHT;
 if(f>=TAB_LINKED&&f<TAB_LINKED+linked_count(im))return f;
 if(f==1||f==2)return f;
 if(im->has_highlight)return TAB_HIGHLIGHT;
 return u->deck>=0&&u->deck<XZ_SHELL_INFO_DECKS?u->deck+1:1;
}
static const struct xz_shell_info_track *track_of(const struct xz_shell_info_model *im, int tab) {
 return tab>=TAB_LINKED?&im->linked[tab-TAB_LINKED].track:tab==TAB_HIGHLIGHT?&im->highlight:&im->deck[tab-1];
}
static int tab_deck(int tab) { return tab>=TAB_HIGHLIGHT?-1:tab-1; }
/* The tabs left to right: the decks, LIST while a list is open, then the linked players. */
static int tab_order(const struct xz_shell_info_model *im, int out[TABS_MAX]) {
 int n=0;
 out[n++]=1;out[n++]=2;
 if(im->has_highlight)out[n++]=TAB_HIGHLIGHT;
 for(int i=0;i<linked_count(im);i++)out[n++]=TAB_LINKED+i;
 return n;
}
/* A linked player's track is read only here: nothing on it takes the encoder. */
static int info_focusable(const struct xz_shell_model *m, const struct xz_shell_info_track *t, int state, enum xz_shell_info_field *list, int tab);
/* 1 loaded, 0 not loaded, -1 unknown. A deck tab falls back to the deck model. */
static int loaded_state(const struct xz_shell_model *m, const struct xz_shell_info_track *t, int deck) {
 if(has(t,XZ_SHELL_I_LOADED))return t->loaded!=0;
 if(deck>=0&&sh_has(&m->deck[deck],XZ_SHELL_F_LOADED))return m->deck[deck].loaded!=0;
 return -1;
}
static int is_link(const struct xz_shell_model *m, const struct xz_shell_info_track *t, enum xz_shell_info_field f) {
 if(!(m->capabilities&XZ_SHELL_CAP_BROWSE)||!has(t,f)||!((t->links>>f)&1))return 0;
 switch(f){
 case XZ_SHELL_I_ARTIST:return t->artist!=NULL;
 case XZ_SHELL_I_ALBUM:return t->album!=NULL;
 case XZ_SHELL_I_GENRE:return t->genre!=NULL;
 case XZ_SHELL_I_KEY:return t->key!=NULL;
 case XZ_SHELL_I_BPM:return 1;
 default:return 0;
 }
}
static int rating_editable(const struct xz_shell_info_track *t) {
 return t->rating_writable&&has(t,XZ_SHELL_I_RATING)&&has(t,XZ_SHELL_I_TRACK_ID);
}
/* Focus order follows reading order down the page. */
static const enum xz_shell_info_field focus_order[]={
 XZ_SHELL_I_ARTIST,XZ_SHELL_I_ALBUM,XZ_SHELL_I_KEY,XZ_SHELL_I_BPM,XZ_SHELL_I_GENRE,XZ_SHELL_I_RATING
};
enum { FOCUS_MAX=sizeof focus_order/sizeof *focus_order };
static int focusable(const struct xz_shell_model *m, const struct xz_shell_info_track *t, int state, enum xz_shell_info_field out[FOCUS_MAX]) {
 int n=0;
 if(state==0)return 0;
 for(int i=0;i<FOCUS_MAX;i++){
  enum xz_shell_info_field f=focus_order[i];
  if(f==XZ_SHELL_I_RATING?rating_editable(t):is_link(m,t,f))out[n++]=f;
 }
 return n;
}
/* The focused field, or -1. */
static int focused(const struct xz_shell *u, const enum xz_shell_info_field list[FOCUS_MAX], int count) {
 int cursor=si_view(u)->cursor;
 return cursor>=1&&cursor<=count?(int)list[cursor-1]:-1;
}

/* ---- drawing ---- */
/* Five-point star with 4x4 supersampled edges. */
static int in_star(float px, float py, const float vx[10], const float vy[10]) {
 int inside=0;
 for(int i=0,j=9;i<10;j=i++)
  if((vy[i]>py)!=(vy[j]>py)&&px<(vx[j]-vx[i])*(py-vy[i])/(vy[j]-vy[i])+vx[i])inside=!inside;
 return inside;
}
static void star(struct xz_canvas c, int cx, int cy, int r, uint32_t color) {
 float vx[10],vy[10];
 for(int i=0;i<10;i++){
  float radius=(float)r*(i&1?0.45f:1.0f);
  vx[i]=(float)cx+0.5f+radius*(float)xz_cos1024(i*36-90)/1024.0f;
  vy[i]=(float)cy+0.5f+radius*(float)xz_sin1024(i*36-90)/1024.0f;
 }
 for(int y=cy-r-1;y<=cy+r+1;y++)for(int x=cx-r-1;x<=cx+r+1;x++){
  int hits=0;
  for(int sy=0;sy<4;sy++)for(int sx=0;sx<4;sx++)hits+=in_star((float)x+(sx+0.5f)/4.0f,(float)y+(sy+0.5f)/4.0f,vx,vy);
  if(hits)xz_plot(c,x,y,color,hits*255/16);
 }
}
static void hollow_star(struct xz_canvas c, int cx, int cy, int r, uint32_t color, uint32_t bg) {
 star(c,cx,cy,r,color);
 star(c,cx,cy,r-2,bg);
}
static void chevron(struct xz_canvas c, int x, int cy, uint32_t color) {
 for(int i=0;i<5;i++)xz_rect(c,x+i,cy-4+i,1,9-2*i,color);
}
static void artwork_placeholder(struct xz_canvas c, struct xz_shell_rect r) {
 int cx=r.x+r.w/2,cy=r.y+r.h/2;
 xz_rect(c,r.x,r.y,r.w,r.h,SH_ROW_ALT);
 xz_disc(c,cx,cy,r.w*3/8,SH_RAIL);
 for(int ring=r.w*3/8-8;ring>r.w/8;ring-=9)xz_ring(c,cx,cy,ring,1,0x464646);
 xz_disc(c,cx,cy,r.w/8,SH_ROW_ALT);
 xz_disc(c,cx,cy,3,SH_RAIL);
}
static void artwork(struct xz_canvas c, const struct xz_shell_info_track *t, struct xz_shell_rect r) {
 if(!has(t,XZ_SHELL_I_ARTWORK)||!t->artwork||t->artwork_side<=0){artwork_placeholder(c,r);return;}
 for(int y=0;y<r.h;y++)for(int x=0;x<r.w;x++){
  uint16_t p=t->artwork[(size_t)(y*t->artwork_side/r.h)*(size_t)t->artwork_side+(size_t)(x*t->artwork_side/r.w)];
  xz_rect(c,r.x+x,r.y+y,1,1,xz_theme_rgb888(p));
 }
}
/* A captioned row: caption top-left, value under it. Links carry a blue rule and a chevron. */
static void value_row(struct xz_canvas c, struct xz_shell_rect r, const char *caption, const char *value, int known, int link, int focus) {
 if(focus)xz_rect(c,r.x,r.y+1,r.w,r.h-2,SH_BLUE);
 xz_rect(c,r.x,r.y+r.h-1,r.w,1,SH_ROW_ALT);
 sh_text(c,r.x+8,r.y+6,caption,SH_SMALL,focus?SH_WHITE:SH_DIM);
 int width=r.w-16-(link?14:0);
 if(!known){sh_text(c,r.x+8,r.y+20,"--",SH_TEXT,SH_DIM);return;}
 sh_text_fit(c,r.x+8,r.y+20,width,value,SH_TEXT,SH_WHITE);
 if(link){
  int w=sh_min(width,sh_text_width(c,value,SH_TEXT));
  if(!focus)xz_rect(c,r.x+8,r.y+r.h-6,w,1,SH_BLUE);
  chevron(c,r.x+8+w+6,r.y+27,focus?SH_WHITE:SH_BLUE);
 }
}
static void stat_box(struct xz_canvas c, struct xz_shell_rect r, const char *caption, int link, int focus) {
 if(focus)xz_rect(c,r.x,r.y,r.w,r.h,SH_BLUE);
 else xz_border(c,r.x,r.y,r.w,r.h,link?SH_BLUE:SH_RAIL);
 sh_text(c,r.x+7,r.y+6,caption,SH_SMALL,focus?SH_WHITE:SH_DIM);
 if(link)chevron(c,r.x+r.w-12,r.y+10,SH_WHITE);
}
static void render_stats(const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_info_track *t, int focus) {
 char text[24],frac[8];
 struct xz_shell_rect r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_KEY);
 int link=is_link(m,t,XZ_SHELL_I_KEY);
 stat_box(c,r,"KEY",link,focus==XZ_SHELL_I_KEY);
 const char *key=text_of(t,XZ_SHELL_I_KEY,t->key);
 int base=r.y+r.h-10-sh_num_cap(SH_NUM_BPM);   /* numerals sit on this line */
 if(key){
  char cut[16];snprintf(cut,sizeof cut,"%s",key);
  while(cut[0]&&sh_text_width(c,cut,SH_TEXT)>r.w-14)cut[strlen(cut)-1]=0;
  sh_text_right(c,r.x+r.w-8,base+sh_num_cap(SH_NUM_BPM)-SH_CAP_TEXT,cut,SH_TEXT,SH_WHITE);
 }else sh_text_right(c,r.x+r.w-8,base+sh_num_cap(SH_NUM_BPM)-SH_CAP_TEXT,"--",SH_TEXT,SH_DIM);

 r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_BPM);
 stat_box(c,r,"BPM",is_link(m,t,XZ_SHELL_I_BPM),focus==XZ_SHELL_I_BPM);
 if(has(t,XZ_SHELL_I_BPM)){
  int32_t b=sh_max(0,sh_min(t->bpm_x100,99999));
  snprintf(text,sizeof text,"%d",(int)(b/100));snprintf(frac,sizeof frac,".%02d",(int)(b%100));
  int fw=sh_num_width(SH_NUM_MID,frac);
  sh_num_right(c,r.x+r.w-8-fw,base,text,SH_NUM_BPM,SH_WHITE);
  sh_num_right(c,r.x+r.w-8,base+sh_num_cap(SH_NUM_BPM)-sh_num_cap(SH_NUM_MID),frac,SH_NUM_MID,SH_WHITE);
 }else sh_num_right(c,r.x+r.w-8,base,"---",SH_NUM_BPM,SH_DIM);

 r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_DURATION);
 stat_box(c,r,"TIME",0,0);
 if(has(t,XZ_SHELL_I_DURATION)){
  int32_t sec=sh_max(0,t->duration_ms)/1000,min=sec/60;
  if(min>99){min=99;sec=99*60+59;}
  snprintf(text,sizeof text,"%02d:%02d",(int)min,(int)(sec%60));
  sh_num_right(c,r.x+r.w-8,base,text,SH_NUM_BPM,SH_WHITE);
 }else sh_num_right(c,r.x+r.w-8,base,"--:--",SH_NUM_BPM,SH_DIM);

 r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_BITRATE);
 stat_box(c,r,"BITRATE",0,0);
 int unit=sh_text_width(c,"kbps",SH_SMALL);
 if(has(t,XZ_SHELL_I_BITRATE)){
  snprintf(text,sizeof text,"%d",(int)sh_max(0,sh_min(t->bitrate_kbps,99999)));
  sh_num_right(c,r.x+r.w-8-unit-3,base,text,SH_NUM_BPM,SH_WHITE);
 }else sh_num_right(c,r.x+r.w-8-unit-3,base,"---",SH_NUM_BPM,SH_DIM);
 sh_text_right(c,r.x+r.w-8,base+sh_num_cap(SH_NUM_BPM)-SH_CAP_SMALL,"kbps",SH_SMALL,SH_DIM);
}
static void render_left(struct xz_canvas c, const struct xz_shell_info_track *t, int focus) {
 /* rating */
 struct xz_shell_rect r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_RATING);
 int editable=rating_editable(t),on=focus==XZ_SHELL_I_RATING;
 uint32_t bg=on?SH_BLUE:SH_BLACK;
 if(on)xz_rect(c,r.x,r.y,r.w,r.h,SH_BLUE);
 sh_text(c,LEFT_X,RATING_Y+4,editable?"RATING  (EDIT)":"RATING",SH_SMALL,on?SH_WHITE:SH_DIM);
 int rated=has(t,XZ_SHELL_I_RATING);
 int stars=rated?sh_max(0,sh_min(t->rating,XZ_SHELL_RATING_MAX)):0;
 for(int i=0;i<XZ_SHELL_RATING_MAX;i++){
  struct xz_shell_rect s=xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,i);
  int cx=s.x+s.w/2,cy=s.y+s.h/2-2;
  if(!rated)hollow_star(c,cx,cy,STAR_R,SH_RAIL,bg);
  else if(i<stars)star(c,cx,cy,STAR_R,SH_WHITE);
  else hollow_star(c,cx,cy,STAR_R,editable||on?SH_DIM:0x6e6e6e,bg);
 }
 /* colour */
 r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_COLOR);
 xz_rect(c,r.x,r.y,r.w,1,SH_ROW_ALT);
 sh_text(c,LEFT_X,r.y+6,"COLOR",SH_SMALL,SH_DIM);
 if(has(t,XZ_SHELL_I_COLOR)&&(int)t->color>=0&&(int)t->color<XZ_SHELL_COLOR_COUNT){
  if(t->color==XZ_SHELL_COLOR_NONE){xz_border(c,LEFT_X,r.y+22,14,14,SH_DIM);sh_text(c,LEFT_X+22,r.y+20,color_tags[0].name,SH_TEXT,SH_DIM);}
  else{xz_rect(c,LEFT_X,r.y+22,14,14,color_tags[t->color].rgb);sh_text(c,LEFT_X+22,r.y+20,color_tags[t->color].name,SH_TEXT,SH_WHITE);}
 }else sh_text(c,LEFT_X,r.y+20,"--",SH_TEXT,SH_DIM);
 /* source */
 r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_SOURCE);
 xz_rect(c,r.x,r.y,r.w,1,SH_ROW_ALT);
 sh_text(c,LEFT_X,r.y+6,"SOURCE",SH_SMALL,SH_DIM);
 if(has(t,XZ_SHELL_I_SOURCE)&&t->source>=XZ_SHELL_SRC_USB1&&t->source<=XZ_SHELL_SRC_COMPUTER){
  sh_usb_icon(c,LEFT_X+1,r.y+22,SH_WHITE);
  sh_text(c,LEFT_X+18,r.y+20,si_source_name(t->source),SH_TEXT,SH_WHITE);
 }else sh_text(c,LEFT_X,r.y+20,"--",SH_TEXT,SH_DIM);
 /* date added */
 r=xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,XZ_SHELL_I_DATE_ADDED);
 xz_rect(c,r.x,r.y,r.w,1,SH_ROW_ALT);
 sh_text(c,LEFT_X,r.y+6,"DATE ADDED",SH_SMALL,SH_DIM);
 const char *date=text_of(t,XZ_SHELL_I_DATE_ADDED,t->date_added);
 if(date)sh_text_fit(c,LEFT_X,r.y+20,LEFT_W,date,SH_TEXT,SH_WHITE);
 else sh_text(c,LEFT_X,r.y+20,"--",SH_TEXT,SH_DIM);
}
static void render_tabs(const struct xz_shell *u, const struct xz_shell_info_model *im, struct xz_canvas c, int tab) {
 sh_info_icon(c,22,SI_HEAD_H/2,SH_WHITE);
 sh_text(c,36,15,"INFO",SH_TEXT,SH_WHITE);
 int order[TABS_MAX],tabs=tab_order(im,order);
 for(int i=0;i<tabs;i++){
  struct xz_shell_rect r=xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,i);
  int id=order[i],on=tab==id;
  xz_rect(c,r.x,7,r.w,30,on?SH_BLUE:SH_RAIL);
  char label[16];
  if(id<=2)snprintf(label,sizeof label,"DECK %d",id);
  else if(id==TAB_HIGHLIGHT)snprintf(label,sizeof label,"LIST");
  else snprintf(label,sizeof label,"PLAYER %d",sh_max(0,sh_min(im->linked[id-TAB_LINKED].player,99)));
  sh_text_center(c,r.x+r.w/2,15,label,SH_TEXT,on?SH_WHITE:SH_DIM);
  if(id<=2&&u->deck==id-1&&!on)xz_rect(c,r.x,34,r.w,3,SH_BLUE);  /* the active deck */
 }
}
static void info_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 const struct xz_shell_info_model *im=m->info;
 xz_rect(c,0,0,c.width,c.height,SH_BLACK);
 si_header(c,"");
 si_strip(u,m,c);
 if(!im){sh_text_center(c,c.width/2,220,"No track information.",SH_TEXT,SH_DIM);return;}
 int tab=shown_tab(u,im);
 const struct xz_shell_info_track *t=track_of(im,tab);
 render_tabs(u,im,c,tab);
 int state=loaded_state(m,t,tab_deck(tab));
 if(state==0){
  char hint[64];
  if(tab>=TAB_LINKED)snprintf(hint,sizeof hint,"PLAYER %d has no track loaded.",sh_max(0,sh_min(im->linked[tab-TAB_LINKED].player,99)));
  else if(tab==TAB_HIGHLIGHT)snprintf(hint,sizeof hint,"No track is highlighted in the list.");
  else snprintf(hint,sizeof hint,"Load a track on DECK %d to see its details.",tab);
  sh_text_center(c,c.width/2,(SI_HEAD_H+SI_STRIP_Y)/2-16,"Not Loaded.",SH_TEXT,SH_RED);
  sh_text_center(c,c.width/2,(SI_HEAD_H+SI_STRIP_Y)/2+12,hint,SH_SMALL,SH_DIM);
  return;
 }
 enum xz_shell_info_field list[FOCUS_MAX];
 int count=info_focusable(m,t,state,list,tab),focus=focused(u,list,count);
 artwork(c,t,xz_shell_info_rect(XZ_SHELL_INFO_R_ARTWORK,0));
 render_left(c,t,focus);
 /* title, up to two lines */
 struct xz_shell_rect r=xz_shell_info_rect(XZ_SHELL_INFO_R_TITLE,0);
 const char *title=text_of(t,XZ_SHELL_I_TITLE,t->title);
 if(title){
  /* The atlas has no face above 20 px, so the title is set heavier: drawn twice, one pixel apart. */
  int one=sh_text_width(c,title,SH_TEXT)<=r.w-17,ty=one?r.y+(r.h-SH_CAP_TEXT)/2-2:r.y+4;
  for(int dx=0;dx<2;dx++)si_text_wrap2(c,r.x+8+dx,ty,r.w-17,25,title,SH_TEXT,SH_WHITE);
 }
 else sh_text(c,r.x+8,r.y+8,"--",SH_TEXT,SH_DIM);
 static const struct { enum xz_shell_info_field field; const char *caption; } rows[]={
  {XZ_SHELL_I_ARTIST,"ARTIST"},{XZ_SHELL_I_ALBUM,"ALBUM"},{XZ_SHELL_I_GENRE,"GENRE"},{XZ_SHELL_I_LABEL,"LABEL"},
  {XZ_SHELL_I_COMMENT,"COMMENT"},{XZ_SHELL_I_FILE_NAME,"FILE"}
 };
 for(size_t i=0;i<sizeof rows/sizeof *rows;i++){
  enum xz_shell_info_field f=rows[i].field;
  const char *s=f==XZ_SHELL_I_ARTIST?t->artist:f==XZ_SHELL_I_ALBUM?t->album:f==XZ_SHELL_I_GENRE?t->genre:
   f==XZ_SHELL_I_LABEL?t->label:f==XZ_SHELL_I_COMMENT?t->comment:t->file_name;
  s=text_of(t,f,s);
  value_row(c,xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,f),rows[i].caption,s,s!=NULL,is_link(m,t,f),focus==(int)f);
 }
 render_stats(m,c,t,focus);
}

/* ---- input ---- */
static size_t confirm(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_info_track *t, int deck,
                      enum xz_shell_info_field f, int star_value, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(f==XZ_SHELL_I_RATING){
  int rating=sh_max(0,sh_min(t->rating,XZ_SHELL_RATING_MAX));
  int value=star_value>=0?(star_value==rating?star_value-1:star_value):(rating+1)%(XZ_SHELL_RATING_MAX+1);
  return si_emit(out,0,(struct xz_shell_action){(enum xz_shell_action_kind)XZ_SHELL_ACT_SET_RATING,deck,value,t->track_id});
 }
 int32_t value=f==XZ_SHELL_I_ARTIST?t->artist_id:f==XZ_SHELL_I_ALBUM?t->album_id:f==XZ_SHELL_I_GENRE?t->genre_id:
  f==XZ_SHELL_I_KEY?t->key_id:t->bpm_x100;
 size_t n=si_emit(out,0,(struct xz_shell_action){(enum xz_shell_action_kind)XZ_SHELL_ACT_INFO_JUMP,deck,value,(int)f});
 return si_key(u,m,XZ_SHELL_KEY_BROWSE,out,n);
}
static size_t info_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_info_model *im=m->info;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 if(e->kind==XZ_SHELL_TOUCH_DOWN&&sh_inside(si_close_rect(),e->x,e->y))return sh_back(u,m,out);
 if(!im)return 0;
 int tab=shown_tab(u,im);
 const struct xz_shell_info_track *t=track_of(im,tab);
 enum xz_shell_info_field list[FOCUS_MAX];
 int count=info_focusable(m,t,loaded_state(m,t,tab_deck(tab)),list,tab);
 if(v->cursor<0||v->cursor>count)v->cursor=0;
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:{
  int order[TABS_MAX],tabs=tab_order(im,order);
  for(int i=0;i<tabs;i++)if(sh_inside(xz_shell_info_rect(XZ_SHELL_INFO_R_TAB,i),e->x,e->y)){v->focus=order[i];v->cursor=0;return 0;}
  for(int d=0;d<XZ_SHELL_INFO_DECKS;d++)if(sh_inside(si_strip_rect(d),e->x,e->y)){v->focus=d+1;v->cursor=0;return 0;}
  for(int i=0;i<count;i++){
   if(!sh_inside(xz_shell_info_rect(XZ_SHELL_INFO_R_VALUE,list[i]),e->x,e->y))continue;
   if(v->cursor!=i+1){v->cursor=i+1;return 0;}      /* first touch highlights */
   int star_value=-1;
   if(list[i]==XZ_SHELL_I_RATING){
    for(int s=0;s<XZ_SHELL_RATING_MAX;s++)if(sh_inside(xz_shell_info_rect(XZ_SHELL_INFO_R_STAR,s),e->x,e->y))star_value=s+1;
    if(star_value<0)return 0;
   }
   return confirm(u,m,t,tab_deck(tab),list[i],star_value,out);
  }
  return 0;
 }
 case XZ_SHELL_ENCODER_TURN:{
  if(!count)return 0;
  int cursor=v->cursor+e->delta;
  v->cursor=cursor<1?1:cursor>count?count:cursor;
  return 0;
 }
 case XZ_SHELL_ENCODER_PUSH:
  if(v->cursor<1)return 0;
  return confirm(u,m,t,tab_deck(tab),list[v->cursor-1],-1,out);
 default:return 0;
 }
}
static int info_focusable(const struct xz_shell_model *m, const struct xz_shell_info_track *t, int state, enum xz_shell_info_field *list, int tab) {
 return tab>=TAB_LINKED?0:focusable(m,t,state,list);
}
const struct sh_screen xz_shell_info_screen={info_render,info_input};
