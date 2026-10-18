/* BROWSE, PLAYLIST, TAG LIST and SEARCH at 800x480, after the flagship
   player's browse screen (the 2026-10-18 flagship UI study, sections 3.2,
   3.3 and 6.2): category rail, one-level breadcrumb header, sortable column
   row, 38 px rows with a LOAD chip in the touched row, a PREVIEW column and
   an INFO pane, a letter/page jump overlay, an on-screen keyboard for SEARCH,
   and the two-deck status strip at the bottom. */
#include "shell_internal.h"
#include "shell_browse.h"
#include <stdio.h>
#include <string.h>

enum {
 W=XZ_SHELL_WIDTH, HEAD_H=44, HEAD_HIT=36, SRC_W=64, RAIL_W=64, RAIL_ITEM=47, RAIL_SLOTS=8,
 COL_Y=44, COL_H=36, LIST_Y=80, ROW_H=38, ROWS=9, SEARCH_ROWS=4,
 STRIP_Y=422, STRIP_H=XZ_SHELL_HEIGHT-STRIP_Y, HALF_W=W/2,
 KB_Y=232, KB_ROW=47, KB_KEY_H=44, KB_X=8, KB_W=W-16, KB_UNITS=24,
 INFO_W=264, TILE_W=84, BACK_X=68, BACK_S=32, TITLE_MIN=200, LOAD_W=56, LOAD_DECK_W=34,
 UP_DEPTH=8, SLOTS=4, DRAG_START=10, FLICK_STEP=12,
 HEAD_RIGHT=XZ_SHELL_HEADER_X-4   /* the mod header buttons own the bar right of this */
};
enum { SH_RAIL_SEL=XZ_FLAGSHIP_RAIL_SELECTED, SH_TILE=XZ_FLAGSHIP_TILE, SH_UNAVAILABLE=0x6a6a6a, SH_STAR_OFF=0x5a5a5a };

/* ---- module state ----
   The view (cursor, scroll) lives in the shell's stack; focus==0 marks a view
   this module has not started yet. Everything else a list needs between
   events is here: one shell drives these screens at a time. */
enum target { T_NONE, T_LIST, T_RAIL, T_KEY, T_OTHER };
static struct {
 int touched, jump, pressed;
 int hide_preview, info_open, keyboard_hidden;
 char query[XZ_SHELL_BROWSE_QUERY];
 int32_t last_request;
 int rail_first;
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
 return xz_shell_screen(u)==XZ_SHELL_SEARCH&&(top_view(u)->focus?!st.keyboard_hidden:1);
}
int xz_shell_browse_rows_visible(const struct xz_shell *u) { return keyboard_shown(u)?SEARCH_ROWS:ROWS; }
static int list_x(enum xz_shell_screen s) { return s==XZ_SHELL_BROWSE?RAIL_W:0; }
static int list_w(enum xz_shell_screen s) { return W-list_x(s)-(st.info_open?INFO_W:0); }

enum { COL_PREVIEW=XZ_SHELL_COL_COUNT, COL_KINDS };
struct col { int kind, x, w; };
static const int col_width[COL_KINDS]={
 [XZ_SHELL_COL_NUMBER]=60,[XZ_SHELL_COL_TITLE]=0,[XZ_SHELL_COL_ARTIST]=170,[XZ_SHELL_COL_BPM]=76,
 [XZ_SHELL_COL_KEY]=60,[XZ_SHELL_COL_RATING]=78,[XZ_SHELL_COL_COLOR]=72,[XZ_SHELL_COL_TIME]=64,[COL_PREVIEW]=112
};
/* Least useful first; TITLE always stays. */
static const int drop_order[]={XZ_SHELL_COL_COLOR,XZ_SHELL_COL_RATING,XZ_SHELL_COL_TIME,XZ_SHELL_COL_ARTIST,COL_PREVIEW,XZ_SHELL_COL_NUMBER,XZ_SHELL_COL_BPM,XZ_SHELL_COL_KEY};
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

struct xz_shell_rect xz_shell_browse_rect(const struct xz_shell *u, const struct xz_shell_model *m, enum xz_shell_browse_region r, int index) {
 static const struct xz_shell_rect none={0,0,0,0};
 if(!u||u->depth<1||u->depth>XZ_SHELL_STACK)return none;
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m?m->browse:NULL;
 int rows=xz_shell_browse_rows_visible(u);
 switch(r){
 case XZ_SHELL_BR_SOURCE:return (struct xz_shell_rect){0,0,SRC_W,HEAD_H};
 case XZ_SHELL_BR_BACK:return (struct xz_shell_rect){SRC_W,0,BACK_S+12,HEAD_HIT};
 case XZ_SHELL_BR_FIELD:return s==XZ_SHELL_SEARCH?(struct xz_shell_rect){112,0,HEAD_RIGHT-2*TILE_W-2-8-112,HEAD_HIT}:none;
 case XZ_SHELL_BR_PREVIEW:return (struct xz_shell_rect){HEAD_RIGHT-2*TILE_W-2,0,TILE_W,HEAD_HIT};
 case XZ_SHELL_BR_INFO:return (struct xz_shell_rect){HEAD_RIGHT-TILE_W,0,TILE_W,HEAD_HIT};
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
  if(index<0||index>2||!v->focus||st.touched<0||st.touched!=v->cursor||i<0||i>=rows)return none;
  const struct xz_shell_browse_row *t_row=row_at(b,s,st.touched);
  if(!t_row||t_row->kind!=XZ_SHELL_ROW_TRACK)return none;
  struct col t=title_col(b,s);
  int right=t.x+t.w-6,x=right-LOAD_W-2*LOAD_DECK_W;
  int y=LIST_Y+i*ROW_H;
  if(index==0)return (struct xz_shell_rect){x,y,LOAD_W,ROW_H};
  return (struct xz_shell_rect){x+LOAD_W+(index-1)*LOAD_DECK_W,y,LOAD_DECK_W,ROW_H};
 }
 case XZ_SHELL_BR_INFO_PANE:return st.info_open?(struct xz_shell_rect){W-INFO_W,HEAD_H,INFO_W,STRIP_Y-HEAD_H}:none;
 case XZ_SHELL_BR_KEY:return keyboard_shown(u)&&index>=0&&index<XZ_SHELL_BROWSE_KEYS?key_rect(index):none;
 case XZ_SHELL_BR_STRIP:return index>=0&&index<XZ_SHELL_DECKS?(struct xz_shell_rect){index*HALF_W,STRIP_Y,HALF_W,STRIP_H}:none;
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
static int rail_entries(const struct xz_shell_browse_model *b, int out[XZ_SHELL_CAT_COUNT]) {
 int n=0;
 if(b)for(int k=0;k<XZ_SHELL_CAT_COUNT;k++)if((b->category_known>>k)&1)out[n++]=k;
 return n;
}
static int rail_first(int entries) { return sh_max(0,sh_min(st.rail_first,entries-RAIL_SLOTS)); }
static void render_rail(const struct xz_shell_browse_model *b, struct xz_canvas c) {
 int entries[XZ_SHELL_CAT_COUNT],n=rail_entries(b,entries),first=rail_first(n);
 xz_rect(c,0,HEAD_H,RAIL_W,STRIP_Y-HEAD_H,SH_RAIL);
 for(int slot=0;slot<RAIL_SLOTS&&first+slot<n;slot++){
  int k=entries[first+slot],y=HEAD_H+slot*RAIL_ITEM;
  int current=bknown(b,XZ_SHELL_BF_CATEGORY)&&(int)b->category==k;
  int available=(b->category_available>>k)&1;
  uint32_t ink=current?SH_WHITE:available?0xe6e6e6:SH_UNAVAILABLE;
  if(current)xz_rect(c,0,y,RAIL_W,RAIL_ITEM-1,SH_RAIL_SEL);
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
 tile(c,pv,"PREVIEW",!st.hide_preview);
 tile(c,in,"INFO",st.info_open);
 for(int i=0;i<5;i++)xz_rect(c,in.x+16-i,in.y+13+i,1,9-2*i,SH_WHITE);
 if(s==XZ_SHELL_SEARCH){render_field(u,m,c);return;}
 int right=pv.x-10;
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
 xz_rect(c,l.x,y,LOAD_W+2*LOAD_DECK_W,h,fill);
 sh_text_center(c,l.x+LOAD_W/2,y+(h-SH_CAP_SMALL)/2,live?"LOAD":"LOAD OFF",SH_SMALL,ink);
 for(int d=0;d<XZ_SHELL_DECKS;d++){
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
 const struct xz_shell_browse_row *r=row_at(b,s,index);
 int32_t limit=row_limit(b,s);
 int selected=index<limit&&index==v->cursor&&r;
 uint32_t bg=selected?SH_BLUE:index%2?SH_ROW_ALT:SH_BLACK;
 xz_rect(c,list_x(s),y,list_w(s),ROW_H,bg);
 for(int i=1;i<n;i++)if(!selected||cols[i-1].kind==COL_PREVIEW)xz_rect(c,cols[i].x,y,1,ROW_H,selected?SH_BLACK:SH_RAIL);
 if(!r)return;
 char text[32];
 int ty=y+(ROW_H-SH_CAP_TEXT)/2;
 int touched=v->touched==index&&r->kind==XZ_SHELL_ROW_TRACK;
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
   if(touched)right=k->x+k->w-6-LOAD_W-2*LOAD_DECK_W-8;
   else if(rknown(r,XZ_SHELL_RF_ON_DECK))for(int d=XZ_SHELL_DECKS-1;d>=0;d--)if((r->on_deck>>d)&1){
    right-=14;deck_badge(c,right,y+12,d,selected?SH_WHITE:SH_BLUE,selected?SH_BLUE:SH_WHITE);right-=4;
   }
   if(r->kind==XZ_SHELL_ROW_FOLDER)folder_icon(c,x,y+12,SH_WHITE);
   else if(s==XZ_SHELL_TAG_LIST)check_icon(c,x-1,y+12,SH_WHITE);
   else sh_note(c,x+1,y+11,!selected&&rknown(r,XZ_SHELL_RF_PLAYED)&&r->played?SH_GREEN:SH_WHITE);
   if(rknown(r,XZ_SHELL_RF_TITLE)&&r->title)sh_text_fit(c,x+22,ty,right-(x+22),r->title,SH_TEXT,SH_WHITE);
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
   if(rknown(r,XZ_SHELL_RF_KEY)&&r->key){char key[8];sh_text_fit(c,x,ty,room,sh_key_text(r->key,sh_key_display(u),key,sizeof key),SH_TEXT,!selected&&r->key_matches_master?SH_GREEN:SH_WHITE);}
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
  case 1:known=rknown(r,XZ_SHELL_RF_KEY)&&r->key;if(known){char key[8];snprintf(text,sizeof text,"%.20s",sh_key_text(r->key,sh_key_display(u),key,sizeof key));if(r->key_matches_master)ink=SH_GREEN;}break;
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
    for(int d=XZ_SHELL_DECKS-1;d>=0;d--)if((r->on_deck>>d)&1){right-=14;deck_badge(c,right,y+1,d,SH_BLUE,SH_WHITE);right-=4;}
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
static void render_list(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c, const struct sess *v) {
 enum xz_shell_screen s=xz_shell_screen(u);
 const struct xz_shell_browse_model *b=m->browse;
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
 if(st.info_open)render_info_pane(u,m,c,v);
 if(v->jump)render_jump(u,m,c,v);
}
static void render_keyboard(const struct xz_shell *u, struct xz_canvas c, const struct sess *v) {
 (void)u;
 xz_rect(c,0,KB_Y,W,STRIP_Y-KB_Y,SH_CHROME);
 xz_rect(c,0,KB_Y,W,1,SH_BLACK);
 for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++){
  struct xz_shell_rect r=key_rect(i);
  int special=keys[i].code<' '||keys[i].code==' ';
  uint32_t fill=v->pressed==i?SH_BLUE:keys[i].code==KB_SEARCH?SH_BLUE:special?SH_RAIL:SH_TILE;
  if(v->pressed==i&&keys[i].code==KB_SEARCH)fill=SH_WHITE;
  xz_rect(c,r.x,r.y,r.w,r.h,fill);
  uint32_t ink=fill==SH_WHITE?SH_BLUE:SH_WHITE;
  if(special)sh_text_center(c,r.x+r.w/2,r.y+(r.h-SH_CAP_SMALL)/2,keys[i].label,SH_SMALL,ink);
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
 if(sh_has(d,XZ_SHELL_F_OVERVIEW)&&d->overview.columns&&d->overview.count)
  sh_wave_overview(c,(struct xz_shell_rect){x,y,w,h},&d->overview,sh_wave_color(u),ph);
 else xz_rect(c,x,y+h-1,w,1,SH_RAIL);
 if(ph>=0){
  int remain=time_mode_of(m,d)==XZ_SHELL_TIME_REMAIN;
  xz_rect(c,x,y+h+1,ph-x,2,remain?SH_RED:SH_WHITE);
  xz_rect(c,ph,y+h+1,x+w-ph,2,remain?SH_WHITE:SH_RAIL);
  xz_rect(c,ph,y,1,h,SH_PLAYHEAD);
 }else xz_rect(c,x,y+h+1,w,2,SH_RAIL);
}
static void render_strip(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 xz_rect(c,0,STRIP_Y,W,STRIP_H,SH_BLACK);
 for(int deck=0;deck<XZ_SHELL_DECKS;deck++){
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
  if(loaded&&sh_has(d,XZ_SHELL_F_KEY)&&d->key)sh_text_fit(c,px,y+37,44,sh_key_text(d->key,sh_key_display(u),key,sizeof key),SH_TEXT,d->key_matches_master?SH_GREEN:SH_WHITE);
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
 enum xz_shell_screen s=xz_shell_screen(u);
 struct sess v=session(u,m->browse);
 xz_rect(c,0,0,c.width,c.height,SH_BLACK);
 render_header(u,m,c);
 if(s==XZ_SHELL_BROWSE)render_rail(m->browse,c);
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
 v->focus=1;
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
static size_t category(struct xz_shell_view *v, const struct xz_shell_browse_model *b, enum xz_shell_screen s, int k, struct xz_shell_action out[XZ_SHELL_ACTIONS], size_t n) {
 if(!((b->category_available>>k)&1))return hand_off(out,n,s);
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
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_BACK,0),x,y)){
   if(matches(b,s)&&depth_of(b)>0)return up(v,s,out,n);
   return sh_key(u,m,XZ_SHELL_KEY_BACK,-1,out);
  }
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_PREVIEW,0),x,y)){st.hide_preview=!st.hide_preview;return n;}
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_INFO,0),x,y)){st.info_open=!st.info_open;return n;}
  if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_FIELD,0),x,y)){st.keyboard_hidden=!st.keyboard_hidden;follow_cursor(v,xz_shell_browse_rows_visible(u));return n;}
  return n;
 }
 for(int d=0;d<XZ_SHELL_DECKS;d++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_STRIP,d),x,y)){
  if(u->deck==d)return n;
  u->deck=d;
  return emit(out,n,(struct xz_shell_action){XZ_SHELL_ACT_ACTIVE_DECK,d,0,0});
 }
 for(int slot=0;slot<RAIL_SLOTS;slot++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_RAIL,slot),x,y)){
  st.target=T_RAIL;st.down_rail=slot;st.down_rail_first=st.rail_first;
  return n;
 }
 if(keyboard_shown(u)&&y>=KB_Y){
  for(int i=0;i<XZ_SHELL_BROWSE_KEYS;i++)if(sh_inside(key_rect(i),x,y)){st.target=T_KEY;st.pressed=i;return type_key(v,i,out,n);}
  return n;
 }
 if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_INFO_PANE,0),x,y))return n;
 struct col cols[MAX_COLS];int count=layout_columns(b,s,cols);
 for(int i=0;i<count;i++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_COLUMN,i),x,y)){
  if(cols[i].kind==COL_PREVIEW||!matches(b,s))return n;
  int desc=bknown(b,XZ_SHELL_BF_SORT)&&(int)b->sort_column==cols[i].kind?!b->sort_descending:0;
  return op(out,n,XZ_SHELL_BROWSE_SORT,cols[i].kind|desc<<8);
 }
 for(int i=0;i<3;i++)if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_LOAD,i),x,y)){
  const struct xz_shell_browse_row *r=row_at(b,s,st.touched);
  return load(u,m,r,i==0?u->deck:i-1,out,n);
 }
 if(sh_inside(xz_shell_browse_rect(u,m,XZ_SHELL_BR_LIST,0),x,y)){
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
  int entries[XZ_SHELL_CAT_COUNT],count=rail_entries(m->browse,entries);
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
  int entries[XZ_SHELL_CAT_COUNT],count=rail_entries(b,entries),i=rail_first(count)+st.down_rail;
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
  if(!r||r->kind!=XZ_SHELL_ROW_TRACK||e->deck<0||e->deck>=XZ_SHELL_DECKS)return 0;
  if((m->capabilities&XZ_SHELL_CAP_LOAD)&&!v->focus)begin(v,b,s);
  return load(u,m,r,e->deck,out,0);
 }
 int depth=u->depth;
 if(!v->focus)begin(v,b,s);
 size_t n=0;
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
