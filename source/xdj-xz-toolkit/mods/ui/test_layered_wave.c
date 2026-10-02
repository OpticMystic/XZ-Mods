/* Layered waveform: loaders on a synthetic rekordbox/OverCue USB, and layer drawing. */
#include "layered_wave.h"
#include "layered_wave_source.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p,0700)
#endif
#ifdef NDEBUG
#error Layered waveform acceptance requires active assertions
#endif

static void dirs(const char *path){char b[512];snprintf(b,sizeof(b),"%s",path);for(char *p=b+1;*p;p++)if(*p=='/'){*p=0;MKDIR(b);*p='/';}MKDIR(b);}
static size_t be(uint8_t *o,uint32_t v){o[0]=(uint8_t)(v>>24);o[1]=(uint8_t)(v>>16);o[2]=(uint8_t)(v>>8);o[3]=(uint8_t)v;return 4;}
static void put(const char *path,const uint8_t *data,size_t n){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,n,f)==n);fclose(f);}
/* PMAI with one PPTH section holding a UTF-16BE path. */
static size_t dat(uint8_t *o,const char *track){
 size_t len=(strlen(track)+1)*2,total=16+len,n=0;
 memcpy(o,"PMAI",4);be(o+4,28);be(o+8,(uint32_t)(28+total));memset(o+12,0,16);n=28;
 memcpy(o+n,"PPTH",4);be(o+n+4,16);be(o+n+8,(uint32_t)total);be(o+n+12,(uint32_t)len);n+=16;
 for(size_t i=0;i<=strlen(track);i++){o[n++]=0;o[n++]=(uint8_t)track[i];}
 return n;
}
/* PMAI with a PWV6 decoy (20-byte header) then PWV7 (24-byte header). */
static size_t two_ex(uint8_t *o,const uint8_t *bands,uint32_t count){
 size_t n=28;memcpy(o,"PMAI",4);be(o+4,28);memset(o+12,0,16);
 memcpy(o+n,"PWV6",4);be(o+n+4,20);be(o+n+8,20+3);be(o+n+12,3);be(o+n+16,1);n+=20;o[n++]=9;o[n++]=9;o[n++]=9;
 memcpy(o+n,"PWV7",4);be(o+n+4,24);be(o+n+8,24+count*3);be(o+n+12,3);be(o+n+16,count);be(o+n+20,0);n+=24;
 memcpy(o+n,bands,count*3);n+=count*3;
 be(o+8,(uint32_t)n);return n;
}
int main(void){
 const char *root="layered-wave-fixture/usb";
 const char *track="/Contents/Artist/Song.flac";
 enum{COUNT=200};uint8_t bands[COUNT*3];
 for(unsigned i=0;i<COUNT;i++){bands[i*3]=(uint8_t)(i%50);bands[i*3+1]=(uint8_t)(i%100);bands[i*3+2]=(uint8_t)(i%200);}
 static uint8_t buf[65536];char path[512];
 dirs("layered-wave-fixture/usb/PIONEER/USBANLZ/P001/0000ABCD");
 dirs("layered-wave-fixture/usb/PIONEER/USBANLZ/P002/00000001");
 snprintf(path,sizeof(path),"%s/PIONEER/USBANLZ/P001/0000ABCD/ANLZ0000.DAT",root);put(path,buf,dat(buf,track));
 snprintf(path,sizeof(path),"%s/PIONEER/USBANLZ/P001/0000ABCD/ANLZ0000.2EX",root);put(path,buf,two_ex(buf,bands,COUNT));
 snprintf(path,sizeof(path),"%s/PIONEER/USBANLZ/P002/00000001/ANLZ0000.DAT",root);put(path,buf,dat(buf,"/Contents/Other.mp3"));
 char source[512];snprintf(source,sizeof(source),"%s%s",root,track);
 struct xz_wave_load w;
 xz_wave_index_reset();
 assert(!xz_wave_load_three_band(source,&w));
 assert(w.kind==XZ_LAYERED_THREE_BAND&&w.count==COUNT&&!memcmp(w.bands,bands,sizeof(bands)));
 assert(w.normalization==xz_wave_normalization(bands,COUNT,XZ_LAYERED_THREE_BAND)&&w.normalization>=190);
 xz_wave_load_free(&w);
 /* Case-insensitive like FAT; unknown tracks fail cleanly. */
 snprintf(source,sizeof(source),"%s/contents/artist/SONG.flac",root);
 assert(xz_wave_load_three_band(source,&w)<0||w.count==COUNT);xz_wave_load_free(&w);
 snprintf(source,sizeof(source),"%s/Contents/Missing.flac",root);
 assert(xz_wave_load_three_band(source,&w)<0&&!w.bands);
 /* Stems: index.json -> bundle -> three per-stem PWV7 files. */
 dirs("layered-wave-fixture/usb/CDJMODS/stems/00112233445566aa");
 const char *index="{\"schema\":\"overcue-index/1\",\"tracks\":{\"1\":{\"bundle\":\"00112233445566aa\",\"file_path\":\"/Contents/Artist/Song.flac\"}}}";
 snprintf(path,sizeof(path),"%s/CDJMODS/index.json",root);put(path,(const uint8_t*)index,strlen(index));
 static const char *names[3]={"drums","harmonics","vocal"};
 for(unsigned s=0;s<3;s++){
  uint8_t part[COUNT*3];
  for(unsigned i=0;i<COUNT;i++){part[i*3]=(uint8_t)(s==0?40:0);part[i*3+1]=(uint8_t)(s==1?40:0);part[i*3+2]=(uint8_t)(s==2?40:0);}
  snprintf(path,sizeof(path),"%s/CDJMODS/stems/00112233445566aa/stems-%s-waveform.2EX",root,names[s]);put(path,buf,two_ex(buf,part,COUNT));
 }
 snprintf(source,sizeof(source),"%s%s",root,track);
 assert(!xz_wave_load_stems(source,&w));
 /* Each stem's bands sum at equal weight: drums low 40, harmonics mid 40, vocals high 40. */
 assert(w.kind==XZ_LAYERED_STEMS&&w.count==COUNT&&w.bands[0]==40&&w.bands[1]==40&&w.bands[2]==40&&w.normalization==120);
 /* Drawing: stacked, drums outside, harmonics, vocals at the core. */
 struct xz_layered_data data={w.bands,w.count,w.normalization,w.kind,{0xf800,0x07e0,0x001f}};
 static uint16_t px[536*268];struct xz_layered_col col;
 assert(xz_layered_column(px,536,536,268,10,65,52,&data,0,1,0,0,&col)&&col.valid);
 assert(col.h[0]==52&&col.h[1]==34&&col.h[2]==17);
 assert(px[65*536+10]==0x001f&&px[(65-col.h[2])*536+10]==0x07e0&&px[(65-col.h[1])*536+10]==0xf800);
 /* Each stem is exact at its inner boundary and dims gently toward its outer edge. */
 assert(xz_layered_pixel(&data,&col,34)==0xf800&&xz_layered_pixel(&data,&col,51)==(26<<11)&&xz_layered_pixel(&data,&col,52)<0);
 xz_wave_load_free(&w);
 /* 3-band stacks at equal weight: white highs at the core, amber mids, blue lows outside. */
 uint8_t one[3]={60,20,30};struct xz_layered_data tb={one,1,240,XZ_LAYERED_THREE_BAND,{0,0,0}};
 assert(xz_layered_column(px,536,536,268,0,201,52,&tb,0,1,0,0,&col));
 assert(col.h[0]==23&&col.h[1]==10&&col.h[2]==6);
 assert(xz_layered_pixel(&tb,&col,0)==0xffff&&xz_layered_pixel(&tb,&col,7)==(int)(((242>>3)<<11)|((170>>2)<<5)|(60>>3)));
 assert(xz_layered_pixel(&tb,&col,15)==(int)(((32>>3)<<11)|((83>>2)<<5)|(217>>3))&&px[(201-15)*536]==(uint16_t)xz_layered_pixel(&tb,&col,15));
 assert(xz_layered_pixel(&tb,&col,30)<0);
 /* Out-of-range samples leave the stock column. */
 assert(!xz_layered_column(px,536,536,268,0,201,52,&tb,1,2,0,0,&col)&&!col.valid);
 printf("PASS layered waveform: ANLZ index/PPTH, PWV7 header offset, stems bundle, stacked layers, 3-band colours, bounds\n");
 return 0;
}
