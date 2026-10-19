/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "shell_analysis.h"
#include "ui/layered_wave.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#define JSMN_STATIC
#include "audio/vendor/jsmn/jsmn.h"

#define ANALYSIS_CAP (8u*1024u*1024u)
#define PATH_CAP 1024
struct entry { uint64_t hash; char folder[32]; }; /* "P016/0001A2B3" below PIONEER/USBANLZ */
struct xz_shana { char root[PATH_CAP]; struct entry *e; uint32_t n, cap; };

static int head_path(const char *dir, char *track, size_t cap) {
 /* Every analysis file starts with the same PPTH; DAT is the smallest. */
 char path[PATH_CAP];FILE *f=NULL;
 static const char *const ext[2]={"DAT","EXT"};
 for(int i=0;i<2&&!f;i++){
  if(snprintf(path,sizeof path,"%s/ANLZ0000.%s",dir,ext[i])>=(int)sizeof path)return -1;
  f=fopen(path,"rb");
 }
 if(!f)return -1;
 uint8_t head[4096];size_t n=fread(head,1,sizeof head,f);fclose(f);
 return xz_anlz_track_path(head,n,track,cap);
}
static int add(struct xz_shana *a, uint64_t hash, const char *group, const char *name) {
 if(a->n==a->cap){
  uint32_t next=a->cap?a->cap*2:512;struct entry *grown=realloc(a->e,(size_t)next*sizeof *grown);
  if(!grown)return -1;a->e=grown;a->cap=next;
 }
 struct entry *e=&a->e[a->n];
 if(snprintf(e->folder,sizeof e->folder,"%s/%s",group,name)>=(int)sizeof e->folder)return 0; /* not a rekordbox name */
 e->hash=hash;a->n++;return 0;
}
int xz_shana_build(const char *root, struct xz_shana **out, uint32_t *files) {
 *out=NULL;if(files)*files=0;
 struct xz_shana *a=calloc(1,sizeof *a);
 if(!a||snprintf(a->root,sizeof a->root,"%s",root)>=(int)sizeof a->root){free(a);return -1;}
 char base[PATH_CAP],group[PATH_CAP],dir[PATH_CAP],track[PATH_CAP];
 if(snprintf(base,sizeof base,"%s/PIONEER/USBANLZ",root)<(int)sizeof base){
  DIR *top=opendir(base);
  for(struct dirent *g;top&&(g=readdir(top));){
   if(g->d_name[0]=='.'||snprintf(group,sizeof group,"%s/%s",base,g->d_name)>=(int)sizeof group)continue;
   DIR *mid=opendir(group);if(!mid)continue;
   for(struct dirent *t;(t=readdir(mid));){
    if(t->d_name[0]=='.'||snprintf(dir,sizeof dir,"%s/%s",group,t->d_name)>=(int)sizeof dir)continue;
    if(files)(*files)++;
    if(!head_path(dir,track,sizeof track)&&add(a,xz_layered_path_hash(track),g->d_name,t->d_name)){closedir(mid);closedir(top);xz_shana_free(a);return -1;}
   }
   closedir(mid);
  }
  if(top)closedir(top);
 }
 *out=a;return 0;
}
void xz_shana_free(struct xz_shana *a) { if(a){free(a->e);free(a);} }
const char *xz_shana_root(const struct xz_shana *a) { return a?a->root:""; }
uint32_t xz_shana_count(const struct xz_shana *a) { return a?a->n:0; }
int xz_shana_find(const struct xz_shana *a, const char *track, char *dir, size_t cap) {
 if(!a||!track||!dir)return -1;
 uint64_t hash=xz_layered_path_hash(track);char found[PATH_CAP];
 for(uint32_t i=0;i<a->n;i++){
  if(a->e[i].hash!=hash)continue;
  if(snprintf(dir,cap,"%s/PIONEER/USBANLZ/%s",a->root,a->e[i].folder)>=(int)cap)return -1;
  if(!head_path(dir,found,sizeof found)&&!strcasecmp(found,track))return 0;
 }
 return -1;
}
int xz_shana_three_band(const char *dir, struct xz_wave_load *out) {
 memset(out,0,sizeof *out);
 char path[PATH_CAP];
 if(snprintf(path,sizeof path,"%s/ANLZ0000.2EX",dir)>=(int)sizeof path)return -1;
 FILE *f=fopen(path,"rb");if(!f)return -1;
 uint8_t *file=NULL;size_t n=0;
 /* One read of the whole file: the size is known up front. */
 if(!fseek(f,0,SEEK_END)){
  long size=ftell(f);
  if(size>0&&(unsigned long)size<=ANALYSIS_CAP&&!fseek(f,0,SEEK_SET)&&(file=malloc((size_t)size))){
   n=fread(file,1,(size_t)size,f);
   if(n!=(size_t)size){free(file);file=NULL;}
  }
 }
 fclose(f);
 if(!file)return -1;
 size_t at;uint32_t count;
 if(xz_anlz_section(file,n,"PWV7",3,&at,&count)||!(out->bands=malloc((size_t)count*3))){free(file);return -1;}
 memcpy(out->bands,file+at,(size_t)count*3);free(file);
 out->count=count;out->kind=XZ_LAYERED_THREE_BAND;
 out->normalization=xz_wave_normalization(out->bands,count,out->kind);
 return 0;
}
int xz_shana_split(const char *source, char *root, size_t cap, const char **track) {
 const char *rel=source?strstr(source,"/Contents/"):NULL;
 if(!rel||(size_t)(rel-source)>=cap)return -1;
 memcpy(root,source,(size_t)(rel-source));root[rel-source]=0;*track=rel;return 0;
}

/* ---- signature ---- */
static uint64_t mix(uint64_t h, uint64_t v) { h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);return h; }
uint64_t xz_shana_signature(const char *root) {
 char base[PATH_CAP],group[PATH_CAP];struct stat st;uint64_t h=0,groups=0;int any=0;
 if(snprintf(base,sizeof base,"%s/PIONEER/USBANLZ",root)<(int)sizeof base&&!stat(base,&st)){
  any=1;h=mix(h,(uint64_t)st.st_dev);h=mix(h,(uint64_t)st.st_mtime);
  DIR *top=opendir(base);
  for(struct dirent *g;top&&(g=readdir(top));){
   if(g->d_name[0]=='.'||snprintf(group,sizeof group,"%s/%s",base,g->d_name)>=(int)sizeof group||stat(group,&st))continue;
   /* Order-independent: readdir order is not guaranteed to repeat. */
   groups+=mix(xz_layered_path_hash(g->d_name),(uint64_t)st.st_mtime);
  }
  if(top)closedir(top);
  h=mix(h,groups);
 }
 if(snprintf(base,sizeof base,"%s/CDJMODS/index.json",root)<(int)sizeof base&&!stat(base,&st)){
  any=1;h=mix(h,(uint64_t)st.st_mtime);h=mix(h,(uint64_t)st.st_size);
 }
 return any?(h?h:1):0;
}

/* ---- stems ---- */
#define INDEX_CAP (4u*1024u*1024u)
struct stem_entry { uint64_t hash; char bundle[17]; };
struct xz_shana_stems { char root[PATH_CAP]; struct stem_entry *e; uint32_t n, cap; };
static uint8_t *read_all(const char *path, size_t cap, size_t *size) {
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint8_t *data=NULL;long n=-1;
 if(!fseek(f,0,SEEK_END))n=ftell(f);
 if(n>0&&(unsigned long)n<=cap&&!fseek(f,0,SEEK_SET)&&(data=malloc((size_t)n+1))){
  if(fread(data,1,(size_t)n,f)!=(size_t)n){free(data);data=NULL;}else{data[n]=0;*size=(size_t)n;}
 }
 fclose(f);return data;
}
static int token_is(const char *text, const jsmntok_t *t, const char *s) {
 size_t n=strlen(s);return t->type==JSMN_STRING&&(size_t)(t->end-t->start)==n&&!memcmp(text+t->start,s,n);
}
static int skip_token(const jsmntok_t *t, int i, int count) { int end=t[i].end;while(++i<count&&t[i].start<end){}return i; }
static size_t utf8(char *out, size_t at, size_t cap, uint32_t c) {
 if(c<0x80){if(at+1>=cap)return 0;out[at++]=(char)c;}
 else if(c<0x800){if(at+2>=cap)return 0;out[at++]=(char)(0xc0|c>>6);out[at++]=(char)(0x80|(c&63));}
 else if(c<0x10000){if(at+3>=cap)return 0;out[at++]=(char)(0xe0|c>>12);out[at++]=(char)(0x80|((c>>6)&63));out[at++]=(char)(0x80|(c&63));}
 else{if(at+4>=cap)return 0;out[at++]=(char)(0xf0|c>>18);out[at++]=(char)(0x80|((c>>12)&63));out[at++]=(char)(0x80|((c>>6)&63));out[at++]=(char)(0x80|(c&63));}
 return at;
}
static int hex4(const char *p, uint32_t *v) {
 *v=0;
 for(int i=0;i<4;i++){
  char c=p[i];*v<<=4;
  if(c>='0'&&c<='9')*v|=(uint32_t)(c-'0');else if(c>='a'&&c<='f')*v|=(uint32_t)(c-'a'+10);
  else if(c>='A'&&c<='F')*v|=(uint32_t)(c-'A'+10);else return -1;
 }
 return 0;
}
/* A JSON string token as UTF-8, escapes decoded. */
static int json_string(const char *text, const jsmntok_t *t, char *out, size_t cap) {
 if(t->type!=JSMN_STRING||!cap)return -1;
 size_t at=0;const char backslash=92;
 for(int p=t->start;p<t->end;){
  char c=text[p++];uint32_t cp=(unsigned char)c;
  if(c==backslash){
   if(p>=t->end)return -1;
   char e=text[p++];
   switch(e){
   case '"':cp='"';break;case '/':cp='/';break;case 'b':cp=8;break;case 'f':cp=12;break;
   case 'n':cp=10;break;case 'r':cp=13;break;case 't':cp=9;break;
   case 'u':
    if(p+4>t->end||hex4(text+p,&cp))return -1;
    p+=4;
    if(cp>=0xd800&&cp<0xdc00){
     uint32_t lo;
     if(p+6>t->end||text[p]!=backslash||text[p+1]!='u'||hex4(text+p+2,&lo)||lo<0xdc00||lo>0xdfff)return -1;
     p+=6;cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);
    }
    break;
   default:
    if(e!=backslash)return -1;
    cp=(uint32_t)backslash;break;
   }
   if(!(at=utf8(out,at,cap,cp)))return -1;
  }else{if(at+1>=cap)return -1;out[at++]=c;}
 }
 out[at]=0;return 0;
}
static int stems_add(struct xz_shana_stems *s, uint64_t hash, const char *bundle) {
 for(uint32_t i=0;i<s->n;i++)if(s->e[i].hash==hash){
  if(strcmp(s->e[i].bundle,bundle))s->e[i].bundle[0]=0; /* contradictory duplicate */
  return 0;
 }
 if(s->n==s->cap){
  uint32_t next=s->cap?s->cap*2:256;struct stem_entry *grown=realloc(s->e,(size_t)next*sizeof *grown);
  if(!grown)return -1;
  s->e=grown;s->cap=next;
 }
 s->e[s->n].hash=hash;memcpy(s->e[s->n].bundle,bundle,17);s->n++;return 0;
}
int xz_shana_stems_build(const char *root, struct xz_shana_stems **out) {
 *out=NULL;
 struct xz_shana_stems *s=calloc(1,sizeof *s);
 if(!s||snprintf(s->root,sizeof s->root,"%s",root)>=(int)sizeof s->root){free(s);return -1;}
 char path[PATH_CAP];size_t n=0;
 uint8_t *text=snprintf(path,sizeof path,"%s/CDJMODS/index.json",root)<(int)sizeof path?read_all(path,INDEX_CAP,&n):NULL;
 if(!text){*out=s;return 0;} /* no stems on this stick */
 jsmn_parser parser;jsmn_init(&parser);
 const char *c=(const char *)text;
 /* Count first, so the token array is exactly as large as the index needs. */
 int count=jsmn_parse(&parser,c,n,NULL,0);
 jsmntok_t *t=count>0?malloc((size_t)count*sizeof *t):NULL;
 if(t){jsmn_init(&parser);if(jsmn_parse(&parser,c,n,t,(unsigned)count)!=count){free(t);t=NULL;}}
 int ok=t&&t[0].type==JSMN_OBJECT,schema=0;
 /* {"schema": "overcue-index/1", "tracks": {key: {"file_path": ..., "bundle": ...}}, "tracks_onelibrary": {...}} */
 for(int i=1;ok&&i<count&&t[i].start<t[0].end;){
  int v=i+1;if(v>=count)break;
  if(token_is(c,&t[i],"schema")&&token_is(c,&t[v],"overcue-index/1"))schema=1;
  if((token_is(c,&t[i],"tracks")||token_is(c,&t[i],"tracks_onelibrary"))&&t[v].type==JSMN_OBJECT){
   for(int k=v+1;k<count&&t[k].start<t[v].end;){
    int entry=k+1;if(entry>=count)break;
    if(t[entry].type==JSMN_OBJECT){
     char track[PATH_CAP]="",bundle[32]="";
     for(int f=entry+1;f<count&&t[f].start<t[entry].end;){
      int fv=f+1;if(fv>=count)break;
      if(token_is(c,&t[f],"file_path")&&json_string(c,&t[fv],track,sizeof track))track[0]=0;
      if(token_is(c,&t[f],"bundle")&&json_string(c,&t[fv],bundle,sizeof bundle))bundle[0]=0;
      f=skip_token(t,fv,count);
     }
     int hex=strlen(bundle)==16;
     for(int b=0;hex&&b<16;b++)hex=(bundle[b]>='0'&&bundle[b]<='9')||(bundle[b]>='a'&&bundle[b]<='f');
     if(track[0]&&hex&&stems_add(s,xz_layered_path_hash(track),bundle)){ok=0;break;}
    }
    k=skip_token(t,entry,count);
   }
  }
  i=skip_token(t,v,count);
 }
 free(t);free(text);
 if(!ok||!schema)s->n=0;
 *out=s;return 0;
}
void xz_shana_stems_free(struct xz_shana_stems *s) { if(s){free(s->e);free(s);} }
uint32_t xz_shana_stems_count(const struct xz_shana_stems *s) { return s?s->n:0; }
int xz_shana_stems_find(const struct xz_shana_stems *s, const char *track, char *folder, size_t cap) {
 if(!s||!track)return -1;
 uint64_t hash=xz_layered_path_hash(track);
 for(uint32_t i=0;i<s->n;i++)if(s->e[i].hash==hash){
  if(!s->e[i].bundle[0])return -1;
  return snprintf(folder,cap,"%s/CDJMODS/stems/%s",s->root,s->e[i].bundle)<(int)cap?0:-1;
 }
 return -1;
}
static uint32_t be32(const uint8_t *b) { return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3]; }
/* One 3-byte-entry section (PWV7, PWV6): walk the section headers with seeks and read just that payload. */
static uint8_t *read_section(const char *path, const char tag[4], uint32_t limit, uint32_t *count) {
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint8_t head[24],*out=NULL;
 if(fread(head,1,12,f)!=12||memcmp(head,"PMAI",4)){fclose(f);return NULL;}
 uint32_t at=be32(head+4),length=be32(head+8);
 while(at>=12&&at+12<=length){
  if(fseek(f,(long)at,SEEK_SET)||fread(head,1,12,f)!=12)break;
  uint32_t h=be32(head+4),total=be32(head+8);
  if(h<12||total<h||total>length-at)break;
  if(!memcmp(head,tag,4)){
   if(h<20||fread(head+12,1,8,f)!=8||be32(head+12)!=3)break;
   uint32_t c=be32(head+16);
   if(!c||c>limit||(uint64_t)c*3>total-h||fseek(f,(long)(at+h),SEEK_SET))break;
   out=malloc((size_t)c*3);
   if(out&&fread(out,1,(size_t)c*3,f)!=(size_t)c*3){free(out);out=NULL;}
   if(out)*count=c;
   break;
  }
  at+=total;
 }
 fclose(f);return out;
}
static uint8_t *read_pwv7(const char *path, uint32_t *count) { return read_section(path,"PWV7",1000000u,count); }
int xz_shana_preview(const char *dir, uint8_t **bands, uint32_t *count) {
 char path[PATH_CAP];*bands=NULL;*count=0;
 if(snprintf(path,sizeof path,"%s/ANLZ0000.2EX",dir)>=(int)sizeof path)return -1;
 return (*bands=read_section(path,"PWV6",65536u,count))?0:-1;
}
/* gettimeofday: timespec_get and clock_gettime are newer than the deck's GLIBC_2.4 ceiling. */
static uint32_t since_ms(struct timeval a) {
 struct timeval b;gettimeofday(&b,NULL);
 return (uint32_t)((b.tv_sec-a.tv_sec)*1000+(b.tv_usec-a.tv_usec)/1000);
}
int xz_shana_stems_load(const char *folder, struct xz_wave_load *out, uint32_t read_ms[3]) {
 memset(out,0,sizeof *out);
 static const char *const names[3]={"drums","harmonics","vocal"}; /* the stem runtime's and the pads' order */
 uint8_t *parts[3]={0};uint32_t counts[3]={0},count=0;char path[PATH_CAP];int rc=-1;
 for(int s=0;s<3;s++){
  struct timeval t;gettimeofday(&t,NULL);
  if(snprintf(path,sizeof path,"%s/stems-%s-waveform.2EX",folder,names[s])>=(int)sizeof path)goto done;
  uint8_t *bands=read_pwv7(path,&counts[s]);
  if(read_ms)read_ms[s]=since_ms(t);
  if(!bands)goto done;
  /* One amplitude per stem: its three bands at equal weight. */
  if(!(parts[s]=malloc(counts[s]))){free(bands);goto done;}
  for(uint32_t i=0;i<counts[s];i++){unsigned e=bands[i*3]+bands[i*3+1]+bands[i*3+2];parts[s][i]=(uint8_t)(e>255?255:e);}
  free(bands);
  if(counts[s]>count)count=counts[s];
 }
 if(!(out->bands=calloc(count,3)))goto done;
 for(int s=0;s<3;s++)for(uint32_t i=0;i<counts[s];i++)out->bands[i*3+(uint32_t)s]=parts[s][i];
 out->count=count;out->kind=XZ_LAYERED_STEMS;
 out->normalization=xz_wave_normalization(out->bands,count,out->kind);rc=0;
done:
 for(int s=0;s<3;s++)free(parts[s]);
 if(rc)xz_wave_load_free(out);
 return rc;
}

/* ---- memory cues ---- */
/* UTF-16BE (bytes) to UTF-8, NUL or the end ending it; malformed pairs end it too. */
static void utf16be(const uint8_t *p, uint32_t bytes, char *out, size_t cap) {
 size_t at=0;
 for(uint32_t i=0;i+1<bytes;i+=2){
  uint32_t c=(uint32_t)p[i]<<8|p[i+1];
  if(!c)break;
  if(c>=0xd800&&c<=0xdbff){
   if(i+3>=bytes)break;
   uint32_t lo=(uint32_t)p[i+2]<<8|p[i+3];if(lo<0xdc00||lo>0xdfff)break;
   c=0x10000+((c-0xd800)<<10)+(lo-0xdc00);i+=2;
  }else if(c>=0xdc00&&c<=0xdfff)break;
  char b[4];size_t n=0;
  if(c<0x80)b[n++]=(char)c;
  else if(c<0x800){b[n++]=(char)(0xc0|(c>>6));b[n++]=(char)(0x80|(c&63));}
  else if(c<0x10000){b[n++]=(char)(0xe0|(c>>12));b[n++]=(char)(0x80|((c>>6)&63));b[n++]=(char)(0x80|(c&63));}
  else{b[n++]=(char)(0xf0|(c>>18));b[n++]=(char)(0x80|((c>>12)&63));b[n++]=(char)(0x80|((c>>6)&63));b[n++]=(char)(0x80|(c&63));}
  if(at+n+1>cap)break;
  memcpy(out+at,b,n);at+=n;
 }
 out[at]=0;
}
static int cue_list_ext(const char *dir, struct xz_shana_cue *out, int cap) {
 char path[PATH_CAP];size_t n=0;
 if(snprintf(path,sizeof path,"%s/ANLZ0000.EXT",dir)>=(int)sizeof path)return -1;
 uint8_t *f=read_all(path,ANALYSIS_CAP,&n);
 if(!f)return -1;
 int count=-1;
 if(n>=12&&!memcmp(f,"PMAI",4)){
  uint32_t at=be32(f+4),length=be32(f+8);if(length>n)length=(uint32_t)n;
  while(at>=12&&(uint64_t)at+12<=length){
   uint32_t head=be32(f+at+4),total=be32(f+at+8);
   if(head<12||total<head||total>length-at)break;
   if(!memcmp(f+at,"PCO2",4)&&head>=20&&be32(f+at+12)==0){
    if(count<0)count=0;
    for(uint32_t e=at+head;(uint64_t)e+44<=(uint64_t)at+total;){
     uint32_t eh=be32(f+e+4),et=be32(f+e+8);
     if(memcmp(f+e,"PCP2",4)||eh<16||et<44||et>at+total-e)break;
     uint32_t hot=be32(f+e+12),ms=be32(f+e+20),end=be32(f+e+24),clen=be32(f+e+40);uint8_t type=f[e+16];
     if(!hot&&(type==1||type==2)&&ms<=INT32_MAX&&count<cap){
      struct xz_shana_cue *c=&out[count];memset(c,0,sizeof *c);
      c->loop=type==2&&end!=0xffffffffu&&end>ms&&end<=INT32_MAX;c->ms=(int32_t)ms;c->end_ms=c->loop?(int32_t)end:(int32_t)ms;
      if(clen&&clen<=et-44)utf16be(f+e+44,clen,c->comment,sizeof c->comment);
      if(clen<=et-44&&(uint64_t)44+clen+4<=et){const uint8_t *k=f+e+44+clen;if(k[0])c->rgb=(uint32_t)k[1]<<16|(uint32_t)k[2]<<8|k[3];}
      count++;
     }
     e+=et;
    }
   }
   at+=total;
  }
 }
 free(f);
 return count;
}
int xz_shana_cue_list(const char *dir, struct xz_shana_cue *out, int cap) {
 int count=cue_list_ext(dir,out,cap);
 if(count<0){
  /* The DAT's memory list: times and loop ends, no comments or colours. */
  char path[PATH_CAP];size_t n=0;
  if(snprintf(path,sizeof path,"%s/ANLZ0000.DAT",dir)>=(int)sizeof path)return -1;
  uint8_t *f=read_all(path,ANALYSIS_CAP,&n);
  if(!f)return -1;
  if(n>=12&&!memcmp(f,"PMAI",4)){
   uint32_t at=be32(f+4),length=be32(f+8);if(length>n)length=(uint32_t)n;
   while(at>=12&&(uint64_t)at+12<=length){
    uint32_t head=be32(f+at+4),total=be32(f+at+8);
    if(head<12||total<head||total>length-at)break;
    if(!memcmp(f+at,"PCOB",4)&&head>=24&&be32(f+at+12)==0){
     if(count<0)count=0;
     for(uint32_t e=at+head;e+40<=at+total;){
      uint32_t eh=be32(f+e+4),et=be32(f+e+8);
      if(memcmp(f+e,"PCPT",4)||eh<28||et<eh||et>at+total-e||et<40)break;
      uint32_t hot=be32(f+e+12),ms=be32(f+e+32),end=be32(f+e+36);uint8_t type=f[e+28];
      if(!hot&&(type==1||type==2)&&ms<=INT32_MAX&&count<cap){
       struct xz_shana_cue *c=&out[count];memset(c,0,sizeof *c);
       c->loop=type==2&&end!=0xffffffffu&&end>ms&&end<=INT32_MAX;c->ms=(int32_t)ms;c->end_ms=c->loop?(int32_t)end:(int32_t)ms;
       count++;
      }
      e+=et;
     }
    }
    at+=total;
   }
  }
  free(f);
 }
 for(int i=1;i<count;i++){struct xz_shana_cue c=out[i];int j=i-1;while(j>=0&&out[j].ms>c.ms){out[j+1]=out[j];j--;}out[j+1]=c;}
 return count;
}
int xz_shana_memory_cues(const char *dir, struct xz_shell_cue out[XZ_SHELL_MEMORY_CUES]) {
 char path[PATH_CAP];size_t n=0;
 if(snprintf(path,sizeof path,"%s/ANLZ0000.DAT",dir)>=(int)sizeof path)return -1;
 uint8_t *f=read_all(path,ANALYSIS_CAP,&n);
 if(!f)return -1;
 int count=-1;
 if(n>=12&&!memcmp(f,"PMAI",4)){
  uint32_t at=be32(f+4),length=be32(f+8);if(length>n)length=(uint32_t)n;
  while(at>=12&&(uint64_t)at+12<=length){
   uint32_t head=be32(f+at+4),total=be32(f+at+8);
   if(head<12||total<head||total>length-at)break;
   if(!memcmp(f+at,"PCOB",4)&&head>=24&&be32(f+at+12)==0){
    if(count<0)count=0;
    for(uint32_t e=at+head;e+40<=at+total;){
     uint32_t eh=be32(f+e+4),et=be32(f+e+8);
     if(memcmp(f+e,"PCPT",4)||eh<28||et<eh||et>at+total-e||et<40)break;
     uint32_t hot=be32(f+e+12),ms=be32(f+e+32),end=be32(f+e+36);uint8_t type=f[e+28];
     if(!hot&&(type==1||type==2)&&ms<=INT32_MAX&&count<XZ_SHELL_MEMORY_CUES){
      out[count]=(struct xz_shell_cue){1,(int32_t)ms,0,type==2&&end!=0xffffffffu&&end>ms};
      count++;
     }
     e+=et;
    }
   }
   at+=total;
  }
 }
 free(f);
 /* Sorted by time: the stock list order and what the lane expects. */
 for(int i=1;i<count;i++){struct xz_shell_cue c=out[i];int j=i-1;while(j>=0&&out[j].ms>c.ms){out[j+1]=out[j];j--;}out[j+1]=c;}
 return count;
}
