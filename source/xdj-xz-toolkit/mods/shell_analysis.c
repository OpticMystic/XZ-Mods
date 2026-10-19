/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "shell_analysis.h"
#include "ui/layered_wave.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
