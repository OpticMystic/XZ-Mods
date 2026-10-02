/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "layered_wave_source.h"
#include "layered_wave.h"
#include "../audio/overcue.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define MAX_ANALYSIS (8u*1024u*1024u)
#define MAX_SAMPLES 1000000u
#define PATH_CAP 1024

static uint32_t be32(const uint8_t *b){return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}

static uint8_t *read_file(const char *path,size_t limit,size_t *size){
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint8_t *data=NULL;size_t n=0,cap=0;
 for(;;){
  if(n==cap){
   if(cap>=limit){free(data);data=NULL;break;}
   size_t next=cap?cap*2:65536;if(next>limit)next=limit;
   uint8_t *grown=realloc(data,next);if(!grown){free(data);data=NULL;break;}
   data=grown;cap=next;
  }
  size_t got=fread(data+n,1,cap-n,f);n+=got;
  if(got==0){if(ferror(f)){free(data);data=NULL;}break;}
 }
 fclose(f);if(data)*size=n;return data;
}

int xz_anlz_section(const uint8_t *file,size_t n,const char tag[4],uint32_t entry_size,size_t *entries,uint32_t *count){
 if(!file||n<12||memcmp(file,"PMAI",4))return -1;
 uint32_t header=be32(file+4),length=be32(file+8);
 if(header<12||header>n||length>n)return -1;
 for(size_t at=header;at+12<=length;){
  uint32_t head=be32(file+at+4),total=be32(file+at+8);
  if(head<12||total<head||total>length-at)return -1;
  if(!memcmp(file+at,tag,4)){
   /* Take the entry offset from this section's header length: PWV7 has 24 bytes, PWV6 20. */
   if(head<20||be32(file+at+12)!=entry_size)return -1;
   uint32_t c=be32(file+at+16);
   if(!c||c>MAX_SAMPLES||(uint64_t)c*entry_size>total-head)return -1;
   *entries=at+head;*count=c;return 0;
  }
  at+=total;
 }
 return -1;
}

static size_t put_utf8(char *out,size_t at,size_t cap,uint32_t c){
 uint8_t b[4];size_t k;
 if(c<0x80){b[0]=(uint8_t)c;k=1;}
 else if(c<0x800){b[0]=(uint8_t)(0xc0|c>>6);b[1]=(uint8_t)(0x80|(c&63));k=2;}
 else if(c<0x10000){b[0]=(uint8_t)(0xe0|c>>12);b[1]=(uint8_t)(0x80|((c>>6)&63));b[2]=(uint8_t)(0x80|(c&63));k=3;}
 else{b[0]=(uint8_t)(0xf0|c>>18);b[1]=(uint8_t)(0x80|((c>>12)&63));b[2]=(uint8_t)(0x80|((c>>6)&63));b[3]=(uint8_t)(0x80|(c&63));k=4;}
 if(at+k>=cap)return 0;
 memcpy(out+at,b,k);return at+k;
}

int xz_anlz_track_path(const uint8_t *file,size_t n,char *out,size_t cap){
 if(!file||n<12||memcmp(file,"PMAI",4)||!out||cap<2)return -1;
 uint32_t header=be32(file+4);
 for(size_t at=header;at+16<=n;){
  uint32_t head=be32(file+at+4),total=be32(file+at+8);
  if(head<12||total<head)return -1;
  if(!memcmp(file+at,"PPTH",4)){
   uint32_t len=be32(file+at+12);
   if(head<16||len%2||len>total-head||at+head+len>n)return -1;
   const uint8_t *p=file+at+head;size_t o=0;
   for(uint32_t i=0;i+1<len;i+=2){
    uint32_t c=(uint32_t)p[i]<<8|p[i+1];
    if(!c)break;
    if(c>=0xd800&&c<0xdc00&&i+3<len){
     uint32_t lo=(uint32_t)p[i+2]<<8|p[i+3];
     if(lo>=0xdc00&&lo<0xe000){c=0x10000+((c-0xd800)<<10)+(lo-0xdc00);i+=2;}
    }
    if(!(o=put_utf8(out,o,cap,c)))return -1;
   }
   out[o]=0;return strncmp(out,"/Contents/",10)?-1:0;
  }
  if((size_t)total>n-at)return -1;
  at+=total;
 }
 return -1;
}

uint32_t xz_wave_normalization(const uint8_t *bands,uint32_t count,unsigned kind){
 (void)kind;
 uint32_t histogram[1021];memset(histogram,0,sizeof(histogram));
 for(uint32_t i=0;i<count;i++){
  /* Layers stack, so the drawn height is the sum of the three bytes for both kinds. */
  const uint8_t *s=bands+i*3;histogram[s[0]+s[1]+s[2]]++;
 }
 /* The 99.5th percentile keeps a few clipped transients from flattening the whole track. */
 uint64_t target=((uint64_t)count*995+999)/1000,seen=0;
 for(unsigned e=0;e<1021;e++){seen+=histogram[e];if(seen>=target)return e?e:1;}
 return 1;
}

void xz_wave_load_free(struct xz_wave_load *w){if(w){free(w->bands);memset(w,0,sizeof(*w));}}

/* Track path -> ANLZ folder, built by reading each analysis file's PPTH once per USB. */
struct index_entry{uint64_t hash;char *dir;};
static _Thread_local struct {char root[PATH_CAP];struct index_entry *e;size_t n,cap;time_t built;} idx;

void xz_wave_index_reset(void){
 for(size_t i=0;i<idx.n;i++)free(idx.e[i].dir);
 free(idx.e);memset(&idx,0,sizeof(idx));
}
static int index_add(const char *dir,uint64_t hash){
 if(idx.n==idx.cap){
  size_t next=idx.cap?idx.cap*2:256;struct index_entry *grown=realloc(idx.e,next*sizeof(*grown));
  if(!grown)return -1;idx.e=grown;idx.cap=next;
 }
 char *copy=strdup(dir);if(!copy)return -1;
 idx.e[idx.n++]=(struct index_entry){hash,copy};return 0;
}
static int dat_path(const char *dir,char *path,size_t cap,char *track,size_t track_cap){
 /* Every analysis file starts with the same PPTH; DAT is the smallest. */
 FILE *f=NULL;
 for(const char *const *ext=(const char *const[]){"DAT","EXT",NULL};*ext&&!f;ext++){
  if(snprintf(path,cap,"%s/ANLZ0000.%s",dir,*ext)>=(int)cap)return -1;
  f=fopen(path,"rb");
 }
 if(!f)return -1;
 uint8_t head[4096];size_t n=fread(head,1,sizeof(head),f);fclose(f);
 return xz_anlz_track_path(head,n,track,track_cap);
}
static void index_build(const char *root){
 xz_wave_index_reset();
 snprintf(idx.root,sizeof(idx.root),"%s",root);idx.built=time(NULL);
 char base[PATH_CAP],group[PATH_CAP],dir[PATH_CAP],path[PATH_CAP],track[PATH_CAP];
 if(snprintf(base,sizeof(base),"%s/PIONEER/USBANLZ",root)>=(int)sizeof(base))return;
 DIR *top=opendir(base);if(!top)return;
 for(struct dirent *a;(a=readdir(top));){
  if(a->d_name[0]=='.'||snprintf(group,sizeof(group),"%s/%s",base,a->d_name)>=(int)sizeof(group))continue;
  DIR *mid=opendir(group);if(!mid)continue;
  for(struct dirent *b;(b=readdir(mid));){
   if(b->d_name[0]=='.'||snprintf(dir,sizeof(dir),"%s/%s",group,b->d_name)>=(int)sizeof(dir))continue;
   if(!dat_path(dir,path,sizeof(path),track,sizeof(track)))index_add(dir,xz_layered_path_hash(track));
  }
  closedir(mid);
 }
 closedir(top);
}
static int find_analysis(const char *root,const char *track,char *dir,size_t cap){
 uint64_t hash=xz_layered_path_hash(track);char path[PATH_CAP],found[PATH_CAP];
 for(int pass=0;pass<2;pass++){
  if(strcmp(idx.root,root)||(pass&&time(NULL)-idx.built>=15))index_build(root);
  for(size_t i=0;i<idx.n;i++)
   if(idx.e[i].hash==hash&&!dat_path(idx.e[i].dir,path,sizeof(path),found,sizeof(found))&&!strcasecmp(found,track)){
    if(snprintf(dir,cap,"%s",idx.e[i].dir)>=(int)cap)return -1;
    return 0;
   }
 }
 return -1;
}
static int split(const char *source,char *root,size_t cap,const char **track){
 const char *rel=strstr(source,"/Contents/");
 if(!rel||(size_t)(rel-source)>=cap)return -1;
 memcpy(root,source,(size_t)(rel-source));root[rel-source]=0;*track=rel;return 0;
}

int xz_wave_load_three_band(const char *source,struct xz_wave_load *out){
 memset(out,0,sizeof(*out));
 char root[PATH_CAP],dir[PATH_CAP],path[PATH_CAP];const char *track;
 if(!source||split(source,root,sizeof(root),&track)||find_analysis(root,track,dir,sizeof(dir))||
    snprintf(path,sizeof(path),"%s/ANLZ0000.2EX",dir)>=(int)sizeof(path))return -1;
 size_t n=0,at;uint32_t count;uint8_t *file=read_file(path,MAX_ANALYSIS,&n);
 if(!file)return -1;
 if(xz_anlz_section(file,n,"PWV7",3,&at,&count)||!(out->bands=malloc((size_t)count*3))){free(file);return -1;}
 memcpy(out->bands,file+at,(size_t)count*3);free(file);
 out->count=count;out->kind=XZ_LAYERED_THREE_BAND;
 out->normalization=xz_wave_normalization(out->bands,count,out->kind);
 return 0;
}

int xz_wave_load_stems(const char *source,struct xz_wave_load *out){
 memset(out,0,sizeof(*out));
 char folder[PATH_CAP],path[PATH_CAP];
 if(!source||xz_oc_bundle(source,folder,sizeof(folder)))return -1;
 /* Same order as the stem runtime and pad colours: drums, harmonics, vocals. */
 static const char *names[3]={"drums","harmonics","vocal"};
 uint8_t *parts[3]={0};uint32_t counts[3]={0};int rc=-1;
 for(unsigned s=0;s<3;s++){
  size_t n=0,at;uint32_t count;
  if(snprintf(path,sizeof(path),"%s/stems-%s-waveform.2EX",folder,names[s])>=(int)sizeof(path))goto done;
  uint8_t *file=read_file(path,MAX_ANALYSIS,&n);if(!file)goto done;
  if(xz_anlz_section(file,n,"PWV7",3,&at,&count)||!(parts[s]=malloc(count))){free(file);goto done;}
  /* One amplitude per stem: its three bands at equal weight, as rekordbox sums them. */
  for(uint32_t i=0;i<count;i++){
   const uint8_t *b=file+at+(size_t)i*3;unsigned e=b[0]+b[1]+b[2];
   parts[s][i]=(uint8_t)(e>255?255:e);
  }
  counts[s]=count;free(file);
 }
 uint32_t count=counts[0];if(counts[1]>count)count=counts[1];if(counts[2]>count)count=counts[2];
 if(!(out->bands=calloc(count,3)))goto done;
 for(unsigned s=0;s<3;s++)for(uint32_t i=0;i<counts[s];i++)out->bands[i*3+s]=parts[s][i];
 out->count=count;out->kind=XZ_LAYERED_STEMS;
 out->normalization=xz_wave_normalization(out->bands,count,out->kind);rc=0;
done:
 for(unsigned s=0;s<3;s++)free(parts[s]);
 if(rc)xz_wave_load_free(out);
 return rc;
}
