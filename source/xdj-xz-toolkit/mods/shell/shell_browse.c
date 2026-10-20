/* BROWSE, PLAYLIST, TAG LIST and SEARCH at 800x480, after the flagship
   player's browse screen (the 2026-10-18 flagship UI study, sections 3.2,
   3.3 and 6.2): category rail, one-level breadcrumb header, sortable column
   row, 38 px rows with a LOAD chip in the touched row, a PREVIEW column and
   an INFO pane, a letter/page jump overlay, an on-screen keyboard for SEARCH,
   and the two-deck status strip at the bottom. */
#include "shell_internal.h"
#include "shell_browse.h"
#include "shell_source.h"
#include "shell_settings.h"
#include <stdio.h>
#include <string.h>

enum {
 W=XZ_SHELL_WIDTH, HEAD_H=44, HEAD_HIT=36, SRC_W=64, RAIL_W=64, RAIL_ITEM=47, RAIL_SLOTS=8,
 COL_Y=44, COL_H=36, LIST_Y=80, ROW_H=38, ROWS=9, SEARCH_ROWS=4,
 STRIP_Y=422, STRIP_H=XZ_SHELL_HEIGHT-STRIP_Y, HALF_W=W/2,
 KB_Y=232, KB_ROW=47, KB_KEY_H=44, KB_X=8, KB_W=W-16, KB_UNITS=24,
 INFO_W=264, TILE_W=70, BACK_X=68, BACK_S=32, TITLE_MIN=200, LOAD_W=56, LOAD_DECK_W=34,
 UP_DEPTH=8, SLOTS=4, DRAG_START=10, FLICK_STEP=12,
 HEAD_RIGHT=XZ_SHELL_HEADER_X-4   /* the mod header buttons own the bar right of this */
};
enum { SH_RAIL_SEL=XZ_FLAGSHIP_RAIL_SELECTED, SH_TILE=XZ_FLAGSHIP_TILE, SH_UNAVAILABLE=0x6a6a6a, SH_STAR_OFF=0x5a5a5a };

/* ---- module state ----
   The view (cursor, scroll) lives in the shell's stack; focus==0 marks a view
   this module has not started yet. Everything else a list needs between
   events is here: one shell drives these screens at a time. */
enum target { T_NONE, T_LIST, T_RAIL, T_KEY, T_OTHER };
enum { FOCUS_LIST=1, FOCUS_RAIL=2 };
static struct {
 int touched, jump, pressed;
 int hide_preview, side_open, keyboard_hidden;
 char query[XZ_SHELL_BROWSE_QUERY];
 int32_t last_request;
 int rail_first, rail_cursor;
 enum target target;
 int down_y, last_y, last_step, dragging, confirm, down_scroll, down_rail, down_rail_first, prev_cursor, prev_touched;
 struct { int valid; uint32_t list; int cursor, scroll; } memory[SLOTS];
 struct { int cursor, scroll; } up[SLOTS][UP_DEPTH];
 int up_n[SLOTS];
} st;

void xz_shell_browse_reset(void) { memset(&st,0,sizeof st);st.touched=-1;st.pressed=-1;st.last_request=-1; }
const char *xz_shell_browse_query(void) { return st.query; }
int xz_shell_browse_touched(void) { return st.touched; }

static int list_screen(enum xz_shell_screen s) { return s>=XZ_SHELL_BROWSE&&s<=XZ_SHELL_SEARCH; }
/* The list narrows while a panel is open; the panel lives in the shell struct, this mirror serves the
   geometry helpers that see only the screen. Set at every entry point. */
static void sync(const struct xz_shell *u) {
 st.side_open=u&&u->depth>=1&&u->depth<=XZ_SHELL_STACK&&list_screen(xz_shell_screen(u))&&u->panel.kind!=XZ_SHELL_PANEL_NONE;
}
static int naming(const struct xz_shell *u);
static int panel_open(const struct xz_shell *u);
static int32_t grab_source_row(const struct xz_shell *u, int32_t shown);
static void render_panel(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c);
static int slot_of(enum xz_shell_screen s) { return list_screen(s)?(int)s-(int)XZ_SHELL_BROWSE:0; }
static int bknown(const struct xz_shell_browse_model *b, enum xz_shell_browse_field f) { return b&&((b->known>>f)&1); }
static int rknown(const struct xz_shell_browse_row *r, enum xz_shell_browse_row_field f) { return (r->known>>f)&1; }
static int matches(const struct xz_shell_browse_model *b, enum xz_shell_screen s) { return b&&b->screen==s; }
/* Rows the cursor may address: the total, or the end of the window while the total is unknown. */
static int32_t row_limit(const struct xz_shell_browse_model *b, enum xz_shell_screen s) {
 if(!matches(b,s))return 0;
 if(bknown(b,XZ_SHELL_BF_TOTAL))return b->total>0?b->total:0;
 return b->rows&&b->window_start>=0&&b->window_count>0?b->window_start+b->window_count:0;
}
static const struct xz_shell_browse_row *row_at(const struct xz_shell_browse_model *b, enum xz_shell_screen s, int32_t index) {
 if(!matches(b,s)||!b->rows||b->window_count<=0||index<b->window_start||index-b->window_start>=b->window_count)return NULL;
 if(bknown(b,XZ_SHELL_BF_TOTAL)&&index>=b->total)return NULL;
 return &b->rows[index-b->window_start];
}
static int depth_of(const struct xz_shell_browse_model *b) { return bknown(b,XZ_SHELL_BF_LEVEL)&&b->depth>0?b->depth:0; }

/* ---- geometry ---- */
static const struct xz_shell_view *top_view(const struct xz_shell *u) { return &u->stack[u->depth-1]; }
static int keyboard_shown(const struct xz_shell *u) {
 return naming(u)||(xz_shell_screen(u)==XZ_SHELL_SEARCH&&(top_view(u)->focus?!st.keyboard_hidden:1));
}
int xz_shell_browse_rows_visible(const struct xz_shell *u) { return keyboard_shown(u)?SEARCH_ROWS:ROWS; }
static int list_x(enum xz_shell_screen s) { return s==XZ_SHELL_BROWSE?RAIL_W:0; }
static int list_w(enum xz_shell_screen s) { return W-list_x(s)-(st.side_open?INFO_W:0); }

enum { COL_PREVIEW=XZ_SHELL_COL_COUNT, COL_KINDS };
struct col { int kind, x, w; };
static const int col_width[COL_KINDS]={
 [XZ_SHELL_COL_NUMBER]=60,[XZ_SHELL_COL_TITLE]=0,[XZ_SHELL_COL_ARTIST]=170,[XZ_SHELL_COL_BPM]=76,
 [XZ_SHELL_COL_KEY]=60,[XZ_SHELL_COL_RATING]=78,[XZ_SHELL_COL_COLOR]=72,[XZ_SHELL_COL_TIME]=64,
 [XZ_SHELL_COL_ALBUM]=150,[XZ_SHELL_COL_GENRE]=110,[XZ_SHELL_COL_DATE_ADDED]=104,[COL_PREVIEW]=112
};
/* Least useful first; TITLE always stays. */
static const int drop_order[]={XZ_SHELL_COL_DATE_ADDED,XZ_SHELL_COL_GENRE,XZ_SHELL_COL_ALBUM,XZ_SHELL_COL_COLOR,XZ_SHELL_COL_RATING,
 XZ_SHELL_COL_TIME,XZ_SHELL_COL_ARTIST,COL_PREVIEW,XZ_SHELL_COL_NUMBER,XZ_SHELL_COL_BPM,XZ_SHELL_COL_KEY};
#define MAX_COLS (XZ_SHELL_BROWSE_COLUMNS+2)
/* PREVIEW only over tracks: a folder level has nothing to preview. */
static int window_has_tracks(const struct xz_shell_browse_model *b, enum xz_shell_screen s) {
 if(!matches(b,s)||!b->rows)return 0;
 for(int i=0;i<b->window_count&&i<XZ_SHELL_BROWSE_WINDOW;i++)if(row_at(b,s,b->window_start+i)&&b->rows[i].kind==XZ_SHELL_ROW_TRACK)return 1;
 return 0;
}
static int layout_columns(const struct xz_shell_browse_model *b, enum xz_shell_screen s, struct col out[MAX_COLS]) {
 int kinds[MAX_COLS],n=0,have_title=0,seen[COL_KINDS]={0};
 if(!st.hide_preview&&window_has_tracks(b,s)){kinds[n++]=COL_PREVIEW;seen[COL_PREVIEW]=1;}
 int first=n;
 if(bknown(b,XZ_SHELL_BF_COLUMNS)&&b->column_count>0){
  for(int i=0;i<b->column_count&&i<XZ_SHELL_BROWSE_COLUMNS;i++){
   unsigned k=(unsigned)b->columns[i];
   if(k>=XZ_SHELL_COL_COUNT||seen[k])continue;
   seen[k]=1;kinds[n++]=(int)k;have_title|=k==XZ_SHELL_COL_TITLE;
  }
 }else{
  static const int defaults[]={XZ_SHELL_COL_NUMBER,XZ_SHELL_COL_TITLE,XZ_SHELL_COL_BPM,XZ_SHELL_COL_KEY};
  for(size_t i=0;i<sizeof defaults/sizeof *defaults;i++)kinds[n++]=defaults[i];
  have_title=1;
 }
 if(!have_title){
  int at=first+(n>first&&kinds[first]==XZ_SHELL_COL_NUMBER);
  for(int i=n;i>at;i--)kinds[i]=kinds[i-1];
  kinds[at]=XZ_SHELL_COL_TITLE;n++;
 }
 int width=list_w(s);
 for(size_t d=0;;){
  int used=0;
  for(int i=0;i<n;i++)used+=col_width[kinds[i]];
  if(width-used>=TITLE_MIN||d>=sizeof drop_order/sizeof *drop_order)break;
  int k=drop_order[d++];
  for(int i=0;i<n;i++)if(kinds[i]==k){for(int j=i;j<n-1;j++)kinds[j]=kinds[j+1];n--;break;}
 }
 int used=0;
 for(int i=0;i<n;i++)used+=col_width[kinds[i]];
 int x=list_x(s);
 for(int i=0;i<n;i++){
  int w=kinds[i]==XZ_SHELL_COL_TITLE?sh_max(0,width-used):col_width[kinds[i]];
  out[i]=(struct col){kinds[i],x,w};x+=w;
 }
 return n;
}
static struct col title_col(const struct xz_shell_browse_model *b, enum xz_shell_screen s) {
 struct col cols[MAX_COLS];int n=layout_columns(b,s,cols);
 for(int i=0;i<n;i++)if(cols[i].kind==XZ_SHELL_COL_TITLE)return cols[i];
 return (struct col){XZ_SHELL_COL_TITLE,list_x(s),list_w(s)};
}

/* Keyboard: four rows in half-key units, 24 to a row. */
#define KB_DEL '\x01'
#define KB_CLEAR '\x02'
#define KB_SEARCH '\x03'
static const struct { const char *label; char code; unsigned char row, start, width; } keys[XZ_SHELL_BROWSE_KEYS]={
 {"1",'1',0,0,2},{"2",'2',0,2,2},{"3",'3',0,4,2},{"4",'4',0,6,2},{"5",'5',0,8,2},
 {"6",'6',0,10,2},{"7",'7',0,12,2},{"8",'8',0,14,2},{"9",'9',0,16,2},{"0",'0',0,18,2},{"DELETE",KB_DEL,0,20,4},
 {"Q",'Q',1,0,2},{"W",'W',1,2,2},{"E",'E',1,4,2},{"R",'R',1,6,2},{"T",'T',1,8,2},
 {"Y",'Y',1,10,2},{"U",'U',1,12,2},{"I",'I',1,14,2},{"O",'O',1,16,2},{"P",'P',1,18,2},{"CLEAR",KB_CLEAR,1,20,4},
 {"A",'A',2,1,2},{"S",'S',2,3,2},{"D",'D',2,5,2},{"F",'F',2,7,2},{"G",'G',2,9,2},
 {"H",'H',2,11,2},{"J",'J',2,13,2},{"K",'K',2,15,2},{"L",'L',2,17,2},{"SEARCH",KB_SEARCH,2,19,5},
 {"Z",'Z',3,2,2},{"X",'X',3,4,2},{"C",'C',3,6,2},{"V",'V',3,8,2},{"B",'B',3,10,2},
 {"N",'N',3,12,2},{"M",'M',3,14,2},{"SPACE",' ',3,16,8},
};
const char *xz_shell_browse_key_label(int i) { return i>=0&&i<XZ_SHELL_BROWSE_KEYS?keys[i].label:""; }
static struct xz_shell_rect key_rect(int i) {
 int x0=KB_X+keys[i].start*KB_W/KB_UNITS,x1=KB_X+(keys[i].start+keys[i].width)*KB_W/KB_UNITS-4;
 return (struct xz_shell_rect){x0,KB_Y+2+keys[i].row*KB_ROW,x1-x0,KB_KEY_H};
}

static int filter_active(const struct xz_shell_filter *f);
static void filter_summary(const struct xz_shell_filter *f, char *out, size_t cap);
struct xz_shell_rect xz_shell_browse_rect(const struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_browse_region r, int index) {
 static const struct xz_shell_rect none={0,0,0,0};
 if(!u||u->depth<1||u->depth>XZ_SHELL_STACK)return none;
 sync(u);
 enum xz_shell_screen s=xz_shell_screen(u);
 int filters=m&&(m->capabilities&XZ_SHELL_CAP_FILTER);
 int tiles_x=HEAD_RIGHT-(filters?3:2)*(TILE_W+2)+2;
 const struct xz_shell_browse_model *b=m?m->browse:NULL;
 int rows=xz_shell_browse_rows_visible(u);
 switch(r){
 case XZ_SHELL_BR_SOURCE:return (struct xz_shell_rect){0,0,SRC_W,HEAD_H};
 case XZ_SHELL_BR_BACK:return (struct xz_shell_rect){SRC_W,0,BACK_S+12,HEAD_HIT};
 case XZ_SHELL_BR_FIELD:{
  if(s!=XZ_SHELL_SEARCH)return none;
  struct xz_shell_rect chip=xz_shell_browse_rect(u,m,XZ_SHELL_BR_FILTER_CHIP,0);
  return (struct xz_shell_rect){112,0,(chip.w?chip.x:tiles_x)-8-112,HEAD_HIT};
 }
 case XZ_SHELL_BR_PREVIEW:return (struct xz_shell_rect){HEAD_RIGHT-2*TILE_W-2,0,TILE_W,HEAD_HIT};
 case XZ_SHELL_BR_INFO:return (struct xz_shell_rect){HEAD_RIGHT-TILE_W,0,TILE_W,HEAD_HIT};
 case XZ_SHELL_BR_FILTER:return filters?(struct xz_shell_rect){HEAD_RIGHT-3*TILE_W-4,0,TILE_W,HEAD_HIT}:none;
 case XZ_SHELL_BR_FILTER_CHIP:{
  if(!filters||!filter_active(&u->filter))return none;
  char text[48];filter_summary(&u->filter,text,sizeof text);
  int w=sh_min(200,sh_text_width((struct xz_canvas){NULL,W,W,XZ_SHELL_HEIGHT,XZ_THEME_FLAGSHIP},text,SH_SMALL)+38);
  return (struct xz_shell_rect){tiles_x-8-w,4,w,HEAD_HIT-8};
 }
 case XZ_SHELL_BR_PANEL:return st.side_open&&u->panel.kind!=XZ_SHELL_PANEL_INFO?(struct xz_shell_rect){W-INFO_W,HEAD_H,INFO_W,STRIP_Y-HEAD_H}:none;
 case XZ_SHELL_BR_PANEL_ROW:
  if(!st.side_open||u->panel.kind==XZ_SHELL_PANEL_INFO||index<0)return none;
  return (struct xz_shell_rect){W-INFO_W,HEAD_H+36+index*34,INFO_W,34};
 case XZ_SHELL_BR_RAIL:
  if(s!=XZ_SHELL_BROWSE||index<0||index>=RAIL_SLOTS)return none;
  return (struct xz_shell_rect){0,HEAD_H+index*RAIL_ITEM,RAIL_W,RAIL_ITEM};
 case XZ_SHELL_BR_COLUMN:{
  struct col cols[MAX_COLS];int n=layout_columns(b,s,cols);
  if(index<0||index>=n)return none;
  /* 36 px drawn, padded up to 44 into the header's lower margin. */
  return (struct xz_shell_rect){cols[index].x,HEAD_HIT,cols[index].w,LIST_Y-HEAD_HIT};
 }
 case XZ_SHELL_BR_LIST:return (struct xz_shell_rect){list_x(s),LIST_Y,list_w(s),rows*ROW_H};
 case XZ_SHELL_BR_ROW:
  if(index<0||index>=rows)return none;
  return (struct xz_shell_rect){list_x(s),LIST_Y+index*ROW_H,list_w(s),ROW_H};
 case XZ_SHELL_BR_LOAD:{
  const struct xz_shell_view *v=top_view(u);
  int i=v->focus?st.touched-v->scroll:-1;
  if(index<0||index>sh_deck_count(m)||!v->focus||st.touched<0||st.touched!=v->cursor||i<0||i>=rows)return none;
  const struct xz_shell_browse_row *t_row=row_at(b,s,st.touched);
  if(!t_row||t_row->kind!=XZ_SHELL_ROW_TRACK)return none;
  struct col t=title_col(b,s);
  int right=t.x+t.w-6,x=right-LOAD_W-sh_deck_count(m)*LOAD_DECK_W;
  int y=LIST_Y+i*ROW_H;
  if(index==0)return (struct xz_shell_rect){x,y,LOAD_W,ROW_H};
  return (struct xz_shell_rect){x+LOAD_W+(index-1)*LOAD_DECK_W,y,LOAD_DECK_W,ROW_H};
 }
 case XZ_SHELL_BR_INFO_PANE:return u->panel.kind==XZ_SHELL_PANEL_INFO?(struct xz_shell_rect){W-INFO_W,HEAD_H,INFO_W,STRIP_Y-HEAD_H}:none;
 case XZ_SHELL_BR_KEY:return keyboard_shown(u)&&index>=0&&index<XZ_SHELL_BROWSE_KEYS?key_rect(index):none;
 case XZ_SHELL_BR_STRIP:return index>=0&&index<sh_deck_count(m)?sh_strip_cell(m,(struct xz_shell_rect){0,STRIP_Y,W,STRIP_H},index):none;
 }
 return none;
}

/* ---- view: cursor and scroll ---- */
struct sess { int cursor, scroll, touched, jump, pressed; };
static void clamp_view(int *cursor, int *scroll, int32_t limit, int rows) {
 int max_cursor=limit>0?(int)limit-1:0,max_scroll=limit>rows?(int)limit-rows:0;
 if(*cursor>max_cursor)*cursor=max_cursor;
 if(*cursor<0)*cursor=0;
 if(*scroll>max_scroll)*scroll=max_scroll;
 if(*scroll<0)*scroll=0;
}
static int remembered(const struct xz_shell_browse_model *b, enum xz_shell_screen s) {
 int k=slot_of(s);
 return st.memory[k].valid&&bknown(b,XZ_SHELL_BF_LIST)&&matches(b,s)&&st.memory[k].list==b->list_id;
}
/* What render shows: the view, or for a view not started yet the remembered position. */
static struct sess session(const struct xz_shell *u, const struct xz_shell_browse_model *b) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_view *v=top_view(u);
 struct sess r={v->cursor,v->scroll,-1,0,-1};
 if(v->focus){r.touched=st.touched;r.jump=st.jump;r.pressed=st.pressed;}
 else if(remembered(b,s)){r.cursor=st.memory[slot_of(s)].cursor;r.scroll=st.memory[slot_of(s)].scroll;}
 clamp_view(&r.cursor,&r.scroll,row_limit(b,s),xz_shell_browse_rows_visible(u));
 return r;
}

const struct xz_shell_browse_row *xz_shell_browse_highlight(const struct xz_shell *u, const struct xz_shell_model *m) {
 enum xz_shell_screen s=xz_shell_screen(u);
 if(!list_screen(s))return NULL;
 struct sess r=session(u,m->browse);
 return row_at(m->browse,s,r.cursor);
}

/* ---- small marks ---- */
static void folder_icon(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_rect(c,x,y,6,2,ink);
 xz_border(c,x,y+2,15,11,ink);
}
static void check_icon(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_line16(c,(x+1)*16,(y+7)*16,(x+5)*16,(y+11)*16,32,ink);
 xz_line16(c,(x+5)*16,(y+11)*16,(x+13)*16,(y+2)*16,32,ink);
}
static void list_icon(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_border(c,x,y,16,16,ink);
 for(int i=0;i<3;i++){xz_rect(c,x+3,y+4+i*3,2,2,ink);xz_rect(c,x+6,y+4+i*3,7,1,ink);}
}
static void back_arrow(struct xz_canvas c, int x, int y, uint32_t ink) {
 xz_border(c,x,y,BACK_S,BACK_S,0x6a6a6a);
 int cy=y+BACK_S/2;
 xz_line16(c,(x+9)*16,cy*16,(x+23)*16,cy*16,32,ink);
 xz_line16(c,(x+9)*16,cy*16,(x+15)*16,(cy-6)*16,32,ink);
 xz_line16(c,(x+9)*16,cy*16,(x+15)*16,(cy+6)*16,32,ink);
}
static const uint16_t star_bits[9]={0x010,0x010,0x038,0x1ff,0x0fe,0x07c,0x06c,0x0c6,0x082};
static void star(struct xz_canvas c, int x, int y, uint32_t ink) {
 for(int r=0;r<9;r++)for(int b=0;b<9;b++)if(star_bits[r]&(1u<<(8-b)))xz_rect(c,x+b,y+r,1,1,ink);
}
static void star_outline(struct xz_canvas c, int cx, int cy, int radius, uint32_t ink) {
 int px=0,py=0;
 for(int i=0;i<=10;i++){
  int deg=-90+i*36,rr=i%2?radius*2/5:radius;
  int x=cx*16+rr*16*xz_cos1024(deg)/1024,y=cy*16+rr*16*xz_sin1024(deg)/1024;
  if(i)xz_line16(c,px,py,x,y,16,ink);
  px=x;py=y;
 }
}
/* A short label magnified from the text face, for the jump overlay. */
static void big_text(struct xz_canvas c, int cx, int cy, const char *s, int scale, uint32_t ink) {
 enum { CW=40, CH=24 };
 uint16_t cell[CW*CH];
 memset(cell,0,sizeof cell);
 struct xz_canvas g={cell,CW,CW,CH,c.theme};
 int w=sh_text_width(g,s,SH_TEXT);
 if(w>CW-2)w=CW-2;
 sh_text_fit(g,1,5,CW-2,s,SH_TEXT,SH_WHITE);
 int x0=cx-(w+2)*scale/2,y0=cy-(SH_CAP_TEXT/2+5)*scale;
 /* Bilinear magnification with a contrast curve: smooth edges, solid strokes. */
 for(int oy=0;oy<CH*scale;oy++)for(int ox=0;ox<(w+2)*scale;ox++){
  int sx=(ox*256+128)/scale-128+256,sy=(oy*256+128)/scale-128+256;
  int ix=sx/256-1,iy=sy/256-1,fx=sx%256,fy=sy%256,a[4];
  for(int k=0;k<4;k++){
   int px=ix+(k&1),py=iy+(k>>1);
   a[k]=px>=0&&py>=0&&px<CW&&py<CH?((cell[py*CW+px]>>5)&63)*255/63:0;
  }
  int top=a[0]*(256-fx)+a[1]*fx,bottom=a[2]*(256-fx)+a[3]*fx;
  int v=(top*(256-fy)+bottom*fy)>>16;
  v=(v-35)*255/100;
  if(v>255)v=255;
  if(v>8)xz_plot(c,x0+ox,y0+oy,ink,v);
 }
}

/* ---- rail icons ---- */
static const char *const category_label[XZ_SHELL_CAT_COUNT]={
 "ARTIST","ALBUM","TRACK","KEY","BPM","GENRE","RATING","COLOR","PLAYLIST","FOLDER","HISTORY","MATCHING"
};
static void rail_icon(struct xz_canvas c, enum xz_shell_browse_category k, int cx, int y, uint32_t ink) {
 switch(k){
 case XZ_SHELL_CAT_ARTIST:
  xz_ring(c,cx,y+5,4,1,ink);
  xz_ring(xz_sub(c,cx-8,y+11,17,6),8,8,7,1,ink);
  break;
 case XZ_SHELL_CAT_ALBUM:xz_ring(c,cx,y+8,8,1,ink);xz_ring(c,cx,y+8,3,1,ink);break;
 case XZ_SHELL_CAT_TRACK:sh_note(c,cx-5,y+2,ink);break;
 case XZ_SHELL_CAT_KEY:xz_border(c,cx-10,y,21,16,ink);sh_text_center(c,cx,y+4,"b#",SH_SMALL,ink);break;
 case XZ_SHELL_CAT_BPM:xz_border(c,cx-13,y,27,16,ink);sh_text_center(c,cx+1,y+4,"BPM",SH_SMALL,ink);break;
 case XZ_SHELL_CAT_GENRE:
  xz_line16(c,(cx-8)*16,(y+3)*16,(cx+1)*16,(y+3)*16,16,ink);
  xz_line16(c,(cx+1)*16,(y+3)*16,(cx+8)*16,(y+10)*16,16,ink);
  xz_line16(c,(cx+8)*16,(y+10)*16,(cx+2)*16,(y+16)*16,16,ink);
  xz_line16(c,(cx+2)*16,(y+16)*16,(cx-8)*16,(y+6)*16,16,ink);
  xz_line16(c,(cx-8)*16,(y+6)*16,(cx-8)*16,(y+3)*16,16,ink);
  xz_disc(c,cx-4,y+6,1,ink);
  break;
 case XZ_SHELL_CAT_RATING:star_outline(c,cx,y+9,9,ink);break;
 case XZ_SHELL_CAT_COLOR:
  for(int i=0;i<4;i++)xz_rect(c,cx-8+(i%2)*9,y+(i/2)*9,7,7,ink);
  break;
 case XZ_SHELL_CAT_PLAYLIST:list_icon(c,cx-8,y,ink);break;
 case XZ_SHELL_CAT_FOLDER:folder_icon(c,cx-8,y+1,ink);xz_rect(c,cx-7,y+5,13,1,ink);break;
 case XZ_SHELL_CAT_HISTORY:
  list_icon(c,cx-10,y,ink);
  xz_disc(c,cx+6,y+11,6,SH_RAIL);xz_ring(c,cx+6,y+11,5,1,ink);
  xz_rect(c,cx+6,y+8,1,4,ink);xz_rect(c,cx+6,y+11,3,1,ink);
  break;
 case XZ_SHELL_CAT_MATCHING:xz_ring(c,cx-4,y+8,6,1,ink);xz_ring(c,cx+4,y+8,6,1,ink);break;
 default:break;
 }
}
/* The rail's last entry opens SEARCH: the XZ has no SEARCH key. */
enum { RAIL_SEARCH=XZ_SHELL_CAT_COUNT, RAIL_MAX=XZ_SHELL_CAT_COUNT+1 };
static int search_reachable(const struct xz_shell_model *m) { return m->browse&&(m->capabilities&XZ_SHELL_CAP_SEARCH); }
static int rail_entries(const struct xz_shell_model *m, int out[RAIL_MAX]) {
 const struct xz_shell_browse_model *b=m->browse;
 int n=0;
 if(b)for(int k=0;k<XZ_SHELL_CAT_COUNT;k++)if((b->category_known>>k)&1)out[n++]=k;
 if(search_reachable(m))out[n++]=RAIL_SEARCH;
 return n;
}
static int rail_first(int entries) { return sh_max(0,sh_min(st.rail_first,entries-RAIL_SLOTS)); }
static int rail_current(const struct xz_shell_browse_model *b, const int *entries, int n) {
 for(int i=0;i<n;i++)if(bknown(b,XZ_SHELL_BF_CATEGORY)&&entries[i]==(int)b->category)return i;
 return 0;
}
static int rail_focused(const struct xz_shell *u) {
 return xz_shell_screen(u)==XZ_SHELL_BROWSE&&top_view(u)->focus!=FOCUS_LIST;
}
int sh_browse_at_root(const struct xz_shell *u) { return u->depth>=1&&u->depth<=XZ_SHELL_STACK&&rail_focused(u); }
static void rail_follow(int n) {
 st.rail_cursor=sh_max(0,sh_min(n-1,st.rail_cursor));
 if(st.rail_cursor<st.rail_first)st.rail_first=st.rail_cursor;
 if(st.rail_cursor>=st.rail_first+RAIL_SLOTS)st.rail_first=st.rail_cursor-RAIL_SLOTS+1;
 st.rail_first=rail_first(n);
}
static void render_rail(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 const struct xz_shell_browse_model *b=m->browse;
 int entries[RAIL_MAX],n=rail_entries(m,entries),first=rail_first(n);
 int focused=rail_focused(u),cursor=top_view(u)->focus?st.rail_cursor:rail_current(b,entries,n);
 if(!top_view(u)->focus)first=sh_max(0,sh_min(cursor,n-RAIL_SLOTS));
 xz_rect(c,0,HEAD_H,RAIL_W,STRIP_Y-HEAD_H,SH_RAIL);
 for(int slot=0;slot<RAIL_SLOTS&&first+slot<n;slot++){
  int k=entries[first+slot],y=HEAD_H+slot*RAIL_ITEM;
  int selected=focused&&first+slot==cursor;
  if(selected)xz_rect(c,0,y,RAIL_W,RAIL_ITEM-1,SH_BLUE);
  if(k==RAIL_SEARCH){
   xz_ring(c,RAIL_W/2-2,y+12,6,1,0xe6e6e6);
   xz_line16(c,(RAIL_W/2+3)*16,(y+17)*16,(RAIL_W/2+8)*16,(y+22)*16,32,0xe6e6e6);
   sh_text_center(c,RAIL_W/2,y+29,"SEARCH",SH_SMALL,0xe6e6e6);
   xz_rect(c,0,y+RAIL_ITEM-1,RAIL_W,1,SH_BLACK);
   continue;
  }
  int current=bknown(b,XZ_SHELL_BF_CATEGORY)&&(int)b->category==k;
  int available=(b->category_available>>k)&1;
  uint32_t ink=current?SH_WHITE:available?0xe6e6e6:SH_UNAVAILABLE;
  if(current&&!selected)xz_rect(c,0,y,RAIL_W,RAIL_ITEM-1,SH_RAIL_SEL);
  rail_icon(c,(enum xz_shell_browse_category)k,RAIL_W/2,y+7,ink);
  sh_text_center(c,RAIL_W/2,y+29,category_label[k],SH_SMALL,ink);
  xz_rect(c,0,y+RAIL_ITEM-1,RAIL_W,1,SH_BLACK);
 }
 xz_rect(c,RAIL_W-1,HEAD_H,1,STRIP_Y-HEAD_H,SH_BLACK);
 if(n>RAIL_SLOTS){
  int track=STRIP_Y-HEAD_H-8,thumb=track*RAIL_SLOTS/n,at=HEAD_H+4+(track-thumb)*first/(n-RAIL_SLOTS);
  xz_rect(c,RAIL_W-5,at,3,thumb,SH_DIM);
 }
}

/* ---- header ---- */
static void tile(struct xz_canvas c, struct xz_shell_rect r, const char *label, int lit) {
 xz_rect(c,r.x,r.y,r.w,r.h,lit?SH_BLUE:SH_TILE);
 sh_text_center(c,r.x+r.w/2,r.y+(r.h-SH_CAP_SMALL)/2-1,label,SH_SMALL,SH_WHITE);
 xz_rect(c,r.x+16,r.y+r.h-4,r.w-32,2,lit?SH_WHITE:0x5a5a5a);
}
static void render_source(const struct xz_shell_browse_model *b, struct xz_canvas c) {
 int color=bknown(b,XZ_SHELL_BF_SOURCE_COLOR);
 uint32_t fill=color?b->source_color&0xffffff:SH_RAIL;
 uint32_t ink=xz_theme_luma(fill)>150?SH_BLACK:SH_WHITE;
 xz_rect(c,0,0,SRC_W,HEAD_H,fill);
 if(bknown(b,XZ_SHELL_BF_SOURCE)&&b->source_name){
  sh_usb_icon(c,SRC_W/2-5,6,ink);
  int w=sh_min(SRC_W-6,sh_text_width(c,b->source_name,SH_SMALL));
  sh_text_fit(c,(SRC_W-w)/2,HEAD_H-14,SRC_W-6,b->source_name,SH_SMALL,ink);
 }else sh_text_center(c,SRC_W/2,(HEAD_H-SH_CAP_SMALL)/2,"---",SH_SMALL,SH_DIM);
}
static const char *screen_title(enum xz_shell_screen s) { return xz_shell_screen_name(s); }
static void render_field(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct xz_shell_rect r=xz_shell_browse_rect(u,m,XZ_SHELL_BR_FIELD,0);
 int open=keyboard_shown(u);
 int x=r.x,y=5,w=r.w,h=28;
 xz_rect(c,x,y,w,h,SH_BLACK);
 xz_border(c,x,y,w,h,open?SH_BLUE:0x6a6a6a);
 xz_ring(c,x+13,y+12,5,1,SH_DIM);
 xz_line16(c,(x+17)*16,(y+16)*16,(x+21)*16,(y+20)*16,32,SH_DIM);
 int tx=x+28,room=w-28-10;
 const char *q=st.query;
 if(!*q){sh_text(c,tx,y+8,"Type to search",SH_TEXT,0x6a6a6a);}
 else{
  /* Show the end of a long query: that is where typing happens. */
  while(*q&&sh_text_width(c,q,SH_TEXT)>room)q++;
  sh_text(c,tx,y+8,q,SH_TEXT,SH_WHITE);
  tx+=sh_text_width(c,q,SH_TEXT)+1;
 }
 if(open&&((m->clock_ms/500)&1)==0)xz_rect(c,*st.query?tx:x+27,y+6,2,17,SH_BLUE);
}
static void render_header(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 xz_rect(c,0,0,W,HEAD_H,SH_CHROME);
 render_source(b,c);
 back_arrow(c,BACK_X+4,2,SH_WHITE);
 struct xz_shell_rect pv=xz_shell_browse_rect(u,m,XZ_SHELL_BR_PREVIEW,0),in=xz_shell_browse_rect(u,m,XZ_SHELL_BR_INFO,0);
 struct xz_shell_rect fl=xz_shell_browse_rect(u,m,XZ_SHELL_BR_FILTER,0),chip=xz_shell_browse_rect(u,m,XZ_SHELL_BR_FILTER_CHIP,0);
 tile(c,pv,"PREVIEW",!st.hide_preview);
 tile(c,in,"INFO",u->panel.kind==XZ_SHELL_PANEL_INFO);
 for(int i=0;i<5;i++)xz_rect(c,in.x+12-i,in.y+13+i,1,9-2*i,SH_WHITE);
 if(fl.w){
  tile(c,fl,"FILTER",u->panel.kind==XZ_SHELL_PANEL_FILTER);
  /* a small funnel, lit amber while a filter applies */
  uint32_t ink=filter_active(&u->filter)?SH_KEY_AMBER:SH_WHITE;
  for(int i=0;i<5;i++)xz_rect(c,fl.x+6+i,fl.y+9+i,10-2*i,1,ink);
  xz_rect(c,fl.x+10,fl.y+14,2,5,ink);
 }
 if(chip.w){
  char text[48];filter_summary(&u->filter,text,sizeof text);
  xz_rect(c,chip.x,chip.y,chip.w,chip.h,SH_KEY_AMBER);
  sh_text_fit(c,chip.x+8,chip.y+(chip.h-SH_CAP_SMALL)/2,chip.w-34,text,SH_SMALL,SH_BLACK);
  int cx=chip.x+chip.w-14,cy=chip.y+chip.h/2;
  xz_line16(c,(cx-5)*16,(cy-5)*16,(cx+5)*16,(cy+5)*16,28,SH_BLACK);
  xz_line16(c,(cx+5)*16,(cy-5)*16,(cx-5)*16,(cy+5)*16,28,SH_BLACK);
 }
 if(s==XZ_SHELL_SEARCH){render_field(u,m,c);return;}
 int right=(chip.w?chip.x:fl.w?fl.x:pv.x)-10;
 if(s==XZ_SHELL_TAG_LIST){
  char text[24];
  if(matches(b,s)&&bknown(b,XZ_SHELL_BF_TOTAL))snprintf(text,sizeof text,"%d/%d",(int)sh_min(b->total,99999),XZ_SHELL_TAG_LIST_MAX);
  else snprintf(text,sizeof text,"--/%d",XZ_SHELL_TAG_LIST_MAX);
  int full=matches(b,s)&&bknown(b,XZ_SHELL_BF_TOTAL)&&b->total>=XZ_SHELL_TAG_LIST_MAX;
  int w=sh_text_width(c,text,SH_TEXT);
  sh_text(c,right-w,15,text,SH_TEXT,full?SH_RED:SH_WHITE);
  check_icon(c,right-w-20,14,SH_WHITE);
  right-=w+28;
 }
 list_icon(c,BACK_X+44,12,SH_WHITE);
 int tx=BACK_X+68,room=right-tx;
 const char *name=matches(b,s)&&bknown(b,XZ_SHELL_BF_LEVEL)&&b->level_name?b->level_name:screen_title(s);
 int named=matches(b,s)&&bknown(b,XZ_SHELL_BF_LEVEL)&&b->level_name;
 if(matches(b,s)&&bknown(b,XZ_SHELL_BF_PARENT)&&b->parent_name&&depth_of(b)>0){
  sh_text_fit(c,tx,5,room,b->parent_name,SH_SMALL,SH_DIM);
  sh_text_fit(c,tx,19,room,name,SH_TEXT,named?SH_WHITE:SH_DIM);
 }else sh_text_fit(c,tx,12,room,name,SH_TEXT,named?SH_WHITE:SH_DIM);
}

/* ---- list ---- */
static const char *column_label(int kind, const struct xz_shell_browse_model *b, enum xz_shell_screen s) {
 switch(kind){
 case XZ_SHELL_COL_NUMBER:return "#";
 case XZ_SHELL_COL_TITLE:{
  const struct xz_shell_browse_row *r=matches(b,s)?row_at(b,s,b->window_start):NULL;
  return r&&r->kind==XZ_SHELL_ROW_FOLDER?"NAME":"TRACK";
 }
 case XZ_SHELL_COL_ARTIST:return "ARTIST";
 case XZ_SHELL_COL_ALBUM:return "ALBUM";
 case XZ_SHELL_COL_GENRE:return "GENRE";
 case XZ_SHELL_COL_DATE_ADDED:return "ADDED";
 case XZ_SHELL_COL_BPM:return "BPM";
 case XZ_SHELL_COL_KEY:return "KEY";
 case XZ_SHELL_COL_RATING:return "RATING";
 case XZ_SHELL_COL_COLOR:return "COLOR";
 case XZ_SHELL_COL_TIME:return "TIME";
 default:return "PREVIEW";
 }
}
static void render_columns(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct col *cols, int n) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 int lx=list_x(s),lw=list_w(s);
 xz_rect(c,lx,COL_Y,lw,COL_H,SH_RAIL);
 for(int i=0;i<n;i++){
  const struct col *k=&cols[i];
  int sorted=matches(b,s)&&bknown(b,XZ_SHELL_BF_SORT)&&(int)b->sort_column==k->kind;
  const char *label=column_label(k->kind,b,s);
  struct xz_canvas cell=xz_sub(c,k->x,COL_Y,k->w,COL_H);
  int ty=(COL_H-SH_CAP_SMALL)/2;
  uint32_t ink=sorted?SH_BLUE:0xd2d2d2;
  sh_text_fit(cell,10,ty,k->w-12,label,SH_SMALL,ink);
  int x=10+sh_text_width(cell,label,SH_SMALL)+9;
  if(k->kind!=COL_PREVIEW){
   int down=sorted&&b->sort_descending;
   if(x+5<k->w)sh_triangle(cell,x,ty+1,6,down,sorted?SH_BLUE:0xd2d2d2);
   x+=12;
  }
  if(k->kind==XZ_SHELL_COL_TITLE){
   char count[16];
   if(matches(b,s)&&bknown(b,XZ_SHELL_BF_TOTAL))snprintf(count,sizeof count,"[%04d]",(int)sh_min(sh_max(b->total,0),99999));
   else snprintf(count,sizeof count,"[----]");
   sh_text_fit(cell,x+4,ty,k->w-x-8,count,SH_SMALL,0xd2d2d2);
  }
  if(i)xz_rect(c,k->x,COL_Y,1,COL_H,SH_CHROME);
 }
}
static void render_preview_cell(struct xz_canvas c, const struct xz_shell_browse_row *r, const struct col *k, int y) {
 if(!rknown(r,XZ_SHELL_RF_PREVIEW)||!r->preview||!r->preview_count)return;
 static const uint32_t band[3]={SH_WAVE_LOW,SH_WAVE_MID,SH_WHITE};
 int x0=k->x+4,w=k->w-8,h=ROW_H-8,base=y+ROW_H-4;
 for(int x=0;x<w;x++){
  uint32_t from=(uint32_t)((uint64_t)x*r->preview_count/(uint32_t)w),to=(uint32_t)((uint64_t)(x+1)*r->preview_count/(uint32_t)w);
  if(to<=from)to=from+1;
  int v[3]={0,0,0};
  for(uint32_t i=from;i<to&&i<r->preview_count;i++){
   v[0]=sh_max(v[0],r->preview[i].low);v[1]=sh_max(v[1],r->preview[i].mid);v[2]=sh_max(v[2],r->preview[i].high);
  }
  /* As the flagship's list preview: blue body, amber edge above it, white tips where highs lead. */
  int hh=v[2]*h/255,hm=v[1]*h/255,hl=v[0]*h*4/5/255;
  if(hh)xz_rect(c,x0+x,base-hh,1,hh,band[2]);
  if(hm)xz_rect(c,x0+x,base-hm,1,hm,band[1]);
  if(hl)xz_rect(c,x0+x,base-hl,1,hl,band[0]);
 }
}
static void deck_badge(struct xz_canvas c, int x, int y, int deck, uint32_t fill, uint32_t ink) {
 char text[4];snprintf(text,sizeof text,"%d",deck+1);
 xz_rect(c,x,y,14,14,fill);
 sh_text_center(c,x+7,y+3,text,SH_SMALL,ink);
}
static void render_load_chip(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct xz_shell_rect l=xz_shell_browse_rect(u,m,XZ_SHELL_BR_LOAD,0);
 if(!l.w)return;
 int y=l.y+5,h=ROW_H-10;
 /* Without the capability the chip still answers (the stock browser takes over), so it stays, greyed, saying so. */
 int live=(m->capabilities&XZ_SHELL_CAP_LOAD)!=0;
 uint32_t fill=live?SH_WHITE:SH_RAIL,ink=live?SH_BLUE:SH_DIM;
 xz_rect(c,l.x,y,LOAD_W+sh_deck_count(m)*LOAD_DECK_W,h,fill);
 sh_text_center(c,l.x+LOAD_W/2,y+(h-SH_CAP_SMALL)/2,live?"LOAD":"LOAD OFF",SH_SMALL,ink);
 for(int d=0;d<sh_deck_count(m);d++){
  struct xz_shell_rect r=xz_shell_browse_rect(u,m,XZ_SHELL_BR_LOAD,d+1);
  int target=live&&u->deck==d;
  xz_rect(c,r.x,y+2,1,h-4,live?SH_BLUE:0x5a5a5a);
  if(target)xz_rect(c,r.x+3,y+3,r.w-6,h-6,SH_BLUE);
  char text[4];snprintf(text,sizeof text,"%d",d+1);
  sh_text_center(c,r.x+r.w/2,y+(h-SH_CAP_TEXT)/2,text,SH_TEXT,target?SH_WHITE:ink);
 }
}
static void render_row(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct col *cols, int n, int32_t index, int y, const struct sess *v) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 const struct xz_shell_browse_row *r=row_at(b,s,grab_source_row(u,index));
 int32_t limit=row_limit(b,s);
 int grabbed=u->grab.active&&index==u->grab.to;
 int selected=!rail_focused(u)&&index<limit&&index==v->cursor&&r&&!grabbed;
 uint32_t bg=selected?SH_BLUE:grabbed?0x3a2a08:index%2?SH_ROW_ALT:SH_BLACK;
 xz_rect(c,list_x(s),y,list_w(s),ROW_H,bg);
 if(grabbed){xz_border(c,list_x(s),y,list_w(s),ROW_H,SH_KEY_AMBER);xz_border(c,list_x(s)+1,y+1,list_w(s)-2,ROW_H-2,SH_KEY_AMBER);}
 for(int i=1;i<n;i++)if(!selected||cols[i-1].kind==COL_PREVIEW)xz_rect(c,cols[i].x,y,1,ROW_H,selected?SH_BLACK:SH_RAIL);
 if(!r)return;
 char text[32];
 int ty=y+(ROW_H-SH_CAP_TEXT)/2;
 int touched=v->touched==index&&r->kind==XZ_SHELL_ROW_TRACK;
 int artist_column=0;
 for(int i=0;i<n;i++)artist_column|=cols[i].kind==XZ_SHELL_COL_ARTIST;
 for(int i=0;i<n;i++){
  const struct col *k=&cols[i];
  int x=k->x+10,room=k->w-18;
  switch(k->kind){
  case COL_PREVIEW:
   xz_rect(c,k->x,y,k->w,ROW_H,SH_BLACK);
   render_preview_cell(c,r,k,y);
   break;
  case XZ_SHELL_COL_NUMBER:
   snprintf(text,sizeof text,"%03d",(int)(index+1)%100000);
   sh_text_fit(c,x,ty,room,text,SH_TEXT,SH_WHITE);
   break;
  case XZ_SHELL_COL_TITLE:{
   int right=k->x+k->w-8;
   if(touched)right=k->x+k->w-6-LOAD_W-sh_deck_count(m)*LOAD_DECK_W-8;
   else if(rknown(r,XZ_SHELL_RF_ON_DECK))for(int d=sh_deck_count(m)-1;d>=0;d--)if((r->on_deck>>d)&1){
    right-=14;deck_badge(c,right,y+12,d,selected?SH_WHITE:SH_BLUE,selected?SH_BLUE:SH_WHITE);right-=4;
   }
   /* On the TAG LIST: a check after the title, as the TAG LIST rows carry one before theirs. */
   if(!touched&&s!=XZ_SHELL_TAG_LIST&&r->kind==XZ_SHELL_ROW_TRACK&&rknown(r,XZ_SHELL_RF_TAGGED)&&r->tagged){
    right-=16;check_icon(c,right,y+12,selected?SH_WHITE:SH_KEY_AMBER);right-=4;
   }
   if(r->kind==XZ_SHELL_ROW_FOLDER){
    folder_icon(c,x,y+12,SH_WHITE);
    /* An edited playlist carries a small pencil-blue dot: its contents come from the shell's edits. */
    if(rknown(r,XZ_SHELL_RF_EDITED))xz_disc(c,x+16,y+12,3,selected?SH_WHITE:SH_KEY_AMBER);
   }
   else if(s==XZ_SHELL_TAG_LIST)check_icon(c,x-1,y+12,SH_WHITE);
   else sh_note(c,x+1,y+11,!selected&&rknown(r,XZ_SHELL_RF_PLAYED)&&r->played?SH_GREEN:SH_WHITE);
   int artist_below=r->kind==XZ_SHELL_ROW_TRACK&&rknown(r,XZ_SHELL_RF_ARTIST)&&r->artist&&*r->artist&&!artist_column;
   if(rknown(r,XZ_SHELL_RF_TITLE)&&r->title)sh_text_fit(c,x+22,artist_below?y+4:ty,right-(x+22),r->title,SH_TEXT,SH_WHITE);
   if(artist_below)sh_text_fit(c,x+22,y+24,right-(x+22),r->artist,SH_SMALL,selected?0xd8e8ff:SH_DIM);
   break;
  }
  case XZ_SHELL_COL_ARTIST:
   if(rknown(r,XZ_SHELL_RF_ARTIST)&&r->artist)sh_text_fit(c,x,ty,room,r->artist,SH_TEXT,SH_WHITE);
   break;
  case XZ_SHELL_COL_BPM:
   if(rknown(r,XZ_SHELL_RF_BPM)&&r->bpm_x100>0){
    int32_t v100=r->bpm_x100>99999?99999:r->bpm_x100;
    snprintf(text,sizeof text,"%d.%d",(int)(v100/100),(int)((v100/10)%10));
    sh_text_right(c,k->x+k->w-10,ty,text,SH_TEXT,SH_WHITE);
   }
   break;
  case XZ_SHELL_COL_KEY:
   if(rknown(r,XZ_SHELL_RF_KEY)&&r->key){
    char key[8];uint32_t ink=sh_key_ink(m,r->key,r->key_matches_master,SH_WHITE);
    /* On the blue cursor row the light moves into a bar beside the key, where it still reads. */
    if(selected&&ink!=SH_WHITE){xz_rect(c,x-6,y+10,3,ROW_H-20,ink);ink=SH_WHITE;}
    sh_text_fit(c,x,ty,room,sh_key_text(r->key,sh_key_display(u),key,sizeof key),SH_TEXT,ink);
   }
   break;
  case XZ_SHELL_COL_ALBUM:
   if(rknown(r,XZ_SHELL_RF_ALBUM)&&r->album)sh_text_fit(c,x,ty,room,r->album,SH_TEXT,SH_WHITE);
   break;
  case XZ_SHELL_COL_GENRE:
   if(rknown(r,XZ_SHELL_RF_GENRE)&&r->genre)sh_text_fit(c,x,ty,room,r->genre,SH_TEXT,SH_WHITE);
   break;
  case XZ_SHELL_COL_DATE_ADDED:
   if(rknown(r,XZ_SHELL_RF_DATE_ADDED)&&r->date_added>0){
    int32_t d=r->date_added;
    snprintf(text,sizeof text,"%04d-%02d-%02d",(int)(d/10000%10000),(int)(d/100%100),(int)(d%100));
    sh_text_fit(c,x,ty,room,text,SH_SMALL,SH_WHITE);
   }
   break;
  case XZ_SHELL_COL_RATING:
   if(rknown(r,XZ_SHELL_RF_RATING))for(int j=0;j<5;j++)star(c,x+j*12,y+15,j<r->rating?SH_WHITE:selected?xz_blend(SH_BLUE,SH_WHITE,90):SH_STAR_OFF);
   break;
  case XZ_SHELL_COL_COLOR:
   if(rknown(r,XZ_SHELL_RF_COLOR)&&r->color){
    xz_rect(c,k->x+k->w/2-7,y+12,14,14,r->color&0xffffff);
    if(selected)xz_border(c,k->x+k->w/2-8,y+11,16,16,SH_WHITE);
   }
   break;
  case XZ_SHELL_COL_TIME:
   if(rknown(r,XZ_SHELL_RF_DURATION)&&r->duration_ms>=0){
    int32_t sec=r->duration_ms/1000;
    snprintf(text,sizeof text,"%d:%02d",(int)sh_min(sec/60,999),(int)(sec%60));
    sh_text_right(c,k->x+k->w-10,ty,text,SH_TEXT,SH_WHITE);
   }
   break;
  }
 }
}
static void render_info_pane(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct sess *v) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 int x0=W-INFO_W,y0=HEAD_H;
 xz_rect(c,x0,y0,INFO_W,STRIP_Y-y0,SH_CHROME);
 xz_rect(c,x0,y0,1,STRIP_Y-y0,SH_BLACK);
 const struct xz_shell_browse_row *r=row_at(b,s,v->cursor);
 if(!r){sh_text_center(c,x0+INFO_W/2,y0+40,"NO ROW SELECTED",SH_SMALL,SH_DIM);return;}
 int x=x0+14,w=INFO_W-28;
 char text[48];
 if(r->kind==XZ_SHELL_ROW_FOLDER)folder_icon(c,x,y0+16,SH_WHITE);else sh_note(c,x+1,y0+15,SH_WHITE);
 if(rknown(r,XZ_SHELL_RF_TITLE)&&r->title)sh_text_fit(c,x+22,y0+15,w-22,r->title,SH_TEXT,SH_WHITE);
 else sh_text(c,x+22,y0+15,"--",SH_TEXT,SH_DIM);
 if(rknown(r,XZ_SHELL_RF_ARTIST)&&r->artist)sh_text_fit(c,x+22,y0+37,w-22,r->artist,SH_SMALL,SH_DIM);
 xz_rect(c,x0+1,y0+56,INFO_W-1,1,SH_BLACK);
 if(r->kind==XZ_SHELL_ROW_FOLDER)return;
 static const char *const labels[]={"BPM","KEY","TIME","RATING","COLOR","PLAYED","ON DECK"};
 for(int i=0;i<7;i++){
  int y=y0+66+i*36;
  sh_text(c,x,y+5,labels[i],SH_SMALL,SH_DIM);
  int right=x0+INFO_W-14,known=1;
  uint32_t ink=SH_WHITE;
  text[0]=0;
  switch(i){
  case 0:known=rknown(r,XZ_SHELL_RF_BPM)&&r->bpm_x100>0;if(known)snprintf(text,sizeof text,"%d.%02d",(int)(sh_min(r->bpm_x100,99999)/100),(int)(sh_min(r->bpm_x100,99999)%100));break;
  case 1:known=rknown(r,XZ_SHELL_RF_KEY)&&r->key;if(known){char key[8];snprintf(text,sizeof text,"%.20s",sh_key_text(r->key,sh_key_display(u),key,sizeof key));ink=sh_key_ink(m,r->key,r->key_matches_master,SH_WHITE);}break;
  case 2:known=rknown(r,XZ_SHELL_RF_DURATION)&&r->duration_ms>=0;if(known)snprintf(text,sizeof text,"%d:%02d",(int)sh_min(r->duration_ms/60000,999),(int)(r->duration_ms/1000%60));break;
  case 3:
   known=rknown(r,XZ_SHELL_RF_RATING);
   if(known){for(int j=0;j<5;j++)star(c,right-5*12+j*12+3,y+3,j<r->rating?SH_WHITE:SH_STAR_OFF);continue;}
   break;
  case 4:
   known=rknown(r,XZ_SHELL_RF_COLOR);
   if(known&&r->color){xz_rect(c,right-14,y+1,14,14,r->color&0xffffff);continue;}
   if(known)snprintf(text,sizeof text,"NONE");
   break;
  case 5:known=rknown(r,XZ_SHELL_RF_PLAYED);if(known){snprintf(text,sizeof text,"%s",r->played?"PLAYED":"NOT PLAYED");if(r->played)ink=SH_GREEN;}break;
  case 6:
   known=rknown(r,XZ_SHELL_RF_ON_DECK);
   if(known&&(r->on_deck&3)){
    for(int d=sh_deck_count(m)-1;d>=0;d--)if((r->on_deck>>d)&1){right-=14;deck_badge(c,right,y+1,d,SH_BLUE,SH_WHITE);right-=4;}
    continue;
   }
   if(known)snprintf(text,sizeof text,"NO");
   break;
  }
  if(!known)sh_text_right(c,right,y+1,"--",SH_TEXT,SH_DIM);
  else if(text[0])sh_text_right(c,right,y+1,text,SH_TEXT,ink);
 }
}
/* The letter under the cursor in the alphabet index, or 0. */
static char jump_letter(const struct xz_shell_browse_model *b, int cursor) {
 char letter=0;
 if(!b||!b->letters)return 0;
 for(int i=0;i<b->letter_count;i++)if(b->letters[i].row<=cursor)letter=b->letters[i].letter;
 if(!letter&&b->letter_count>0)letter=b->letters[0].letter;
 return letter;
}
static void render_jump(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct sess *v) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 int rows=xz_shell_browse_rows_visible(u);
 int cx=list_x(s)+list_w(s)/2,cy=LIST_Y+rows*ROW_H/2,side=sh_min(140,rows*ROW_H-8);
 xz_rect(c,cx-side/2,cy-side/2,side,side,0x282828);
 xz_border(c,cx-side/2,cy-side/2,side,side,SH_RAIL_SEL);
 char letter=matches(b,s)&&b->letter_count>0?jump_letter(b,v->cursor):0;
 if(letter&&letter>' '){
  char text[2]={letter,0};
  big_text(c,cx,cy,text,side>=120?6:4,SH_WHITE);
 }else{
  char text[16];
  int32_t limit=row_limit(b,s);
  snprintf(text,sizeof text,"%d",v->cursor/rows+1);
  sh_text_center(c,cx,cy-side/2+10,"PAGE",SH_SMALL,SH_DIM);
  sh_num(c,cx-sh_num_width(SH_NUM_BIG,text)/2,cy-sh_num_cap(SH_NUM_BIG)/2,text,SH_NUM_BIG,SH_WHITE);
  if(matches(b,s)&&bknown(b,XZ_SHELL_BF_TOTAL)){
   snprintf(text,sizeof text,"OF %d",(int)((limit+rows-1)/rows));
   sh_text_center(c,cx,cy+side/2-20,text,SH_SMALL,SH_DIM);
  }
 }
}
/* The device the list reads is known to be gone: no stale rows, one line and a way to the picker. */
static int source_absent(const struct xz_shell_model *m, enum xz_shell_screen s) {
 const struct xz_shell_source_model *sm=m->source;
 if(s==XZ_SHELL_TAG_LIST||!sm||!sm->current_known)return 0;
 int i=(int)sm->current-1;
 if(i<0||i>=XZ_SHELL_SOURCE_TILES)return 0;
 const struct xz_shell_device *d=&sm->device[i];
 return ((d->known>>XZ_SHELL_D_STATUS)&1)&&d->status==XZ_SHELL_DEVICE_NOT_CONNECTED;
}
static struct xz_shell_rect picker_button(enum xz_shell_screen s) {
 return (struct xz_shell_rect){list_x(s)+list_w(s)/2-110,LIST_Y+ROW_H*3,220,44};
}
static void render_absent(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 enum xz_shell_screen s=xz_shell_screen(u);
 char text[48];
 int slot=(int)m->source->current;
 snprintf(text,sizeof text,"%s IS NOT CONNECTED",slot==XZ_SHELL_SRC_USB2?"USB 2":slot==XZ_SHELL_SRC_USB1?"USB 1":slot==XZ_SHELL_SRC_LINK?"LINK":"THE DEVICE");
 xz_rect(c,list_x(s),COL_Y,list_w(s),STRIP_Y-COL_Y,SH_BLACK);
 sh_text_center(c,list_x(s)+list_w(s)/2,LIST_Y+ROW_H+8,text,SH_TEXT,SH_DIM);
 struct xz_shell_rect b=picker_button(s);
 xz_rect(c,b.x,b.y,b.w,b.h,SH_BLUE);
 sh_text_center(c,b.x+b.w/2,b.y+(b.h-SH_CAP_TEXT)/2,"CHOOSE A SOURCE",SH_TEXT,SH_WHITE);
}
/* The picked-up track: its row under the cursor, the rest closed up around it, a hint bar below. */
static int32_t grab_source_row(const struct xz_shell *u, int32_t shown) {
 const struct xz_shell_grab *g=&u->grab;
 if(!g->active)return shown;
 if(shown==g->to)return g->from;
 if(g->from<g->to&&shown>=g->from&&shown<g->to)return shown+1;
 if(g->from>g->to&&shown>g->to&&shown<=g->from)return shown-1;
 return shown;
}
static void render_grab_bar(const struct xz_shell *u, struct xz_canvas c) {
 enum xz_shell_screen s=xz_shell_screen(u);
 int y=STRIP_Y-ROW_H;   /* over the last row exactly, so no half row shows above it */
 xz_rect(c,list_x(s),y,list_w(s),ROW_H,SH_KEY_AMBER);
 sh_text_fit(c,list_x(s)+12,y+(ROW_H-SH_CAP_SMALL)/2,list_w(s)-24,"MOVING TRACK: TURN OR TOUCH A ROW, PUSH OR TOUCH AGAIN TO DROP, BACK TO CANCEL",SH_SMALL,SH_BLACK);
}
static void render_list(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct sess *v) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 if(source_absent(m,s)){
  render_absent(u,m,c);
  if(panel_open(u))render_panel(u,m,c);
  return;
 }
 struct col cols[MAX_COLS];int n=layout_columns(b,s,cols);
 int rows=xz_shell_browse_rows_visible(u);
 render_columns(u,m,c,cols,n);
 for(int i=0;i<rows;i++)render_row(u,m,c,cols,n,v->scroll+i,LIST_Y+i*ROW_H,v);
 if(v->touched>=v->scroll&&v->touched<v->scroll+rows&&v->touched==v->cursor){
  const struct xz_shell_browse_row *r=row_at(b,s,v->touched);
  if(r&&r->kind==XZ_SHELL_ROW_TRACK)render_load_chip(u,m,c);
 }
 int32_t limit=row_limit(b,s);
 if(!matches(b,s)||(!bknown(b,XZ_SHELL_BF_TOTAL)&&limit==0))
  sh_text_center(c,list_x(s)+list_w(s)/2,LIST_Y+ROW_H+12,"LIST NOT READ YET",SH_SMALL,SH_DIM);
 else if(bknown(b,XZ_SHELL_BF_TOTAL)&&limit==0)
  sh_text_center(c,list_x(s)+list_w(s)/2,LIST_Y+ROW_H+12,s==XZ_SHELL_SEARCH&&!st.query[0]?"TYPE TO SEARCH THE LIBRARY":"NO ITEMS",SH_SMALL,SH_DIM);
 /* Thin scrollbar at the list's right edge when there is more than a page. */
 if(limit>rows){
  int track=rows*ROW_H-8,thumb=sh_max(12,(int)((int64_t)track*rows/limit));
  int at=LIST_Y+4+(int)((int64_t)(track-thumb)*v->scroll/(limit-rows));
  xz_rect(c,list_x(s)+list_w(s)-4,at,3,thumb,SH_DIM);
 }
 if(u->panel.kind==XZ_SHELL_PANEL_INFO)render_info_pane(u,m,c,v);
 else if(panel_open(u))render_panel(u,m,c);
 if(u->grab.active)render_grab_bar(u,c);
 if(v->jump)render_jump(u,m,c,v);
}
static size_t emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a);
static size_t op(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, enum xz_shell_browse_op o, int32_t value);
/* ---- slide-over panels: MENU (sort, columns, playlist edits) and FILTER ----
   Same chrome as the INFO pane, over the list's right side. The encoder moves
   the row cursor and pushes the row; a touch acts on the row at once; BACK
   leaves a sub-page, then closes the panel (sh_browse_back_step). Rows the
   model gives no data for are left out. */
enum { PANEL_X=W-INFO_W, PANEL_HEAD=36, PANEL_ROW=34, PANEL_ROWS=(STRIP_Y-HEAD_H-PANEL_HEAD)/PANEL_ROW, MAX_PITEMS=72 };
/* PAGE_CUES + d: the CUE/LOOP LIST loading onto deck d. */
enum { PAGE_MAIN, PAGE_ADD, PAGE_NEW, PAGE_RENAME, PAGE_TAG_PLAYLIST, PAGE_CUES };
enum pk {
 PK_HEAD, PK_SORT, PK_COLUMN, PK_FILTER_OPEN, PK_NEW, PK_RENAME, PK_DELETE, PK_ADD_OPEN, PK_REMOVE, PK_MOVE, PK_ADD_TO, PK_SAVE,
 PK_F_BPM, PK_F_FROM, PK_F_TO, PK_F_KEY, PK_F_RATING, PK_F_COLOR, PK_F_TAG, PK_F_CLEAR, PK_NOTE, PK_SEARCH,
 PK_MY_LOAD, PK_MY_SAVE, PK_PROFILE, PK_PROFILE_SAVE, PK_MY_OWN, PK_MY_WAVE, PK_MY_STORED,
 PK_TAG_TOGGLE, PK_TAG_ALL, PK_TAG_CLEAR, PK_TAG_PLAYLIST, PK_PLAYED, PK_HISTORY_DELETE, PK_HISTORY_ALL,
 PK_CUES_OPEN, PK_CUE, PK_CUE_DECK, PK_MY_STATE
};
struct pitem { enum pk kind; int value; const char *label; char text[28]; int on; uint32_t swatch; const char *help; };
static const char *const sort_label[XZ_SHELL_COL_COUNT]={
 [XZ_SHELL_COL_NUMBER]="TRACK No.",[XZ_SHELL_COL_TITLE]="TITLE",[XZ_SHELL_COL_ARTIST]="ARTIST",[XZ_SHELL_COL_BPM]="BPM",
 [XZ_SHELL_COL_KEY]="KEY",[XZ_SHELL_COL_RATING]="RATING",[XZ_SHELL_COL_COLOR]="COLOR",[XZ_SHELL_COL_TIME]="TIME",
 [XZ_SHELL_COL_ALBUM]="ALBUM",[XZ_SHELL_COL_GENRE]="GENRE",[XZ_SHELL_COL_DATE_ADDED]="DATE ADDED"
};
static const struct { const char *name; uint32_t rgb; } tag_colors[XZ_SHELL_TAG_COLOR_COUNT]={
 {"PINK",0xf870b8},{"RED",0xe8302c},{"ORANGE",0xf08a24},{"YELLOW",0xf2d42c},
 {"GREEN",0x42c84a},{"AQUA",0x36c8d8},{"BLUE",0x2a6cf0},{"PURPLE",0x9a54e0}
};
static const int bpm_spreads[]={2,4,6,10};
static int filter_active(const struct xz_shell_filter *f) {
 return f->bpm!=XZ_SHELL_FILTER_BPM_OFF||f->key_compatible||f->rating_min>0||f->colors||f->my_tags;
}
/* The active-filter chip's words, most telling first. */
static void filter_summary(const struct xz_shell_filter *f, char *out, size_t cap) {
 size_t used=0;out[0]=0;
#define PART(...) do{ if(used&&used+3<cap){memcpy(out+used," \xc2\xb7 ",4);used+=4;} \
  int written_=snprintf(out+used,cap>used?cap-used:0,__VA_ARGS__);if(written_>0)used=(size_t)sh_min((int)cap-1,(int)(used+(size_t)written_)); }while(0)
 if(f->bpm==XZ_SHELL_FILTER_BPM_MASTER)PART("BPM \xb1%d",f->bpm_spread);
 else if(f->bpm==XZ_SHELL_FILTER_BPM_RANGE)PART("BPM %d-%d",(int)(f->bpm_min_x100/100),(int)(f->bpm_max_x100/100));
 if(f->key_compatible)PART("KEY");
 if(f->rating_min>0)PART("%d+ STARS",f->rating_min);
 if(f->colors){int k=0;for(int i=0;i<XZ_SHELL_TAG_COLOR_COUNT;i++)k+=(f->colors>>i)&1;PART(k==1?"1 COLOR":"%d COLORS",k);}
 if(f->my_tags)PART("MY TAG");
#undef PART
}
static const struct xz_shell_browse_row *highlight_row(const struct xz_shell *u, const struct xz_shell_model *m) {
 return xz_shell_browse_highlight(u,m);
}
static int column_shown(const struct xz_shell_browse_model *b, int k) {
 if(!bknown(b,XZ_SHELL_BF_COLUMNS))return 0;
 for(int i=0;i<b->column_count&&i<XZ_SHELL_BROWSE_COLUMNS;i++)if((int)b->columns[i]==k)return 1;
 return 0;
}
static int add(struct pitem *out, int n, enum pk kind, int value, const char *label) {
 if(n>=MAX_PITEMS)return n;
 out[n]=(struct pitem){kind,value,label,"",0,0,NULL};
 return n+1;
}
static const struct xz_shell_browse_row *highlight_row(const struct xz_shell *u, const struct xz_shell_model *m);
static void fmt_cue_time(char *out, size_t cap, int32_t ms) {
 if(ms<0)ms=0;
 int32_t s=ms/1000;
 snprintf(out,cap,"%d:%02d.%d",(int)sh_min(s/60,999),(int)(s%60),(int)((ms%1000)/100));
}
/* CUE/LOOP LIST (XZ manual pp.81-82): the destination deck, then the points; push loads there. */
static char cue_head[64],cue_deck[24];
static int cue_items(const struct xz_shell *u, const struct xz_shell_model *m, struct pitem *out) {
 const struct xz_shell_browse_model *b=m->browse;
 const struct xz_shell_browse_row *r=highlight_row(u,m);
 int n=0,deck=sh_max(0,sh_min(sh_deck_count(m)-1,u->panel.page-PAGE_CUES));
 if(!r||!bknown(b,XZ_SHELL_BF_CUES)||!b->cues||b->cue_track!=r->id)return add(out,n,PK_NOTE,0,"THE TRACK'S POINTS ARE NOT READ YET");
 snprintf(cue_deck,sizeof cue_deck,"LOAD TO DECK %d",deck+1);
 n=add(out,n,PK_CUE_DECK,deck,cue_deck);
 snprintf(out[n-1].text,sizeof out[n-1].text,"CHANGE");
 snprintf(cue_head,sizeof cue_head,"%s",rknown(r,XZ_SHELL_RF_TITLE)&&r->title?r->title:"POINTS");
 n=add(out,n,PK_HEAD,0,cue_head);
 for(int i=0;i<sh_min(b->cue_count,64);i++){
  const struct xz_shell_cue_entry *q=&b->cues[i];
  n=add(out,n,PK_CUE,i,q->comment&&*q->comment?q->comment:q->loop?"LOOP":"MEMORY CUE");
  fmt_cue_time(out[n-1].text,sizeof out[n-1].text,q->ms);
  out[n-1].swatch=q->color?q->color&0xffffff:q->loop?SH_LOOP:SH_MEMORY;
 }
 return n;
}
static int menu_items(const struct xz_shell *u, const struct xz_shell_model *m, struct pitem *out) {
 const struct xz_shell_browse_model *b=m->browse;
 enum xz_shell_screen s=xz_shell_screen(u);
 int n=0;
 if(!matches(b,s))return 0;
 const struct xz_shell_browse_row *r=highlight_row(u,m);
 if(u->panel.page==PAGE_ADD){
  n=add(out,n,PK_HEAD,0,"ADD TO PLAYLIST");
  if(bknown(b,XZ_SHELL_BF_PLAYLISTS)&&b->playlists)for(int i=0;i<b->playlist_count&&n<MAX_PITEMS;i++){
   n=add(out,n,PK_ADD_TO,i,b->playlists[i].name?b->playlists[i].name:"---");
   if(b->playlists[i].tracks>=0)snprintf(out[n-1].text,sizeof out[n-1].text,"%d",(int)sh_min(b->playlists[i].tracks,99999));
  }
  return n;
 }
 if(u->panel.page==PAGE_NEW||u->panel.page==PAGE_RENAME||u->panel.page==PAGE_TAG_PLAYLIST)return add(out,n,PK_SAVE,0,"SAVE");
 if(u->panel.page>=PAGE_CUES)return cue_items(u,m,out);
 const int armed_at=u->panel.armed?u->panel.cursor:-1;
 /* The highlighted track (newer players' track menu, XZ manual pp.81-84, 86) */
 if(r&&r->kind==XZ_SHELL_ROW_TRACK){
  int head=n;
  n=add(out,n,PK_HEAD,0,"TRACK");
  if((m->capabilities&XZ_SHELL_CAP_TAG_EDIT)&&rknown(r,XZ_SHELL_RF_TAGGED))
   n=add(out,n,PK_TAG_TOGGLE,0,r->tagged||s==XZ_SHELL_TAG_LIST?"REMOVE FROM TAG LIST":"ADD TO TAG LIST");
  if((m->capabilities&XZ_SHELL_CAP_CUE_LIST)&&bknown(b,XZ_SHELL_BF_CUES)&&b->cues&&b->cue_count>0&&b->cue_track==r->id){
   n=add(out,n,PK_CUES_OPEN,0,"CUE/LOOP LIST");
   snprintf(out[n-1].text,sizeof out[n-1].text,"%d",sh_min(b->cue_count,99));
  }
  if((m->capabilities&XZ_SHELL_CAP_HISTORY_EDIT)&&rknown(r,XZ_SHELL_RF_PLAYED))
   n=add(out,n,PK_PLAYED,0,r->played?"MARK UNPLAYED":"MARK PLAYED");
  if(n==head+1)n=head;
 }
 if(r&&r->kind==XZ_SHELL_ROW_FOLDER&&s!=XZ_SHELL_TAG_LIST&&(m->capabilities&XZ_SHELL_CAP_TAG_EDIT)){
  n=add(out,n,PK_HEAD,0,"FOLDER");
  n=add(out,n,PK_TAG_ALL,0,"ADD ALL TO TAG LIST");
 }
 if(s==XZ_SHELL_TAG_LIST&&(m->capabilities&XZ_SHELL_CAP_TAG_EDIT)&&bknown(b,XZ_SHELL_BF_TOTAL)&&b->total>0){
  n=add(out,n,PK_HEAD,0,"TAG LIST");
  n=add(out,n,PK_TAG_PLAYLIST,0,"CREATE PLAYLIST");
  n=add(out,n,PK_TAG_CLEAR,0,armed_at==n?"PUSH AGAIN TO CLEAR IT":"REMOVE ALL TRACKS");
  out[n-1].on=armed_at==n-1;
 }
 if(s==XZ_SHELL_BROWSE&&bknown(b,XZ_SHELL_BF_CATEGORY)&&b->category==XZ_SHELL_CAT_HISTORY&&depth_of(b)==0
    &&(m->capabilities&XZ_SHELL_CAP_HISTORY_EDIT)&&bknown(b,XZ_SHELL_BF_TOTAL)&&b->total>0){
  n=add(out,n,PK_HEAD,0,"HISTORY");
  if(r&&r->kind==XZ_SHELL_ROW_FOLDER){
   n=add(out,n,PK_HISTORY_DELETE,0,armed_at==n?"PUSH AGAIN TO DELETE":"DELETE HISTORY");
   out[n-1].on=armed_at==n-1;
  }
  n=add(out,n,PK_HISTORY_ALL,0,armed_at==n?"PUSH AGAIN: DELETE ALL":"DELETE ALL HISTORIES");
  out[n-1].on=armed_at==n-1;
 }
 if(bknown(b,XZ_SHELL_BF_SORT_KEYS)&&b->sort_keys){
  n=add(out,n,PK_HEAD,0,"SORT BY");
  for(int k=0;k<XZ_SHELL_COL_COUNT;k++)if((b->sort_keys>>k)&1){
   n=add(out,n,PK_SORT,k,sort_label[k]);
   out[n-1].on=bknown(b,XZ_SHELL_BF_SORT)&&(int)b->sort_column==k;
  }
 }
 if(bknown(b,XZ_SHELL_BF_COLUMN_CHOICES)&&b->column_choices){
  n=add(out,n,PK_HEAD,0,"COLUMNS");
  for(int k=0;k<XZ_SHELL_COL_COUNT;k++)if(k!=XZ_SHELL_COL_TITLE&&((b->column_choices>>k)&1)){
   n=add(out,n,PK_COLUMN,k,sort_label[k]);
   out[n-1].on=column_shown(b,k);
  }
 }
 if(m->capabilities&XZ_SHELL_CAP_PLAYLIST_EDIT){
  int edit=bknown(b,XZ_SHELL_BF_EDIT)?(int)b->edit:0,head=n;
  n=add(out,n,PK_HEAD,0,"PLAYLIST");
  if(edit&XZ_SHELL_EDIT_PLAYLISTS){
   n=add(out,n,PK_NEW,0,"NEW PLAYLIST");
   if(r&&r->kind==XZ_SHELL_ROW_FOLDER){
    n=add(out,n,PK_RENAME,0,"RENAME");
    n=add(out,n,PK_DELETE,0,u->panel.armed?"PUSH AGAIN TO DELETE":"DELETE");
    out[n-1].on=u->panel.armed;
   }
  }
  if((edit&XZ_SHELL_EDIT_TRACKS)&&r&&r->kind==XZ_SHELL_ROW_TRACK){
   n=add(out,n,PK_MOVE,0,"MOVE TRACK");
   n=add(out,n,PK_REMOVE,0,"REMOVE FROM PLAYLIST");
  }
  if(r&&r->kind==XZ_SHELL_ROW_TRACK&&bknown(b,XZ_SHELL_BF_PLAYLISTS)&&b->playlist_count>0)n=add(out,n,PK_ADD_OPEN,0,"ADD TO PLAYLIST");
  if(n==head+1)n=head;
 }
 if((m->capabilities&XZ_SHELL_CAP_FILTER)||(s!=XZ_SHELL_SEARCH&&search_reachable(m))){
  n=add(out,n,PK_HEAD,0,"FIND");
  if(m->capabilities&XZ_SHELL_CAP_FILTER){n=add(out,n,PK_FILTER_OPEN,0,"TRACK FILTER");out[n-1].on=filter_active(&u->filter);}
  if(s!=XZ_SHELL_SEARCH&&search_reachable(m))n=add(out,n,PK_SEARCH,0,"SEARCH");
 }
 return n;
}
static void stars_text(char *out, size_t cap, int count) {
 if(count<=0){snprintf(out,cap,"ANY");return;}
 snprintf(out,cap,"%d+ STARS",count);
}
static int filter_items(const struct xz_shell *u, const struct xz_shell_model *m, struct pitem *out) {
 const struct xz_shell_filter *f=&u->filter;
 const struct xz_shell_browse_model *b=m->browse;
 int n=0;
 n=add(out,n,PK_HEAD,0,"BPM");
 n=add(out,n,PK_F_BPM,0,"BPM");
 if(f->bpm==XZ_SHELL_FILTER_BPM_OFF)snprintf(out[n-1].text,sizeof out[n-1].text,"OFF");
 else if(f->bpm==XZ_SHELL_FILTER_BPM_MASTER)snprintf(out[n-1].text,sizeof out[n-1].text,"MASTER \xb1%d",f->bpm_spread);
 else snprintf(out[n-1].text,sizeof out[n-1].text,"RANGE");
 out[n-1].on=f->bpm!=XZ_SHELL_FILTER_BPM_OFF;
 if(f->bpm==XZ_SHELL_FILTER_BPM_RANGE){
  n=add(out,n,PK_F_FROM,0,"FROM");
  snprintf(out[n-1].text,sizeof out[n-1].text,"%d.%d",(int)(f->bpm_min_x100/100),(int)(f->bpm_min_x100/10%10));
  n=add(out,n,PK_F_TO,0,"TO");
  snprintf(out[n-1].text,sizeof out[n-1].text,"%d.%d",(int)(f->bpm_max_x100/100),(int)(f->bpm_max_x100/10%10));
 }
 n=add(out,n,PK_HEAD,0,"KEY");
 n=add(out,n,PK_F_KEY,0,"MATCH MASTER KEY");
 out[n-1].on=f->key_compatible;
 const char *master=xz_shell_master_key(m);
 if(master){char key[8];snprintf(out[n-1].text,sizeof out[n-1].text,"%s",sh_key_text(master,sh_key_display(u),key,sizeof key));}
 n=add(out,n,PK_HEAD,0,"RATING");
 n=add(out,n,PK_F_RATING,0,"AT LEAST");
 stars_text(out[n-1].text,sizeof out[n-1].text,f->rating_min);
 out[n-1].on=f->rating_min>0;
 n=add(out,n,PK_HEAD,0,"COLOR");
 for(int i=0;i<XZ_SHELL_TAG_COLOR_COUNT;i++){
  n=add(out,n,PK_F_COLOR,i,tag_colors[i].name);
  out[n-1].on=(f->colors>>i)&1;out[n-1].swatch=tag_colors[i].rgb;
 }
 if(bknown(b,XZ_SHELL_BF_MY_TAGS)&&b&&b->my_tags&&b->my_tag_count>0){
  n=add(out,n,PK_HEAD,0,"MY TAG");
  for(int i=0;i<b->my_tag_count&&i<32;i++){
   n=add(out,n,PK_F_TAG,i,b->my_tags[i].name?b->my_tags[i].name:"---");
   out[n-1].on=(f->my_tags>>i)&1;
  }
 }
 if(filter_active(f))n=add(out,n,PK_F_CLEAR,0,"CLEAR ALL FILTERS");
 return n;
}
/* ---- MY SETTINGS (shell_settings.h): the stick's settings, its DJ profiles, and the waveform look ---- */
static char link_name[24];
static const char *stick_name(const struct xz_shell_my_settings_model *s) {
 if(s->source_name)return s->source_name;
 if(s->source==XZ_SHELL_SRC_LINK){snprintf(link_name,sizeof link_name,"PLAYER %d",sh_max(0,sh_min(s->link_player,99)));return link_name;}
 return s->source==XZ_SHELL_SRC_USB2?"USB 2":"USB 1";
}
static int item_count(const struct xz_shell_settings_model *t) { return t&&t->items?(int)sh_min((int)t->count,24):0; }
static void choice_text(struct pitem *p, const struct xz_shell_setting_item *it) {
 int n=it->choices&&it->choice_count>0?it->choice_count:0;
 const char *v=it->known&&it->current>=0&&it->current<n&&it->choices[it->current]?it->choices[it->current]:"---";
 snprintf(p->text,sizeof p->text,"%s",v);
}
static int next_choice(const struct xz_shell_setting_item *it) {
 int n=it->choices&&it->choice_count>0?sh_min(it->choice_count,255):0;
 if(n<2)return -1;
 return it->known&&it->current>=0&&it->current<n?(it->current+1)%n:0;
}
static int my_open(const struct xz_shell_model *m) {
 const struct xz_shell_my_settings_model *s=m->my_settings;
 return s&&(s->source==XZ_SHELL_SRC_USB1||s->source==XZ_SHELL_SRC_USB2||(s->source==XZ_SHELL_SRC_LINK&&s->link_player>0));
}
/* LOAD waits for a paused deck (XDJ-AZ manual p.150) and for the last LOAD to finish. */
static int my_load_blocked(const struct xz_shell *u, const struct xz_shell_model *m) {
 const struct xz_shell_my_settings_model *s=m->my_settings;
 const struct xz_shell_deck *d=&m->deck[sh_active_deck(u,m)];
 if(s->state==XZ_SHELL_MY_APPLYING)return 2;
 return s->state==XZ_SHELL_MY_NEEDS_PAUSE||(sh_has(d,XZ_SHELL_F_PLAYING)&&d->playing);
}
static int profile_valid(const struct xz_shell_my_settings_model *s, int i) {
 return s->profiles&&i>=0&&i<sh_min(s->profile_count,16);
}
/* Labels built per frame; one shell draws at a time (shell.h rule 4). */
static char my_head[40],my_load[24],my_save[40],my_profile_save[48],my_stored_head[40];
static int my_items(const struct xz_shell *u, const struct xz_shell_model *m, struct pitem *out) {
 if(!my_open(m))return 0;
 const struct xz_shell_my_settings_model *s=m->my_settings;
 int n=0;
 const int armed_at=u->panel.armed?u->panel.cursor:-1;
 snprintf(my_head,sizeof my_head,"%s",stick_name(s));
 n=add(out,n,PK_HEAD,0,my_head);
 if(s->on_stick){
  int blocked=my_load_blocked(u,m);
  if(blocked==2)snprintf(my_load,sizeof my_load,"LOADING...");
  else if(blocked)snprintf(my_load,sizeof my_load,"PAUSE DECK %d TO LOAD",u->deck+1);
  else snprintf(my_load,sizeof my_load,"LOAD TO DECK %d",u->deck+1);
  n=add(out,n,PK_MY_LOAD,0,my_load);
  out[n-1].help="Applies the settings saved here to the active deck. Settings load onto a paused deck.";
  if(blocked)out[n-1].on=-1;
  if((s->state==XZ_SHELL_MY_DONE||s->state==XZ_SHELL_MY_FAILED)&&s->result){
   n=add(out,n,PK_MY_STATE,0,s->result);
   out[n-1].on=s->state==XZ_SHELL_MY_DONE;
  }
 }
 if(s->writable){
  snprintf(my_save,sizeof my_save,"SAVE TO %s",stick_name(s));
  n=add(out,n,PK_MY_SAVE,0,armed_at==n?"PUSH AGAIN TO SAVE":my_save);
  out[n-1].help="Saves this unit's current settings here, replacing the ones saved before.";
  out[n-1].on=armed_at==n-1;
 }
 if(!s->on_stick&&!s->writable)n=add(out,n,PK_NOTE,0,"NOTHING SAVED ON IT, AND IT IS LOCKED");
 if(s->profile_count>0&&s->profiles){
  n=add(out,n,PK_HEAD,0,"DJ PROFILE");
  for(int i=0;i<sh_min(s->profile_count,16);i++){
   const struct xz_shell_dj_profile *p=&s->profiles[i];
   n=add(out,n,PK_PROFILE,i,p->name?p->name:"---");
   out[n-1].help=p->damaged?"This profile's files cannot be read.":i==s->profile_active?"The DJ whose settings and look are in use.":
    "Makes this DJ the active profile and applies their settings and look.";
   out[n-1].on=i==s->profile_active;
   out[n-1].swatch=p->has_accent?p->accent_rgb&0xffffff:SH_RAIL;
   snprintf(out[n-1].text,sizeof out[n-1].text,"%s",p->damaged?"DAMAGED":i==s->profile_active?"ACTIVE":"");
  }
  if(s->writable&&profile_valid(s,s->profile_active)&&!s->profiles[s->profile_active].damaged){
   const char *name=s->profiles[s->profile_active].name;
   snprintf(my_profile_save,sizeof my_profile_save,"SAVE SETTINGS TO %s",name?name:"PROFILE");
   n=add(out,n,PK_PROFILE_SAVE,s->profile_active,armed_at==n?"PUSH AGAIN TO SAVE":my_profile_save);
   out[n-1].help="Saves the current settings into the active DJ's profile.";
   out[n-1].on=armed_at==n-1;
  }
 }
 n=add(out,n,PK_HEAD,0,"WAVEFORM");
 /* Short names: the group head already says WAVEFORM, and the panel is narrow. */
 static const int own[]={XZ_SHELL_ITEM_WAVE_COLOR,XZ_SHELL_ITEM_WAVE_POSITION,XZ_SHELL_ITEM_WAVE_ZOOM,XZ_SHELL_ITEM_USB_COLORS};
 static const char *const own_label[]={"COLOR","POSITION","ZOOM","USB COLORS"};
 for(size_t k=0;k<sizeof own/sizeof *own;k++){
  struct xz_shell_setting_item it;
  if(!sh_settings_item(u,m,own[k],&it))continue;
  n=add(out,n,PK_MY_OWN,own[k],own_label[k]);
  out[n-1].help=sh_item_help(m,XZ_SHELL_HELP_UTILITY,&it);
  choice_text(&out[n-1],&it);
 }
 for(int i=0;i<item_count(s->waveform);i++){
  const struct xz_shell_setting_item *it=&s->waveform->items[i];
  n=add(out,n,PK_MY_WAVE,i,it->label?it->label:"---");
  out[n-1].help=sh_item_help(m,s->waveform_tab+1,it);
  choice_text(&out[n-1],it);
  if(!it->writable)out[n-1].on=-1;   /* shown, greyed: nothing here changes it */
 }
 if(item_count(s->stored)){
  snprintf(my_stored_head,sizeof my_stored_head,"SAVED ON %s",stick_name(s));
  n=add(out,n,PK_HEAD,0,my_stored_head);
  for(int i=0;i<item_count(s->stored);i++){
   const struct xz_shell_setting_item *it=&s->stored->items[i];
   n=add(out,n,PK_MY_STORED,i,it->label?it->label:"---");
   out[n-1].help=sh_item_help(m,XZ_SHELL_HELP_MY_SETTINGS,it);
   choice_text(&out[n-1],it);
   if(!it->writable||!s->writable)out[n-1].on=-1;
  }
 }
 return n;
}
static int panel_items(const struct xz_shell *u, const struct xz_shell_model *m, struct pitem *out) {
 if(u->panel.kind==XZ_SHELL_PANEL_MY_SETTINGS)return my_items(u,m,out);
 if(u->panel.kind==XZ_SHELL_PANEL_FILTER)return filter_items(u,m,out);
 if(u->panel.kind==XZ_SHELL_PANEL_MENU)return menu_items(u,m,out);
 return 0;
}
static int selectable_item(const struct pitem *it) { return it->kind!=PK_HEAD&&it->kind!=PK_NOTE&&it->kind!=PK_MY_STATE; }
/* The panel cursor on a selectable row: the stored one, else the first. -1 when there is none. */
static int panel_cursor(const struct xz_shell *u, const struct pitem *items, int n) {
 int c=u->panel.cursor;
 if(c>=0&&c<n&&selectable_item(&items[c]))return c;
 for(int i=0;i<n;i++)if(selectable_item(&items[i]))return i;
 return -1;
}
/* MY SETTINGS keeps its foot for what the focused row does. */
enum { HELP_ROWS=2 };
static int panel_rows(const struct xz_shell *u) { return u->panel.kind==XZ_SHELL_PANEL_MY_SETTINGS?PANEL_ROWS-HELP_ROWS:PANEL_ROWS; }
static int panel_scroll(const struct xz_shell *u, int cursor, int n) {
 int rows=panel_rows(u);
 int sc=sh_max(0,sh_min(u->panel.scroll,n-rows));
 if(cursor>=0&&cursor<sc)sc=cursor;
 if(cursor>=sc+rows)sc=cursor-rows+1;
 return sh_max(0,sc);
}
static const char *panel_title(const struct xz_shell *u) {
 if(u->panel.kind==XZ_SHELL_PANEL_FILTER)return "TRACK FILTER";
 if(u->panel.kind==XZ_SHELL_PANEL_MY_SETTINGS)return "MY SETTINGS";
 switch(u->panel.page){
 case PAGE_ADD:return "ADD TO PLAYLIST";
 case PAGE_NEW:return "NEW PLAYLIST";
 case PAGE_RENAME:return "RENAME PLAYLIST";
 case PAGE_TAG_PLAYLIST:return "TAG LIST TO PLAYLIST";
 case PAGE_CUES:case PAGE_CUES+1:case PAGE_CUES+2:case PAGE_CUES+3:return "CUE/LOOP LIST";
 default:return "MENU";
 }
}
static int panel_open(const struct xz_shell *u) {
 return u->panel.kind==XZ_SHELL_PANEL_MENU||u->panel.kind==XZ_SHELL_PANEL_FILTER||u->panel.kind==XZ_SHELL_PANEL_MY_SETTINGS;
}
static int naming(const struct xz_shell *u) {
 return u->panel.kind==XZ_SHELL_PANEL_MENU&&(u->panel.page==PAGE_NEW||u->panel.page==PAGE_RENAME||u->panel.page==PAGE_TAG_PLAYLIST);
}
static void check_box(struct xz_canvas c, int x, int y, int on, uint32_t ink) {
 xz_border(c,x,y,14,14,ink);
 if(on)xz_rect(c,x+3,y+3,8,8,ink);
}
static void render_panel(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 struct pitem items[MAX_PITEMS];
 int n=panel_items(u,m,items),cur=panel_cursor(u,items,n),sc=panel_scroll(u,cur,n);
 int x0=PANEL_X,y0=HEAD_H,h=STRIP_Y-HEAD_H;
 xz_rect(c,x0,y0,INFO_W,h,SH_CHROME);
 xz_rect(c,x0,y0,1,h,SH_BLACK);
 /* title bar with a close cross */
 xz_rect(c,x0+1,y0,INFO_W-1,PANEL_HEAD,SH_RAIL);
 sh_text_fit(c,x0+14,y0+(PANEL_HEAD-SH_CAP_TEXT)/2,INFO_W-60,panel_title(u),SH_TEXT,SH_WHITE);
 int cx=x0+INFO_W-20,cy=y0+PANEL_HEAD/2;
 xz_line16(c,(cx-6)*16,(cy-6)*16,(cx+6)*16,(cy+6)*16,24,SH_WHITE);
 xz_line16(c,(cx+6)*16,(cy-6)*16,(cx-6)*16,(cy+6)*16,24,SH_WHITE);
 if(naming(u)){
  /* the name being typed, with the keyboard below the panel */
  int fy=y0+PANEL_HEAD+12;
  sh_text(c,x0+14,fy,"NAME",SH_SMALL,SH_DIM);
  xz_rect(c,x0+12,fy+14,INFO_W-24,32,SH_BLACK);xz_border(c,x0+12,fy+14,INFO_W-24,32,SH_BLUE);
  const char *t=u->name;int room=INFO_W-40;
  while(*t&&sh_text_width(c,t,SH_TEXT)>room)t++;
  sh_text(c,x0+20,fy+23,t,SH_TEXT,SH_WHITE);
  if(((m->clock_ms/500)&1)==0)xz_rect(c,x0+21+sh_text_width(c,t,SH_TEXT),fy+20,2,20,SH_BLUE);
  sh_text_fit(c,x0+14,fy+56,INFO_W-28,"TYPE, THEN SAVE OR PUSH",SH_SMALL,SH_DIM);
  return;
 }
 if(!n){
  sh_text_center(c,x0+INFO_W/2,y0+PANEL_HEAD+30,u->panel.kind==XZ_SHELL_PANEL_MY_SETTINGS?"THE STICK IS GONE":"NO OPTIONS FOR THIS LIST",SH_SMALL,SH_DIM);
  return;
 }
 struct xz_canvas body=xz_sub(c,x0+1,y0+PANEL_HEAD,INFO_W-1,h-PANEL_HEAD);
 int rows=panel_rows(u);
 for(int row=0;row<rows&&sc+row<n;row++){
  const struct pitem *it=&items[sc+row];
  int y=row*PANEL_ROW,w=INFO_W-1;
  if(it->kind==PK_HEAD){
   sh_text_fit(body,12,y+16,w-24,it->label,SH_SMALL,SH_DIM);
   xz_rect(body,12,y+PANEL_ROW-4,w-24,1,SH_RAIL);
   continue;
  }
  if(it->kind==PK_MY_STATE){
   sh_text_fit(body,12,y+(PANEL_ROW-SH_CAP_SMALL)/2,w-24,it->label,SH_SMALL,it->on?SH_GREEN:SH_KEY_AMBER);
   continue;
  }
  if(it->kind==PK_NOTE){
   sh_text_fit(body,12,y+(PANEL_ROW-SH_CAP_SMALL)/2,w-24,it->label,SH_SMALL,SH_DIM);
   continue;
  }
  int focused=sc+row==cur;
  int armed=u->panel.armed&&focused&&(it->kind==PK_F_FROM||it->kind==PK_F_TO);
  uint32_t bg=focused?(armed?SH_WHITE:SH_BLUE):SH_CHROME,ink=armed?SH_BLUE:it->on<0&&!focused?SH_DIM:SH_WHITE;
  if(it->kind==PK_DELETE&&it->on)bg=SH_RED;
  if((it->kind==PK_MY_SAVE||it->kind==PK_PROFILE_SAVE)&&it->on&&focused){bg=SH_WHITE;ink=SH_BLUE;}
  if((it->kind==PK_TAG_CLEAR||it->kind==PK_HISTORY_DELETE||it->kind==PK_HISTORY_ALL)&&it->on)bg=SH_RED;
  xz_rect(body,0,y+1,w,PANEL_ROW-2,bg);
  int x=12,right=w-12;
  switch(it->kind){
  case PK_SORT:
   xz_ring(body,x+7,y+PANEL_ROW/2,6,1,ink);
   if(it->on)xz_disc(body,x+7,y+PANEL_ROW/2,3,ink);
   if(it->on)sh_triangle(body,right-6,y+12,6,matches(m->browse,xz_shell_screen(u))&&m->browse->sort_descending,ink);
   x+=22;break;
  case PK_COLUMN:case PK_F_KEY:case PK_F_TAG:
   check_box(body,x,y+10,it->on,ink);x+=22;break;
  case PK_F_COLOR:
   xz_rect(body,x,y+10,14,14,it->swatch);
   if(it->on)xz_border(body,x-2,y+8,18,18,ink);
   x+=22;
   if(it->on)check_box(body,right-14,y+10,1,ink);
   break;
  case PK_CUE:
   xz_rect(body,x,y+11,4,12,it->swatch);
   x+=12;break;
  case PK_CUES_OPEN:
   sh_triangle(body,right-4,y+11,6,0,ink);
   right-=14;break;
  case PK_PROFILE:
   xz_rect(body,x,y+9,16,16,it->swatch);
   if(it->on)xz_border(body,x-2,y+7,20,20,ink);
   x+=24;break;
  case PK_ADD_OPEN:case PK_FILTER_OPEN:case PK_SEARCH:
   sh_triangle(body,right-4,y+11,6,0,ink);   /* points on into a sub-page */
   if(it->kind==PK_FILTER_OPEN&&it->on)xz_disc(body,right-20,y+PANEL_ROW/2,4,SH_KEY_AMBER);
   break;
  default:break;
  }
  /* The value gives way to the label down to 56 px, in the small face when that fits it whole: names
     are what tells the rows apart. */
  enum sh_face vface=SH_TEXT;
  int tw=it->text[0]?sh_min(sh_text_width(body,it->text,SH_TEXT),110):0;
  if(tw){
   int room=sh_max(56,right-x-sh_text_width(body,it->label?it->label:"",SH_TEXT)-10);
   if(tw>room&&sh_text_width(body,it->text,SH_SMALL)<=room){vface=SH_SMALL;tw=sh_text_width(body,it->text,SH_SMALL);}
   tw=sh_min(tw,room);
  }
  sh_text_fit(body,x,y+(PANEL_ROW-SH_CAP_TEXT)/2-1,right-x-tw-(tw?10:0),it->label,SH_TEXT,ink);
  uint32_t value_ink=focused?ink:it->kind==PK_PROFILE?(it->on>0?SH_WHITE:SH_DIM):it->on>0?SH_KEY_AMBER:SH_DIM;
  if(tw)sh_text_fit(body,right-tw-(it->kind==PK_F_COLOR?20:0),y+(PANEL_ROW-(vface==SH_TEXT?SH_CAP_TEXT:SH_CAP_SMALL))/2-1,tw,it->text,vface,value_ink);
 }
 if(rows<PANEL_ROWS&&cur>=0&&items[cur].help){
  /* what the focused row does */
  int hy=rows*PANEL_ROW;
  char lines[4][SH_WRAP_CHARS];
  int k=sh_wrap(items[cur].help,INFO_W-26,lines,4);
  xz_rect(body,0,hy,INFO_W-1,HELP_ROWS*PANEL_ROW,SH_BLACK);
  xz_rect(body,12,hy+3,INFO_W-25,1,SH_RAIL);
  for(int i=0;i<k;i++)sh_text(body,12,hy+10+i*14,lines[i],SH_SMALL,0xd8d8d8);
 }
 if(n>rows){
  int track=rows*PANEL_ROW-8,thumb=sh_max(12,track*rows/n),at=4+(track-thumb)*sc/(n-rows);
  xz_rect(body,INFO_W-5,at,3,thumb,SH_DIM);
 }
}

/* ---- panel actions ---- */
static size_t emit_playlist(struct xz_shell *u, enum xz_shell_playlist_op o, uint32_t playlist, uint32_t track, int32_t from, int32_t to, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 u->edit.op=o;u->edit.playlist=playlist;u->edit.track=track;u->edit.from=from;u->edit.to=to;
 if(o==XZ_SHELL_PL_CREATE||o==XZ_SHELL_PL_RENAME||o==XZ_SHELL_PL_FROM_TAG_LIST)snprintf(u->edit.name,sizeof u->edit.name,"%s",u->name);
 else u->edit.name[0]=0;
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_PLAYLIST,-1,(int32_t)playlist,(int)o});
}
static size_t apply_filter(struct xz_shell *u, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 return op(out,n,XZ_SHELL_BROWSE_FILTER,filter_active(&u->filter));
}
static int32_t master_bpm_x100(const struct xz_shell_model *m) {
 for(int d=0;d<sh_deck_count(m);d++){
  const struct xz_shell_deck *k=&m->deck[d];
  if(sh_has(k,XZ_SHELL_F_MASTER)&&k->master&&sh_has(k,XZ_SHELL_F_BPM)&&k->bpm_x100>0)return k->bpm_x100;
 }
 return 12000;
}
static void close_panel(struct xz_shell *u) { memset(&u->panel,0,sizeof u->panel); }
/* MY SETTINGS rows. SAVE rows arm on the first push and write on the second. */
static size_t my_activate(struct xz_shell *u, const struct xz_shell_model *m, const struct pitem *it, int was_armed, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 if(!my_open(m))return n;
 const struct xz_shell_my_settings_model *s=m->my_settings;
 int stick=(int)s->source;
 switch(it->kind){
 case PK_MY_LOAD:
  if(!s->on_stick||my_load_blocked(u,m))return n;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_MY_SETTINGS,u->deck,stick,XZ_SHELL_MY_LOAD});
 case PK_MY_SAVE:
  if(!s->writable)return n;
  if(!was_armed){u->panel.armed=1;return n;}
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_MY_SETTINGS,-1,stick,XZ_SHELL_MY_SAVE});
 case PK_PROFILE:
  if(!profile_valid(s,it->value)||!s->profiles_switchable||s->profiles[it->value].damaged||it->value==s->profile_active)return n;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_DJ_PROFILE,stick,it->value,XZ_SHELL_PROFILE_LOAD});
 case PK_PROFILE_SAVE:
  if(!s->writable||!profile_valid(s,it->value))return n;
  if(!was_armed){u->panel.armed=1;return n;}
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_DJ_PROFILE,stick,it->value,XZ_SHELL_PROFILE_SAVE});
 case PK_MY_OWN:{
  struct xz_shell_setting_item own;
  if(!sh_settings_item(u,m,it->value,&own))return n;
  int next=next_choice(&own);
  if(next<0)return n;
  struct xz_shell_action set[XZ_SHELL_ACTIONS];
  size_t k=sh_settings_set(u,it->value,next,set);
  for(size_t i=0;i<k;i++)n=emit(out,n,set[i]);
  return n;
 }
 case PK_MY_WAVE:{
  if(it->value>=item_count(s->waveform))return n;
  const struct xz_shell_setting_item *w=&s->waveform->items[it->value];
  int next=next_choice(w);
  if(!w->writable||next<0)return n;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_SET_MOD,s->waveform_tab,next,w->id});
 }
 case PK_MY_STORED:{
  if(it->value>=item_count(s->stored))return n;
  const struct xz_shell_setting_item *w=&s->stored->items[it->value];
  int next=next_choice(w);
  if(!s->writable||!w->writable||next<0)return n;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_MY_SETTING_SET,stick,next,w->id});
 }
 default:return n;
 }
}
static size_t activate_item(struct xz_shell *u, const struct xz_shell_model *m, const struct pitem *it, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 const struct xz_shell_browse_model *b=m->browse;
 const struct xz_shell_browse_row *r=highlight_row(u,m);
 struct xz_shell_filter *f=&u->filter;
 int was_armed=u->panel.armed;
 u->panel.armed=0;
 switch(it->kind){
 case PK_SORT:{
  int desc=it->on?!b->sort_descending:0;
  return op(out,n,XZ_SHELL_BROWSE_SORT,it->value|desc<<8);
 }
 case PK_COLUMN:{
  uint32_t mask=1u<<XZ_SHELL_COL_TITLE;
  for(int k=0;k<XZ_SHELL_COL_COUNT;k++)if(column_shown(b,k))mask|=1u<<k;
  mask^=1u<<it->value;
  return op(out,n,XZ_SHELL_BROWSE_SET_COLUMNS,(int32_t)mask);
 }
 case PK_FILTER_OPEN:close_panel(u);u->panel.kind=XZ_SHELL_PANEL_FILTER;return n;
 case PK_SEARCH:close_panel(u);return n+sh_key(u,m,XZ_SHELL_KEY_SEARCH,-1,out+n);
 case PK_ADD_OPEN:u->panel.page=PAGE_ADD;u->panel.cursor=u->panel.scroll=0;return n;
 case PK_ADD_TO:
  if(!r||r->kind!=XZ_SHELL_ROW_TRACK)return n;
  n=emit_playlist(u,XZ_SHELL_PL_ADD_TRACK,b->playlists[it->value].id,r->id,-1,-1,out,n);
  close_panel(u);return n;
 case PK_NEW:{
  int count=bknown(b,XZ_SHELL_BF_TOTAL)?(int)b->total:0;
  snprintf(u->name,sizeof u->name,"PLAYLIST %d",count+1);
  u->panel.page=PAGE_NEW;u->panel.cursor=0;return n;
 }
 case PK_RENAME:
  if(!r)return n;
  snprintf(u->name,sizeof u->name,"%s",rknown(r,XZ_SHELL_RF_TITLE)&&r->title?r->title:"");
  u->panel.page=PAGE_RENAME;u->panel.cursor=0;return n;
 case PK_SAVE:
  if(!u->name[0])return n;
  if(u->panel.page==PAGE_TAG_PLAYLIST)n=emit_playlist(u,XZ_SHELL_PL_FROM_TAG_LIST,0,0,-1,-1,out,n);
  else if(u->panel.page==PAGE_NEW)n=emit_playlist(u,XZ_SHELL_PL_CREATE,0,0,-1,-1,out,n);
  else if(r)n=emit_playlist(u,XZ_SHELL_PL_RENAME,r->id,0,-1,-1,out,n);
  close_panel(u);return n;
 case PK_TAG_TOGGLE:
  if(!r||r->kind!=XZ_SHELL_ROW_TRACK)return n;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_TAG_LIST,-1,(int32_t)r->id,
   r->tagged||xz_shell_screen(u)==XZ_SHELL_TAG_LIST?XZ_SHELL_TAG_REMOVE:XZ_SHELL_TAG_ADD});
 case PK_TAG_ALL:
  if(!r||r->kind!=XZ_SHELL_ROW_FOLDER)return n;
  close_panel(u);
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_TAG_LIST,-1,(int32_t)r->id,XZ_SHELL_TAG_ADD_LIST});
 case PK_TAG_CLEAR:
  if(!was_armed){u->panel.armed=1;return n;}
  close_panel(u);
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_TAG_LIST,-1,0,XZ_SHELL_TAG_CLEAR});
 case PK_TAG_PLAYLIST:{
  int count=bknown(b,XZ_SHELL_BF_TOTAL)?(int)b->total:0;
  snprintf(u->name,sizeof u->name,"TAG LIST %d",count);
  u->panel.page=PAGE_TAG_PLAYLIST;u->panel.cursor=0;return n;
 }
 case PK_PLAYED:
  if(!r||r->kind!=XZ_SHELL_ROW_TRACK)return n;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HISTORY,-1,(int32_t)r->id,r->played?XZ_SHELL_HISTORY_UNPLAYED:XZ_SHELL_HISTORY_PLAYED});
 case PK_HISTORY_DELETE:
  if(!r||r->kind!=XZ_SHELL_ROW_FOLDER)return n;
  if(!was_armed){u->panel.armed=1;return n;}
  close_panel(u);
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HISTORY,-1,(int32_t)r->id,XZ_SHELL_HISTORY_DELETE});
 case PK_HISTORY_ALL:
  if(!was_armed){u->panel.armed=1;return n;}
  close_panel(u);
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HISTORY,-1,0,XZ_SHELL_HISTORY_DELETE_ALL});
 case PK_CUES_OPEN:
  u->panel.page=PAGE_CUES+sh_active_deck(u,m);u->panel.cursor=u->panel.scroll=0;return n;
 case PK_CUE_DECK:
  u->panel.page=PAGE_CUES+(it->value+1)%sh_deck_count(m);return n;
 case PK_CUE:
  if(!r||!bknown(b,XZ_SHELL_BF_CUES)||b->cue_track!=r->id||it->value>=b->cue_count)return n;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_LOAD_CUE,u->panel.page-PAGE_CUES,it->value,(int)r->id});
 case PK_DELETE:
  if(!r||r->kind!=XZ_SHELL_ROW_FOLDER)return n;
  if(!was_armed){u->panel.armed=1;return n;}
  n=emit_playlist(u,XZ_SHELL_PL_DELETE,r->id,0,-1,-1,out,n);
  close_panel(u);return n;
 case PK_REMOVE:{
  if(!r)return n;
  const struct xz_shell_view *v=top_view(u);
  n=emit_playlist(u,XZ_SHELL_PL_REMOVE_TRACK,b->edit_playlist,r->id,v->cursor,-1,out,n);
  close_panel(u);return n;
 }
 case PK_MOVE:{
  if(!r)return n;
  const struct xz_shell_view *v=top_view(u);
  close_panel(u);
  u->grab=(struct xz_shell_grab){1,b->edit_playlist,r->id,v->cursor,v->cursor};
  return n;
 }
 case PK_F_BPM:
  if(f->bpm==XZ_SHELL_FILTER_BPM_OFF){f->bpm=XZ_SHELL_FILTER_BPM_MASTER;f->bpm_spread=bpm_spreads[0];}
  else if(f->bpm==XZ_SHELL_FILTER_BPM_MASTER){
   int i=0;while(i<4&&bpm_spreads[i]!=f->bpm_spread)i++;
   if(i+1<4)f->bpm_spread=bpm_spreads[i+1];
   else{
    int32_t mid=master_bpm_x100(m)/100*100;
    f->bpm=XZ_SHELL_FILTER_BPM_RANGE;f->bpm_min_x100=mid-500;f->bpm_max_x100=mid+500;
   }
  }else f->bpm=XZ_SHELL_FILTER_BPM_OFF;
  return apply_filter(u,out,n);
 case PK_F_FROM:case PK_F_TO:u->panel.armed=!was_armed;return n;
 case PK_F_KEY:f->key_compatible=!f->key_compatible;return apply_filter(u,out,n);
 case PK_F_RATING:f->rating_min=(f->rating_min+1)%6;return apply_filter(u,out,n);
 case PK_F_COLOR:f->colors^=1u<<it->value;return apply_filter(u,out,n);
 case PK_F_TAG:f->my_tags^=1u<<it->value;return apply_filter(u,out,n);
 case PK_F_CLEAR:memset(f,0,sizeof *f);return apply_filter(u,out,n);
 default:return my_activate(u,m,it,was_armed,out,n);
 }
}
/* A BPM bound being dialled: the encoder steps it 1 BPM, the other bound follows to stay ordered. */
static size_t dial_bound(struct xz_shell *u, int to, int delta, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 struct xz_shell_filter *f=&u->filter;
 int32_t *v=to?&f->bpm_max_x100:&f->bpm_min_x100;
 *v=sh_max(2000,sh_min(30000,*v+delta*100));
 if(f->bpm_min_x100>f->bpm_max_x100){if(to)f->bpm_min_x100=f->bpm_max_x100;else f->bpm_max_x100=f->bpm_min_x100;}
 return apply_filter(u,out,n);
}
static size_t panel_turn(struct xz_shell *u, const struct xz_shell_model *m, int delta, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct pitem items[MAX_PITEMS];
 int n=panel_items(u,m,items),cur=panel_cursor(u,items,n);
 if(cur<0)return 0;
 if(u->panel.armed&&(items[cur].kind==PK_F_FROM||items[cur].kind==PK_F_TO))return dial_bound(u,items[cur].kind==PK_F_TO,delta,out,0);
 int step=delta>0?1:-1;
 for(int k=delta>0?delta:-delta;k>0;k--){
  int next=cur+step;
  while(next>=0&&next<n&&!selectable_item(&items[next]))next+=step;
  if(next<0||next>=n)break;
  cur=next;
 }
 if(cur!=u->panel.cursor)u->panel.armed=0;
 u->panel.cursor=cur;
 u->panel.scroll=panel_scroll(u,cur,n);
 if(cur>0&&u->panel.scroll==cur&&!selectable_item(&items[cur-1]))u->panel.scroll=cur-1;
 return 0;
}
static size_t panel_push(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 struct pitem items[MAX_PITEMS];
 int n=panel_items(u,m,items),cur=panel_cursor(u,items,n);
 if(cur<0)return 0;
 u->panel.cursor=cur;
 return activate_item(u,m,&items[cur],out,0);
}
static size_t panel_touch(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 if(y<HEAD_H+PANEL_HEAD){
  if(x>=PANEL_X+INFO_W-44)close_panel(u);
  return 0;
 }
 if(naming(u))return 0;
 struct pitem items[MAX_PITEMS];
 int n=panel_items(u,m,items),cur=panel_cursor(u,items,n),sc=panel_scroll(u,cur,n);
 int row=(y-HEAD_H-PANEL_HEAD)/PANEL_ROW,i=sc+row;
 if(row>=panel_rows(u)||i<0||i>=n||!selectable_item(&items[i]))return 0;
 if(i!=cur)u->panel.armed=0;
 u->panel.cursor=i;
 return activate_item(u,m,&items[i],out,0);
}

static void render_keyboard(const struct xz_shell *u, struct xz_canvas c, const struct sess *v) {
 xz_rect(c,0,KB_Y,W,STRIP_Y-KB_Y,SH_CHROME);
 xz_rect(c,0,KB_Y,W,1,SH_BLACK);
 for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++){
  struct xz_shell_rect r=key_rect(i);
  int special=keys[i].code<' '||keys[i].code==' ';
  uint32_t fill=v->pressed==i?SH_BLUE:keys[i].code==KB_SEARCH?SH_BLUE:special?SH_RAIL:SH_TILE;
  if(v->pressed==i&&keys[i].code==KB_SEARCH)fill=SH_WHITE;
  xz_rect(c,r.x,r.y,r.w,r.h,fill);
  uint32_t ink=fill==SH_WHITE?SH_BLUE:SH_WHITE;
  const char *label=keys[i].code==KB_SEARCH&&naming(u)?"SAVE":keys[i].label;
  if(special)sh_text_center(c,r.x+r.w/2,r.y+(r.h-SH_CAP_SMALL)/2,label,SH_SMALL,ink);
  else sh_text_center(c,r.x+r.w/2,r.y+(r.h-SH_CAP_TEXT)/2,keys[i].label,SH_TEXT,ink);
 }
}

/* ---- deck status strip (both decks) ---- */
static enum xz_shell_time_mode time_mode_of(const struct xz_shell_model *m, const struct xz_shell_deck *d) {
 if(sh_has(d,XZ_SHELL_F_TIME_MODE))return d->time_mode;
 if(sh_setting(m,XZ_SHELL_SET_TIME_MODE))return m->settings.time_mode;
 return XZ_SHELL_TIME_REMAIN;
}
static void strip_overview(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, int deck, int x, int y, int w, int h) {
 const struct xz_shell_deck *d=&m->deck[deck];
 int32_t e=xz_shell_elapsed_now(m,deck);
 int timed=e>=0&&sh_has(d,XZ_SHELL_F_TOTAL)&&d->total_ms>0;
 int ph=timed?x+(int)((int64_t)e*(w-1)/d->total_ms):-1;
 xz_rect(c,x,y,w,h,SH_BLACK);
 xz_rect(c,x,y+h-1,w,1,SH_RAIL);
 sh_deck_overview(c,(struct xz_shell_rect){x,y,w,h},u,m,d,ph);
 if(ph>=0){
  int remain=time_mode_of(m,d)==XZ_SHELL_TIME_REMAIN;
  xz_rect(c,x,y+h+1,ph-x,2,remain?SH_RED:SH_WHITE);
  xz_rect(c,ph,y+h+1,x+w-ph,2,remain?SH_WHITE:SH_RAIL);
  xz_rect(c,ph,y,1,h,SH_PLAYHEAD);
 }else xz_rect(c,x,y+h+1,w,2,SH_RAIL);
}
static void render_strip(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 if(sh_deck_quarters(u,m,c,(struct xz_shell_rect){0,STRIP_Y,W,STRIP_H}))return;
 xz_rect(c,0,STRIP_Y,W,STRIP_H,SH_BLACK);
 for(int deck=0;deck<sh_deck_count(m);deck++){
  const struct xz_shell_deck *d=&m->deck[deck];
  int x0=deck*HALF_W+(deck?1:0),w=HALF_W-1,y=STRIP_Y+1;
  char text[24],frac[8];
  xz_rect(c,x0,y,w,STRIP_H-1,SH_CHROME);
  /* deck box: blue when it is the LOAD target */
  int active=u->deck==deck;
  int bx=x0+6,by=y+4,bw=42,bh=STRIP_H-10;
  if(active)xz_rect(c,bx,by,bw,bh,SH_BLUE);else xz_border(c,bx,by,bw,bh,SH_WHITE);
  sh_text_center(c,bx+bw/2,by+4,"DECK",SH_SMALL,SH_WHITE);
  snprintf(text,sizeof text,"%d",deck+1);
  sh_num(c,bx+(bw-sh_num_width(SH_NUM_MID,text))/2,by+18,text,SH_NUM_MID,SH_WHITE);
  int loaded_known=sh_has(d,XZ_SHELL_F_LOADED),loaded=loaded_known&&d->loaded;
  int tx=x0+56;
  if(loaded_known&&!loaded){sh_text_center(c,x0+56+(w-56)/2,y+(STRIP_H-SH_CAP_TEXT)/2-1,"Not Loaded.",SH_TEXT,SH_RED);continue;}
  /* time */
  enum xz_shell_time_mode mode=time_mode_of(m,d);
  sh_text(c,tx,y+6,mode==XZ_SHELL_TIME_REMAIN?"REMAIN":"TIME",SH_SMALL,SH_DIM);
  int32_t shown=-1,e=loaded?xz_shell_elapsed_now(m,deck):-1;
  if(e>=0){
   if(mode==XZ_SHELL_TIME_ELAPSED)shown=e;
   else if(sh_has(d,XZ_SHELL_F_TOTAL))shown=d->total_ms-e;
  }
  if(shown>=0){int32_t sec=shown/1000;snprintf(text,sizeof text,"%02d:%02d",(int)sh_min(sec/60,99),(int)(sec%60));}
  else snprintf(text,sizeof text,"--:--");
  sh_num(c,tx,y+22,text,SH_NUM_MID,shown>=0?SH_WHITE:SH_DIM);
  /* overview */
  int ox=x0+130,ow=152;
  if(loaded)strip_overview(u,m,c,deck,ox,y+6,ow,36);
  else{xz_rect(c,ox,y+6,ow,36,SH_BLACK);xz_rect(c,ox,y+43,ow,2,SH_RAIL);}
  /* tempo range, tempo, key */
  int px=x0+290;
  if(sh_has(d,XZ_SHELL_F_TEMPO_RANGE)){
   if(d->tempo_range>=100)snprintf(text,sizeof text,"WIDE");else snprintf(text,sizeof text,"\xb1%d",d->tempo_range);
   int tw=sh_text_width(c,text,SH_SMALL)+8;
   xz_rect(c,px,y+5,tw,13,SH_RED);sh_text(c,px+4,y+7,text,SH_SMALL,SH_WHITE);
  }else sh_text(c,px,y+7,"\xb1--",SH_SMALL,SH_DIM);
  if(sh_has(d,XZ_SHELL_F_TEMPO)){
   int32_t t=d->tempo_x100,a=t<0?-t:t;
   snprintf(text,sizeof text,"%s%d.%02d%%",t>0?"+":t<0?"-":"",(int)(a/100%1000),(int)(a%100));
   sh_text(c,px,y+23,text,SH_SMALL,SH_WHITE);
  }else sh_text(c,px,y+23,"--.--%",SH_SMALL,SH_DIM);
  char key[8];
  if(loaded&&sh_has(d,XZ_SHELL_F_KEY)&&d->key)sh_text_fit(c,px,y+37,44,sh_key_text(d->key,sh_key_display(u),key,sizeof key),SH_TEXT,
   sh_has(d,XZ_SHELL_F_MASTER)&&d->master?SH_WHITE:sh_key_ink(m,d->key,d->key_matches_master,SH_WHITE));
  else sh_text(c,px,y+37,"--",SH_TEXT,SH_DIM);
  /* BPM box */
  int qx=x0+w-62,qw=56,qy=y+4,qh=STRIP_H-10;
  xz_border(c,qx,qy,qw,qh,SH_WHITE);
  int master=sh_has(d,XZ_SHELL_F_MASTER)&&d->master;
  sh_text(c,qx+4,qy+3,"BPM",SH_SMALL,master?SH_RED:SH_WHITE);
  uint32_t ink=master?SH_RED:SH_WHITE;
  if(loaded&&sh_has(d,XZ_SHELL_F_BPM)&&d->bpm_x100>0){
   int32_t b=sh_min(d->bpm_x100,99999);
   snprintf(text,sizeof text,"%d",(int)(b/100));snprintf(frac,sizeof frac,".%d",(int)(b/10%10));
   int fw=sh_text_width(c,frac,SH_SMALL);
   sh_num_right(c,qx+qw-4-fw,qy+18,text,SH_NUM_MID,ink);
   sh_text(c,qx+qw-3-fw,qy+29,frac,SH_SMALL,ink);
  }else sh_num_right(c,qx+qw-5,qy+18,"---",SH_NUM_MID,SH_DIM);
 }
}

static void list_render(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 sync(u);
 enum xz_shell_screen s=xz_shell_screen(u);
 struct sess v=session(u,m->browse);
 xz_rect(c,0,0,c.width,c.height,SH_BLACK);
 render_header(u,m,c);
 if(s==XZ_SHELL_BROWSE)render_rail(u,m,c);
 render_list(u,m,c,&v);
 if(keyboard_shown(u))render_keyboard(u,c,&v);
 render_strip(u,m,c);
}

/* ---- input ---- */
static size_t emit(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, struct xz_shell_action a) {
 if(n<XZ_SHELL_ACTIONS)out[n++]=a;
 return n;
}
static size_t op(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, enum xz_shell_browse_op o, int32_t value) {
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_BROWSE,-1,value,(int)o});
}
static size_t hand_off(struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n, enum xz_shell_screen s) {
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_HAND_OFF,-1,0,(int)s});
}
static void remember(const struct xz_shell_browse_model *b, enum xz_shell_screen s, const struct xz_shell_view *v) {
 int k=slot_of(s);
 if(!bknown(b,XZ_SHELL_BF_LIST)||!matches(b,s))return;
 st.memory[k].valid=1;
 st.memory[k].list=b->list_id;
 st.memory[k].cursor=v->cursor;st.memory[k].scroll=v->scroll;
}
static void begin(struct xz_shell_view *v, const struct xz_shell_browse_model *b, enum xz_shell_screen s) {
 v->focus=s==XZ_SHELL_BROWSE?FOCUS_RAIL:FOCUS_LIST;
 if(s==XZ_SHELL_BROWSE){
  int entries[RAIL_MAX],n=0;
  if(b)for(int k=0;k<XZ_SHELL_CAT_COUNT;k++)if((b->category_known>>k)&1)entries[n++]=k;
  st.rail_cursor=rail_current(b,entries,n);rail_follow(n);
 }
 st.touched=-1;st.jump=0;st.pressed=-1;st.target=T_NONE;st.confirm=0;st.dragging=0;
 st.last_request=-1;st.keyboard_hidden=0;
 if(remembered(b,s)){v->cursor=st.memory[slot_of(s)].cursor;v->scroll=st.memory[slot_of(s)].scroll;}
}
static void reset_view(struct xz_shell_view *v) { v->cursor=v->scroll=0;st.touched=-1;st.last_request=-1; }
static size_t enter(struct xz_shell_view *v, enum xz_shell_screen s, const struct xz_shell_browse_row *r, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 int k=slot_of(s);
 if(st.up_n[k]<UP_DEPTH){st.up[k][st.up_n[k]].cursor=v->cursor;st.up[k][st.up_n[k]].scroll=v->scroll;}
 st.up_n[k]++;
 reset_view(v);
 return op(out,n,XZ_SHELL_BROWSE_OPEN,(int32_t)r->id);
}
static size_t up(struct xz_shell_view *v, enum xz_shell_screen s, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 int k=slot_of(s);
 v->focus=FOCUS_LIST;
 reset_view(v);
 if(st.up_n[k]>0){
  st.up_n[k]--;
  if(st.up_n[k]<UP_DEPTH){v->cursor=st.up[k][st.up_n[k]].cursor;v->scroll=st.up[k][st.up_n[k]].scroll;}
 }
 return op(out,n,XZ_SHELL_BROWSE_UP,0);
}
static size_t to_top(struct xz_shell_view *v, enum xz_shell_screen s, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 st.up_n[slot_of(s)]=0;reset_view(v);
 return op(out,n,XZ_SHELL_BROWSE_TOP,0);
}
/* LOAD returns to PLAY, like the flagship. Without the capability the stock browser takes over. */
static size_t load(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_browse_row *r, int deck, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 enum xz_shell_screen s=xz_shell_screen(u);
 if(!r||r->kind!=XZ_SHELL_ROW_TRACK)return n;
 if(!(m->capabilities&XZ_SHELL_CAP_LOAD))return hand_off(out,n,s);
 remember(m->browse,s,top_view(u));
 st.touched=-1;
 u->depth=1;
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_LOAD,deck,0,(int)r->id});
}
static void follow_cursor(struct xz_shell_view *v, int rows) {
 if(v->cursor<v->scroll)v->scroll=v->cursor;
 if(v->cursor>=v->scroll+rows)v->scroll=v->cursor-rows+1;
}
/* Ask for rows the view shows but the window lacks. Once per distinct start. */
static size_t request_window(const struct xz_shell_browse_model *b, enum xz_shell_screen s, const struct xz_shell_view *v, int rows, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 if(!matches(b,s))return n;
 int total_known=bknown(b,XZ_SHELL_BF_TOTAL);
 int32_t end=v->scroll+rows;
 if(total_known&&end>b->total)end=b->total;
 if(end<=v->scroll)return n;
 int covered=b->rows&&b->window_count>0&&b->window_start<=v->scroll&&b->window_start+b->window_count>=end;
 if(covered){st.last_request=-1;return n;}
 int32_t start=v->scroll-(XZ_SHELL_BROWSE_WINDOW-rows)/2;
 if(total_known&&start>b->total-XZ_SHELL_BROWSE_WINDOW)start=b->total-XZ_SHELL_BROWSE_WINDOW;
 if(start<0)start=0;
 if(start==st.last_request)return n;
 st.last_request=start;
 return op(out,n,XZ_SHELL_BROWSE_REQUEST_WINDOW,start);
}
static size_t turn(struct xz_shell *u, const struct xz_shell_model *m, int delta, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 int rows=xz_shell_browse_rows_visible(u);
 int32_t limit=row_limit(b,s);
 st.touched=-1;
 if(st.jump){
  if(matches(b,s)&&b->letters&&b->letter_count>0){
   int at=0;
   for(int i=0;i<b->letter_count;i++)if(b->letters[i].row<=v->cursor)at=i;
   at=sh_max(0,sh_min(b->letter_count-1,at+delta));
   v->cursor=b->letters[at].row;v->scroll=v->cursor;
  }else{v->cursor+=delta*rows;v->scroll+=delta*rows;}
 }else{
  v->cursor+=delta;
 }
 int want=v->cursor;
 clamp_view(&v->cursor,&v->scroll,limit,rows);
 follow_cursor(v,rows);
 /* The list end is unknown: ask for the rows past the window instead of stopping dead. */
 if(!bknown(b,XZ_SHELL_BF_TOTAL)&&matches(b,s)&&limit>0&&want>=limit)n=op(out,n,XZ_SHELL_BROWSE_REQUEST_WINDOW,sh_max(0,limit-rows));
 return n;
}
static size_t push(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 enum xz_shell_screen s=xz_shell_screen(u);
 struct xz_shell_view *v=&u->stack[u->depth-1];
 if(st.jump){st.jump=0;return n;}
 const struct xz_shell_browse_row *r=row_at(m->browse,s,v->cursor);
 if(!r)return n;
 if(r->kind==XZ_SHELL_ROW_FOLDER)return enter(v,s,r,out,n);
 return load(u,m,r,u->deck,out,n);
}
static size_t type_key(struct xz_shell_view *v, int i, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 size_t len=strlen(st.query);
 switch(keys[i].code){
 case KB_DEL:if(!len)return n;st.query[len-1]=0;break;
 case KB_CLEAR:if(!len)return n;st.query[0]=0;break;
 case KB_SEARCH:st.keyboard_hidden=1;return n;
 default:
  if(len+1>=sizeof st.query)return n;
  st.query[len]=keys[i].code;st.query[len+1]=0;
  break;
 }
 reset_view(v);
 return op(out,n,XZ_SHELL_BROWSE_SEARCH_QUERY,(int32_t)strlen(st.query));
}
static size_t type_name(struct xz_shell *u, const struct xz_shell_model *m, int i, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 size_t len=strlen(u->name);
 switch(keys[i].code){
 case KB_DEL:if(len)u->name[len-1]=0;return n;
 case KB_CLEAR:u->name[0]=0;return n;
 case KB_SEARCH:return n+panel_push(u,m,out+n);
 default:
  if(len+1<sizeof u->name){u->name[len]=keys[i].code;u->name[len+1]=0;}
  return n;
 }
}
static size_t drop(struct xz_shell *u, struct xz_shell_view *v, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 struct xz_shell_grab g=u->grab;
 u->grab.active=0;v->cursor=g.to;
 if(g.to==g.from)return n;
 u->edit=(struct xz_shell_playlist_edit){XZ_SHELL_PL_MOVE_TRACK,g.playlist,g.track,g.from,g.to,""};
 return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_PLAYLIST,-1,(int32_t)g.playlist,XZ_SHELL_PL_MOVE_TRACK});
}
static size_t category(struct xz_shell_view *v, const struct xz_shell_browse_model *b, enum xz_shell_screen s, int k, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 if(!((b->category_available>>k)&1))return hand_off(out,n,s);
 v->focus=FOCUS_LIST;
 st.up_n[slot_of(s)]=0;reset_view(v);
 return op(out,n,XZ_SHELL_BROWSE_CATEGORY,k);
}
static size_t touch_down(struct xz_shell *u, const struct xz_shell_model *m, int x, int y, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 size_t n=0;
 st.jump=0;st.target=T_OTHER;st.down_y=st.last_y=y;st.last_step=0;st.dragging=0;st.confirm=0;
 if(y<HEAD_HIT||(y<HEAD_H&&x<SRC_W)){
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_SOURCE,0),x,y))return sh_key(u,m,XZ_SHELL_KEY_SOURCE,-1,out);
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_BACK,0),x,y))return sh_back(u,m,out);
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_PREVIEW,0),x,y)){st.hide_preview=!st.hide_preview;return n;}
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_INFO,0),x,y))return sh_key(u,m,XZ_SHELL_KEY_INFO,-1,out);
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_FILTER,0),x,y)){
   enum xz_shell_panel_kind was=u->panel.kind;
   close_panel(u);u->grab.active=0;
   if(was!=XZ_SHELL_PANEL_FILTER)u->panel.kind=XZ_SHELL_PANEL_FILTER;
   return n;
  }
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_FILTER_CHIP,0),x,y)){memset(&u->filter,0,sizeof u->filter);return apply_filter(u,out,n);}
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_FIELD,0),x,y)){st.keyboard_hidden=!st.keyboard_hidden;follow_cursor(v,xz_shell_browse_rows_visible(u));return n;}
  return n;
 }
 for(int d=0;d<sh_deck_count(m);d++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_STRIP,d),x,y)){
  if(u->deck==d)return n;
  u->deck=d;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_ACTIVE_DECK,d,0,0});
 }
 for(int slot=0;slot<RAIL_SLOTS;slot++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_RAIL,slot),x,y)){
  st.target=T_RAIL;st.down_rail=slot;st.down_rail_first=st.rail_first;
  return n;
 }
 if(keyboard_shown(u)&&y>=KB_Y){
  for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++)if(sh_inside(key_rect(i),x,y)){
   st.target=T_KEY;st.pressed=i;
   return naming(u)?type_name(u,m,i,out,n):type_key(v,i,out,n);
  }
  return n;
 }
 if(panel_open(u)){
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_PANEL,0),x,y))return panel_touch(u,m,x,y,out);
  close_panel(u);   /* a touch beside an open panel puts it away */
  return n;
 }
 if(source_absent(m,s)){
  if(sh_inside(picker_button(s),x,y))return sh_key(u,m,XZ_SHELL_KEY_SOURCE,-1,out);
  return n;
 }
 if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_INFO_PANE,0),x,y))return n;
 if(u->grab.active){
  if(!sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_LIST,0),x,y))return n;
  int row=v->scroll+(y-LIST_Y)/ROW_H;
  if(row>=row_limit(b,s))return n;
  if(row==u->grab.to)return drop(u,v,out,n);
  u->grab.to=row;v->cursor=row;
  return n;
 }
 struct col cols[MAX_COLS];int count=layout_columns(b,s,cols);
 for(int i=0;i<count;i++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_COLUMN,i),x,y)){
  if(cols[i].kind==COL_PREVIEW||!matches(b,s))return n;
  int desc=bknown(b,XZ_SHELL_BF_SORT)&&(int)b->sort_column==cols[i].kind?!b->sort_descending:0;
  return op(out,n,XZ_SHELL_BROWSE_SORT,cols[i].kind|desc<<8);
 }
 for(int i=0;i<=sh_deck_count(m);i++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_LOAD,i),x,y)){
  const struct xz_shell_browse_row *r=row_at(b,s,st.touched);
  return load(u,m,r,i==0?u->deck:i-1,out,n);
 }
 if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_LIST,0),x,y)){
  v->focus=FOCUS_LIST;
  int row=v->scroll+(y-LIST_Y)/ROW_H;
  st.target=T_LIST;st.down_scroll=v->scroll;st.prev_cursor=v->cursor;st.prev_touched=st.touched;
  if(row<row_limit(b,s)&&row_at(b,s,row)){
   st.confirm=row==v->cursor&&st.touched==row;
   v->cursor=row;st.touched=row;
  }
 }
 return n;
}
static int rounded_steps(int distance, int step) { return distance>=0?(distance+step/2)/step:-((-distance+step/2)/step); }
static size_t touch_move(struct xz_shell *u, const struct xz_shell_model *m, int y) {
 enum xz_shell_screen s=xz_shell_screen(u);
 struct xz_shell_view *v=&u->stack[u->depth-1];
 int dy=y-st.down_y;
 st.last_step=y-st.last_y;st.last_y=y;
 if(st.target!=T_LIST&&st.target!=T_RAIL)return 0;
 if(!st.dragging&&(dy>=DRAG_START||dy<=-DRAG_START)){
  st.dragging=1;st.confirm=0;
  if(st.target==T_LIST){v->cursor=st.prev_cursor;st.touched=st.prev_touched;}
 }
 if(!st.dragging)return 0;
 if(st.target==T_RAIL){
  int entries[RAIL_MAX],count=rail_entries(m,entries);
  st.rail_first=sh_max(0,sh_min(count-RAIL_SLOTS,st.down_rail_first-rounded_steps(dy,RAIL_ITEM)));
  return 0;
 }
 v->scroll=st.down_scroll-rounded_steps(dy,ROW_H);
 int cursor=v->cursor;
 clamp_view(&cursor,&v->scroll,row_limit(m->browse,s),xz_shell_browse_rows_visible(u));
 return 0;
}
static size_t touch_up(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 size_t n=0;
 enum target t=st.target;
 st.target=T_NONE;st.pressed=-1;
 if(t==T_RAIL&&!st.dragging){
  int entries[RAIL_MAX],count=rail_entries(m,entries),i=rail_first(count)+st.down_rail;
  if(i<count)st.rail_cursor=i;
  if(i<count&&entries[i]==RAIL_SEARCH)return sh_key(u,m,XZ_SHELL_KEY_SEARCH,-1,out);
  if(i<count)return category(v,b,s,entries[i],out,n);
  return n;
 }
 if(t!=T_LIST)return n;
 if(st.dragging){
  /* A fast last step carries on, like a flick. */
  if(st.last_step>=FLICK_STEP||st.last_step<=-FLICK_STEP)v->scroll-=st.last_step*4/ROW_H;
  int cursor=v->cursor;
  clamp_view(&cursor,&v->scroll,row_limit(b,s),xz_shell_browse_rows_visible(u));
  return n;
 }
 if(st.confirm){
  st.confirm=0;
  const struct xz_shell_browse_row *r=row_at(b,s,v->cursor);
  if(r&&r->kind==XZ_SHELL_ROW_FOLDER)return enter(v,s,r,out,n);
 }
 return n;
}
static size_t key_input(struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_key k, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 struct xz_shell_view *v=&u->stack[u->depth-1];
 st.jump=0;
 if(!matches(b,s)||depth_of(b)==0)return 0;
 if(k==XZ_SHELL_KEY_BACK)return up(v,s,out,0);
 if(k==XZ_SHELL_KEY_BACK_HOLD)return to_top(v,s,out,0);
 return 0;
}
/* BACK on a list: put away a grab, a sub-page, a panel or the jump overlay, else climb one level. */
size_t sh_browse_back_step(struct xz_shell *u, const struct xz_shell_model *m, struct xz_shell_action out[XZ_SHELL_ACTIONS], int *taken) {
 enum xz_shell_screen s=xz_shell_screen(u);
 struct xz_shell_view *v=&u->stack[u->depth-1];
 *taken=1;
 if(u->grab.active){v->cursor=u->grab.from;u->grab.active=0;return 0;}
 if(u->panel.kind==XZ_SHELL_PANEL_MENU&&(u->panel.page!=PAGE_MAIN||u->panel.armed)){
  if(u->panel.armed)u->panel.armed=0;else{u->panel.page=PAGE_MAIN;u->panel.cursor=u->panel.scroll=0;}
  return 0;
 }
 if((u->panel.kind==XZ_SHELL_PANEL_FILTER||u->panel.kind==XZ_SHELL_PANEL_MY_SETTINGS)&&u->panel.armed){u->panel.armed=0;return 0;}
 if(u->panel.kind!=XZ_SHELL_PANEL_NONE){close_panel(u);return 0;}
 if(st.jump){st.jump=0;return 0;}
 if(list_screen(s)&&matches(m->browse,s)&&depth_of(m->browse)>0&&!source_absent(m,s)){
  if(!v->focus)begin(v,m->browse,s);
  return up(v,s,out,0);
 }
 if(s==XZ_SHELL_BROWSE&&v->focus==FOCUS_LIST&&!source_absent(m,s)){
  int entries[RAIL_MAX],n=rail_entries(m,entries);
  v->focus=FOCUS_RAIL;st.touched=-1;
  st.rail_cursor=rail_current(m->browse,entries,n);rail_follow(n);
  return 0;
 }
 *taken=0;
 return 0;
}
int xz_shell_browse_back(struct xz_shell *u, const struct xz_shell_model *m) {
 if(!u||!m||u->depth<1||u->depth>XZ_SHELL_STACK)return 0;
 enum xz_shell_screen s=xz_shell_screen(u);
 return list_screen(s)&&matches(m->browse,s)&&depth_of(m->browse)>0;
}
/* shell.c offers LOAD and the list-level BACK keys to this screen first. */
static size_t list_input(struct xz_shell *u, const struct xz_shell_model *m, const struct xz_shell_event *e, struct xz_shell_action out[XZ_SHELL_ACTIONS]) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
 if(!list_screen(s))return 0;
 if(!b)return e->kind==XZ_SHELL_KEY?0:hand_off(out,0,s);
 struct xz_shell_view *v=&u->stack[u->depth-1];
 if(e->kind==XZ_SHELL_KEY&&e->key==XZ_SHELL_KEY_LOAD){
  const struct xz_shell_browse_row *r=xz_shell_browse_highlight(u,m);
  if(!r||r->kind!=XZ_SHELL_ROW_TRACK||e->deck<0||e->deck>=sh_deck_count(m))return 0;
  if((m->capabilities&XZ_SHELL_CAP_LOAD)&&!v->focus)begin(v,b,s);
  return load(u,m,r,e->deck,out,0);
 }
 int depth=u->depth;
 sync(u);
 if(!v->focus)begin(v,b,s);
 size_t n=0;
 /* An open panel or a picked-up track takes the encoder. */
 if(panel_open(u)&&(e->kind==XZ_SHELL_ENCODER_TURN||e->kind==XZ_SHELL_ENCODER_PUSH||e->kind==XZ_SHELL_ENCODER_HOLD)){
  if(e->kind==XZ_SHELL_ENCODER_TURN)return panel_turn(u,m,e->delta,out);
  if(e->kind==XZ_SHELL_ENCODER_PUSH)return panel_push(u,m,out);
  return 0;
 }
 if(u->grab.active&&(e->kind==XZ_SHELL_ENCODER_TURN||e->kind==XZ_SHELL_ENCODER_PUSH)){
  if(e->kind==XZ_SHELL_ENCODER_PUSH)return drop(u,v,out,0);
  int32_t limit=row_limit(b,s);
  u->grab.to=sh_max(0,sh_min((int)limit-1,u->grab.to+e->delta));
  v->cursor=u->grab.to;
  follow_cursor(v,xz_shell_browse_rows_visible(u));
  return request_window(b,s,v,xz_shell_browse_rows_visible(u),out,0);
 }
 if(source_absent(m,s)&&e->kind!=XZ_SHELL_TOUCH_DOWN)return 0;
 if(rail_focused(u)&&(e->kind==XZ_SHELL_ENCODER_TURN||e->kind==XZ_SHELL_ENCODER_PUSH||e->kind==XZ_SHELL_ENCODER_HOLD)){
  int entries[RAIL_MAX],count=rail_entries(m,entries);
  if(!count)return 0;
  if(e->kind==XZ_SHELL_ENCODER_TURN){st.rail_cursor+=e->delta;rail_follow(count);return 0;}
  if(e->kind==XZ_SHELL_ENCODER_HOLD)return 0;
  rail_follow(count);
  int k=entries[st.rail_cursor];
  if(k==RAIL_SEARCH)return sh_key(u,m,XZ_SHELL_KEY_SEARCH,-1,out);
  if(bknown(b,XZ_SHELL_BF_CATEGORY)&&k==(int)b->category){v->focus=FOCUS_LIST;return 0;}
  return category(v,b,s,k,out,0);
 }
 switch(e->kind){
 case XZ_SHELL_KEY:n=key_input(u,m,e->key,out);break;
 case XZ_SHELL_TOUCH_DOWN:n=touch_down(u,m,e->x,e->y,out);break;
 case XZ_SHELL_TOUCH_MOVE:n=touch_move(u,m,e->y);break;
 case XZ_SHELL_TOUCH_UP:n=touch_up(u,m,out);break;
 case XZ_SHELL_ENCODER_TURN:n=turn(u,m,e->delta,out,0);break;
 case XZ_SHELL_ENCODER_PUSH:n=push(u,m,out,0);break;
 case XZ_SHELL_ENCODER_HOLD:st.jump=!st.jump;st.touched=-1;break;
 }
 if(u->depth!=depth||xz_shell_screen(u)!=s)return n;
 int rows=xz_shell_browse_rows_visible(u);
 clamp_view(&v->cursor,&v->scroll,row_limit(b,s),rows);
 n=request_window(b,s,v,rows,out,n);
 remember(b,s,v);
 return n;
}
const struct sh_screen xz_shell_browse_screen={list_render,list_input};
const struct sh_screen xz_shell_playlist_screen={list_render,list_input};
const struct sh_screen xz_shell_tag_list_screen={list_render,list_input};
const struct sh_screen xz_shell_search_screen={list_render,list_input};
