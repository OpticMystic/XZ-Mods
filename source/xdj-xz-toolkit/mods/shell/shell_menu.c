/* The MODS screen's MENU tabs: a mod page the runtime describes (shell_settings.h struct xz_shell_menu),
   drawn in the three-pane UTILITY chrome (rail, list, values) and driven by the same encoder and touch
   model. Every push turns into one XZ_SHELL_ACT_MOD_MENU for the runtime; the page the runtime publishes
   next is what shows. No push here ever opens another menu. */
#include "shell_internal.h"
#include "../ui/ui_about.h"
#include "../ui/ui_support.h"
extern void xz_ui_support_render_qr_box(struct xz_canvas,int,int,int,const char *) __attribute__((weak));
#include "shell_settings.h"
#include <stdio.h>
#include <string.h>

enum {
 W=XZ_SHELL_WIDTH, TITLE_H=40, U_Y=42, ROW_H=XZ_SHELL_SETTINGS_ROW_H, ROWS=XZ_SHELL_SETTINGS_ROWS,
 RAIL_W=88, LIST_X=RAIL_W+2, LIST_W=408, VALUE_X=LIST_X+LIST_W+2, VALUE_W=W-VALUE_X, PANE_H=ROWS*ROW_H,
 GREY_TEXT=0x8c8c8c, READ_ONLY_GREY=0x6e6e6e, PLAIN_INK=0xd8d8d8, DONE_FILL=0x0b3f6b,
 LINE_H=14, PAD_H=136,
 /* KEYBOARD: the typed text, five key rows, the buttons */
 KB_X=LIST_X+6, KB_W=W-KB_X-6, KB_FIELD_Y=U_Y+4, KB_FIELD_H=48, KB_Y=U_Y+58, KB_PITCH=46, KB_KEY_H=42,
 KB_BUTTON_Y=KB_Y+5*KB_PITCH+4, KB_BUTTON_H=44
};
enum pane { PANE_RAIL, PANE_ITEMS, PANE_VALUES, PANE_PAD };
static const struct xz_canvas measure={NULL,W,W,XZ_SHELL_HEIGHT,XZ_THEME_FLAGSHIP};

/* ---- the page and the view ---- */
static const struct xz_shell_menu *menu_of(const struct xz_shell_model *m, const struct xz_shell_mods_tab *tab) {
 const struct xz_shell_menu *p=m->menu;
 if(!p||!tab||tab->kind!=XZ_SHELL_TAB_MENU||p->root!=tab->page)return NULL;
 return p;
}
static int rows_of(const struct xz_shell_menu *p) { return p?sh_min(p->row_count,XZ_SHELL_MENU_ROWS):0; }
static const struct xz_shell_menu_row *row_at(const struct xz_shell_menu *p, int r) { return r>=0&&r<rows_of(p)?&p->row[r]:NULL; }
static int choices_of(const struct xz_shell_menu *p, const struct xz_shell_menu_row *r) {
 if(!r||r->first_choice<0||r->first_choice>=XZ_SHELL_MENU_CHOICES)return 0;
 return sh_max(0,sh_min(r->choice_count,sh_min(p->choice_count,XZ_SHELL_MENU_CHOICES)-r->first_choice));
}
static const struct xz_shell_menu_choice *choice_at(const struct xz_shell_menu *p, const struct xz_shell_menu_row *r, int i) {
 return i>=0&&i<choices_of(p,r)?&p->choice[r->first_choice+i]:NULL;
}
static int current_of(const struct xz_shell_menu *p, const struct xz_shell_menu_row *r) {
 return r&&r->current>=0&&r->current<choices_of(p,r)?r->current:-1;
}
static int has_values(const struct xz_shell_menu *p, const struct xz_shell_menu_row *r) {
 return r&&(r->kind==XZ_SHELL_MENU_CHOICE||r->kind==XZ_SHELL_MENU_TOGGLE)&&choices_of(p,r)>0;
}
static int keyboard(const struct xz_shell_menu *p) { return p&&p->layout==XZ_SHELL_MENU_KEYBOARD; }
static const char *section_name(const struct xz_shell_menu *p, int s) {
 return s>=0&&s<sh_min(p->section_count,XZ_SHELL_MENU_SECTIONS)?p->sections[s]:"";
}
static int clamp(int v, int lo, int hi) { return v<lo?lo:v>hi?hi:v; }

/* cursor = the row's key; focus = pane | value << 4 | touch pane + 1 << 16 | armed << 20 | dragged << 21;
   scroll = list scroll | value scroll << 12. menu_view.page says which page the cursor belongs to. */
struct mstate { int pane, row, value, item_scroll, value_scroll, touch_pane, armed, dragged; };
/* The list as shown: a row index, or -1 - section for a section header. */
struct mlist { int entry[XZ_SHELL_MENU_ROWS*2], n, first_slot, slots; };
static struct mlist list_of(const struct xz_shell_menu *p) {
 struct mlist l;memset(&l,0,sizeof l);
 l.first_slot=p&&p->step_count?1:0;l.slots=ROWS-l.first_slot;
 int last=-1;
 for(int r=0;r<rows_of(p);r++){
  int s=p->row[r].section;
  if(s!=last&&section_name(p,s)[0])l.entry[l.n++]=-1-s;
  last=s;l.entry[l.n++]=r;
 }
 return l;
}
static int list_index(const struct mlist *l, int row) { for(int i=0;i<l->n;i++)if(l->entry[i]==row)return i; return -1; }
static int keep_visible(int scroll, int index, int count, int rows) {
 scroll=clamp(scroll,0,sh_max(0,count-rows));
 if(index<scroll)scroll=index;
 if(index>=scroll+rows)scroll=index-rows+1;
 return clamp(scroll,0,sh_max(0,count-rows));
}
/* Value rows the values pane gives a row's choices before its text starts. */
static int value_rows(const struct xz_shell_menu *p, const struct xz_shell_menu_row *r) {
 return sh_max(2,sh_min(choices_of(p,r),ROWS-3));
}
static struct mstate settle(const struct xz_shell *u, const struct xz_shell_menu *p) {
 const struct xz_shell_view *v=&u->stack[u->depth-1];
 struct mstate s={v->focus&3,-1,(v->focus>>4)&0xfff,v->scroll&0xfff,(v->scroll>>12)&0xfff,((v->focus>>16)&7)-1,(v->focus>>20)&1,(v->focus>>21)&1};
 int n=rows_of(p);
 if(!n){s.pane=PANE_ITEMS;s.value=s.item_scroll=s.value_scroll=0;return s;}
 if(u->menu_view.page==p->page)for(int r=0;r<n&&s.row<0;r++)if(p->row[r].key==v->cursor)s.row=r;
 if(s.row<0&&p->depth<u->menu_view.trail_count&&p->depth<XZ_SHELL_STACK&&u->menu_view.trail[p->depth].page==p->page){
  /* back on a page left by a push: the row that pushed */
  for(int r=0;r<n&&s.row<0;r++)if(p->row[r].key==u->menu_view.trail[p->depth].key)s.row=r;
  if(s.row>=0){s.pane=PANE_ITEMS;s.item_scroll=s.value_scroll=0;s.armed=s.dragged=0;s.touch_pane=-1;}
 }
 if(s.row<0){
  /* A page the cursor has not been on: start where the page says, at the top of the list. */
  s.row=p->focus_row>=0&&p->focus_row<n?p->focus_row:0;
  s.pane=PANE_ITEMS;s.item_scroll=s.value_scroll=0;s.armed=s.dragged=0;s.touch_pane=-1;
 }
 const struct xz_shell_menu_row *r=&p->row[s.row];
 if(s.pane==PANE_RAIL||s.pane==PANE_PAD)s.pane=PANE_ITEMS;
 if(s.pane==PANE_VALUES&&!has_values(p,r))s.pane=PANE_ITEMS;
 int k=choices_of(p,r);
 s.value=clamp(s.value,0,sh_max(0,k-1));
 s.value_scroll=keep_visible(s.value_scroll,s.value,k,value_rows(p,r));
 if(!keyboard(p)){
  struct mlist l=list_of(p);
  int at=list_index(&l,s.row);
  s.item_scroll=keep_visible(s.item_scroll,at,l.n,l.slots);
  if(at>0&&l.entry[at-1]<0&&s.item_scroll==at)s.item_scroll--;   /* the section header stays in view */
 }else s.item_scroll=0;
 return s;
}
static void pack(struct xz_shell *u, const struct xz_shell_menu *p, struct mstate s) {
 struct xz_shell_view *v=&u->stack[u->depth-1];
 v->cursor=row_at(p,s.row)?p->row[s.row].key:0;
 v->focus=(s.pane&3)|((s.value&0xfff)<<4)|(((s.touch_pane+1)&7)<<16)|((s.armed&1)<<20)|((s.dragged&1)<<21);
 v->scroll=(s.item_scroll&0xfff)|((s.value_scroll&0xfff)<<12);
 u->menu_view.page=p?p->page:-1;
}

/* ---- geometry ---- */
static struct xz_shell_rect none(void) { return (struct xz_shell_rect){0,0,0,0}; }
struct xz_shell_rect xz_shell_menu_pad_rect(void) { return (struct xz_shell_rect){VALUE_X+12,U_Y+PANE_H-PAD_H-8,VALUE_W-24,PAD_H}; }
/* KEYBOARD: one-character keys ten to a row, the wider keys on the fifth row, the other rows as buttons. */
static int short_key(const struct xz_shell_menu_row *r) { return r->kind==XZ_SHELL_MENU_KEY&&r->label[0]&&!r->label[1]; }
static struct xz_shell_rect key_rect(const struct xz_shell_menu *p, int row) {
 int chars=0,wide=0,buttons=0,ci=-1,wi=-1,bi=-1;
 for(int r=0;r<rows_of(p);r++){
  const struct xz_shell_menu_row *x=&p->row[r];
  if(short_key(x)){if(r==row)ci=chars;chars++;}
  else if(x->kind==XZ_SHELL_MENU_KEY){if(r==row)wi=wide;wide++;}
  else{if(r==row)bi=buttons;buttons++;}
 }
 int pitch=KB_W/10;
 if(ci>=0){
  if(ci>=40)return none();
  return (struct xz_shell_rect){KB_X+(ci%10)*pitch,KB_Y+(ci/10)*KB_PITCH,pitch-4,KB_KEY_H};
 }
 if(wi>=0){
  /* SPACE (the third wide key) takes double width */
  int units=wide+(wide>=3?1:0),x=KB_X;
  for(int i=0;i<wi;i++)x+=KB_W*(i==2?2:1)/units;
  return (struct xz_shell_rect){x,KB_Y+4*KB_PITCH,KB_W*(wi==2?2:1)/units-4,KB_KEY_H};
 }
 if(bi>=0){
  int w=KB_W/sh_max(1,buttons);
  return (struct xz_shell_rect){KB_X+bi*w,KB_BUTTON_Y,w-4,KB_BUTTON_H};
 }
 return none();
}
static struct xz_shell_rect slot_rect(int slot) { return (struct xz_shell_rect){LIST_X,U_Y+slot*ROW_H,LIST_W,ROW_H}; }
int xz_shell_menu_cursor(const struct xz_shell *u, const struct xz_shell_model *m) {
 if(!u||!m||u->depth<1||u->depth>XZ_SHELL_STACK||xz_shell_screen(u)!=XZ_SHELL_UTILITY)return -1;
 const struct xz_shell_menu *p=menu_of(m,sh_mods_tab(u,m));
 return rows_of(p)?settle(u,p).row:-1;
}
struct xz_shell_rect xz_shell_menu_row_rect(const struct xz_shell *u, const struct xz_shell_model *m, int row) {
 if(!u||!m||u->depth<1||u->depth>XZ_SHELL_STACK||xz_shell_screen(u)!=XZ_SHELL_UTILITY)return none();
 const struct xz_shell_menu *p=menu_of(m,sh_mods_tab(u,m));
 if(!row_at(p,row))return none();
 if(p->layout==XZ_SHELL_MENU_ABOUT){struct xz_ui_widget w={0};xz_about_place(&w,row);return (struct xz_shell_rect){w.x,w.y,w.w,w.h};}
 if(keyboard(p))return key_rect(p,row);
 struct mstate s=settle(u,p);struct mlist l=list_of(p);
 int at=list_index(&l,row)-s.item_scroll;
 if(at<0||at>=l.slots)return none();
 return slot_rect(l.first_slot+at);
}

/* ---- text ---- */
static size_t utf8_step(const char *s) {
 unsigned char ch=(unsigned char)*s;
 size_t n=ch<0xc2?1:ch<0xe0?2:ch<0xf0?3:4,i;
 for(i=1;i<n;i++)if((s[i]&0xc0)!=0x80)return i;
 return n;
}
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
static int text_fit(struct xz_canvas c, int x, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char b[200];int w=fit(s,face,width,b,sizeof b);sh_text(c,x,y,b,face,ink);return w;
}
static void text_fit_right(struct xz_canvas c, int right, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char b[200];int w=fit(s,face,width,b,sizeof b);sh_text(c,right-w,y,b,face,ink);
}
static void text_fit_center(struct xz_canvas c, int cx, int y, int width, const char *s, enum sh_face face, uint32_t ink) {
 char b[200];int w=fit(s,face,width,b,sizeof b);sh_text(c,cx-w/2,y,b,face,ink);
}
static void lock_icon(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_ring(c,x+4,y+4,4,1,ink);xz_rect(c,x,y+4,9,7,ink);xz_rect(c,x+4,y+6,1,3,SH_CHROME);
}
static void chevron(struct xz_canvas c, int x, int cy, uint32_t ink) {
 xz_line16(c,x*16,(cy-5)*16,(x+5)*16,cy*16,26,ink);xz_line16(c,(x+5)*16,cy*16,x*16,(cy+5)*16,26,ink);
}
static uint32_t tone_ink(int tone) {
 switch(tone){
 case XZ_SHELL_TONE_DIM:return SH_DIM;
 case XZ_SHELL_TONE_GOOD:return SH_GREEN;
 case XZ_SHELL_TONE_ALERT:return SH_RED;
 case XZ_SHELL_TONE_HEAD:return SH_WHITE;
 default:return PLAIN_INK;
 }
}
/* Wrapped text from y down, no lower than bottom; returns the next y. */
static int paragraph(struct xz_canvas c, int x, int y, int width, int bottom, const char *text, uint32_t ink) {
 char lines[8][SH_WRAP_CHARS];
 int n=sh_wrap(text,width,lines,8);
 for(int i=0;i<n&&y+LINE_H<=bottom;i++,y+=LINE_H)sh_text(c,x,y,lines[i],SH_SMALL,ink);
 return y;
}

/* ---- render ---- */
static void render_steps(struct xz_canvas c, const struct xz_shell_menu *p) {
 int n=sh_min(p->step_count,XZ_SHELL_MENU_STEPS),gap=4,w=(LIST_W-8-gap*(n-1))/sh_max(1,n);
 xz_rect(c,LIST_X,U_Y,LIST_W,ROW_H-2,SH_BLACK);
 for(int i=0;i<n;i++){
  int x=LIST_X+4+i*(w+gap),y=U_Y+5,h=ROW_H-12;
  int now=i==p->step,done=i<p->step;
  if(now)xz_rect(c,x,y,w,h,SH_BLUE);
  else if(done)xz_rect(c,x,y,w,h,DONE_FILL);
  else{xz_rect(c,x,y,w,h,SH_CHROME);xz_border(c,x,y,w,h,SH_RAIL);}
  char label[40];snprintf(label,sizeof label,"%d %s",i+1,p->steps[i]);
  text_fit_center(c,x+w/2,y+(h-SH_CAP_SMALL)/2,w-8,label,SH_SMALL,now||done?SH_WHITE:SH_DIM);
 }
}
static const char *row_value(const struct xz_shell_menu *p, const struct xz_shell_menu_row *r) {
 const struct xz_shell_menu_choice *ch=choice_at(p,r,current_of(p,r));
 if(has_values(p,r))return ch?ch->label:"---";
 return r->value;
}
static void render_list(struct xz_canvas c, const struct xz_shell_menu *p, struct mstate s, int lit) {
 struct mlist l=list_of(p);
 if(l.first_slot)render_steps(c,p);
 struct xz_canvas q=xz_sub(c,LIST_X,U_Y+l.first_slot*ROW_H,LIST_W,l.slots*ROW_H);
 for(int i=0;i<l.slots;i++){
  int y=i*ROW_H,index=s.item_scroll+i;
  if(index>=l.n){xz_rect(q,0,y,LIST_W,ROW_H-2,SH_CHROME);continue;}
  int e=l.entry[index];
  if(e<0){
   xz_rect(q,0,y,LIST_W,ROW_H-2,SH_BLACK);
   text_fit(q,14,y+ROW_H-SH_CAP_SMALL-10,LIST_W-28,section_name(p,-1-e),SH_SMALL,GREY_TEXT);
   xz_rect(q,14,y+ROW_H-5,LIST_W-28,1,SH_RAIL);
   continue;
  }
  const struct xz_shell_menu_row *r=&p->row[e];
  int cursor=e==s.row&&lit;
  xz_rect(q,0,y,LIST_W,ROW_H-2,cursor?(s.pane==PANE_VALUES?XZ_FLAGSHIP_RAIL_SELECTED:SH_BLUE):SH_CHROME);
  int right=LIST_W-14;
  if(r->kind==XZ_SHELL_MENU_LINK){chevron(q,right-5,y+ROW_H/2-1,cursor?SH_WHITE:GREY_TEXT);right-=16;}
  const char *value=row_value(p,r);
  char vbuf[160];int vw=fit(value,SH_TEXT,170,vbuf,sizeof vbuf);
  uint32_t vink=cursor?SH_WHITE:r->available?GREY_TEXT:READ_ONLY_GREY;
  if(r->kind==XZ_SHELL_MENU_TOGGLE&&current_of(p,r)==1&&r->available&&!cursor)vink=SH_WHITE;
  sh_text(q,right-vw,y+12,vbuf,SH_TEXT,vink);
  int lock=!r->available;
  if(lock)lock_icon(q,right-vw-(vw?17:12),y+13,cursor?SH_WHITE:READ_ONLY_GREY);
  int name_w=right-vw-(lock?19:0)-16-14;
  text_fit(q,14,y+12,name_w,r->label,SH_TEXT,r->available||cursor?SH_WHITE:SH_DIM);
 }
 if(l.n>l.slots){
  int h=l.slots*ROW_H-6,thumb=sh_max(16,h*l.slots/l.n),travel=h-thumb;
  xz_rect(q,LIST_W-4,2+travel*s.item_scroll/sh_max(1,l.n-l.slots),3,thumb,GREY_TEXT);
 }
}
/* The page's own state: its last result, then its lines. Returns the next y. */
static int render_page_text(struct xz_canvas q, const struct xz_shell_menu *p, int y, int bottom) {
 int width=q.width-28;
 if(p->status[0]){
  y=paragraph(q,14,y,width,bottom,p->status,tone_ink(p->status_tone));
  y+=6;
 }
 for(int i=0;i<sh_min(p->line_count,XZ_SHELL_MENU_LINES)&&y+LINE_H<=bottom;i++){
  const struct xz_shell_menu_line *ln=&p->line[i];
  if(ln->tone==XZ_SHELL_TONE_HEAD){
   if(y+22>bottom)break;
   text_fit(q,14,y,width,ln->text,SH_TEXT,SH_WHITE);y+=24;
  }else y=paragraph(q,14,y,width,bottom,ln->text,tone_ink(ln->tone));
 }
 return y;
}
static int render_help(struct xz_canvas q, const struct xz_shell_menu_row *r, int y, int bottom) {
 int width=VALUE_W-28;
 if(!r->available&&r->reason[0]){
  if(y+LINE_H<=bottom)lock_icon(q,14,y,SH_RED);
  char lines[4][SH_WRAP_CHARS];int n=sh_wrap(r->reason,width-16,lines,4);
  for(int i=0;i<n&&y+LINE_H<=bottom;i++,y+=LINE_H)sh_text(q,i?14:30,y,lines[i],SH_SMALL,SH_RED);
  y+=6;
 }
 if(r->help[0])y=paragraph(q,14,y,width,bottom,r->help,PLAIN_INK)+6;
 return y;
}
static void render_values(struct xz_canvas c, const struct xz_shell_menu *p, struct mstate s, int lit) {
 struct xz_canvas q=xz_sub(c,VALUE_X,U_Y,VALUE_W,PANE_H);
 xz_rect(q,0,0,VALUE_W,PANE_H,SH_BLACK);
 if(p->layout==XZ_SHELL_MENU_SUPPORT){
  if(p->report_qr_url[0]){
   render_page_text(q,p,12,154);
   if(xz_ui_support_render_qr_box)xz_ui_support_render_qr_box(c,VALUE_X+(VALUE_W-176)/2,U_Y+166,176,p->report_qr_url);
  }else render_page_text(q,p,12,PANE_H-4);
  return;
 }
 const struct xz_shell_menu_row *r=row_at(p,s.row);
 if(!r){render_page_text(q,p,12,PANE_H-4);return;}
 int y=0,bottom=PANE_H-4;
 if(has_values(p,r)){
  int n=choices_of(p,r),rows=value_rows(p,r),cur=current_of(p,r);
  for(int i=0;i<rows;i++){
   int index=s.value_scroll+i,ry=i*ROW_H;
   xz_rect(q,0,ry,VALUE_W,ROW_H-2,i&1?SH_BLACK:SH_CHROME);
   if(index>=n)continue;
   const struct xz_shell_menu_choice *ch=choice_at(p,r,index);
   int cursor=lit&&s.pane==PANE_VALUES&&index==s.value;
   if(cursor)xz_rect(q,0,ry,VALUE_W,ROW_H-2,SH_BLUE);
   xz_ring(q,22,ry+18,8,1,ch->available?SH_WHITE:READ_ONLY_GREY);
   if(index==cur)xz_disc(q,22,ry+18,4,cursor?SH_WHITE:SH_BLUE);
   int tx=40;
   if(ch->has_rgb){xz_rect(q,tx,ry+9,20,20,ch->rgb&0xffffff);xz_border(q,tx,ry+9,20,20,SH_RAIL);tx+=28;}
   text_fit(q,tx,ry+12,VALUE_W-tx-16,ch->label,SH_TEXT,ch->available||cursor?SH_WHITE:SH_DIM);
  }
  if(n>rows){
   int h=rows*ROW_H-6,thumb=sh_max(16,h*rows/n),travel=h-thumb;
   xz_rect(q,VALUE_W-5,2+travel*s.value_scroll/sh_max(1,n-rows),3,thumb,GREY_TEXT);
  }
  y=rows*ROW_H+8;
  xz_rect(q,12,y-4,VALUE_W-24,1,SH_RAIL);
  y=render_help(q,r,y+4,bottom);
  render_page_text(q,p,y,bottom);
  return;
 }
 /* An action, a link or a pad: its title, the page's state, then what it does. */
 xz_rect(q,0,0,VALUE_W,ROW_H-2,SH_CHROME);
 text_fit(q,14,12,VALUE_W-28,r->label,SH_TEXT,r->available?SH_WHITE:SH_DIM);
 if(r->value[0]&&r->kind!=XZ_SHELL_MENU_LINK)text_fit_right(q,VALUE_W-14,13,VALUE_W/2,r->value,SH_SMALL,GREY_TEXT);
 y=ROW_H+8;
 if(r->kind==XZ_SHELL_MENU_PAD)bottom=xz_shell_menu_pad_rect().y-U_Y-8;
 y=render_help(q,r,y,bottom);
 render_page_text(q,p,y,bottom);
 if(r->kind==XZ_SHELL_MENU_PAD){
  struct xz_shell_rect pr=xz_shell_menu_pad_rect();pr.x-=VALUE_X;pr.y-=U_Y;
  int held=s.touch_pane==PANE_PAD;
  xz_rect(q,pr.x,pr.y,pr.w,pr.h,held?SH_BLUE:SH_ROW_ALT);xz_border(q,pr.x,pr.y,pr.w,pr.h,r->available?SH_BLUE:SH_RAIL);
  text_fit_center(q,pr.x+pr.w/2,pr.y+pr.h/2-7,pr.w-16,r->label,SH_TEXT,r->available?SH_WHITE:SH_DIM);
 }
}
static void render_keyboard(struct xz_canvas c, const struct xz_shell_menu *p, struct mstate s, int lit) {
 xz_rect(c,LIST_X,U_Y,W-LIST_X,PANE_H,SH_BLACK);
 /* the text being typed, with a caret */
 xz_rect(c,KB_X,KB_FIELD_Y,KB_W,KB_FIELD_H,SH_CHROME);
 text_fit(c,KB_X+10,KB_FIELD_Y+5,KB_W-20,p->field_label[0]?p->field_label:p->title,SH_SMALL,GREY_TEXT);
 /* a long text shows its end, where the typing happens */
 const char *tail=p->field;
 while(*tail&&sh_text_width(measure,tail,SH_TEXT)>KB_W-40)tail+=utf8_step(tail);
 int w=text_fit(c,KB_X+10,KB_FIELD_Y+22,KB_W-34,tail,SH_TEXT,SH_WHITE);
 xz_rect(c,KB_X+12+w,KB_FIELD_Y+20,2,18,SH_BLUE);
 for(int r=0;r<rows_of(p);r++){
  const struct xz_shell_menu_row *row=&p->row[r];
  struct xz_shell_rect k=key_rect(p,r);
  if(!k.w)continue;
  int cursor=lit&&r==s.row;
  uint32_t fill=cursor?SH_BLUE:row->kind==XZ_SHELL_MENU_KEY?XZ_FLAGSHIP_TILE:SH_RAIL;
  xz_rect(c,k.x,k.y,k.w,k.h,fill);
  if(!cursor&&row->kind!=XZ_SHELL_MENU_KEY)xz_border(c,k.x,k.y,k.w,k.h,SH_BLUE);
  text_fit_center(c,k.x+k.w/2,k.y+(k.h-SH_CAP_TEXT)/2,k.w-8,row->label,SH_TEXT,row->available||cursor?SH_WHITE:SH_DIM);
 }
 /* the page's last result under the buttons */
 int y=KB_BUTTON_Y+KB_BUTTON_H+6;
 if(p->status[0])text_fit(c,KB_X,y,KB_W,p->status,SH_SMALL,tone_ink(p->status_tone));
}
void sh_menu_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct xz_shell_mods_tab *tab, int x, int right) {
 const struct xz_shell_menu *p=menu_of(m,tab);
 /* The title bar names the page and the pages above it. */
 char path[160];
 if(p&&p->path[0])snprintf(path,sizeof path,"%s / %s",p->path,p->title);
 else snprintf(path,sizeof path,"%s",p?p->title:tab&&tab->label?tab->label:"");
 text_fit(c,x,17,right-x,path,SH_SMALL,0xc8c8c8);
 if(!p){
  xz_rect(c,LIST_X,U_Y,W-LIST_X,PANE_H,SH_CHROME);
  return;
 }
 struct mstate s=settle(u,p);
 int lit=!u->mods_view.on_tabs;   /* the rail has the encoder: no content row is lit */
 if(!rows_of(p)){
  /* nothing to set here: the page's own lines across the content */
  struct xz_canvas q=xz_sub(c,LIST_X,U_Y,W-LIST_X,PANE_H);
  xz_rect(q,0,0,W-LIST_X,PANE_H,SH_CHROME);
  render_page_text(q,p,16,PANE_H-4);
  return;
 }
 if(p->layout==XZ_SHELL_MENU_ABOUT){xz_about_render(c,p->step,s.row,lit);return;}
 if(keyboard(p)){render_keyboard(c,p,s,lit);return;}
 render_list(c,p,s,lit);
 render_values(c,p,s,lit);
}

/* ---- input ---- */
static size_t emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a) {
 if(n<XZ_SHELL_ACTIONS)out[n++]=a;
 return n;
}
static size_t ask(struct xz_shell_action out[XZ_SHELL_ACTIONS], int op, int value, int deck) {
 return emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_MOD_MENU,deck,value,op});
}
/* A push may lead to a deeper page: remember where it came from. */
static void leave_mark(struct xz_shell *u, const struct xz_shell_menu *p, int row) {
 int d=p->depth;
 if(d<0||d>=XZ_SHELL_STACK||!row_at(p,row))return;
 u->menu_view.trail[d].page=p->page;u->menu_view.trail[d].key=p->row[row].key;
 u->menu_view.trail_count=d+1;
}
static size_t reopen(const struct xz_shell_mods_tab *tab, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 return ask(out,XZ_SHELL_MENU_OPEN,tab->page,-1);
}
size_t sh_menu_enter(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_mods_tab *tab, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_menu *p=menu_of(m,tab);
 if(!p)return reopen(tab,out);
 struct mstate s=settle(u,p);s.pane=PANE_ITEMS;
 pack(u,p,s);
 return 0;
}
/* Push on the cursor row: a CHOICE opens its values, a TOGGLE flips, anything else runs. */
static size_t push_row(struct mstate *s, const struct xz_shell_menu *p, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_menu_row *r=row_at(p,s->row);
 if(!r)return 0;
 if(r->kind==XZ_SHELL_MENU_CHOICE&&has_values(p,r)){
  int cur=current_of(p,r);
  s->pane=PANE_VALUES;s->value=cur<0?0:cur;s->value_scroll=keep_visible(0,s->value,choices_of(p,r),value_rows(p,r));
  return 0;
 }
 if(r->kind==XZ_SHELL_MENU_TOGGLE&&has_values(p,r))return ask(out,XZ_SHELL_MENU_ACTIVATE,s->row,current_of(p,r)==1?0:1);
 return ask(out,XZ_SHELL_MENU_ACTIVATE,s->row,-1);
}
static size_t pick_value(struct mstate *s, const struct xz_shell_menu *p, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_menu_row *r=row_at(p,s->row);
 s->pane=PANE_ITEMS;
 if(!r||s->value==current_of(p,r))return 0;
 return ask(out,XZ_SHELL_MENU_ACTIVATE,s->row,s->value);
}
static void move(struct mstate *s, const struct xz_shell_menu *p, int delta) {
 if(s->pane==PANE_VALUES){s->value=clamp(s->value+delta,0,choices_of(p,row_at(p,s->row))-1);return;}
 s->row=clamp(s->row+(delta>0?sh_min(delta,8):sh_max(delta,-8)),0,rows_of(p)-1);
}
/* Encoder hold: the first row of the next section (wrapping), or the next page of values. */
static void jump(struct mstate *s, const struct xz_shell_menu *p) {
 const struct xz_shell_menu_row *r=row_at(p,s->row);
 if(s->pane==PANE_VALUES){
  int n=choices_of(p,r),rows=value_rows(p,r);
  s->value=s->value>=n-1?0:sh_min(s->value+rows,n-1);s->value_scroll=s->value;
  return;
 }
 int n=rows_of(p),start=s->row;
 for(int k=1;k<=n;k++){
  int at=(start+k)%n;
  if(p->row[at].section!=r->section||at==0){s->row=at;return;}
 }
}
static int list_row_at(const struct xz_shell_menu *p, struct mstate s, int x, int y) {
 if(p->layout==XZ_SHELL_MENU_ABOUT){
  for(int r=0;r<rows_of(p);r++){struct xz_ui_widget w={0};xz_about_place(&w,r);if(x>=w.x&&x<w.x+w.w&&y>=w.y&&y<w.y+w.h)return r;}
  return -2;
 }
 if(keyboard(p)){
  for(int r=0;r<rows_of(p);r++)if(sh_inside(key_rect(p,r),x,y))return r;
  return -2;
 }
 struct mlist l=list_of(p);
 if(x<LIST_X||x>=LIST_X+LIST_W||y<U_Y+l.first_slot*ROW_H||y>=U_Y+PANE_H)return -2;
 int index=s.item_scroll+(y-U_Y)/ROW_H-l.first_slot;
 return index>=0&&index<l.n?l.entry[index]:-2;
}
static int value_at(const struct xz_shell_menu *p, struct mstate s, int x, int y) {
 const struct xz_shell_menu_row *r=row_at(p,s.row);
 if(keyboard(p)||!has_values(p,r)||x<VALUE_X||y<U_Y)return -1;
 int i=(y-U_Y)/ROW_H;
 if(i>=value_rows(p,r))return -1;
 int index=s.value_scroll+i;
 return index<choices_of(p,r)?index:-1;
}
static int pad_x(int x) { struct xz_shell_rect r=xz_shell_menu_pad_rect();return clamp((x-r.x)*1000/sh_max(1,r.w-1),0,1000); }
static int pad_y(int y) { struct xz_shell_rect r=xz_shell_menu_pad_rect();return clamp((y-r.y)*1000/sh_max(1,r.h-1),0,1000); }
size_t sh_menu_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_mods_tab *tab, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_menu *p=menu_of(m,tab);
 if(!p){
  /* The page is not read yet (or the runtime moved on): ask for it again on a push or a touch. */
  if(e->kind==XZ_SHELL_TOUCH_DOWN)u->mods_view.on_tabs=0;
  return e->kind==XZ_SHELL_ENCODER_PUSH||e->kind==XZ_SHELL_TOUCH_DOWN?reopen(tab,out):0;
 }
 struct mstate s=settle(u,p);
 size_t n=0;
 if(!rows_of(p)){pack(u,p,s);return 0;}
 switch(e->kind){
 case XZ_SHELL_ENCODER_TURN:if(e->delta)move(&s,p,e->delta);break;
 case XZ_SHELL_ENCODER_PUSH:n=s.pane==PANE_VALUES?pick_value(&s,p,out):push_row(&s,p,out);break;
 case XZ_SHELL_ENCODER_HOLD:jump(&s,p);break;
 case XZ_SHELL_TOUCH_DOWN:{
  u->mods_view.on_tabs=0;
  u->down=1;u->down_region=XZ_SHELL_TOUCH_OTHER;u->down_deck=e->y;
  s.armed=s.dragged=0;s.touch_pane=-1;
  const struct xz_shell_menu_row *r=row_at(p,s.row);
  if(!keyboard(p)&&r&&r->kind==XZ_SHELL_MENU_PAD&&sh_inside(xz_shell_menu_pad_rect(),e->x,e->y)){
   s.touch_pane=PANE_PAD;
   n=emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_MOD_MENU,pad_x(e->x)*1024+pad_y(e->y),s.row,XZ_SHELL_MENU_PRESS});
   break;
  }
  int row=list_row_at(p,s,e->x,e->y);
  if(row>=0){
   s.touch_pane=PANE_ITEMS;
   /* A tap runs the row on release; moving cancels activation. */
   if(row!=s.row){s.value=0;s.value_scroll=0;}
   s.row=row;s.pane=PANE_ITEMS;s.armed=1;
   break;
  }
  int v=value_at(p,s,e->x,e->y);
  if(v>=0){
   s.touch_pane=PANE_VALUES;
   s.value=v;s.pane=PANE_VALUES;s.armed=1;
  }
  break;
 }
 case XZ_SHELL_TOUCH_MOVE:{
  if(!u->down)break;
  if(s.touch_pane==PANE_PAD){
   n=emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_MOD_MENU,pad_x(e->x)*1024+pad_y(e->y),s.row,XZ_SHELL_MENU_MOVE});
   break;
  }
  int dy=e->y-u->down_deck;
  if(!s.dragged&&(dy>=8||dy<=-8))s.dragged=1;
  if(!s.dragged||keyboard(p))break;
  int steps=dy/ROW_H;
  if(!steps)break;
  u->down_deck+=steps*ROW_H;
  if(s.touch_pane==PANE_VALUES){
   const struct xz_shell_menu_row *r=row_at(p,s.row);int k=choices_of(p,r),rows=value_rows(p,r);
   s.value_scroll=clamp(s.value_scroll-steps,0,sh_max(0,k-rows));
   s.value=clamp(s.value,s.value_scroll,s.value_scroll+rows-1);
  }else if(s.touch_pane==PANE_ITEMS){
   struct mlist l=list_of(p);
   s.item_scroll=clamp(s.item_scroll-steps,0,sh_max(0,l.n-l.slots));
   /* the cursor stays on a row inside the window */
   int at=list_index(&l,s.row);
   if(at<s.item_scroll||at>=s.item_scroll+l.slots){
    int want=at<s.item_scroll?s.item_scroll:s.item_scroll+l.slots-1;
    for(int d=0;d<l.slots;d++){
     int i=at<s.item_scroll?want+d:want-d;
     if(i>=0&&i<l.n&&l.entry[i]>=0){s.row=l.entry[i];break;}
    }
   }
  }
  break;
 }
 case XZ_SHELL_TOUCH_UP:
  if(u->down&&s.touch_pane==PANE_PAD)
   n=emit(out,0,(struct xz_shell_action){XZ_SHELL_ACT_MOD_MENU,pad_x(e->x)*1024+pad_y(e->y),s.row,XZ_SHELL_MENU_RELEASE});
  else if(u->down&&s.armed&&!s.dragged){
   if(s.touch_pane==PANE_ITEMS&&list_row_at(p,s,e->x,e->y)==s.row)n=push_row(&s,p,out);
   else if(s.touch_pane==PANE_VALUES&&value_at(p,s,e->x,e->y)==s.value)n=pick_value(&s,p,out);
  }
  u->down=0;u->down_region=XZ_SHELL_TOUCH_NONE;s.touch_pane=-1;s.armed=s.dragged=0;
  break;
 default:break;
 }
 for(size_t i=0;i<n;i++)if(out[i].kind==XZ_SHELL_ACT_MOD_MENU&&out[i].id==XZ_SHELL_MENU_ACTIVATE)leave_mark(u,p,out[i].value);
 /* Settle again so the cursor row stays in view; the gesture state survives it. */
 pack(u,p,s);
 struct mstate again=settle(u,p);
 again.touch_pane=s.touch_pane;again.armed=s.armed;again.dragged=s.dragged;
 pack(u,p,again);
 return n;
}
size_t sh_menu_back(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_mods_tab *tab, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 const struct xz_shell_menu *p=menu_of(m,tab);
 if(!p){xz_shell_mods_close(u);return 0;}
 struct mstate s=settle(u,p);
 if(s.pane==PANE_VALUES){s.pane=PANE_ITEMS;pack(u,p,s);return 0;}
 if(p->depth>0){pack(u,p,s);return ask(out,XZ_SHELL_MENU_BACK,p->page,-1);}
 pack(u,p,s);
 xz_shell_mods_close(u);
 return 0;
}
