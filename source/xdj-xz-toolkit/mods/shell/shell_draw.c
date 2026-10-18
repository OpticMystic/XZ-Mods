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
void sh_num(struct xz_canvas c, int x, int y, const char *s, enum sh_num_face face, uint32_t color) {
 const struct xz_shell_glyph *table=num_face(face);
 if(!s)return;
 for(;*s;s++){
  const struct xz_shell_glyph *g=num_glyph(table,(unsigned char)*s);int shift;
  int advance=num_advance(table,g,(unsigned char)*s,&shift);
  for(unsigned row=0;row<g->height;row++)for(unsigned col=0;col<g->width;col++)
   xz_plot(c,x+shift+g->x+(int)col,y+g->y+(int)row,color,xz_shell_font_coverage[g->offset+row*g->width+col]);
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
