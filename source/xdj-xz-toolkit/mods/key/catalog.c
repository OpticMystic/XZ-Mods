/* SPDX-License-Identifier: MIT */
/* Format facts: https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "catalog.h"
#include "../audio/vendor/sha256/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define MAX_TRACKS 131072u
struct key_row {uint32_t id;struct xz_music_key key;};
static uint16_t u16(const unsigned char *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void hash_title(const uint16_t *title,size_t n,unsigned char out[32]){
    SHA256_CTX ctx;sha256_init(&ctx);
    for(size_t i=0;i<n&&title[i];i++){unsigned char word[2]={(unsigned char)title[i],(unsigned char)(title[i]>>8)};sha256_update(&ctx,word,2);}
    sha256_final(&ctx,out);
}
static int text(const unsigned char *p,size_t n,uint16_t out[256],size_t *length){
    if(!n)return -1;unsigned kind=p[0];size_t bytes,header;int wide=0;
    if(kind==0x40||kind==0x90){if(n<4)return -1;bytes=u16(p+1);if(bytes<4||bytes>n)return -1;bytes-=4;header=4;wide=kind==0x90;}
    else{if(!(kind&1)||kind<3)return -1;bytes=(kind>>1)-1;header=1;if(bytes+1>n)return -1;}
    if(wide&&(bytes&1))return -1;
    size_t used=0;for(size_t i=0;i<bytes;i+=wide?2:1){
        uint16_t ch=wide?u16(p+header+i):p[header+i];
        if(!wide&&ch>127)return -1;
        if(!ch)break;
        if(used<255)out[used++]=ch;
    }
    if(used&&out[used-1]>=0xd800&&out[used-1]<=0xdbff)used--;
    out[used]=0;*length=used;return 0;
}
static int compare(const void *a,const void *b){return memcmp(((const struct xz_key_entry*)a)->title,((const struct xz_key_entry*)b)->title,32);}
void xz_key_catalog_free(struct xz_key_catalog *catalog){if(catalog){free(catalog->entries);free(catalog->artists);memset(catalog,0,sizeof(*catalog));}}
static int append(struct xz_key_catalog *cat,uint32_t id,struct xz_music_key key,const uint16_t *title,size_t n,uint32_t artist,uint32_t tempo){
    if(cat->count==MAX_TRACKS)return -1;
    if(cat->count==cat->capacity){size_t cap=cat->capacity?cat->capacity*2:1024;
        struct xz_key_entry *entries=realloc(cat->entries,cap*sizeof(*entries));if(!entries)return -1;
        cat->entries=entries;cat->capacity=cap;}
    struct xz_key_entry *entry=&cat->entries[cat->count++];entry->id=id;entry->artist_id=artist;entry->tempo=tempo;entry->key=key;hash_title(title,n,entry->title);return 0;
}
int xz_key_catalog_load(const char *path,struct xz_key_catalog *out){
    if(!path||!out)return -1;struct xz_key_catalog cat={0};struct key_row keys[512];size_t key_count=0;
    struct stat stat;FILE *file=fopen(path,"rb");unsigned char header[4096],*page=NULL,*visited=NULL;int result=-1;
    if(!file||fstat(fileno(file),&stat)||!S_ISREG(stat.st_mode)||stat.st_size<4096||stat.st_size>512LL*1024*1024)goto done;
    if(fread(header,1,sizeof(header),file)!=sizeof(header)||u32(header)!=0)goto done;
    uint32_t page_size=u32(header+4),tables=u32(header+8);
    if(page_size<4096||page_size>16384||(page_size&(page_size-1))||!tables||tables>128||28+tables*16>sizeof(header)||stat.st_size%page_size)goto done;
    size_t pages=(size_t)(stat.st_size/page_size);page=malloc(page_size);visited=calloc(pages,1);if(!page||!visited)goto done;
    for(int pass=0;pass<3;pass++){
        unsigned type=pass==0?2:pass==1?5:0;int found=0;
        for(unsigned table=0;table<tables;table++){
            const unsigned char *pointer=header+28+table*16;if(u32(pointer)!=type)continue;
            if(found++)goto done;memset(visited,0,pages);
            uint32_t index=u32(pointer+8),last=u32(pointer+12);
            for(;;){
                if(!index||index>=pages||last>=pages||visited[index])goto done;visited[index]=1;
                if(fseeko(file,(off_t)index*page_size,SEEK_SET)||fread(page,1,page_size,file)!=page_size||u32(page+4)!=index||u32(page+8)!=type)goto done;
                unsigned rows=(u32(page+24)&0x1fff),groups=(rows+15)/16;
                if(!(page[27]&0x40)){
                    if(groups*36+40>page_size)goto done;
                    size_t heap_end=page_size-groups*36;
                    for(unsigned row=0;row<rows;row++){
                        size_t base=page_size-(row/16)*36;
                        if(!(u16(page+base-4)&(1u<<(row%16))))continue;
                        size_t start=40+u16(page+base-6-(row%16)*2);if(start>=heap_end)goto done;
                        const unsigned char *data=page+start;size_t size=heap_end-start;uint16_t title[256];size_t n;
                        if(type==5){
                            if(size<9||key_count==512||text(data+8,size-8,title,&n)||n>7)goto done;
                            char name[8];for(size_t i=0;i<n;i++){if(title[i]==0x266f)title[i]='#';if(title[i]==0x266d)title[i]='b';if(title[i]>127)goto done;name[i]=(char)title[i];}name[n]=0;
                            struct xz_music_key key={-1,0};(void)xz_music_key_parse(name,&key);
                            keys[key_count++]=(struct key_row){u32(data),key};
                        }else if(type==2){
                            if(size<10)goto done;
                            size_t offset=(u16(data)&4)?(size<12?size:u16(data+10)):data[9];
                            if(offset<10||offset>=size||text(data+offset,size-offset,title,&n)||cat.artist_count==65536)goto done;
                            if(cat.artist_count==cat.artist_capacity){size_t cap=cat.artist_capacity?cat.artist_capacity*2:128;
                                struct xz_artist_entry *next=realloc(cat.artists,cap*sizeof(*next));if(!next)goto done;
                                cat.artists=next;cat.artist_capacity=cap;}
                            struct xz_artist_entry *artist=&cat.artists[cat.artist_count++];memset(artist,0,sizeof(*artist));artist->id=u32(data+4);
                            size_t used=n<63?n:63;if(used&&title[used-1]>=0xd800&&title[used-1]<=0xdbff)used--;
                            memcpy(artist->name,title,used*2);
                        }else{
                            if(size<0x88||u16(data)!=0x24)goto done;
                            size_t offset=u16(data+0x5e + 17*2);if(offset<0x88||offset>=size||text(data+offset,size-offset,title,&n))goto done;
                            struct xz_music_key key={-1,0};uint32_t id=u32(data+0x20);
                            for(size_t i=0;i<key_count;i++)if(keys[i].id==id){key=keys[i].key;break;}
                            if(n&&append(&cat,u32(data+0x4c),key,title,n,u32(data+0x48),u32(data+0x38)))goto done;
                        }
                    }
                }
                if(index==last)break;index=u32(page+12);
            }
        }
        if(!found&&type!=2)goto done;
    }
    qsort(cat.entries,cat.count,sizeof(*cat.entries),compare);*out=cat;memset(&cat,0,sizeof(cat));result=0;
done:
    if(file)fclose(file);free(page);free(visited);xz_key_catalog_free(&cat);return result;
}
int xz_key_catalog_lookup(const struct xz_key_catalog *cat,const uint16_t *title,size_t n,struct xz_music_key *out){
    if(!cat||!title||!n||n>255||!out)return 0;unsigned char digest[32];hash_title(title,n,digest);
    size_t lo=0,hi=cat->count;while(lo<hi){size_t mid=lo+(hi-lo)/2;int c=memcmp(cat->entries[mid].title,digest,32);if(c<0)lo=mid+1;else hi=mid;}
    if(lo==cat->count||memcmp(cat->entries[lo].title,digest,32))return 0;
    struct xz_music_key key=cat->entries[lo].key;if(key.root<0)return -1;
    for(size_t i=lo+1;i<cat->count&&!memcmp(cat->entries[i].title,digest,32);i++)if(cat->entries[i].key.root!=key.root||cat->entries[i].key.minor!=key.minor)return -1;
    *out=key;return 1;
}

int xz_key_catalog_track(const struct xz_key_catalog *cat,const uint16_t *title,size_t n,struct xz_key_entry *out){
    if(!cat||!title||!n||n>255||!out)return 0;unsigned char digest[32];hash_title(title,n,digest);
    size_t lo=0,hi=cat->count;while(lo<hi){size_t mid=lo+(hi-lo)/2;int c=memcmp(cat->entries[mid].title,digest,32);if(c<0)lo=mid+1;else hi=mid;}
    if(lo==cat->count||memcmp(cat->entries[lo].title,digest,32))return 0;
    struct xz_key_entry entry=cat->entries[lo];
    for(size_t i=lo+1;i<cat->count&&!memcmp(cat->entries[i].title,digest,32);i++)
        if(cat->entries[i].key.root!=entry.key.root||cat->entries[i].key.minor!=entry.key.minor||
           cat->entries[i].artist_id!=entry.artist_id||cat->entries[i].tempo!=entry.tempo)return -1;
    *out=entry;return 1;
}
int xz_key_catalog_artist(const struct xz_key_catalog *cat,uint32_t id,uint16_t out[64]){
    if(!cat||!out)return 0;out[0]=0;
    for(size_t i=0;i<cat->artist_count;i++)if(cat->artists[i].id==id){memcpy(out,cat->artists[i].name,128);return 1;}
    return 0;
}
