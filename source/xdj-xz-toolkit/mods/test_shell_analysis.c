/* SPDX-License-Identifier: MIT */
/* Host test for the shell's per-USB analysis index (shell_analysis.c) on a generated USBANLZ tree. */
#ifdef NDEBUG
#error Shell analysis acceptance requires active assertions
#endif
#include "shell_analysis.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p,0755)
#endif
/* The stems loader in layered_wave_source.c needs the OverCue bundle lookup; this test never asks it. */
int xz_oc_bundle(const char *source,char *folder,size_t capacity){(void)source;(void)folder;(void)capacity;return -1;}

static size_t be(uint8_t *o,uint32_t v){o[0]=(uint8_t)(v>>24);o[1]=(uint8_t)(v>>16);o[2]=(uint8_t)(v>>8);o[3]=(uint8_t)v;return 4;}
static void put(const char *path,const uint8_t *data,size_t n){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,n,f)==n);fclose(f);}
static size_t dat(uint8_t *o,const char *track){
 size_t len=(strlen(track)+1)*2,total=16+len,n=0;
 memcpy(o,"PMAI",4);be(o+4,28);be(o+8,(uint32_t)(28+total));memset(o+12,0,16);n=28;
 memcpy(o+n,"PPTH",4);be(o+n+4,16);be(o+n+8,(uint32_t)total);be(o+n+12,(uint32_t)len);n+=16;
 for(size_t i=0;i<=strlen(track);i++){o[n++]=0;o[n++]=(uint8_t)track[i];}
 return n;
}
static size_t two_ex(uint8_t *o,const char *track,const uint8_t *bands,uint32_t count){
 size_t n=dat(o,track);
 memcpy(o+n,"PWV7",4);be(o+n+4,24);be(o+n+8,24+count*3);be(o+n+12,3);be(o+n+16,count);be(o+n+20,0);n+=24;
 memcpy(o+n,bands,count*3);n+=count*3;
 be(o+8,(uint32_t)n);return n;
}
static void track(const char *root,const char *group,const char *name,const char *path,uint8_t level,int with_ex){
 char dir[512],file[600];uint8_t buffer[4096],bands[300];
 snprintf(dir,sizeof dir,"%s/PIONEER/USBANLZ/%s",root,group);make_dir(dir);
 snprintf(dir,sizeof dir,"%s/PIONEER/USBANLZ/%s/%s",root,group,name);make_dir(dir);
 snprintf(file,sizeof file,"%s/ANLZ0000.DAT",dir);put(file,buffer,dat(buffer,path));
 for(int i=0;i<300;i++)bands[i]=(uint8_t)(level+i%3);
 if(with_ex){snprintf(file,sizeof file,"%s/ANLZ0000.2EX",dir);put(file,buffer,two_ex(buffer,path,bands,100));}
}
int main(void){
 const char *root="shell-analysis-fixture";
 char p[256];make_dir(root);snprintf(p,sizeof p,"%s/PIONEER",root);make_dir(p);snprintf(p,sizeof p,"%s/PIONEER/USBANLZ",root);make_dir(p);
 track(root,"P001","0000AAAA","/Contents/One.mp3",10,1);
 track(root,"P001","0000BBBB","/Contents/Two.flac",20,1);
 track(root,"P07F","0001CCCC","/Contents/Folder/Three.wav",30,1);
 track(root,"P07F","0001DDDD","/Contents/No Wave.mp3",40,0);
 struct xz_shana *a=NULL;uint32_t files=0;
 assert(!xz_shana_build(root,&a,&files)&&a&&files==4&&xz_shana_count(a)==4&&!strcmp(xz_shana_root(a),root));
 char dir[1024];struct xz_wave_load w;
 static const struct { const char *track, *folder; uint8_t level; } cases[3]={
  {"/Contents/One.mp3","P001/0000AAAA",10},{"/Contents/Two.flac","P001/0000BBBB",20},{"/Contents/Folder/Three.wav","P07F/0001CCCC",30}};
 for(int i=0;i<3;i++){
  assert(!xz_shana_find(a,cases[i].track,dir,sizeof dir)&&strstr(dir,cases[i].folder));
  assert(!xz_shana_three_band(dir,&w)&&w.count==100&&w.bands[0]==cases[i].level&&w.bands[299]==cases[i].level+2);
  xz_wave_load_free(&w);
 }
 /* Unknown tracks and missing waves fail. */
 assert(xz_shana_find(a,"/Contents/Missing.mp3",dir,sizeof dir));
 assert(!xz_shana_find(a,"/Contents/No Wave.mp3",dir,sizeof dir)&&xz_shana_three_band(dir,&w));
 /* The deck reader's absolute path splits into the root this index is keyed by. */
 const char *rel;char split_root[256];
 assert(!xz_shana_split("/media/usb2/sda2/Contents/Folder/Three.wav",split_root,sizeof split_root,&rel)&&
        !strcmp(split_root,"/media/usb2/sda2")&&!strcmp(rel,"/Contents/Folder/Three.wav"));
 assert(xz_shana_split("/media/usb2/sda2/Music/x.mp3",split_root,sizeof split_root,&rel));
 xz_shana_free(a);
 /* A stick without USBANLZ gives an empty index, not an error. */
 assert(!xz_shana_build("shell-analysis-fixture/none",&a,&files)&&a&&!xz_shana_count(a)&&!files);xz_shana_free(a);
 puts("PASS analysis index: one build per USB, PPTH-confirmed lookups, nested folders, misses, missing 2EX, path split");
 /* Timing on this host's disk (not the deck's USB): two 300-track sticks, decks alternating between
    them, ten wave loads. The old loader rebuilds its single-root index on every root change. */
 const char *roots[2]={"shell-analysis-fixture/usb1","shell-analysis-fixture/usb2"};
 for(int r=0;r<2;r++){
  make_dir(roots[r]);snprintf(p,sizeof p,"%s/PIONEER",roots[r]);make_dir(p);snprintf(p,sizeof p,"%s/PIONEER/USBANLZ",roots[r]);make_dir(p);
  for(int i=0;i<300;i++){char g[16],n[16],t[64];snprintf(g,sizeof g,"P%03X",i%40);snprintf(n,sizeof n,"%08X",i);snprintf(t,sizeof t,"/Contents/T%03d.mp3",i);track(roots[r],g,n,t,1,1);}
 }
 struct timespec t0,t1;timespec_get(&t0,TIME_UTC);
 for(int i=0;i<10;i++){
  char source[256];snprintf(source,sizeof source,"%s/Contents/T%03d.mp3",roots[i&1],i*7);
  assert(!xz_wave_load_three_band(source,&w));xz_wave_load_free(&w);
 }
 timespec_get(&t1,TIME_UTC);double old_ms=(t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6;
 struct xz_shana *idx[2];timespec_get(&t0,TIME_UTC);
 for(int r=0;r<2;r++)assert(!xz_shana_build(roots[r],&idx[r],&files)&&xz_shana_count(idx[r])==300);
 timespec_get(&t1,TIME_UTC);double build_ms=(t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6;
 timespec_get(&t0,TIME_UTC);
 for(int i=0;i<10;i++){
  char t[64];snprintf(t,sizeof t,"/Contents/T%03d.mp3",i*7);
  assert(!xz_shana_find(idx[i&1],t,dir,sizeof dir)&&!xz_shana_three_band(dir,&w));xz_wave_load_free(&w);
 }
 timespec_get(&t1,TIME_UTC);double new_ms=(t1.tv_sec-t0.tv_sec)*1e3+(t1.tv_nsec-t0.tv_nsec)/1e6;
 printf("  host timing, 2 x 300 tracks, 10 alternating loads: old loader %.1f ms total (%.1f ms/load); "
        "new: index build %.1f ms once at mount, then %.2f ms/load\n",old_ms,old_ms/10,build_ms,new_ms/10);
 xz_shana_free(idx[0]);xz_shana_free(idx[1]);
 return 0;
}
