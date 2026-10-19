#include "shell_internal.h"
#include "shell_font.h"
#include <string.h>

#define SH_NO_LIMIT 4096

int sh_text_width(struct xz_canvas c, const char *s, enum sh_face face) { return xz_text_width(c, s, face, SH_NO_LIMIT); }
void sh_text(struct xz_canvas c, int x, int y, const char *s, enum sh_face face, uint32_t color) { xz_text(c, x, y, s, face, SH_NO_LIMIT, color); }

static size_t utf8_step(const char *s) {
 unsigned char ch=(unsigned char)*s;
 size_t n=ch<0xc2?1:ch<0xe0?2:ch<0xf0?3:4,i;
 for(i=1;i<n;i++)if((s[i]&0xc0)!=0x80)return i;
 return n;
}
/* A string that does not fit is cut back to a word boundary (when one is in the second half) and ends in "...". */
void sh_text_fit(struct xz_canvas c, int x, int y, int width, const char *s, enum sh_face face, uint32_t color) {
 char buffer[256];
 size_t used=0;
 if(!s||width<=0)return;
 if(sh_text_width(c,s,face)<=width&&strlen(s)<sizeof(buffer)){sh_text(c,x,y,s,face,color);return;}
 int room=width-sh_text_width(c,"...",face);
 while(*s&&used<sizeof(buffer)-5){
  size_t step=utf8_step(s);
  memcpy(buffer+used,s,step);buffer[used+step]=0;
  if(sh_text_width(c,buffer,face)>room)break;
  used+=step;s+=step;
 }
 size_t cut=used;
 while(cut>used/2&&buffer[cut-1]!=' ')cut--;
 if(cut>used/2)used=cut;
 while(used&&(buffer[used-1]==' '||buffer[used-1]=='('||buffer[used-1]=='['||buffer[used-1]=='-'))used--;
 if(room>0)memcpy(buffer+used,"...",4);else buffer[used]=0;
 sh_text(c,x,y,buffer,face,color);
}
void sh_text_right(struct xz_canvas c, int right, int y, const char *s, enum sh_face face, uint32_t color) {
 sh_text(c,right-sh_text_width(c,s,face),y,s,face,color);
}
void sh_text_center(struct xz_canvas c, int cx, int y, const char *s, enum sh_face face, uint32_t color) {
 sh_text(c,cx-sh_text_width(c,s,face)/2,y,s,face,color);
}

static const struct xz_shell_glyph *num_face(enum sh_num_face face) {
 return face==SH_NUM_BIG?xz_shell_font_big:face==SH_NUM_BPM?xz_shell_font_bpm:xz_shell_font_mid;
}
int sh_num_cap(enum sh_num_face face) {
 return face==SH_NUM_BIG?XZ_SHELL_FONT_BIG_CAP:face==SH_NUM_BPM?XZ_SHELL_FONT_BPM_CAP:XZ_SHELL_FONT_MID_CAP;
}
static const struct xz_shell_glyph *num_glyph(const struct xz_shell_glyph *table, unsigned char ch) {
 const char *at=ch?strchr(XZ_SHELL_FONT_CHARSET,ch):NULL;
 return at?&table[at-XZ_SHELL_FONT_CHARSET]:&table[XZ_SHELL_FONT_GLYPHS-1];
}
/* Digits advance like 0 and sit centred in that cell, so a ticking clock does not shiver. */
static int num_advance(const struct xz_shell_glyph *table, const struct xz_shell_glyph *g, unsigned char ch, int *shift) {
 if(ch>='0'&&ch<='9'){int cell=table[0].advance;*shift=(cell-g->advance)/2;return cell;}
 *shift=0;return g->advance;
}
int sh_num_width(enum sh_num_face face, const char *s) {
 const struct xz_shell_glyph *table=num_face(face);int x=0,shift;
 if(!s)return 0;
 for(;*s;s++)x+=num_advance(table,num_glyph(table,(unsigned char)*s),(unsigned char)*s,&shift);
 return x;
}
/* Coverage blend in RGB565 with shifts: the big clock and BPM digits are redrawn every frame. */
static void glyph(struct xz_canvas c, int x, int y, const struct xz_shell_glyph *g, uint32_t color) {
 int sr=(int)((color>>19)&31),sg=(int)((color>>10)&63),sb=(int)((color>>3)&31);
 uint16_t solid=(uint16_t)(sr<<11|sg<<5|sb);
 int x0=x+g->x,y0=y+g->y;
 for(int row=0;row<(int)g->height;row++){
  int py=y0+row;
  if(py<0||py>=c.height)continue;
  const unsigned char *cov=xz_shell_font_coverage+g->offset+(size_t)row*g->width;
  uint16_t *line=c.p+(size_t)py*c.stride;
  for(int col=0;col<(int)g->width;col++){
   int a=cov[col],px=x0+col;
   if(!a||px<0||px>=c.width)continue;
   if(a>=255){line[px]=solid;continue;}
   unsigned d=line[px];int k=a+(a>>7);
   int r=(int)(d>>11),gg=(int)((d>>5)&63),b=(int)(d&31);
   r=(r*(256-k)+sr*k)>>8;gg=(gg*(256-k)+sg*k)>>8;b=(b*(256-k)+sb*k)>>8;
   line[px]=(uint16_t)(r<<11|gg<<5|b);
  }
 }
}
void sh_num(struct xz_canvas c, int x, int y, const char *s, enum sh_num_face face, uint32_t color) {
 const struct xz_shell_glyph *table=num_face(face);
 if(!s||!c.p)return;
 for(;*s;s++){
  const struct xz_shell_glyph *g=num_glyph(table,(unsigned char)*s);int shift;
  int advance=num_advance(table,g,(unsigned char)*s,&shift);
  glyph(c,x+shift,y,g,color);
  x+=advance;
 }
}
void sh_num_right(struct xz_canvas c, int right, int y, const char *s, enum sh_num_face face, uint32_t color) {
 sh_num(c,right-sh_num_width(face,s),y,s,face,color);
}

void sh_triangle(struct xz_canvas c, int cx, int y, int size, int down, uint32_t color) {
 for(int row=0;row<size;row++){
  int half=down?size-1-row:row;
  xz_rect(c,cx-half,y+row,half*2+1,1,color);
 }
}
void sh_note(struct xz_canvas c, int x, int y, uint32_t color) {
 xz_disc(c,x+3,y+10,3,color);
 xz_rect(c,x+5,y,2,10,color);
 xz_line16(c,(x+6)*16,y*16,(x+10)*16,(y+4)*16,24,color);
}
void sh_usb_icon(struct xz_canvas c, int x, int y, uint32_t color) {
 xz_rect(c,x+4,y+2,2,10,color);
 xz_disc(c,x+5,y+13,2,color);
 sh_triangle(c,x+5,y-1,3,0,color);
 xz_rect(c,x,y+5,4,2,color);xz_rect(c,x,y+5,2,4,color);xz_disc(c,x+1,y+9,1,color);
 xz_rect(c,x+6,y+3,4,2,color);xz_rect(c,x+8,y+3,2,4,color);xz_rect(c,x+8,y+7,2,2,color);
}
void sh_info_icon(struct xz_canvas c, int cx, int cy, uint32_t color) {
 xz_ring(c,cx,cy,7,1,color);
 xz_rect(c,cx-1,cy-4,2,2,color);
 xz_rect(c,cx-1,cy-1,2,5,color);
}
int sh_chip(struct xz_canvas c, int x, int y, const char *s, uint32_t outline, uint32_t ink, int filled) {
 int w=sh_text_width(c,s,SH_SMALL)+12;
 if(filled)xz_rect(c,x,y,w,16,outline);else xz_border(c,x,y,w,16,outline);
 sh_text(c,x+6,y+3,s,SH_SMALL,ink);
 return w;
}
void sh_placeholder(struct xz_canvas c, const char *title, const char *note) {
 xz_rect(c,0,0,c.width,c.height,SH_BLACK);
 xz_rect(c,0,0,c.width,40,SH_CHROME);
 sh_text(c,16,12,title,SH_TEXT,SH_WHITE);
 sh_text_center(c,c.width/2,c.height/2-20,title,SH_TEXT,SH_DIM);
 sh_text_center(c,c.width/2,c.height/2+4,note,SH_SMALL,SH_DIM);
 sh_text_center(c,c.width/2,c.height/2+22,"BACK RETURNS TO THE PLAY SCREEN",SH_SMALL,SH_DIM);
}

/* ---- runtime notice toast ----
   A dark box centred over the middle of the screen, clear of the header
   buttons. A sentence too wide for one line breaks at the space that balances
   the two lines best; a second line that still does not fit ends in "...". */
enum { NOTICE_TEXT_MAX=560, NOTICE_PAD=20, NOTICE_LINE=24, NOTICE_TOP=196, NOTICE_MIN_W=220 };
static const struct xz_canvas notice_measure={NULL,XZ_SHELL_WIDTH,XZ_SHELL_WIDTH,XZ_SHELL_HEIGHT,XZ_THEME_FLAGSHIP};
int sh_notice_shown(const struct xz_shell_model *m) {
 /* The time comes first: an expired notice's pointer may already be retired. */
 return m->clock_ms-m->notice_at_ms<(uint32_t)XZ_SHELL_NOTICE_MS&&m->notice&&*m->notice;
}
/* Byte offset of the space to break at, or -1 for one line. */
static int notice_break(const char *text) {
 char buffer[256];
 if(sh_text_width(notice_measure,text,SH_TEXT)<=NOTICE_TEXT_MAX)return -1;
 int best=-1,best_w=1<<30;
 for(int i=1;text[i]&&i<(int)sizeof buffer-1;i++){
  if(text[i]!=' ')continue;
  memcpy(buffer,text,(size_t)i);buffer[i]=0;
  int first=sh_text_width(notice_measure,buffer,SH_TEXT);
  if(first>NOTICE_TEXT_MAX)break;
  int second=sh_text_width(notice_measure,text+i+1,SH_TEXT);
  int worst=first>second?first:second;
  if(worst<best_w){best_w=worst;best=i;}
 }
 return best;
}
struct xz_shell_rect sh_notice_rect(const char *text, int *lines) {
 int cut=text?notice_break(text):-1,w;
 if(lines)*lines=cut<0?1:2;
 if(!text)return (struct xz_shell_rect){0,0,0,0};
 if(cut<0)w=sh_text_width(notice_measure,text,SH_TEXT);
 else{
  char first[256];memcpy(first,text,(size_t)cut);first[cut]=0;
  w=sh_max(sh_text_width(notice_measure,first,SH_TEXT),sh_text_width(notice_measure,text+cut+1,SH_TEXT));
 }
 w=sh_max(NOTICE_MIN_W,sh_min(NOTICE_TEXT_MAX,w)+2*NOTICE_PAD);
 int h=(cut<0?1:2)*NOTICE_LINE+20;
 return (struct xz_shell_rect){(XZ_SHELL_WIDTH-w)/2,NOTICE_TOP-(cut<0?0:NOTICE_LINE/2),w,h};
}
/* The runtime's notice, or the shell's own when that is the newer one. */
static const char *notice_text(const struct xz_shell *u, const struct xz_shell_model *m) {
 int shell_live=u->notice&&m->clock_ms-u->notice_at_ms<(uint32_t)XZ_SHELL_NOTICE_MS&&*u->notice;
 int model_live=sh_notice_shown(m);
 if(shell_live&&(!model_live||(int32_t)(u->notice_at_ms-m->notice_at_ms)>=0))return u->notice;
 return model_live?m->notice:NULL;
}
void sh_notice(const struct xz_shell *u, const struct xz_shell_model *m, struct xz_canvas c) {
 const char *text=notice_text(u,m);
 if(!text)return;
 struct xz_shell_model shown=*m;shown.notice=text;
 m=&shown;
 int lines;
 struct xz_shell_rect r=sh_notice_rect(m->notice,&lines);
 xz_rect(c,r.x-1,r.y-1,r.w+2,r.h+2,SH_BLACK);
 xz_rect(c,r.x,r.y,r.w,r.h,SH_ROW_ALT);
 xz_border(c,r.x,r.y,r.w,r.h,XZ_FLAGSHIP_RAIL_SELECTED);
 xz_rect(c,r.x,r.y,r.w,3,SH_BLUE);
 int y=r.y+12;
 int room=r.w-2*NOTICE_PAD;
 if(lines==1){
  int w=sh_min(sh_text_width(c,m->notice,SH_TEXT),room);
  sh_text_fit(c,r.x+(r.w-w)/2,y,room,m->notice,SH_TEXT,SH_WHITE);
  return;
 }
 int cut=notice_break(m->notice);
 char first[256];memcpy(first,m->notice,(size_t)cut);first[cut]=0;
 sh_text_center(c,r.x+r.w/2,y,first,SH_TEXT,SH_WHITE);
 const char *second=m->notice+cut+1;
 int sw=sh_min(sh_text_width(c,second,SH_TEXT),room);
 sh_text_fit(c,r.x+(r.w-sw)/2,y+NOTICE_LINE,room,second,SH_TEXT,SH_WHITE);
}
