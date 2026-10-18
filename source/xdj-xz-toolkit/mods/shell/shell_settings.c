/* SHORTCUT (2x2 quadrants) and UTILITY (rail / items / values) at 800x480,
   after the flagship player's settings screens (the 2026-10-18 flagship UI
   study in docs/research, sections 3.4-3.6 and 6.2). Both keep a compact
   two-deck status strip at the bottom, as every flagship non-play screen does.

   BACK on UTILITY steps focus one pane left through the screen's back hook;
   from the rail it falls through to the navigation table, which closes UTILITY. */
#include "shell_internal.h"
#include "shell_settings.h"
#include <stdio.h>
#include <string.h>

enum {
 W=XZ_SHELL_WIDTH, H=XZ_SHELL_HEIGHT, TITLE_H=40, BODY_Y=42,
 ROW_H=XZ_SHELL_SETTINGS_ROW_H, ROWS=XZ_SHELL_SETTINGS_ROWS, RAIL_H=XZ_SHELL_SETTINGS_RAIL_H,
 STATUS_Y=BODY_Y+ROWS*ROW_H+2, STATUS_H=H-STATUS_Y,
 RAIL_W=76, LIST_X=RAIL_W+2, LIST_W=420, VALUE_X=LIST_X+LIST_W+2, VALUE_W=W-VALUE_X,
 QUAD_W=399, QUAD_H=190, QUAD_TITLE=18, SLOT_H=43, SLOTS=4, CHIP_PAD=12, CHIP_GAP=3, CHIP_Y=15, CHIP_H=25,
 MAX_ITEMS=128, DEEP_BLUE=XZ_FLAGSHIP_DEEP_BLUE, RAIL_SELECTED=XZ_FLAGSHIP_RAIL_SELECTED, GREY_TEXT=0x8c8c8c,
 READ_ONLY_GREY=0x6e6e6e
};
static const struct xz_canvas measure={NULL,W,W,H,XZ_THEME_FLAGSHIP};

/* ---- model access ---- */
struct table { const struct xz_shell_setting_item *items; int count; };
static struct table table_of(const struct xz_shell_model *m) {
 const struct xz_shell_settings_model *s=m->settings_screen;
 if(!s||!s->count||!s->items)return (struct table){NULL,0};
 return (struct table){s->items,s->count>MAX_ITEMS?MAX_ITEMS:(int)s->count};
}
static int valid_category(const struct xz_shell_setting_item *it) { return (unsigned)it->category<XZ_SHELL_SETTING_CAT_COUNT; }
static int find_id(struct table t, int id) {
 for(int i=0;i<t.count;i++)if(t.items[i].id==id)return i;
 return -1;
}
static int choice_count(const struct xz_shell_setting_item *it) { return it->choices&&it->choice_count>0?sh_min(it->choice_count,255):0; }
/* A list of values the DJ picks from; a single choice is information, not a control. */
static int selectable(const struct xz_shell_setting_item *it) { return choice_count(it)>=2; }
static int current_of(const struct xz_shell_setting_item *it) {
 return it->known&&it->current>=0&&it->current<choice_count(it)?it->current:-1;
}
static int settable(const struct xz_shell_model *m, const struct xz_shell_setting_item *it) {
 return it->writable&&(m->capabilities&XZ_SHELL_CAP_SET_SETTING);
}
static const char *choice_text(const struct xz_shell_setting_item *it, int index) {
 if(index<0||index>=choice_count(it)||!it->choices[index])return "---";
 return it->choices[index];
}
static const char *value_text(const struct xz_shell_setting_item *it) {
 if(!it->known)return "UNKNOWN";
 return choice_text(it,current_of(it));
}
static const char *label_of(const struct xz_shell_setting_item *it) { return it->label?it->label:"---"; }

/* ---- text ---- */
static size_t utf8_step(const char *s) {
 unsigned char ch=(unsigned char)*s;
 size_t n=ch<0xc2?1:ch<0xe0?2:ch<0xf0?3:4,i;
 for(i=1;i<n;i++)if((s[i]&0xc0)!=0x80)return i;
 return n;
}
/* Longest whole-character prefix of s that fits width, copied to out. */
static int fit(const char *s, enum sh_face face, int width, char *out, size_t cap) {
 size_t used=0;int w=0;
 out[0]=0;
 if(!s)return 0;
 while(*s&&used+5<cap){
  size_t step=utf8_step(s);
  memcpy(out+used,s,step);out[used+step]=0;
  int next=sh_text_width(measure,out,face);
  if(next>width){out[used]=0;break;}
  used+=step;s+=step;w=next;
 }
 return w;
}
static void text_fit(struct xz_canvas c, int x, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char buf[160];fit(s,face,width,buf,sizeof buf);sh_text(c,x,y,buf,face,ink);
}
static void text_fit_right(struct xz_canvas c, int right, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char buf[160];int w=fit(s,face,width,buf,sizeof buf);sh_text(c,right-w,y,buf,face,ink);
}
static void text_fit_center(struct xz_canvas c, int cx, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char buf[160];int w=fit(s,face,width,buf,sizeof buf);sh_text(c,cx-w/2,y,buf,face,ink);
}
static void h_triangle(struct xz_canvas c, int x, int cy, int size, int right, uint32_t ink) {
 for(int col=0;col<size;col++){
  int half=right?size-1-col:col;
  xz_rect(c,x+col,cy-half,1,half*2+1,ink);
 }
}
static void lock_icon(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_ring(c,x+4,y+4,4,1,ink);xz_rect(c,x,y+4,9,7,ink);xz_rect(c,x+4,y+6,1,3,SH_CHROME);
}

/* ---- shared chrome ---- */
static size_t emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a) {
 if(n<XZ_SHELL_ACTIONS)out[n++]=a;
 return n;
}
struct xz_shell_rect xz_shell_settings_rect(enum xz_shell_settings_region r) {
 switch(r){
 case XZ_SHELL_SR_BACK_BUTTON:return (struct xz_shell_rect){0,0,52,TITLE_H+4};
 case XZ_SHELL_SR_RAIL:return (struct xz_shell_rect){0,BODY_Y,RAIL_W,ROWS*ROW_H};
 case XZ_SHELL_SR_ITEMS:return (struct xz_shell_rect){LIST_X,BODY_Y,LIST_W,ROWS*ROW_H};
 case XZ_SHELL_SR_VALUES:return (struct xz_shell_rect){VALUE_X,BODY_Y,VALUE_W,ROWS*ROW_H};
 case XZ_SHELL_SR_STATUS:return (struct xz_shell_rect){0,STATUS_Y,W,STATUS_H};
 }
 return (struct xz_shell_rect){0,0,0,0};
}
static void render_title(struct xz_canvas c, const char *title) {
 xz_rect(c,0,0,W,TITLE_H,SH_CHROME);
 xz_border(c,8,5,30,30,0x646464);
 xz_line16(c,15*16,20*16,31*16,20*16,32,SH_WHITE);
 xz_line16(c,15*16,20*16,22*16,13*16,32,SH_WHITE);
 xz_line16(c,15*16,20*16,22*16,27*16,32,SH_WHITE);
 sh_text(c,52,13,title,SH_TEXT,SH_WHITE);
}

/* Compact two-deck strip: deck tag, time, mini overview, tempo, BPM. */
static void fmt_time(char *out, size_t cap, int32_t ms) {
 if(ms<0)ms=0;
 int32_t s=ms/1000,m=s/60;if(m>99){m=99;s=99*60+59;}
 snprintf(out,cap,"%02d:%02d",(int)m,(int)(s%60));
}
static enum xz_shell_time_mode time_mode_of(const struct xz_shell_model *m, const struct xz_shell_deck *d, int *known) {
 if(sh_has(d,XZ_SHELL_F_TIME_MODE)){*known=1;return d->time_mode;}
 if(sh_setting(m,XZ_SHELL_SET_TIME_MODE)){*known=1;return m->settings.time_mode;}
 *known=0;return XZ_SHELL_TIME_REMAIN;
}
static void render_mini_overview(const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_deck *d, struct xz_shell_rect r) {
 int loaded=sh_has(d,XZ_SHELL_F_LOADED)&&d->loaded;
 int timed=loaded&&sh_has(d,XZ_SHELL_F_ELAPSED)&&sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0;
 int ph=-1;
 xz_rect(c,r.x,r.y,r.w,r.h,SH_BLACK);
 if(timed){int32_t e=d->elapsed_ms<0?0:d->elapsed_ms>d->total_ms?d->total_ms:d->elapsed_ms;ph=r.x+(int)((int64_t)e*(r.w-1)/d->total_ms);}
 if(loaded&&sh_has(d,XZ_SHELL_F_OVERVIEW)&&d->overview.columns&&d->overview.count){
  const struct xz_shell_wave *w=&d->overview;
  enum xz_shell_wave_color mode=sh_setting(m,XZ_SHELL_SET_WAVE_COLOR)?m->settings.wave_color:XZ_SHELL_WAVE_3BAND;
  uint32_t color[3]={SH_WAVE_LOW_DEEP,SH_WAVE_MID,SH_WHITE};
  if(mode==XZ_SHELL_WAVE_RGB){color[0]=0xe03030;color[1]=0x30c040;color[2]=0x4080ff;}
  else if(mode==XZ_SHELL_WAVE_BLUE){color[1]=SH_WAVE_LOW_DEEP;}
  static const uint32_t played[3]={0x3c3c3c,0x6e6e6e,0xa0a0a0};
  for(int x=0;x<r.w;x++){
   uint32_t from=(uint32_t)((int64_t)x*w->count/r.w),to=(uint32_t)((int64_t)(x+1)*w->count/r.w);
   if(to<=from)to=from+1;
   int band[3]={0,0,0};
   for(uint32_t i=from;i<to&&i<w->count;i++){
    band[0]=sh_max(band[0],w->columns[i].low);band[1]=sh_max(band[1],w->columns[i].mid);band[2]=sh_max(band[2],w->columns[i].high);
   }
   if(mode==XZ_SHELL_WAVE_BLUE)band[2]/=2;
   for(int b=0;b<3;b++){
    int h=band[b]*(r.h-1)/255;
    if(h>0)xz_rect(c,r.x+x,r.y+r.h-1-h,1,h,ph>=0&&r.x+x<ph?played[b]:color[b]);
   }
  }
 }
 xz_rect(c,r.x,r.y+r.h-1,r.w,1,SH_RAIL);
 if(ph>=0)xz_rect(c,ph,r.y,1,r.h,SH_PLAYHEAD);
}
static void render_status_deck(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int x0=deck*(W/2)+(deck?1:0),w=W/2-1,y=STATUS_Y;
 struct xz_canvas s=xz_sub(c,x0,y,w,STATUS_H);
 char text[32];
 int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
 xz_rect(s,0,0,w,STATUS_H,SH_CHROME);
 /* deck box */
 xz_rect(s,8,7,40,STATUS_H-14,u->deck==deck?SH_BLUE:SH_RAIL);
 sh_text_center(s,28,11,"DECK",SH_SMALL,SH_WHITE);
 snprintf(text,sizeof text,"%d",deck+1);
 sh_text_center(s,28,25,text,SH_TEXT,SH_WHITE);
 /* time */
 int tm_known;enum xz_shell_time_mode mode=time_mode_of(m,d,&tm_known);
 sh_text(s,58,8,tm_known&&mode==XZ_SHELL_TIME_ELAPSED?"TIME":"REMAIN",SH_SMALL,tm_known?SH_WHITE:SH_DIM);
 int32_t shown=-1;
 if(loaded_known&&!loaded)shown=0;
 else if(loaded&&mode==XZ_SHELL_TIME_REMAIN&&sh_has(d,XZ_SHELL_F_ELAPSED)&&sh_has(d,XZ_SHELL_F_TOTAL))shown=d->total_ms-d->elapsed_ms;
 else if(loaded&&mode==XZ_SHELL_TIME_ELAPSED&&sh_has(d,XZ_SHELL_F_ELAPSED))shown=d->elapsed_ms;
 if(shown>=0)fmt_time(text,sizeof text,shown);else snprintf(text,sizeof text,"--:--");
 sh_num(s,58,24,text,SH_NUM_MID,shown>=0?SH_WHITE:SH_DIM);
 /* title over the mini overview */
 struct xz_shell_rect ov={134,25,120,22};
 if(loaded_known&&!loaded)sh_text(s,ov.x,8,"Not Loaded.",SH_SMALL,SH_RED);
 else if(loaded&&sh_has(d,XZ_SHELL_F_TITLE)&&d->title){sh_note(s,ov.x,6,SH_WHITE);text_fit(s,ov.x+14,8,ov.w-14,d->title,SH_SMALL,SH_WHITE);}
 else sh_text(s,ov.x,8,"---",SH_SMALL,SH_DIM);
 render_mini_overview(m,s,d,ov);
 /* tempo */
 int tx=262,bx=w-66,bw=58;
 if(sh_has(d,XZ_SHELL_F_TEMPO_RANGE)){
  if(d->tempo_range>=100)snprintf(text,sizeof text,"WIDE");else snprintf(text,sizeof text,"\xb1%d",d->tempo_range);
  int rw=sh_text_width(s,text,SH_SMALL)+8;
  xz_rect(s,tx,7,rw,13,SH_RED);sh_text(s,tx+4,9,text,SH_SMALL,SH_WHITE);
 }
 if(sh_has(d,XZ_SHELL_F_TEMPO)){
  int32_t t=d->tempo_x100,a=t<0?-t:t;
  if(a>9999)a=9999;
  snprintf(text,sizeof text,"%s%d.%02d",t>0?"+":t<0?"-":"",(int)(a/100),(int)(a%100));
 }else snprintf(text,sizeof text,"--.--");
 int known_tempo=sh_has(d,XZ_SHELL_F_TEMPO),pw=sh_text_width(s,"%",SH_SMALL);
 char shown_tempo[32];int tw=fit(text,SH_TEXT,bx-6-tx-pw-2,shown_tempo,sizeof shown_tempo);
 sh_text(s,tx,27,shown_tempo,SH_TEXT,known_tempo?SH_WHITE:SH_DIM);
 sh_text(s,tx+tw+2,32,"%",SH_SMALL,known_tempo?SH_WHITE:SH_DIM);
 /* BPM box */
 int master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
 xz_border(s,bx,6,bw,STATUS_H-12,master?SH_RED:SH_WHITE);
 sh_text(s,bx+4,9,master?"MASTER":"BPM",SH_SMALL,master?SH_RED:SH_WHITE);
 if(loaded&&sh_has(d,XZ_SHELL_F_BPM)){
  int32_t b=d->bpm_x100<0?0:d->bpm_x100>99999?99999:d->bpm_x100;
  snprintf(text,sizeof text,"%d.%d",(int)(b/100),(int)((b/10)%10));
  text_fit_right(s,bx+bw-5,27,bw-8,text,SH_TEXT,master?SH_RED:SH_WHITE);
 }else sh_text_right(s,bx+bw-5,27,"---.-",SH_TEXT,SH_DIM);
}
static void render_status(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,0,STATUS_Y-2,W,STATUS_H+2,SH_BLACK);
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++)render_status_deck(u,m,c,deck);
}

/* ================= UTILITY ================= */

/* View state, packed into the shell's view slot:
   cursor = id+1 of the item under the item cursor (0 = first item);
   focus  = pane | value cursor<<4 | touch pane+1<<16 | armed<<20 | dragged<<21;
   scroll = item scroll | value scroll<<12. */
enum pane { PANE_RAIL, PANE_ITEMS, PANE_VALUES };
struct ustate { int pane, item, value, item_scroll, value_scroll, touch_pane, armed, dragged; };
static struct ustate unpack(const struct xz_shell_view *v) {
 return (struct ustate){v->focus&3,v->cursor-1,(v->focus>>4)&0xfff,v->scroll&0xfff,(v->scroll>>12)&0xfff,
  ((v->focus>>16)&7)-1,(v->focus>>20)&1,(v->focus>>21)&1};
}
static void pack(struct xz_shell_view *v, struct ustate s) {
 v->cursor=s.item+1;
 v->focus=(s.pane&3)|((s.value&0xfff)<<4)|(((s.touch_pane+1)&7)<<16)|((s.armed&1)<<20)|((s.dragged&1)<<21);
 v->scroll=(s.item_scroll&0xfff)|((s.value_scroll&0xfff)<<12);
}

/* Derived layout for one model snapshot and one view. */
struct ulayout {
 struct table t;
 int cats[XZ_SHELL_SETTING_CAT_COUNT],n_cats,cat_row;   /* rail rows */
 int list[MAX_ITEMS],n_list,list_row;           /* table indices of the shown category */
 int item;                                      /* table index under the cursor, -1 when empty */
 int value_rows,footer;                         /* value pane */
};
static int category_rows(struct table t, int out[XZ_SHELL_SETTING_CAT_COUNT]) {
 int n=0;
 for(int cat=0;cat<XZ_SHELL_SETTING_CAT_COUNT;cat++)
  for(int i=0;i<t.count;i++)if(valid_category(&t.items[i])&&(int)t.items[i].category==cat){out[n++]=cat;break;}
 return n;
}
static int first_of_category(struct table t, int cat) {
 for(int i=0;i<t.count;i++)if(valid_category(&t.items[i])&&(int)t.items[i].category==cat)return i;
 return -1;
}
static int needs_footer(const struct xz_shell_model *m, const struct xz_shell_setting_item *it) {
 return selectable(it)&&(!settable(m,it)||current_of(it)<0);
}
static struct ulayout ulayout_of(const struct xz_shell_model *m, int item_id) {
 struct ulayout l;memset(&l,0,sizeof l);
 l.t=table_of(m);l.item=-1;
 l.n_cats=category_rows(l.t,l.cats);
 if(!l.n_cats)return l;
 int at=item_id>=0?find_id(l.t,item_id):-1;
 if(at<0||!valid_category(&l.t.items[at]))at=first_of_category(l.t,l.cats[0]);
 l.item=at;
 int cat=(int)l.t.items[at].category;
 for(int r=0;r<l.n_cats;r++)if(l.cats[r]==cat)l.cat_row=r;
 for(int i=0;i<l.t.count;i++)if(valid_category(&l.t.items[i])&&(int)l.t.items[i].category==cat){
  if(i==at)l.list_row=l.n_list;
  l.list[l.n_list++]=i;
 }
 const struct xz_shell_setting_item *it=&l.t.items[at];
 l.footer=needs_footer(m,it);
 l.value_rows=ROWS-l.footer;
 return l;
}
static int clamp(int v, int lo, int hi) { return v<lo?lo:v>hi?hi:v; }
/* Scroll that keeps row visible, moving as little as possible from scroll. */
static int keep_visible(int scroll, int row, int count, int rows) {
 scroll=clamp(scroll,0,sh_max(0,count-rows));
 if(row<scroll)scroll=row;
 if(row>=scroll+rows)scroll=row-rows+1;
 return clamp(scroll,0,sh_max(0,count-rows));
}
static int item_id_at(const struct ulayout *l, int table_index) { return table_index>=0?l->t.items[table_index].id:-1; }
/* Re-derive everything from the stored ids after any change. */
static struct ustate settle(const struct xz_shell_model *m, struct ustate s, struct ulayout *l) {
 *l=ulayout_of(m,s.item);
 s.item=item_id_at(l,l->item);
 if(l->item<0){s.pane=PANE_RAIL;s.item_scroll=s.value_scroll=s.value=0;return s;}
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 s.pane=clamp(s.pane,PANE_RAIL,PANE_VALUES);
 if(s.pane==PANE_VALUES&&!selectable(it))s.pane=PANE_ITEMS;
 s.item_scroll=keep_visible(s.item_scroll,l->list_row,l->n_list,ROWS);
 int n=choice_count(it);
 s.value=clamp(s.value,0,sh_max(0,n-1));
 s.value_scroll=keep_visible(s.value_scroll,s.value,n,l->value_rows);
 return s;
}
static int value_start(const struct xz_shell_setting_item *it) { int c=current_of(it);return c<0?0:c; }

static void scrollbar(struct xz_canvas c, int x, int y, int h, int scroll, int count, int rows) {
 if(count<=rows)return;
 int thumb=sh_max(16,h*rows/count),travel=h-thumb;
 int top=y+(count-rows>0?travel*scroll/(count-rows):0);
 xz_rect(c,x,top,3,thumb,0x8c8c8c);
}
static const char *const cat_lines[XZ_SHELL_SETTING_CAT_COUNT][2]={
 {"DJ","SETTING"},{"DISPLAY","(LCD)"},{"DISPLAY","(INDICATOR)"},{"PRO DJ","LINK"},{"MIXER",NULL},{"SYSTEM",NULL},{"INFO",NULL}
};
static void render_rail(struct xz_canvas c, const struct ulayout *l, struct ustate s) {
 for(int r=0;r<7;r++){
  int y=BODY_Y+r*RAIL_H;
  int selected=r<l->n_cats&&r==l->cat_row&&l->item>=0;
  uint32_t fill=selected?(s.pane==PANE_RAIL?SH_BLUE:RAIL_SELECTED):SH_CHROME;
  xz_rect(c,0,y,RAIL_W,RAIL_H-2,fill);
  if(r>=l->n_cats)continue;
  const char *const *lines=cat_lines[l->cats[r]];
  uint32_t ink=selected?SH_WHITE:SH_DIM;
  if(lines[1]){
   text_fit_center(c,RAIL_W/2,y+RAIL_H/2-13,RAIL_W-4,lines[0],SH_SMALL,ink);
   text_fit_center(c,RAIL_W/2,y+RAIL_H/2+1,RAIL_W-4,lines[1],SH_SMALL,ink);
  }else text_fit_center(c,RAIL_W/2,y+RAIL_H/2-6,RAIL_W-4,lines[0],SH_SMALL,ink);
 }
 xz_rect(c,0,BODY_Y+7*RAIL_H,RAIL_W,ROWS*ROW_H-7*RAIL_H,SH_CHROME);
}
static void render_items(const struct xz_shell_model *m, struct xz_canvas c, const struct ulayout *l, struct ustate s) {
 struct xz_canvas p=xz_sub(c,LIST_X,BODY_Y,LIST_W,ROWS*ROW_H);
 for(int r=0;r<ROWS;r++){
  int y=r*ROW_H,index=s.item_scroll+r;
  int has=index<l->n_list;
  int cursor=has&&l->list[index]==l->item;
  uint32_t fill=cursor?(s.pane==PANE_ITEMS?SH_BLUE:RAIL_SELECTED):SH_CHROME;
  xz_rect(p,0,y,LIST_W,ROW_H-2,fill);
  if(!has)continue;
  const struct xz_shell_setting_item *it=&l->t.items[l->list[index]];
  int ro=selectable(it)&&!settable(m,it);
  const char *value=value_text(it);
  uint32_t value_ink=!it->known||current_of(it)<0?(cursor?SH_WHITE:READ_ONLY_GREY):cursor?SH_WHITE:GREY_TEXT;
  char vbuf[160];int vw=fit(value,SH_TEXT,170,vbuf,sizeof vbuf);
  int right=LIST_W-14;
  sh_text(p,right-vw,y+12,vbuf,SH_TEXT,value_ink);
  if(ro)lock_icon(p,right-vw-17,y+13,cursor?SH_WHITE:READ_ONLY_GREY);
  int name_w=right-vw-(ro?17:0)-16-14;
  text_fit(p,14,y+12,name_w,label_of(it),SH_TEXT,SH_WHITE);
 }
 scrollbar(p,LIST_W-4,2,ROWS*ROW_H-6,s.item_scroll,l->n_list,ROWS);
}
static void render_values(const struct xz_shell_model *m, struct xz_canvas c, const struct ulayout *l, struct ustate s) {
 struct xz_canvas p=xz_sub(c,VALUE_X,BODY_Y,VALUE_W,ROWS*ROW_H);
 const struct xz_shell_setting_item *it=l->item>=0?&l->t.items[l->item]:NULL;
 for(int r=0;r<ROWS;r++)xz_rect(p,0,r*ROW_H,VALUE_W,ROW_H-2,r&1?SH_BLACK:SH_CHROME);
 if(!it)return;
 if(!selectable(it)){
  /* Information: the value itself, large, no radio. */
  text_fit(p,16,12,VALUE_W-32,value_text(it),SH_TEXT,it->known?SH_WHITE:SH_DIM);
  return;
 }
 int n=choice_count(it),cur=current_of(it);
 for(int r=0;r<l->value_rows;r++){
  int index=s.value_scroll+r,y=r*ROW_H;
  if(index>=n)break;
  int cursor=s.pane==PANE_VALUES&&index==s.value;
  if(cursor)xz_rect(p,0,y,VALUE_W,ROW_H-2,SH_BLUE);
  xz_ring(p,22,y+18,8,1,SH_WHITE);
  if(index==cur)xz_disc(p,22,y+18,4,cursor?SH_WHITE:SH_BLUE);
  text_fit(p,40,y+12,VALUE_W-56,choice_text(it,index),SH_TEXT,SH_WHITE);
 }
 scrollbar(p,VALUE_W-5,2,l->value_rows*ROW_H-6,s.value_scroll,n,l->value_rows);
 if(l->footer){
  int y=(ROWS-1)*ROW_H;
  xz_rect(p,0,y,VALUE_W,ROW_H,SH_RAIL);
  const char *word=settable(m,it)?"NOT READ":"READ ONLY";
  int ww=sh_chip(p,12,y+11,word,SH_DIM,SH_WHITE,0);
  text_fit(p,12+ww+10,y+14,VALUE_W-ww-48,settable(m,it)?"PICK A VALUE TO SET IT":"CHANGE IT IN THE DECK MENU",SH_SMALL,SH_DIM);
  if(!settable(m,it))h_triangle(p,VALUE_W-18,y+18,5,1,SH_WHITE);
 }
}
static void render_empty_note(struct xz_canvas c, const char *text) {
 sh_text_center(c,LIST_X+LIST_W/2,BODY_Y+ROW_H*4+12,text,SH_TEXT,SH_DIM);
}
static void version_of(struct table t, char *out, size_t cap) {
 out[0]=0;
 int at=find_id(t,XZ_SHELL_ITEM_VERSION);
 if(at<0)return;
 const struct xz_shell_setting_item *it=&t.items[at];
 int cur=current_of(it);
 if(cur<0||!it->choices[cur])return;
 const char *v=it->choices[cur];
 snprintf(out,cap,"%s%s",v[0]=='V'||v[0]=='v'?"":"Ver.",v);
}
static void utility_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct ulayout l;
 struct ustate s=settle(m,unpack(&u->stack[u->depth-1]),&l);
 xz_rect(c,0,0,W,H,SH_BLACK);
 render_title(c,"UTILITY");
 char version[64];version_of(l.t,version,sizeof version);
 if(version[0])text_fit_right(c,W-14,13,260,version,SH_TEXT,DEEP_BLUE);
 render_rail(c,&l,s);
 render_items(m,c,&l,s);
 render_values(m,c,&l,s);
 if(l.item<0)render_empty_note(c,m->settings_screen?"NO SETTINGS READ YET":"SETTINGS NOT AVAILABLE HERE");
 render_status(u,m,c);
}

/* ---- UTILITY input ---- */
static size_t hand_off(enum xz_shell_screen screen, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,(int)screen});
}
static size_t apply(const struct xz_shell_model *m, struct ustate *s, const struct ulayout *l, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(!settable(m,it))return hand_off(XZ_SHELL_UTILITY,out);
 s->pane=PANE_ITEMS;
 if(s->value==current_of(it))return 0;
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,s->value,it->id});
}
static void enter_values(struct ustate *s, const struct ulayout *l) {
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(!selectable(it))return;
 s->pane=PANE_VALUES;s->value=value_start(it);s->value_scroll=keep_visible(0,s->value,choice_count(it),l->value_rows);
}
static void select_category(struct ustate *s, const struct ulayout *l, int row) {
 int at=first_of_category(l->t,l->cats[row]);
 s->item=item_id_at(l,at);s->item_scroll=0;s->value=0;s->value_scroll=0;
}
static void select_item(struct ustate *s, const struct ulayout *l, int row) {
 s->item=l->t.items[l->list[row]].id;s->value=0;s->value_scroll=0;
}
static void move(struct ustate *s, const struct ulayout *l, int delta) {
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 switch(s->pane){
 case PANE_RAIL:{int r=clamp(l->cat_row+delta,0,l->n_cats-1);if(r!=l->cat_row)select_category(s,l,r);break;}
 case PANE_ITEMS:{int r=clamp(l->list_row+delta,0,l->n_list-1);if(r!=l->list_row)select_item(s,l,r);break;}
 default:s->value=clamp(s->value+delta,0,choice_count(it)-1);break;
 }
}
static void page_jump(struct ustate *s, const struct ulayout *l) {
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(s->pane==PANE_ITEMS){
  int r=l->list_row>=l->n_list-1?0:sh_min(l->list_row+ROWS,l->n_list-1);
  select_item(s,l,r);s->item_scroll=r;
 }else if(s->pane==PANE_VALUES){
  int n=choice_count(it);
  s->value=s->value>=n-1?0:sh_min(s->value+l->value_rows,n-1);s->value_scroll=s->value;
 }
}
static int pane_at(int x, int y) {
 for(int p=PANE_RAIL;p<=PANE_VALUES;p++)
  if(sh_inside(xz_shell_settings_rect((enum xz_shell_settings_region)(XZ_SHELL_SR_RAIL+p)),x,y))return p;
 return -1;
}
static size_t utility_touch_down(struct xz_shell *u, const struct xz_shell_model *m, struct ustate *s, const struct ulayout *l, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 int p=pane_at(x,y);
 s->touch_pane=p;s->armed=0;s->dragged=0;
 u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;u->down_deck=y;
 if(p<0||l->item<0)return 0;
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(p==PANE_RAIL){
  int r=(y-BODY_Y)/RAIL_H;
  if(r>=l->n_cats)return 0;
  if(r==l->cat_row&&s->pane==PANE_RAIL)s->armed=1;
  else{if(r!=l->cat_row)select_category(s,l,r);s->pane=PANE_RAIL;}
  return 0;
 }
 int row=(y-BODY_Y)/ROW_H;
 if(p==PANE_ITEMS){
  int index=s->item_scroll+row;
  if(index>=l->n_list)return 0;
  if(index==l->list_row&&s->pane==PANE_ITEMS)s->armed=1;
  else{if(index!=l->list_row)select_item(s,l,index);s->pane=PANE_ITEMS;}
  return 0;
 }
 if(!selectable(it))return 0;
 if(l->footer&&row==ROWS-1){
  s->touch_pane=-1;
  return settable(m,it)?0:hand_off(XZ_SHELL_UTILITY,out);
 }
 int index=s->value_scroll+row;
 if(index>=choice_count(it))return 0;
 if(index==s->value&&s->pane==PANE_VALUES)s->armed=1;
 else{s->value=index;s->pane=PANE_VALUES;}
 return 0;
}
static void utility_drag(struct xz_shell *u, struct ustate *s, const struct ulayout *l, int y) {
 int dy=y-u->down_deck;
 if(!s->dragged&&(dy>=8||dy<=-8))s->dragged=1;
 if(!s->dragged||s->touch_pane==PANE_RAIL||s->touch_pane<0||l->item<0)return;
 int rows=dy/ROW_H;
 if(!rows)return;
 u->down_deck+=rows*ROW_H;
 const struct xz_shell_setting_item *it=&l->t.items[l->item];
 if(s->touch_pane==PANE_ITEMS){
  s->item_scroll=clamp(s->item_scroll-rows,0,sh_max(0,l->n_list-ROWS));
  int r=clamp(l->list_row,s->item_scroll,s->item_scroll+ROWS-1);
  if(r!=l->list_row)select_item(s,l,r);
 }else if(selectable(it)){
  int n=choice_count(it);
  s->value_scroll=clamp(s->value_scroll-rows,0,sh_max(0,n-l->value_rows));
  s->value=clamp(s->value,s->value_scroll,s->value_scroll+l->value_rows-1);
 }
}
static size_t utility_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_view *v=&u->stack[u->depth-1];
 struct ulayout l;
 struct ustate s=settle(m,unpack(v),&l);
 size_t n=0;
 if(e->kind==XZ_SHELL_TOUCH_DOWN&&sh_inside(xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON),e->x,e->y)){
  pack(v,s);
  return xz_shell_settings_back(u,m)?0:sh_key(u,m,XZ_SHELL_KEY_BACK,-1,out);
 }
 /* The slot went away while the screen was open: the stock menu takes over on the next push or touch. */
 if(!m->settings_screen&&(e->kind==XZ_SHELL_ENCODER_PUSH||e->kind==XZ_SHELL_TOUCH_DOWN))return hand_off(XZ_SHELL_UTILITY,out);
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:n=utility_touch_down(u,m,&s,&l,e->x,e->y,out);break;
 case XZ_SHELL_TOUCH_MOVE:if(u->down)utility_drag(u,&s,&l,e->y);break;
 case XZ_SHELL_TOUCH_UP:
  if(u->down&&s.armed&&!s.dragged&&l.item>=0){
   if(s.touch_pane==PANE_RAIL)s.pane=PANE_ITEMS;
   else if(s.touch_pane==PANE_ITEMS)enter_values(&s,&l);
   else if(s.touch_pane==PANE_VALUES)n=apply(m,&s,&l,out);
  }
  u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;s.touch_pane=-1;s.armed=s.dragged=0;
  break;
 case XZ_SHELL_ENCODER_TURN:if(l.item>=0&&e->delta)move(&s,&l,e->delta);break;
 case XZ_SHELL_ENCODER_PUSH:
  if(l.item<0)break;
  if(s.pane==PANE_RAIL)s.pane=PANE_ITEMS;
  else if(s.pane==PANE_ITEMS)enter_values(&s,&l);
  else n=apply(m,&s,&l,out);
  break;
 case XZ_SHELL_ENCODER_HOLD:if(l.item>=0)page_jump(&s,&l);break;
 default:break;
 }
 struct ulayout after;
 s=settle(m,s,&after);
 pack(v,s);
 return n;
}
int xz_shell_settings_back(struct xz_shell *u, const struct xz_shell_model *m) {
 if(!u||!m||u->depth<1||u->depth>XZ_SHELL_STACK||xz_shell_screen(u)!=XZ_SHELL_UTILITY)return 0;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 struct ulayout l;
 struct ustate s=settle(m,unpack(v),&l);
 if(s.pane==PANE_RAIL||l.item<0)return 0;
 s.pane--;
 pack(v,s);
 return 1;
}

/* ================= SHORTCUT ================= */

enum { Q_PLAYER, Q_DEVICE_INFO, Q_DEVICE, Q_MY, QUADS };
static const char *const quad_titles[QUADS]={"PLAYER SETTINGS","DEVICE INFORMATION","DEVICE SETTINGS","MY SETTINGS"};
/* Where each item sits on SHORTCUT, in slot order. Items the table lacks close up. */
static const int placement[QUADS][SLOTS]={
 {XZ_SHELL_ITEM_QUANTIZE_BEAT,XZ_SHELL_ITEM_BEAT_JUMP_BEAT,XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD,XZ_SHELL_ITEM_WAVE_PHASE_METER},
 {XZ_SHELL_ITEM_VERSION,XZ_SHELL_ITEM_PLAYER_NUMBER,XZ_SHELL_ITEM_MIDI_CHANNEL,-1},
 {XZ_SHELL_ITEM_WAVE_COLOR,XZ_SHELL_ITEM_WAVE_POSITION,XZ_SHELL_ITEM_LCD_BRIGHTNESS,XZ_SHELL_ITEM_JOG_LCD_BRIGHTNESS},
 {XZ_SHELL_ITEM_TIME_MODE,XZ_SHELL_ITEM_PLAY_MODE,XZ_SHELL_ITEM_AUTO_CUE_LEVEL,XZ_SHELL_ITEM_VINYL_SPEED_ADJUST},
};
struct xz_shell_rect xz_shell_shortcut_quadrant(int q) {
 if(q<0||q>=QUADS)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){(q&1)*(QUAD_W+2),BODY_Y+(q>>1)*(QUAD_H+1),QUAD_W,QUAD_H};
}
struct xz_shell_rect xz_shell_shortcut_slot(int q, int slot) {
 struct xz_shell_rect r=xz_shell_shortcut_quadrant(q);
 if(!r.w||slot<0||slot>=SLOTS)return (struct xz_shell_rect){0,0,0,0};
 return (struct xz_shell_rect){r.x,r.y+QUAD_TITLE+slot*SLOT_H,r.w,SLOT_H+1};
}
/* Table indices on screen: slots[q][k], -1 for empty. */
struct slayout { struct table t; int slots[QUADS][SLOTS]; int order[QUADS*SLOTS],n_order; };
static struct slayout slayout_of(const struct xz_shell_model *m) {
 struct slayout l;memset(&l,0,sizeof l);
 l.t=table_of(m);
 for(int q=0;q<QUADS;q++){
  int k=0;
  for(int i=0;i<SLOTS;i++)l.slots[q][i]=-1;
  for(int i=0;i<SLOTS;i++){
   int at=placement[q][i]>=0?find_id(l.t,placement[q][i]):-1;
   if(at>=0)l.slots[q][k++]=at;
  }
 }
 /* Focus visits the setting quadrants in reading order; DEVICE INFORMATION only reads. */
 static const int reading[]={Q_PLAYER,Q_DEVICE_INFO,Q_DEVICE,Q_MY};
 for(int i=0;i<QUADS;i++){
  int q=reading[i];
  if(q==Q_DEVICE_INFO)continue;
  for(int k=0;k<SLOTS;k++)if(l.slots[q][k]>=0)l.order[l.n_order++]=l.slots[q][k];
 }
 return l;
}
static int focus_of(const struct slayout *l, int cursor) {
 for(int i=0;i<l->n_order;i++)if(l->t.items[l->order[i]].id==cursor-1)return i;
 return l->n_order?0:-1;
}
/* Chip row: equal segments in the large face, then the small face, else a stepper. */
enum chip_mode { CHIPS_TEXT, CHIPS_SMALL, CHIPS_STEPPER };
struct chips { enum chip_mode mode; int n, x, y, w, h, cw; };
static struct chips chips_of(const struct xz_shell_setting_item *it, struct xz_shell_rect slot) {
 struct chips k={CHIPS_STEPPER,choice_count(it),slot.x+CHIP_PAD,slot.y+CHIP_Y,slot.w-2*CHIP_PAD,CHIP_H,0};
 if(k.n<1)return k;
 k.cw=(k.w-CHIP_GAP*(k.n-1))/k.n;
 int widest_text=0,widest_small=0;
 for(int i=0;i<k.n;i++){
  widest_text=sh_max(widest_text,sh_text_width(measure,choice_text(it,i),SH_TEXT));
  widest_small=sh_max(widest_small,sh_text_width(measure,choice_text(it,i),SH_SMALL));
 }
 if(widest_text+12<=k.cw)k.mode=CHIPS_TEXT;
 else if(widest_small+8<=k.cw)k.mode=CHIPS_SMALL;
 return k;
}
static struct xz_shell_rect chip_rect(const struct chips *k, int i) {
 int x=k->x+i*(k->cw+CHIP_GAP);
 int w=i==k->n-1?k->x+k->w-x:k->cw;
 return (struct xz_shell_rect){x,k->y,w,k->h};
}
enum { STEP_W=40 };
static void render_chip_row(const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_setting_item *it, struct xz_shell_rect slot) {
 struct chips k=chips_of(it,slot);
 int cur=current_of(it),live=settable(m,it);
 uint32_t on=live?SH_BLUE:RAIL_SELECTED,off_ink=live?0xd8d8d8:GREY_TEXT;
 if(k.mode==CHIPS_STEPPER){
  struct xz_shell_rect r={k.x,k.y,k.w,k.h};
  xz_rect(c,r.x,r.y,STEP_W,r.h,SH_RAIL);xz_rect(c,r.x+r.w-STEP_W,r.y,STEP_W,r.h,SH_RAIL);
  h_triangle(c,r.x+STEP_W/2-3,r.y+r.h/2,6,0,cur>0||cur<0?SH_WHITE:READ_ONLY_GREY);
  h_triangle(c,r.x+r.w-STEP_W/2-3,r.y+r.h/2,6,1,cur<k.n-1?SH_WHITE:READ_ONLY_GREY);
  int mx=r.x+STEP_W+CHIP_GAP,mw=r.w-2*(STEP_W+CHIP_GAP);
  xz_rect(c,mx,r.y,mw,r.h,cur>=0?on:SH_ROW_ALT);
  text_fit_center(c,mx+mw/2,r.y+5,mw-12,cur>=0?choice_text(it,cur):"---",SH_TEXT,cur>=0?SH_WHITE:SH_DIM);
  return;
 }
 enum sh_face face=k.mode==CHIPS_TEXT?SH_TEXT:SH_SMALL;
 for(int i=0;i<k.n;i++){
  struct xz_shell_rect r=chip_rect(&k,i);
  int selected=i==cur;
  xz_rect(c,r.x,r.y,r.w,r.h,selected?on:SH_ROW_ALT);
  if(!selected)xz_border(c,r.x,r.y,r.w,r.h,SH_RAIL);
  int ty=face==SH_TEXT?r.y+5:r.y+8;
  text_fit_center(c,r.x+r.w/2,ty,r.w-6,choice_text(it,i),face,selected?SH_WHITE:off_ink);
 }
}
static void render_info_row(struct xz_canvas c, const char *label, const char *value, int known, struct xz_shell_rect slot) {
 text_fit(c,slot.x+CHIP_PAD,slot.y+17,150,label,SH_SMALL,SH_DIM);
 text_fit_right(c,slot.x+slot.w-CHIP_PAD,slot.y+13,slot.w-CHIP_PAD*2-160,value,SH_TEXT,known?SH_WHITE:SH_DIM);
}
static void render_quadrant(const struct xz_shell_model *m, struct xz_canvas c, const struct slayout *l, int q, int focus_index) {
 struct xz_shell_rect r=xz_shell_shortcut_quadrant(q);
 xz_rect(c,r.x,r.y,r.w,r.h,SH_CHROME);
 sh_text(c,r.x+CHIP_PAD,r.y+5,quad_titles[q],SH_SMALL,SH_DIM);
 xz_rect(c,r.x+CHIP_PAD,r.y+QUAD_TITLE-1,r.w-2*CHIP_PAD,1,SH_RAIL);
 int shown=0;
 for(int k=0;k<SLOTS;k++){
  int at=l->slots[q][k];
  if(at<0)continue;
  shown++;
  const struct xz_shell_setting_item *it=&l->t.items[at];
  struct xz_shell_rect slot=xz_shell_shortcut_slot(q,k);slot.h=SLOT_H;
  if(q==Q_DEVICE_INFO){
   render_info_row(c,label_of(it),value_text(it),it->known&&current_of(it)>=0,slot);
   xz_rect(c,r.x+CHIP_PAD,slot.y+SLOT_H-1,r.w-2*CHIP_PAD,1,SH_ROW_ALT);
   continue;
  }
  int focused=focus_index>=0&&l->order[focus_index]==at;
  if(focused){xz_rect(c,slot.x,slot.y,slot.w,slot.h,0x242a33);xz_rect(c,slot.x,slot.y+2,4,slot.h-4,SH_BLUE);}
  const char *word=!it->known||current_of(it)<0?"UNKNOWN":!settable(m,it)?"READ ONLY":NULL;
  int ww=word?sh_text_width(c,word,SH_SMALL):0;
  if(word)sh_text(c,slot.x+slot.w-CHIP_PAD-ww,slot.y+3,word,SH_SMALL,!it->known?SH_DIM:READ_ONLY_GREY);
  text_fit(c,slot.x+CHIP_PAD,slot.y+3,slot.w-2*CHIP_PAD-ww-10,label_of(it),SH_SMALL,focused?SH_WHITE:SH_DIM);
  render_chip_row(m,c,it,slot);
 }
 if(q==Q_DEVICE_INFO){
  /* LINK state comes from the deck model, not the table. */
  int k=shown;
  if(k<SLOTS){
   struct xz_shell_rect slot=xz_shell_shortcut_slot(q,k);
   render_info_row(c,"PRO DJ LINK",!m->link_known?"UNKNOWN":m->link_connected?"CONNECTED":"NOT CONNECTED",m->link_known,slot);
  }
  shown++;
 }
 if(!shown)sh_text_center(c,r.x+r.w/2,r.y+r.h/2-6,"NOT READ ON THIS DECK YET",SH_SMALL,SH_DIM);
}
static void shortcut_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct slayout l=slayout_of(m);
 int focus=focus_of(&l,u->stack[u->depth-1].cursor);
 xz_rect(c,0,0,W,H,SH_BLACK);
 render_title(c,"SHORTCUT");
 const char *hint=focus>=0?"TURN TO MOVE \xc2\xb7 PUSH TO CHANGE \xc2\xb7 SHORTCUT TO CLOSE":"SHORTCUT TO CLOSE";
 text_fit_right(c,W-14,16,W-220,hint,SH_SMALL,SH_DIM);
 for(int q=0;q<QUADS;q++)render_quadrant(m,c,&l,q,focus);
 render_status(u,m,c);
}

/* ---- SHORTCUT input ---- */
static size_t set_or_hand_off(const struct xz_shell_model *m, const struct xz_shell_setting_item *it, int index, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(!settable(m,it))return hand_off(XZ_SHELL_SHORTCUT,out);
 if(index<0||index>=choice_count(it)||index==current_of(it))return 0;
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_SET_SETTING,-1,index,it->id});
}
static size_t shortcut_touch(struct xz_shell *u, const struct xz_shell_model *m, const struct slayout *l, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_view *v=&u->stack[u->depth-1];
 if(sh_inside(xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON),x,y))return sh_key(u,m,XZ_SHELL_KEY_BACK,-1,out);
 for(int q=0;q<QUADS;q++){
  if(q==Q_DEVICE_INFO)continue;
  for(int k=0;k<SLOTS;k++){
   int at=l->slots[q][k];
   struct xz_shell_rect slot=xz_shell_shortcut_slot(q,k);
   if(at<0||!sh_inside(slot,x,y))continue;
   const struct xz_shell_setting_item *it=&l->t.items[at];
   v->cursor=it->id+1;
   struct chips c=chips_of(it,slot);
   if(c.n<1||y<c.y-6)return 0;     /* the label line only moves focus */
   int cur=current_of(it);
   if(c.mode==CHIPS_STEPPER){
    if(x<c.x+STEP_W+CHIP_GAP)return set_or_hand_off(m,it,cur<0?0:sh_max(0,cur-1),out);
    if(x>=c.x+c.w-STEP_W-CHIP_GAP)return set_or_hand_off(m,it,cur<0?0:sh_min(c.n-1,cur+1),out);
    return 0;
   }
   for(int i=0;i<c.n;i++){
    struct xz_shell_rect r=chip_rect(&c,i);r.y=slot.y;r.h=slot.h;
    if(sh_inside(r,x,y))return set_or_hand_off(m,it,i,out);
   }
   return 0;
  }
 }
 return 0;
}
static size_t shortcut_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct xz_shell_view *v=&u->stack[u->depth-1];
 struct slayout l=slayout_of(m);
 int focus=focus_of(&l,v->cursor);
 if(!m->settings_screen&&(e->kind==XZ_SHELL_ENCODER_PUSH||(e->kind==XZ_SHELL_TOUCH_DOWN&&!sh_inside(xz_shell_settings_rect(XZ_SHELL_SR_BACK_BUTTON),e->x,e->y))))
  return hand_off(XZ_SHELL_SHORTCUT,out);
 switch(e->kind){
 case XZ_SHELL_TOUCH_DOWN:u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;return shortcut_touch(u,m,&l,e->x,e->y,out);
 case XZ_SHELL_TOUCH_UP:u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;return 0;
 case XZ_SHELL_ENCODER_TURN:
  if(focus<0)return 0;
  focus=clamp(focus+e->delta,0,l.n_order-1);
  v->cursor=l.t.items[l.order[focus]].id+1;
  return 0;
 case XZ_SHELL_ENCODER_PUSH:{
  if(focus<0)return 0;
  const struct xz_shell_setting_item *it=&l.t.items[l.order[focus]];
  v->cursor=it->id+1;
  int n=choice_count(it),cur=current_of(it);
  if(n<1)return 0;
  return set_or_hand_off(m,it,cur<0?0:(cur+1)%n,out);
 }
 default:return 0;
 }
}

const struct sh_screen xz_shell_shortcut_screen={shortcut_render,shortcut_input};
const struct sh_screen xz_shell_utility_screen={utility_render,utility_input};
