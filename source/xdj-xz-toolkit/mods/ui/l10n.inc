/* SPDX-License-Identifier: MIT */
#include "l10n.h"
#include <string.h>
#include <limits.h>
struct xz_l10n_component { uint32_t offset:21,length:11; uint16_t width; uint8_t height,codec; };
struct xz_l10n_run { uint32_t component:15,x:12,y:5; };
struct xz_l10n_mask { uint32_t offset:22,length:10; uint16_t width; uint8_t height,rtl; };
struct xz_l10n_record { uint32_t english_offset; struct xz_l10n_mask masks[2]; };
_Static_assert(sizeof(struct xz_l10n_component)==8,"component accounting");
_Static_assert(sizeof(struct xz_l10n_run)==4,"run accounting");
_Static_assert(sizeof(struct xz_l10n_mask)==8,"mask accounting");
_Static_assert(sizeof(struct xz_l10n_record)==20,"record accounting");
#include "l10n_data.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define XZ_L10N_COUNT COUNT(xz_l10n_records)
#define XZ_L10N_MESSAGES (XZ_L10N_COUNT/14)
#ifdef XZ_L10N_PROOF
static unsigned char xz_l10n_seen[XZ_L10N_COUNT];
void xz_l10n_proof_reset(void){memset(xz_l10n_seen,0,sizeof xz_l10n_seen);}
void xz_l10n_proof_dump(void){for(size_t i=0;i<XZ_L10N_COUNT;i++)if(xz_l10n_seen[i])printf("PAINT %zu %s\n",i,xz_l10n_english+xz_l10n_records[i].english_offset);}
static void xz_l10n_mark(const struct xz_l10n_mask *m){uintptr_t a=(uintptr_t)m,b=(uintptr_t)xz_l10n_records;if(a<b||a-b>=sizeof(xz_l10n_records))return;size_t i=(a-b)/sizeof(xz_l10n_records[0]);if(m==&xz_l10n_records[i].masks[0]||m==&xz_l10n_records[i].masks[1])xz_l10n_seen[i]=1;}
#else
#define xz_l10n_mark(m) ((void)0)
#endif
static unsigned xz_l10n_locale;
void xz_l10n_frame(unsigned locale){xz_l10n_locale=locale<=14?locale:0;}
const char *xz_l10n_authored(const char *s){return xz_l10n_label(xz_l10n_locale,s);}
const char *xz_l10n_language(unsigned locale){
 static const char *const names[]={"English","LANGUAGE es","LANGUAGE fr","LANGUAGE de","LANGUAGE pt","LANGUAGE it","LANGUAGE ja","LANGUAGE ko","LANGUAGE zh-Hans","LANGUAGE zh-Hant","LANGUAGE nl","LANGUAGE pl","LANGUAGE tr","LANGUAGE uk","LANGUAGE ar"};
 return locale>0&&locale<15?xz_l10n_label(locale,names[locale]):names[0];
}
const char *xz_l10n_label(unsigned locale,const char *s){
 if(!s||!locale||locale>14)return s;
 for(size_t i=0;i<XZ_L10N_MESSAGES;i++)if(!strcmp(s,xz_l10n_english+xz_l10n_records[i].english_offset))
  return xz_l10n_english+xz_l10n_records[(locale-1)*XZ_L10N_MESSAGES+i].english_offset;
 return s;
}
static int xz_l10n_component_valid(const struct xz_l10n_component *p){
 if(!p->width||p->width>4096||!p->height||p->height>32||p->codec>2||p->offset>COUNT(xz_l10n_alpha)||p->length>COUNT(xz_l10n_alpha)-p->offset)return 0;
 unsigned area=p->width*p->height,total=0;
 if(!p->codec)return p->length==(area+1)/2;
 if(p->codec==2){for(unsigned i=0;i<p->length;i++){unsigned n=(xz_l10n_alpha[p->offset+i]>>4)+1;if(n>area-total)return 0;total+=n;}return total==area;}
 if(p->length&1)return 0;
 for(unsigned i=0;i<p->length;i+=2){unsigned n=xz_l10n_alpha[p->offset+i];if(!n||n>area-total)return 0;total+=n;}
 return total==area;
}
static int xz_l10n_run_valid(const struct xz_l10n_run *r,unsigned width,unsigned height,unsigned previous){
 if(r->component>=COUNT(xz_l10n_components))return 0;
 const struct xz_l10n_component *p=&xz_l10n_components[r->component];
 return xz_l10n_component_valid(p)&&r->x>=previous&&r->x+p->width<=width&&r->y+p->height<=height;
}
static int xz_l10n_valid(const struct xz_l10n_mask *m){
 if(!m->width||m->width>4096||!m->height||m->height>32||m->rtl>1||!m->length||m->offset>COUNT(xz_l10n_runs)||m->length>COUNT(xz_l10n_runs)-m->offset)return 0;
 unsigned previous=0;
 for(unsigned i=0;i<m->length;i++){
  const struct xz_l10n_run *r=&xz_l10n_runs[m->offset+i];if(!xz_l10n_run_valid(r,m->width,m->height,previous))return 0;
  const struct xz_l10n_component *p=&xz_l10n_components[r->component];
  previous=r->x+p->width;
 }return 1;
}
static const struct xz_l10n_mask *xz_l10n_mask_for(const char *s,int scale,int limit){
 if(!s||scale<1||scale>2||limit<=0)return NULL;
 uintptr_t address=(uintptr_t)s,base=(uintptr_t)xz_l10n_english;
 if(address<base||address-base>=sizeof(xz_l10n_english))return NULL;
 size_t offset=address-base,lo=0,hi=XZ_L10N_COUNT;
 while(lo<hi){size_t mid=lo+(hi-lo)/2;if(xz_l10n_records[mid].english_offset<offset)lo=mid+1;else hi=mid;}
 if(lo<XZ_L10N_COUNT&&xz_l10n_records[lo].english_offset==offset){const struct xz_l10n_mask *m=&xz_l10n_records[lo].masks[scale-1];if(xz_l10n_valid(m))return m;}
 return NULL;
}
static void xz_l10n_part(struct xz_canvas c,int64_t x,int64_t y,const struct xz_l10n_component *p,uint32_t color,int64_t clip_left,int64_t clip_right){
 unsigned area=p->width*p->height,pos=0,index=0,left=0,alpha=0;
 while(pos<area){
  if(!p->codec){unsigned b=xz_l10n_alpha[p->offset+pos/2];alpha=((pos&1)?b&15:b>>4)*17;}
  else {if(!left){if(p->codec==2){unsigned b=xz_l10n_alpha[p->offset+index++];left=(b>>4)+1;alpha=(b&15)*17;}else{left=xz_l10n_alpha[p->offset+index];alpha=xz_l10n_alpha[p->offset+index+1];index+=2;}}left--;}
  int64_t px=x+pos%p->width,py=y+pos/p->width;
  if(alpha&&px>=clip_left&&px<clip_right&&px>=0&&py>=0&&px<c.width&&py<c.height)xz_plot(c,(int)px,(int)py,color,alpha);
  pos++;
 }
}
static void xz_l10n_paint(struct xz_canvas c,int x,int y,const struct xz_l10n_mask *m,uint32_t color){
 if(!xz_l10n_valid(m))return;
 xz_l10n_mark(m);
 for(unsigned i=0;i<m->length;i++){const struct xz_l10n_run *r=&xz_l10n_runs[m->offset+i];xz_l10n_part(c,(int64_t)x+r->x,(int64_t)y+r->y,&xz_l10n_components[r->component],color,0,c.width);}
}
/* Return -1 only for ordinary text. Nonnegative results never trigger English fallback. */
int xz_l10n_wrap(struct xz_canvas c,int x,int y,int width,int pitch,int lines,const char *s,uint32_t color){
 const struct xz_l10n_mask *m=xz_l10n_mask_for(s,1,INT_MAX);if(!m)return -1;
 if(width<=0||pitch<(int)m->height||lines<=0)return 0;
 xz_l10n_mark(m);
 unsigned end=m->length,begin=0;int row=0;
 while(begin<end&&row<lines){
  unsigned a,b;
  if(!m->rtl){a=begin;b=a+1;while(b<end){const struct xz_l10n_run *r=&xz_l10n_runs[m->offset+b];if(r->x+xz_l10n_components[r->component].width-xz_l10n_runs[m->offset+a].x>(unsigned)width)break;b++;}if(b<end){unsigned cut=b;while(cut>a+1){const struct xz_l10n_run *r=&xz_l10n_runs[m->offset+cut],*prev=r-1;if(r->x-prev->x-xz_l10n_components[prev->component].width>=3)break;cut--;}if(cut>a+1)b=cut;}begin=b;}
  else {b=end;a=b-1;unsigned edge=xz_l10n_runs[m->offset+b-1].x+xz_l10n_components[xz_l10n_runs[m->offset+b-1].component].width;while(a>begin&&edge-xz_l10n_runs[m->offset+a-1].x<=(unsigned)width)a--;if(a>begin){unsigned cut=a;while(cut+1<b){const struct xz_l10n_run *r=&xz_l10n_runs[m->offset+cut],*prev=r-1;if(r->x-prev->x-xz_l10n_components[prev->component].width>=3)break;cut++;}if(cut+1<b)a=cut;}end=a;}
  unsigned origin=xz_l10n_runs[m->offset+a].x;
  unsigned edge=xz_l10n_runs[m->offset+b-1].x+xz_l10n_components[xz_l10n_runs[m->offset+b-1].component].width;
  int64_t dx=(int64_t)x+(m->rtl?width-(int)(edge-origin):0);
  for(unsigned i=a;i<b;i++){const struct xz_l10n_run *r=&xz_l10n_runs[m->offset+i];xz_l10n_part(c,dx+r->x-origin,(int64_t)y+(int64_t)row*pitch+r->y,&xz_l10n_components[r->component],color,x,(int64_t)x+width);}row++;
 }return row;
}
int xz_l10n_height(const char *s,int scale,int fallback){const struct xz_l10n_mask *m=xz_l10n_mask_for(s,scale,INT_MAX);return m?(int)m->height:fallback;}

int xz_l10n_box_height(const char *s,int width,int pitch){
 const struct xz_l10n_mask *m=xz_l10n_mask_for(s,1,INT_MAX);if(!m)return -1;
 struct xz_canvas empty={0};
 int rows=xz_l10n_wrap(empty,0,0,width,pitch,4096,s,0);
 if(!rows)return 0;
 int64_t height=(int64_t)(rows-1)*pitch+m->height;
 return height>INT_MAX?-2:(int)height;
}
