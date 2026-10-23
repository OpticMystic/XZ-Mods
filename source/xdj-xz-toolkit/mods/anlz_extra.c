/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#include "anlz_extra.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_CAP (16u << 20)
#define GRID_CAP 100000u

static uint32_t be32(const uint8_t *b) { return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3]; }
static uint16_t be16(const uint8_t *b) { return (uint16_t)(b[0]<<8|b[1]); }

/* The next section named tag after offset *at (0 = from the start): its offset and total length. */
static int section(const uint8_t *f, size_t n, const char tag[4], uint32_t *at, uint32_t *head, uint32_t *total) {
 if(!f||n<12||memcmp(f,"PMAI",4))return 0;
 uint32_t length=be32(f+8);if(length>n)length=(uint32_t)n;
 uint32_t p=*at?*at+be32(f+*at+8):be32(f+4);
 while(p>=12&&(uint64_t)p+12<=length){
  uint32_t h=be32(f+p+4),t=be32(f+p+8);
  if(h<12||t<h||t>length-p)return 0;
  if(!memcmp(f+p,tag,4)){*at=p;*head=h;*total=t;return 1;}
  p+=t;
 }
 return 0;
}

static void put(struct xz_extra_cue out[XZ_EXTRA_HOT_CUES], int *count, uint32_t hot, uint8_t type, uint32_t ms,
                uint32_t end, uint32_t rgb) {
 if(hot<1||hot>XZ_EXTRA_HOT_CUES||(type!=1&&type!=2)||ms>INT32_MAX||out[hot-1].set)return;
 /* Manual p.54: a pad lights green for a cue and orange for a loop unless rekordbox set a colour. */
 if(!rgb)rgb=type==2?XZ_ANLZ_LOOP_RGB:XZ_ANLZ_CUE_RGB;
 int32_t end_ms=type==2&&end!=0xffffffffu&&end>ms&&end<=INT32_MAX?(int32_t)end:0;
 out[hot-1]=(struct xz_extra_cue){1,(int32_t)ms,rgb,end_ms};(*count)++;
}

int xz_anlz_hot_cues(const uint8_t *f, size_t n, struct xz_extra_cue out[XZ_EXTRA_HOT_CUES]) {
 memset(out,0,sizeof(*out)*XZ_EXTRA_HOT_CUES);
 int count=-1;uint32_t at=0,head,total;
 while(section(f,n,"PCO2",&at,&head,&total)){
  if(head<20||be32(f+at+12)!=1)continue;
  if(count<0)count=0;
  for(uint32_t e=at+head;(uint64_t)e+44<=(uint64_t)at+total;){
   uint32_t eh=be32(f+e+4),et=be32(f+e+8);
   if(memcmp(f+e,"PCP2",4)||eh<16||et<eh||et<44||et>at+total-e)break;
   uint32_t clen=be32(f+e+40),rgb=0;
   if(clen<=et-44&&(uint64_t)44+clen+4<=et){const uint8_t *k=f+e+44+clen;if(k[0])rgb=(uint32_t)k[1]<<16|(uint32_t)k[2]<<8|k[3];}
   put(out,&count,be32(f+e+12),f[e+16],be32(f+e+20),be32(f+e+24),rgb);
   e+=et;
  }
 }
 if(count>=0)return count;
 at=0;
 while(section(f,n,"PCOB",&at,&head,&total)){
  if(head<24||be32(f+at+12)!=1)continue;
  if(count<0)count=0;
  for(uint32_t e=at+head;(uint64_t)e+40<=(uint64_t)at+total;){
   uint32_t eh=be32(f+e+4),et=be32(f+e+8);
   if(memcmp(f+e,"PCPT",4)||eh<28||et<eh||et<40||et>at+total-e)break;
   put(out,&count,be32(f+e+12),f[e+28],be32(f+e+32),be32(f+e+36),0);
   e+=et;
  }
 }
 return count;
}

int xz_anlz_beat_grid(const uint8_t *f, size_t n, struct xz_extra_beat **grid, uint32_t *count, int32_t *bpm_x100) {
 *grid=NULL;*count=0;*bpm_x100=0;
 uint32_t at=0,head,total;
 if(!section(f,n,"PQTZ",&at,&head,&total)||head<24)return -1;
 uint32_t c=be32(f+at+20);
 if(!c||c>GRID_CAP||(uint64_t)c*8>total-head)return -1;
 struct xz_extra_beat *g=calloc((size_t)c,sizeof(*g));
 if(!g)return -1;
 const uint8_t *e=f+at+head;
 for(uint32_t i=0;i<c;i++,e+=8){
  uint16_t beat=be16(e);uint32_t ms=be32(e+4);
  g[i].ms=ms>INT32_MAX?INT32_MAX:(int32_t)ms;
  g[i].beat_in_bar=(uint8_t)(beat<1?1:beat>4?4:beat);
  uint16_t bpm=be16(e+2);
  g[i].bpm_x100=bpm>=2000&&bpm<=60000?bpm:0;
 }
 *grid=g;*count=c;*bpm_x100=g[0].bpm_x100;
 return 0;
}

static uint8_t *read_all(const char *path, size_t *n) {
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint8_t *data=NULL;
 if(!fseek(f,0,SEEK_END)){
  long size=ftell(f);
  if(size>0&&(unsigned long)size<=FILE_CAP&&!fseek(f,0,SEEK_SET)&&(data=malloc((size_t)size))){
   *n=fread(data,1,(size_t)size,f);
   if(*n!=(size_t)size){free(data);data=NULL;}
  }
 }
 fclose(f);return data;
}

void xz_anlz_extra_load(const char *dir, struct xz_anlz_extra *out) {
 memset(out,0,sizeof *out);
 if(!dir||!dir[0])return;
 char path[1100];size_t n=0;uint8_t *f;
 if(snprintf(path,sizeof path,"%s/ANLZ0000.EXT",dir)<(int)sizeof path&&(f=read_all(path,&n))){
  out->hot_cues_found=xz_anlz_hot_cues(f,n,out->hot_cue)>=0;free(f);
 }
 if(snprintf(path,sizeof path,"%s/ANLZ0000.DAT",dir)>=(int)sizeof path||!(f=read_all(path,&n)))return;
 if(!out->hot_cues_found)out->hot_cues_found=xz_anlz_hot_cues(f,n,out->hot_cue)>=0;
 (void)xz_anlz_beat_grid(f,n,&out->grid,&out->grid_count,&out->grid_bpm_x100);
 free(f);
}

void xz_anlz_extra_free(struct xz_anlz_extra *a) { if(a){free(a->grid);memset(a,0,sizeof *a);} }
