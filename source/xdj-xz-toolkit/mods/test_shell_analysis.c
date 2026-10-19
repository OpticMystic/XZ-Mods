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
/* A stem waveform file: a decoy section before PWV7, as the real files carry several. */
static void stem_file(const char *path, uint8_t level, uint32_t count){
 static uint8_t o[65536];size_t n=28;memcpy(o,"PMAI",4);be(o+4,28);memset(o+12,0,16);
 memcpy(o+n,"PWV5",4);be(o+n+4,24);be(o+n+8,24+40);be(o+n+12,2);be(o+n+16,20);be(o+n+20,0);n+=24;memset(o+n,0x77,40);n+=40;
 memcpy(o+n,"PWV7",4);be(o+n+4,24);be(o+n+8,24+count*3);be(o+n+12,3);be(o+n+16,count);be(o+n+20,0);n+=24;
 for(uint32_t i=0;i<count*3;i++)o[n++]=(uint8_t)(level+i%3);
 be(o+8,(uint32_t)n);put(path,o,n);
}
/* A DAT with PPTH, a hot cue list (type 1) and a memory list (type 0): a loop, a cue, a hot-cue-numbered
   entry that must be skipped, all out of time order. */
static size_t pcpt(uint8_t *o,uint32_t hot,uint8_t type,uint32_t ms,uint32_t end){
 memset(o,0,56);memcpy(o,"PCPT",4);be(o+4,28);be(o+8,56);be(o+12,hot);be(o+16,1);o[28]=type;be(o+32,ms);be(o+36,end);return 56;
}
static void cue_dat(const char *path,const char *track){
 static uint8_t o[4096];size_t n=dat(o,track);
 size_t list=n;memcpy(o+n,"PCOB",4);be(o+n+4,24);be(o+n+12,1);memset(o+n+16,0,8);n+=24;
 n+=pcpt(o+n,1,1,700,0xffffffffu);be(o+list+8,(uint32_t)(n-list));
 list=n;memcpy(o+n,"PCOB",4);be(o+n+4,24);be(o+n+12,0);memset(o+n+16,0,8);n+=24;
 n+=pcpt(o+n,0,2,9000,13000);n+=pcpt(o+n,0,1,2500,0xffffffffu);n+=pcpt(o+n,3,1,100,0xffffffffu);
 be(o+list+8,(uint32_t)(n-list));
 be(o+8,(uint32_t)n);put(path,o,n);
}
/* An EXT with the extended memory list: a commented cue with a colour, a loop without a comment, a hot cue. */
static size_t pcp2(uint8_t *o,uint32_t hot,uint8_t type,uint32_t ms,uint32_t end,const char *comment,uint32_t rgb){
 size_t clen=comment?(strlen(comment)+1)*2:0,len=44+clen+4;memset(o,0,len);
 memcpy(o,"PCP2",4);be(o+4,16);be(o+8,(uint32_t)len);be(o+12,hot);o[16]=type;be(o+20,ms);be(o+24,end);be(o+40,(uint32_t)clen);
 for(size_t i=0;comment&&i<=strlen(comment);i++){o[44+2*i]=0;o[45+2*i]=(uint8_t)comment[i];}
 uint8_t *k=o+44+clen;k[0]=rgb?1:0;k[1]=(uint8_t)(rgb>>16);k[2]=(uint8_t)(rgb>>8);k[3]=(uint8_t)rgb;
 return len;
}
static void cue_ext(const char *path){
 static uint8_t o[4096];size_t n=28;memcpy(o,"PMAI",4);be(o+4,28);memset(o+12,0,16);
 size_t list=n;memcpy(o+n,"PCO2",4);be(o+n+4,20);be(o+n+12,0);memset(o+n+16,0,4);n+=20;
 n+=pcp2(o+n,0,2,9000,13000,NULL,0);n+=pcp2(o+n,0,1,2500,0xffffffffu,"Drop 1",0x10a0ff);n+=pcp2(o+n,2,1,100,0xffffffffu,"hot",0);
 be(o+list+8,(uint32_t)(n-list));be(o+8,(uint32_t)n);put(path,o,n);
}
static void text_file(const char *path, const char *text){put(path,(const uint8_t *)text,strlen(text));}
static void stems_fixture(void){
 const char *root="shell-analysis-fixture";char p[512];
 snprintf(p,sizeof p,"%s/CDJMODS",root);make_dir(p);snprintf(p,sizeof p,"%s/CDJMODS/stems",root);make_dir(p);
 snprintf(p,sizeof p,"%s/CDJMODS/stems/0123456789abcdef",root);make_dir(p);
 static const char *const names[3]={"drums","harmonics","vocal"};
 for(int s=0;s<3;s++){snprintf(p,sizeof p,"%s/CDJMODS/stems/0123456789abcdef/stems-%s-waveform.2EX",root,names[s]);stem_file(p,(uint8_t)(10*(s+1)),50);}
 /* "/Contents/Été.mp3" written with a \u escape, a duplicate that agrees, and a contradictory duplicate. */
 char json[1024];const char bs=92;
 snprintf(json,sizeof json,"{\"schema\":\"overcue-index/1\",\"tracks\":{"
  "\"a\":{\"file_path\":\"/Contents/%cu00c9t%cu00e9.mp3\",\"bundle\":\"0123456789abcdef\",\"frames\":1},"
  "\"b\":{\"file_path\":\"/Contents/One.mp3\",\"bundle\":\"0123456789abcdef\"},"
  "\"c\":{\"file_path\":\"/Contents/Two.flac\",\"bundle\":\"0123456789abcdef\"}},"
  "\"tracks_onelibrary\":{\"d\":{\"file_path\":\"/Contents/One.mp3\",\"bundle\":\"0123456789abcdef\"},"
  "\"e\":{\"file_path\":\"/Contents/Two.flac\",\"bundle\":\"fedcba9876543210\"}}}",bs,bs);
 snprintf(p,sizeof p,"%s/CDJMODS/index.json",root);text_file(p,json);
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
 /* Stems: one parse of the stick's stem index, then only the PWV7 sections of the three files. */
 stems_fixture();
 struct xz_shana_stems *st=NULL;
 assert(!xz_shana_stems_build(root,&st)&&st&&xz_shana_stems_count(st)==3);
 assert(!xz_shana_stems_find(st,"/Contents/\xc3\x89t\xc3\xa9.mp3",dir,sizeof dir)&&strstr(dir,"CDJMODS/stems/0123456789abcdef"));
 assert(!xz_shana_stems_find(st,"/Contents/One.mp3",dir,sizeof dir));        /* agreeing duplicate */
 assert(xz_shana_stems_find(st,"/Contents/Two.flac",dir,sizeof dir));        /* contradictory duplicate: refused */
 assert(xz_shana_stems_find(st,"/Contents/Folder/Three.wav",dir,sizeof dir)); /* not in the index */
 uint32_t split[3];
 assert(!xz_shana_stems_find(st,"/Contents/One.mp3",dir,sizeof dir)&&!xz_shana_stems_load(dir,&w,split));
 /* drums 10+11+12, harmonics 20+21+22, vocal 30+31+32 per column, in that order. */
 assert(w.count==50&&w.bands[0]==33&&w.bands[1]==63&&w.bands[2]==93);
 xz_wave_load_free(&w);xz_shana_stems_free(st);
 /* No stem index: an empty map. A malformed one: empty too. */
 assert(!xz_shana_stems_build("shell-analysis-fixture/none",&st)&&st&&!xz_shana_stems_count(st));xz_shana_stems_free(st);
 /* The signature changes when analysis is added, not when nothing changed. */
 uint64_t sig=xz_shana_signature(root);assert(sig&&sig==xz_shana_signature(root));
 track(root,"P0A0","0002EEEE","/Contents/New.mp3",50,1);
 assert(xz_shana_signature(root)!=sig&&!xz_shana_signature("shell-analysis-fixture/none"));
 puts("PASS stems map: escaped paths, duplicates, missing tracks, PWV7-only reads; analysis signature");
 {
  /* Row preview: PWV6 read past a PWV7 section; a 2EX without PWV6 has none. */
  char pv_dir[512],pv_file[600];snprintf(pv_dir,sizeof pv_dir,"%s/PIONEER/USBANLZ/P07F/0001CCCC",root);
  snprintf(pv_file,sizeof pv_file,"%s/ANLZ0000.2EX",pv_dir);
  static uint8_t o[8192];size_t n=dat(o,"/Contents/Folder/Three.wav");
  memcpy(o+n,"PWV7",4);be(o+n+4,24);be(o+n+8,24+30);be(o+n+12,3);be(o+n+16,10);be(o+n+20,0);n+=24;memset(o+n,0x11,30);n+=30;
  memcpy(o+n,"PWV6",4);be(o+n+4,20);be(o+n+8,20+1200*3);be(o+n+12,3);be(o+n+16,1200);n+=20;
  for(int i=0;i<1200*3;i++)o[n++]=(uint8_t)(i%3==0?i/3%200:i%3==1?7:9);
  be(o+8,(uint32_t)n);put(pv_file,o,n);
  uint8_t *bands=NULL;uint32_t count=0;
  assert(!xz_shana_preview(pv_dir,&bands,&count)&&bands&&count==1200&&bands[0]==0&&bands[1]==7&&bands[2]==9&&bands[3*199]==199);
  free(bands);
  snprintf(pv_dir,sizeof pv_dir,"%s/PIONEER/USBANLZ/P001/0000AAAA",root);
  assert(xz_shana_preview(pv_dir,&bands,&count)&&!bands&&!count);
  assert(xz_shana_preview("shell-analysis-fixture/none",&bands,&count));
  puts("PASS row preview: PWV6 behind other sections, missing section, missing file");
 }
 {
  char cue_dir[512],cue_file[600];snprintf(cue_dir,sizeof cue_dir,"%s/PIONEER/USBANLZ/P001/0000AAAA",root);
  snprintf(cue_file,sizeof cue_file,"%s/ANLZ0000.DAT",cue_dir);cue_dat(cue_file,"/Contents/One.mp3");
  struct xz_shell_cue cues[XZ_SHELL_MEMORY_CUES];
  int n=xz_shana_memory_cues(cue_dir,cues);
  assert(n==2&&cues[0].set&&cues[0].ms==2500&&!cues[0].is_loop&&cues[1].ms==9000&&cues[1].is_loop);
  /* A DAT with no cue list at all stays unknown (rekordbox writes the lists even when empty). */
  snprintf(cue_dir,sizeof cue_dir,"%s/PIONEER/USBANLZ/P001/0000BBBB",root);
  assert(xz_shana_memory_cues(cue_dir,cues)==-1);
  assert(xz_shana_memory_cues("shell-analysis-fixture/none",cues)==-1);
  puts("PASS memory cues: PCOB memory list only, loops, hot cue entries skipped, time order");
  /* CUE/LOOP LIST: from the DAT without comments, then from the EXT with comment and colour. */
  struct xz_shana_cue list[8];
  snprintf(cue_dir,sizeof cue_dir,"%s/PIONEER/USBANLZ/P001/0000AAAA",root);
  n=xz_shana_cue_list(cue_dir,list,8);
  assert(n==2&&!list[0].loop&&list[0].ms==2500&&!list[0].comment[0]&&list[1].loop&&list[1].ms==9000&&list[1].end_ms==13000);
  snprintf(cue_file,sizeof cue_file,"%s/ANLZ0000.EXT",cue_dir);cue_ext(cue_file);
  n=xz_shana_cue_list(cue_dir,list,8);
  assert(n==2&&list[0].ms==2500&&!strcmp(list[0].comment,"Drop 1")&&list[0].rgb==0x10a0ff&&list[1].loop&&list[1].end_ms==13000&&!list[1].comment[0]);
  assert(xz_shana_cue_list(cue_dir,list,1)==1);
  assert(xz_shana_cue_list("shell-analysis-fixture/none",list,8)==-1);
  puts("PASS CUE/LOOP LIST: EXT memory list with comments and colours, DAT fallback, cap, missing files");
 }
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
