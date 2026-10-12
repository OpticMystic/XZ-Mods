/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "foreign_stems.h"
#include "stem_media.h"
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define JSMN_STATIC
#include "vendor/jsmn/jsmn.h"
#include "vendor/sha256/sha256.h"
#ifdef _WIN32
#define stem_seek _fseeki64
#define stem_tell _ftelli64
#else
#define stem_seek fseeko
#define stem_tell ftello
#endif
struct document { char *text; jsmntok_t *token; int count; };
static int hex(unsigned char c) { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static int digest_matches(const unsigned char digest[32], const char *hexstr) {
    if (strlen(hexstr)!=64) return 0;
    for (unsigned i=0;i<32;i++) if (hex((unsigned char)hexstr[i*2])<0 || hex((unsigned char)hexstr[i*2+1])<0 ||
        digest[i] != (unsigned)(hex((unsigned char)hexstr[i*2])*16+hex((unsigned char)hexstr[i*2+1]))) return 0;
    return 1;
}
static int source_hash(const char *path,unsigned char sum[32]) {
    FILE *f=fopen(path,"rb"); if(!f)return 0;
    SHA256_CTX c;sha256_init(&c);unsigned char data[32768];size_t n;uint64_t total=0;
    while((n=fread(data,1,sizeof(data),f))){sha256_update(&c,data,n);total+=n;if(total>UINT64_C(2147483648)){fclose(f);return 0;}}
    int ok=!ferror(f);fclose(f);sha256_final(&c,sum);return ok;
}
static int document_open(const char *path,struct document *d) {
    memset(d,0,sizeof(*d));FILE *f=fopen(path,"rb");if(!f)return -1;
    if(fseek(f,0,SEEK_END)){fclose(f);return -1;}long size=ftell(f);rewind(f);
    if(size<2||size>4*1024*1024){fclose(f);return -1;}
    d->text=calloc((size_t)size+1,1);d->token=calloc((size_t)size/2+1,sizeof(*d->token));
    if(!d->text||!d->token){fclose(f);return -1;}
    int ok=fread(d->text,1,(size_t)size,f)==(size_t)size;fclose(f);if(!ok)return -1;
    jsmn_parser parser;jsmn_init(&parser);
    d->count=jsmn_parse(&parser,d->text,(size_t)size,d->token,(unsigned)size/2+1);
    return d->count>0&&d->token[0].type==JSMN_OBJECT?0:-1;
}
static void document_close(struct document *d){free(d->text);free(d->token);memset(d,0,sizeof(*d));}
static int equal(const struct document *d,int t,const char *s){return t>=0&&t<d->count&&(size_t)(d->token[t].end-d->token[t].start)==strlen(s)&&!memcmp(d->text+d->token[t].start,s,strlen(s));}
static int next(const struct document *d,int t){int end=d->token[t].end;while(++t<d->count&&d->token[t].start<end){}return t;}
static int field(const struct document *d,int object,const char *name){
    if(object<0||object>=d->count||d->token[object].type!=JSMN_OBJECT)return -1;
    for(int t=object+1;t<d->count&&d->token[t].start<d->token[object].end;){
        int value=t+1;if(value>=d->count)return -1;
        if(equal(d,t,name))return value;t=next(d,value);
    }return -1;
}
static int string(const struct document *d,int t,char *out,size_t cap){
    if(t<0||t>=d->count||d->token[t].type!=JSMN_STRING||!cap)return -1;
    size_t n=0;int end=d->token[t].end;
    for(int p=d->token[t].start;p<end;){
        unsigned c=(unsigned char)d->text[p++];
        if(c=='\\'){
            if(p>=end)return -1;c=(unsigned char)d->text[p++];
            if(c=='u'){
                if(p+4>end)return -1;c=0;
                for(int k=0;k<4;k++){int h=hex((unsigned char)d->text[p++]);if(h<0)return -1;c=c*16+(unsigned)h;}
                if(c>=0xd800&&c<=0xdbff){
                    if(p+6>end||d->text[p++]!='\\'||d->text[p++]!='u')return -1;unsigned low=0;
                    for(int k=0;k<4;k++){int h=hex((unsigned char)d->text[p++]);if(h<0)return -1;low=low*16+(unsigned)h;}
                    if(low<0xdc00||low>0xdfff)return -1;c=0x10000+(c-0xd800)*1024+low-0xdc00;
                }else if(c>=0xdc00&&c<=0xdfff)return -1;
                unsigned char utf[4];size_t count;
                if(c<0x80){utf[0]=(unsigned char)c;count=1;}
                else if(c<0x800){utf[0]=(unsigned char)(0xc0|(c>>6));utf[1]=(unsigned char)(0x80|(c&63));count=2;}
                else if(c<0x10000){utf[0]=(unsigned char)(0xe0|(c>>12));utf[1]=(unsigned char)(0x80|((c>>6)&63));utf[2]=(unsigned char)(0x80|(c&63));count=3;}
                else{utf[0]=(unsigned char)(0xf0|(c>>18));utf[1]=(unsigned char)(0x80|((c>>12)&63));utf[2]=(unsigned char)(0x80|((c>>6)&63));utf[3]=(unsigned char)(0x80|(c&63));count=4;}
                if(!c||n+count>=cap)return -1;memcpy(out+n,utf,count);n+=count;continue;
            }
            if(c!='/'&&c!='\\'&&c!='"')return -1;
        }
        if(c<32||n+1>=cap)return -1;out[n++]=(char)c;
    }out[n]=0;return 0;
}
static double number(const struct document *d,int t){
    if(t<0||t>=d->count||d->token[t].type!=JSMN_PRIMITIVE)return -1;
    char *end;double n=strtod(d->text+d->token[t].start,&end);
    return end==d->text+d->token[t].end&&isfinite(n)?n:-1;
}

#define FOREIGN_BLOCK 2048u
#define FOREIGN_MAX_FRAMES UINT64_C(134217728)
struct foreign_pcm { FILE *file; uint64_t offset, frames; unsigned format; float gain; };
struct xz_foreign_stems {
    enum xz_foreign_format format;
    struct xz_stem_media *reference, *role[4];
    struct foreign_pcm pcm[4];
    uint64_t frames;
    uint32_t rate;
    unsigned count;
};
static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint64_t le64(const unsigned char *p) { return le32(p) | (uint64_t)le32(p+4)<<32; }
static int zeros(const unsigned char *p, size_t n) { for(size_t i=0;i<n;i++)if(p[i])return 0;return 1; }
static int joined(char out[4096],const char *a,const char *b) { return snprintf(out,4096,"%s/%s",a,b)>=4096?-1:0; }
static int regular(const char *path) {
    struct stat st;
#ifdef _WIN32
    return !stat(path,&st) && (st.st_mode&S_IFMT)==S_IFREG;
#else
    return !lstat(path,&st) && S_ISREG(st.st_mode);
#endif
}
static int directory(const char *path) {
    struct stat st;
#ifdef _WIN32
    return !stat(path,&st) && (st.st_mode&S_IFMT)==S_IFDIR;
#else
    return !lstat(path,&st) && S_ISDIR(st.st_mode);
#endif
}
static int length_of(FILE *f,uint64_t *size) {
    if(stem_seek(f,0,SEEK_END))return -1;
    int64_t end=stem_tell(f);if(end<0)return -1;*size=(uint64_t)end;return stem_seek(f,0,SEEK_SET);
}
static uint64_t fnv(uint64_t h,const unsigned char *data,size_t n) {
    for(size_t i=0;i<n;i++)h=(h^data[i])*UINT64_C(1099511628211);return h;
}
static int az_fingerprint(const char *path,char value[25],uint64_t *size) {
    FILE *f=fopen(path,"rb");if(!f)return -1;
    if(length_of(f,size)){fclose(f);return -1;}
    unsigned char *block=malloc(65536);if(!block){fclose(f);return -1;}
    char decimal[32];int n=snprintf(decimal,sizeof(decimal),"%llu\n",(unsigned long long)*size);
    uint64_t h=fnv(UINT64_C(14695981039346656037),(const unsigned char *)decimal,(size_t)n);
    uint64_t offsets[3]={0,*size/2>32768?*size/2-32768:0,*size>65536?*size-65536:0};
    int rc=0;
    for(unsigned i=0;i<3;i++){
        size_t count=(size_t)(*size-offsets[i]);if(count>65536)count=65536;
        if(stem_seek(f,(int64_t)offsets[i],SEEK_SET)||fread(block,1,count,f)!=count){rc=-1;break;}
        h=fnv(h,block,count);
    }
    free(block);fclose(f);
    if(!rc)snprintf(value,25,"fnv1a64:%016llx",(unsigned long long)h);
    return rc;
}

void xz_foreign_close(struct xz_foreign_stems *s) {
    if(!s)return;
    xz_stem_media_close(s->reference);
    for(unsigned r=0;r<4;r++){xz_stem_media_close(s->role[r]);if(s->pcm[r].file)fclose(s->pcm[r].file);}
    free(s);
}
uint32_t xz_foreign_rate(const struct xz_foreign_stems *s) {return s?s->rate:0;}
uint64_t xz_foreign_frames(const struct xz_foreign_stems *s) {return s?s->frames:0;}
unsigned xz_foreign_stem_count(const struct xz_foreign_stems *s) {return s?s->count:3;}
enum xz_foreign_format xz_foreign_format(const struct xz_foreign_stems *s) {return s?s->format:0;}

static int az_folder(const char *root,const char *source,char selected[4096],char *error,size_t cap) {
    char base[4096],path[4096],fingerprint[25];uint64_t bytes;
    if(joined(base,root,"STEMS")||!directory(base))return 0;
    if(az_fingerprint(source,fingerprint,&bytes))return -1;
    DIR *dir=opendir(base);if(!dir)return -1;
    struct dirent *entry;unsigned scanned=0;int found=0;
    while((entry=readdir(dir))){
        if(entry->d_name[0]=='.')continue;
        if(++scanned>16384){found=-1;break;}
        char folder[4096];if(joined(folder,base,entry->d_name)||!directory(folder)||joined(path,folder,"source.json")||!regular(path))continue;
        struct document d={0};char identity[32];
        int match=!document_open(path,&d)&&!string(&d,field(&d,0,"fp"),identity,sizeof(identity))&&
            !strcmp(identity,fingerprint)&&number(&d,field(&d,0,"size"))==(double)bytes;
        document_close(&d);
        if(!match)continue;
        if(found){snprintf(error,cap,"AZ STEMS: MULTIPLE MATCHING SETS");found=-1;break;}
        strcpy(selected,folder);found=1;
    }
    closedir(dir);return found;
}
static int open_az(struct xz_foreign_stems *s,const char *root,const char *source,char *error,size_t cap) {
    char folder[4096],path[4096];int found=az_folder(root,source,folder,error,cap);
    if(found<=0)return found;
    const char *names[4]={"drums.ogg","other.ogg","vocals.ogg","bass.ogg"};
    s->format=XZ_FOREIGN_AZ;s->rate=44100;s->count=4;
    for(unsigned r=0;r<4;r++){
        if(joined(path,folder,names[r])||!regular(path)||(s->role[r]=xz_stem_media_open(path,0))==NULL){
            snprintf(error,cap,"AZ STEMS: MISSING OR INVALID %s",names[r]);return -1;
        }
        uint64_t frames=xz_stem_media_frames(s->role[r]);
        if(!r)s->frames=frames;
        if(xz_stem_media_rate(s->role[r])!=44100||frames!=s->frames||frames>FOREIGN_MAX_FRAMES){
            snprintf(error,cap,"AZ STEMS: AUDIO GRIDS DIFFER");return -1;
        }
    }
    return 1;
}

static const uint32_t crc_table[256]={
    0x00000000u,0x77073096u,0xee0e612cu,0x990951bau,0x076dc419u,0x706af48fu,0xe963a535u,0x9e6495a3u,
    0x0edb8832u,0x79dcb8a4u,0xe0d5e91eu,0x97d2d988u,0x09b64c2bu,0x7eb17cbdu,0xe7b82d07u,0x90bf1d91u,
    0x1db71064u,0x6ab020f2u,0xf3b97148u,0x84be41deu,0x1adad47du,0x6ddde4ebu,0xf4d4b551u,0x83d385c7u,
    0x136c9856u,0x646ba8c0u,0xfd62f97au,0x8a65c9ecu,0x14015c4fu,0x63066cd9u,0xfa0f3d63u,0x8d080df5u,
    0x3b6e20c8u,0x4c69105eu,0xd56041e4u,0xa2677172u,0x3c03e4d1u,0x4b04d447u,0xd20d85fdu,0xa50ab56bu,
    0x35b5a8fau,0x42b2986cu,0xdbbbc9d6u,0xacbcf940u,0x32d86ce3u,0x45df5c75u,0xdcd60dcfu,0xabd13d59u,
    0x26d930acu,0x51de003au,0xc8d75180u,0xbfd06116u,0x21b4f4b5u,0x56b3c423u,0xcfba9599u,0xb8bda50fu,
    0x2802b89eu,0x5f058808u,0xc60cd9b2u,0xb10be924u,0x2f6f7c87u,0x58684c11u,0xc1611dabu,0xb6662d3du,
    0x76dc4190u,0x01db7106u,0x98d220bcu,0xefd5102au,0x71b18589u,0x06b6b51fu,0x9fbfe4a5u,0xe8b8d433u,
    0x7807c9a2u,0x0f00f934u,0x9609a88eu,0xe10e9818u,0x7f6a0dbbu,0x086d3d2du,0x91646c97u,0xe6635c01u,
    0x6b6b51f4u,0x1c6c6162u,0x856530d8u,0xf262004eu,0x6c0695edu,0x1b01a57bu,0x8208f4c1u,0xf50fc457u,
    0x65b0d9c6u,0x12b7e950u,0x8bbeb8eau,0xfcb9887cu,0x62dd1ddfu,0x15da2d49u,0x8cd37cf3u,0xfbd44c65u,
    0x4db26158u,0x3ab551ceu,0xa3bc0074u,0xd4bb30e2u,0x4adfa541u,0x3dd895d7u,0xa4d1c46du,0xd3d6f4fbu,
    0x4369e96au,0x346ed9fcu,0xad678846u,0xda60b8d0u,0x44042d73u,0x33031de5u,0xaa0a4c5fu,0xdd0d7cc9u,
    0x5005713cu,0x270241aau,0xbe0b1010u,0xc90c2086u,0x5768b525u,0x206f85b3u,0xb966d409u,0xce61e49fu,
    0x5edef90eu,0x29d9c998u,0xb0d09822u,0xc7d7a8b4u,0x59b33d17u,0x2eb40d81u,0xb7bd5c3bu,0xc0ba6cadu,
    0xedb88320u,0x9abfb3b6u,0x03b6e20cu,0x74b1d29au,0xead54739u,0x9dd277afu,0x04db2615u,0x73dc1683u,
    0xe3630b12u,0x94643b84u,0x0d6d6a3eu,0x7a6a5aa8u,0xe40ecf0bu,0x9309ff9du,0x0a00ae27u,0x7d079eb1u,
    0xf00f9344u,0x8708a3d2u,0x1e01f268u,0x6906c2feu,0xf762575du,0x806567cbu,0x196c3671u,0x6e6b06e7u,
    0xfed41b76u,0x89d32be0u,0x10da7a5au,0x67dd4accu,0xf9b9df6fu,0x8ebeeff9u,0x17b7be43u,0x60b08ed5u,
    0xd6d6a3e8u,0xa1d1937eu,0x38d8c2c4u,0x4fdff252u,0xd1bb67f1u,0xa6bc5767u,0x3fb506ddu,0x48b2364bu,
    0xd80d2bdau,0xaf0a1b4cu,0x36034af6u,0x41047a60u,0xdf60efc3u,0xa867df55u,0x316e8eefu,0x4669be79u,
    0xcb61b38cu,0xbc66831au,0x256fd2a0u,0x5268e236u,0xcc0c7795u,0xbb0b4703u,0x220216b9u,0x5505262fu,
    0xc5ba3bbeu,0xb2bd0b28u,0x2bb45a92u,0x5cb36a04u,0xc2d7ffa7u,0xb5d0cf31u,0x2cd99e8bu,0x5bdeae1du,
    0x9b64c2b0u,0xec63f226u,0x756aa39cu,0x026d930au,0x9c0906a9u,0xeb0e363fu,0x72076785u,0x05005713u,
    0x95bf4a82u,0xe2b87a14u,0x7bb12baeu,0x0cb61b38u,0x92d28e9bu,0xe5d5be0du,0x7cdcefb7u,0x0bdbdf21u,
    0x86d3d2d4u,0xf1d4e242u,0x68ddb3f8u,0x1fda836eu,0x81be16cdu,0xf6b9265bu,0x6fb077e1u,0x18b74777u,
    0x88085ae6u,0xff0f6a70u,0x66063bcau,0x11010b5cu,0x8f659effu,0xf862ae69u,0x616bffd3u,0x166ccf45u,
    0xa00ae278u,0xd70dd2eeu,0x4e048354u,0x3903b3c2u,0xa7672661u,0xd06016f7u,0x4969474du,0x3e6e77dbu,
    0xaed16a4au,0xd9d65adcu,0x40df0b66u,0x37d83bf0u,0xa9bcae53u,0xdebb9ec5u,0x47b2cf7fu,0x30b5ffe9u,
    0xbdbdf21cu,0xcabac28au,0x53b39330u,0x24b4a3a6u,0xbad03605u,0xcdd70693u,0x54de5729u,0x23d967bfu,
    0xb3667a2eu,0xc4614ab8u,0x5d681b02u,0x2a6f2b94u,0xb40bbe37u,0xc30c8ea1u,0x5a05df1bu,0x2d02ef8du,
};
static uint32_t crc_update(uint32_t crc,const unsigned char *data,size_t n) {
    for(size_t i=0;i<n;i++)crc=(crc>>8)^crc_table[(crc^data[i])&255];return crc;
}
static int crc_member(FILE *f,uint64_t offset,uint64_t size,uint32_t expected) {
    unsigned char block[8192];uint32_t crc=UINT32_MAX;
    if(offset>INT64_MAX||stem_seek(f,(int64_t)offset,SEEK_SET))return -1;
    while(size){size_t n=size>sizeof(block)?sizeof(block):(size_t)size;if(fread(block,1,n,f)!=n)return -1;crc=crc_update(crc,block,n);size-=n;}
    return (crc^UINT32_MAX)==expected?0:-1;
}
static int pcm_header(FILE *f,uint64_t offset,uint64_t size,uint64_t frames,struct foreign_pcm *out,int packaged) {
    unsigned char h[64];
    if(offset>INT64_MAX||stem_seek(f,(int64_t)offset,SEEK_SET)||fread(h,1,64,f)!=64||memcmp(h,"RX3STM1\0",8)||
        le32(h+8)!=44100||le32(h+12)!=2||le32(h+20)!=64||le64(h+24)!=frames)return -1;
    unsigned format=le32(h+16);float gain=1;
    if(format==3){uint32_t bits=le32(h+32);memcpy(&gain,&bits,4);if(!isfinite(gain)||gain<1||gain>64||!zeros(h+36,28))return -1;}
    else if((format!=1&&format!=2)||!zeros(h+32,32)||(packaged&&format==1))return -1;
    unsigned stride=format==1?8:4;
    if(!frames||frames>FOREIGN_MAX_FRAMES||size!=64+frames*stride)return -1;
    *out=(struct foreign_pcm){.offset=offset+64,.frames=frames,.format=format,.gain=gain};return 0;
}
/* Probe only the bounded directory/manifest before hashing or opening PCM.
 * Folder/basename equality is never accepted as source identity. */
static int rx3_package(const char *path,const unsigned char source_digest[32],struct xz_foreign_stems *s,int load) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char h[64],entries[5][64];uint64_t size=0;int rc=-1;
    if(length_of(f,&size)||fread(h,1,64,f)!=64||memcmp(h,"RX3PKG2\0",8)){fclose(f);return 0;}
    unsigned version=le32(h+8),count=le32(h+16);uint64_t frames=le64(h+32);
    if((version!=2&&version!=3)||le32(h+12)!=64||(count!=4&&count!=5)||le32(h+20)!=64||le64(h+24)!=size||
        !zeros(h+40,24)||!frames||frames>FOREIGN_MAX_FRAMES||fread(entries,64,count,f)!=count)goto done;
    uint64_t offset=64+64*count;
    unsigned parts=count-2;
    const char *names[5]={"vocals","drums",count==5?"bass":"waveform",count==5?"waveform":"manifest","manifest"};
    const unsigned kinds[5]={1,1,count==5?1:2,count==5?2:3,3},roles[5]={2,4,count==5?8:0,0,0};
    for(unsigned i=0;i<count;i++){
        const unsigned char *e=entries[i];uint64_t bytes=le64(e+16);unsigned char label[32]={0};memcpy(label,names[i],strlen(names[i]));
        if(le32(e)!=kinds[i]||le32(e+4)!=roles[i]||le64(e+8)!=offset||le32(e+28)||memcmp(e+32,label,32)||
            offset>size||bytes>size-offset||(!bytes&&!(version==3&&i==parts))||(i==count-1&&bytes>65536))goto done;
        offset+=bytes;
    }
    if(offset!=size)goto done;
    uint64_t manifest_size=le64(entries[count-1]+16),manifest_at=le64(entries[count-1]+8);
    struct document d={0};d.text=calloc((size_t)manifest_size+1,1);d.token=calloc((size_t)manifest_size/2+1,sizeof(*d.token));
    if(!d.text||!d.token||stem_seek(f,(int64_t)manifest_at,SEEK_SET)||fread(d.text,1,(size_t)manifest_size,f)!=manifest_size){document_close(&d);goto done;}
    jsmn_parser parser;jsmn_init(&parser);d.count=jsmn_parse(&parser,d.text,(size_t)manifest_size,d.token,(unsigned)manifest_size/2+1);
    char expected[65];int identity=d.count>0&&d.token[0].type==JSMN_OBJECT&&
        !string(&d,field(&d,0,"source_sha256"),expected,sizeof(expected))&&strlen(expected)==64;
    int grid=number(&d,field(&d,0,"version"))==version&&number(&d,field(&d,0,"frames"))==(double)frames&&
        number(&d,field(&d,0,"sample_rate"))==44100&&number(&d,field(&d,0,"channels"))==2;
    int declared=field(&d,0,"roles");
    grid=grid&&declared>=0&&d.token[declared].type==JSMN_ARRAY&&d.token[declared].size==(int)parts&&
        equal(&d,declared+1,"vocals")&&equal(&d,declared+2,"drums")&&(parts==2||equal(&d,declared+3,"bass"));
    document_close(&d);
    if(!identity||!grid)goto done;
    if(!digest_matches(source_digest,expected)){rc=0;goto done;}
    if(!load){rc=1;goto done;}
    for(unsigned i=0;i<count;i++)if(crc_member(f,le64(entries[i]+8),le64(entries[i]+16),le32(entries[i]+24)))goto done;
    const unsigned map[3]={2,0,3};
    for(unsigned i=0;i<parts;i++){
        struct foreign_pcm pcm={0};
        if(pcm_header(f,le64(entries[i]+8),le64(entries[i]+16),frames,&pcm,1))goto done;
        pcm.file=fopen(path,"rb");if(!pcm.file)goto done;s->pcm[map[i]]=pcm;
    }
    s->format=XZ_FOREIGN_RX3;s->rate=44100;s->frames=frames;s->count=parts+1;rc=1;
done: fclose(f);return rc;
}
static int rx3_legacy(struct xz_foreign_stems *s,const char *base,const unsigned char source_digest[32],char *error,size_t cap) {
    char path[4096],expected[65];struct document d={0};int rc=0,chosen=-1;
    if(joined(path,base,"rx3-stems-manifest.json")||!regular(path)||document_open(path,&d))goto done;
    int tracks=field(&d,0,"tracks");
    if(tracks<0||d.token[tracks].type!=JSMN_ARRAY)goto done;
    for(int t=tracks+1;t<d.count&&d.token[t].start<d.token[tracks].end;t=next(&d,t)){
        if(string(&d,field(&d,t,"source_sha256"),expected,sizeof(expected))||!digest_matches(source_digest,expected))continue;
        if(chosen>=0){snprintf(error,cap,"RX3 STEMS: MULTIPLE MATCHING RECORDS");rc=-1;goto done;}chosen=t;
    }
    if(chosen<0)goto done;
    rc=-1;int stems=field(&d,chosen,"stems");unsigned seen=0;
    if(stems<0||d.token[stems].type!=JSMN_ARRAY||(d.token[stems].size!=2&&d.token[stems].size!=3))goto invalid;
    for(int t=stems+1;t<d.count&&d.token[t].start<d.token[stems].end;t=next(&d,t)){
        char role[16],name[1024];unsigned r;
        if(string(&d,field(&d,t,"role"),role,sizeof(role))||string(&d,field(&d,t,"file"),name,sizeof(name))||
            string(&d,field(&d,t,"sha256"),expected,sizeof(expected)))goto invalid;
        if(!strcmp(role,"vocals"))r=2;else if(!strcmp(role,"drums"))r=0;else if(!strcmp(role,"bass"))r=3;else goto invalid;
        if(seen&(1u<<r)||!name[0]||name[0]=='.'||strchr(name,'/')||strchr(name,'\\')||strchr(name,':')||joined(path,base,name)||!regular(path))goto invalid;
        unsigned char digest[32],h[64];
        if(!source_hash(path,digest)||!digest_matches(digest,expected))goto invalid;
        FILE *f=fopen(path,"rb");uint64_t bytes;
        if(!f)goto invalid;
        if(length_of(f,&bytes)||fread(h,1,64,f)!=64){fclose(f);goto invalid;}
        uint64_t frames=le64(h+24);
        if((seen&&frames!=s->frames)||number(&d,field(&d,t,"bytes"))!=(double)bytes||pcm_header(f,0,bytes,frames,s->pcm+r,0)){fclose(f);goto invalid;}
        s->pcm[r].file=f;s->frames=frames;seen|=1u<<r;
    }
    if(seen!=5&&seen!=13)goto invalid;
    s->format=XZ_FOREIGN_RX3;s->rate=44100;s->count=seen==13?4:3;rc=1;goto done;
invalid: snprintf(error,cap,"RX3 STEMS: LEGACY SET CHECK FAILED");
done: document_close(&d);return rc;
}
static int open_rx3(struct xz_foreign_stems *s,const char *root,const char *source,char *error,size_t cap) {
    char base[4096],path[4096],selected[4096]="";
    if(joined(base,root,"RX3_STEMS")||!directory(base))return 0;
    DIR *dir=opendir(base);if(!dir)return -1;
    unsigned char source_digest[32];if(!source_hash(source,source_digest)){closedir(dir);return -1;}
    struct dirent *entry;unsigned scanned=0;int found=0;
    while((entry=readdir(dir))){
        size_t n=strlen(entry->d_name);if(n<8||strcmp(entry->d_name+n-8,".rx3stem"))continue;
        if(++scanned>16384){found=-1;break;}
        if(joined(path,base,entry->d_name)||!regular(path))continue;
        int match=rx3_package(path,source_digest,NULL,0);
        if(match!=1)continue;
        if(found){snprintf(error,cap,"RX3 STEMS: MULTIPLE MATCHING SETS");found=-1;break;}
        strcpy(selected,path);found=1;
    }
    closedir(dir);
    if(found==1&&rx3_package(selected,source_digest,s,1)!=1){snprintf(error,cap,"RX3 STEMS: PACKAGE CHECK FAILED");return -1;}
    if(!found)return rx3_legacy(s,base,source_digest,error,cap);
    return found;
}

struct xz_foreign_stems *xz_foreign_open(const char *source,char *error,size_t cap) {
    char root[4096];snprintf(error,cap,"NO VERIFIED FOUR-STEM SET");
    if(!source||strlen(source)>=sizeof(root)||!regular(source))return NULL;
    strcpy(root,source);for(char *p=root;*p;p++)if(*p=='\\')*p='/';
    char *contents=strstr(root,"/Contents/");if(!contents)return NULL;*contents=0;
    struct xz_foreign_stems *s=calloc(1,sizeof(*s));if(!s)return NULL;
    int found=open_az(s,root,source,error,cap);
    if(!found)found=open_rx3(s,root,source,error,cap);
    if(found!=1){xz_foreign_close(s);return NULL;}
    if(s->format==XZ_FOREIGN_AZ){error[0]=0;return s;}
    s->reference=xz_stem_media_open(source,s->format==XZ_FOREIGN_RX3);
    if(!s->reference){snprintf(error,cap,"FOUR STEMS: SOURCE CODEC NOT SUPPORTED");xz_foreign_close(s);return NULL;}
    double expected=(double)xz_stem_media_frames(s->reference)*s->rate/xz_stem_media_rate(s->reference);
    if(fabs(expected-(double)s->frames)>8192){snprintf(error,cap,"FOUR STEMS: SOURCE DURATION MISMATCH");xz_foreign_close(s);return NULL;}
    error[0]=0;return s;
}

int xz_foreign_reference(struct xz_foreign_stems *s,int64_t first,size_t count,float *out) {
    if(!s||!out||count>262144)return -1;
    if(s->format==XZ_FOREIGN_AZ)return xz_foreign_read(s,15,first,count,out);
    uint32_t rate=xz_stem_media_rate(s->reference);
    if(rate==s->rate)return xz_stem_media_read(s->reference,first,count,out);
    float raw[FOREIGN_BLOCK*6+8];
    for(size_t offset=0;offset<count;){
        size_t n=count-offset;if(n>FOREIGN_BLOCK)n=FOREIGN_BLOCK;
        double at=((double)first+(double)offset)*rate/s->rate;int64_t base=(int64_t)floor(at);
        size_t needed=(size_t)ceil((double)n*rate/s->rate)+2;
        if(needed>sizeof(raw)/sizeof(raw[0])/2||xz_stem_media_read(s->reference,base,needed,raw))return -1;
        for(size_t i=0;i<n;i++){
            double p=at+(double)i*rate/s->rate-base;size_t k=(size_t)p;float f=(float)(p-k);
            for(unsigned c=0;c<2;c++)out[(offset+i)*2+c]=raw[k*2+c]*(1-f)+raw[(k+1)*2+c]*f;
        }offset+=n;
    }return 0;
}
static int pcm_read(struct foreign_pcm *p,int64_t first,size_t count,float *out) {
    memset(out,0,count*2*sizeof(*out));
    if(first<0){uint64_t skip=(uint64_t)(-(first+1))+1;if(skip>=count)return 0;out+=(size_t)skip*2;count-=(size_t)skip;first=0;}
    if((uint64_t)first>=p->frames)return 0;
    if(count>p->frames-(uint64_t)first)count=(size_t)(p->frames-(uint64_t)first);
    unsigned stride=p->format==1?8:4;unsigned char bytes[FOREIGN_BLOCK*8];
    if(count>FOREIGN_BLOCK||stem_seek(p->file,(int64_t)(p->offset+(uint64_t)first*stride),SEEK_SET)||fread(bytes,stride,count,p->file)!=count)return -1;
    for(size_t i=0;i<count*2;i++){
        if(p->format==1){uint32_t bits=le32(bytes+i*4);memcpy(out+i,&bits,4);}
        else {int16_t v=(int16_t)((unsigned)bytes[i*2]|(unsigned)bytes[i*2+1]<<8);out[i]=(float)v/32768.0f*p->gain;}
        if(!isfinite(out[i]))return -1;
    }return 0;
}
int xz_foreign_read(struct xz_foreign_stems *s,unsigned mask,int64_t first,size_t count,float *out) {
    if(!s||!out||!mask||mask>=(1u<<s->count)||count>262144)return -1;
    memset(out,0,count*2*sizeof(*out));float block[FOREIGN_BLOCK*2];
    int residual=s->format==XZ_FOREIGN_RX3&&!!(mask&2);
    if(residual&&xz_foreign_reference(s,first,count,out))return -1;
    for(unsigned r=0;r<s->count;r++){
        if(s->format==XZ_FOREIGN_RX3&&r==1)continue;
        int coefficient=!!(mask&(1u<<r))-residual;if(!coefficient)continue;
        for(size_t offset=0;offset<count;){
            size_t n=count-offset;if(n>FOREIGN_BLOCK)n=FOREIGN_BLOCK;
            int rc=s->format==XZ_FOREIGN_AZ?xz_stem_media_read(s->role[r],first+(int64_t)offset,n,block):pcm_read(s->pcm+r,first+(int64_t)offset,n,block);
            if(rc)return -1;
            for(size_t i=0;i<n*2;i++)out[offset*2+i]+=coefficient*block[i];offset+=n;
        }
    }return 0;
}
