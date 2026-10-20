/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_profile_write.h"
#include "dj_profile_internal.h"
#include "dj_json.h"
#include "ui/pixel_font.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "audio/vendor/sha256/sha256.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 8192
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "vendor/stb/stb_image.h"
#include "vendor/stb/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
/* STB_IMAGE_STATIC declares functions this file never calls; that is reported at the end of the file. */
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define MAX_JSON XZ_DJP_MAX_JSON
#define BUF_BYTES (XZ_DJ_LOGO_MAX_BYTES + MAX_JSON + 2)
#define MAX_TOTAL_LOGO_BYTES (24u*1024u*1024u)
#define TILE_NAME "tile-160.rgb565"
typedef struct xz_dj_jv jv;
typedef struct xz_djp_str str;

/* --- small helpers --- */
static str S(const char *s){str v={s,strlen(s)};return v;}
static int bounded(const char *s,size_t cap){return memchr(s,0,cap)!=NULL;}
static void hex(const unsigned char *b,size_t n,char *out){static const char d[]="0123456789abcdef";for(size_t i=0;i<n;i++){out[i*2]=d[b[i]>>4];out[i*2+1]=d[b[i]&15];}out[n*2]=0;}
static void sha_hex(const void *data,size_t n,char out[65]){SHA256_CTX c;unsigned char sum[32];sha256_init(&c);sha256_update(&c,data,n);sha256_final(&c,sum);hex(sum,32,out);}
static int join(char out[4096],const char *root,const char *relative){
    size_t a=strlen(root),b=strlen(relative);if(a+1+b>=4096)return -1;
    memcpy(out,root,a);out[a]='/';memcpy(out+a+1,relative,b+1);return 0;
}
/* out = a + "/" + b; -1 when it does not fit. */
static int cat(char *out,size_t cap,const char *a,const char *b){
    size_t x=strlen(a),y=strlen(b);if(x+1+y>=cap)return -1;
    memmove(out,a,x);out[x]='/';memcpy(out+x+1,b,y+1);return 0;
}
static int write_all(int fd,const void *data,size_t n){
    const char *p=data;
    while(n){ssize_t w=write(fd,p,n);if(w<0){if(errno==EINTR)continue;return -1;}p+=w;n-=(size_t)w;}
    return 0;
}
/* FAT and some USB stacks refuse fsync on a directory; the rename has happened either way. */
static void fsync_dir(const char *path){int fd=open(path,O_RDONLY);if(fd>=0){(void)fsync(fd);close(fd);}}
/* /dev/urandom, else SHA-256 of whatever varies (time, pid, a counter, stack and heap addresses). */
static void entropy(unsigned char *out,size_t n){
    size_t got=0;int fd=open("/dev/urandom",O_RDONLY);
    if(fd>=0){
        while(got<n){ssize_t r=read(fd,out+got,n-got);if(r<0&&errno==EINTR)continue;if(r<=0)break;got+=(size_t)r;}
        close(fd);
    }
    static unsigned counter;
    while(got<n){
        char seed[160];unsigned char sum[32];void *heap=malloc(1);
        int k=snprintf(seed,sizeof(seed),"%ld-%ld-%u-%ld-%p-%p",(long)time(NULL),(long)getpid(),++counter,(long)clock(),(void *)seed,heap);
        free(heap);
        SHA256_CTX c;sha256_init(&c);sha256_update(&c,(const unsigned char *)seed,k>0?(size_t)k:0);sha256_final(&c,sum);
        for(size_t i=0;i<32&&got<n;i++)out[got++]=sum[i];
    }
}
static void random_hex(char *out,size_t bytes){unsigned char b[32];entropy(b,bytes);hex(b,bytes,out);}
static void uuid4(char out[37]){
    unsigned char b[16];char h[33];entropy(b,16);b[6]=(unsigned char)((b[6]&0x0f)|0x40);b[8]=(unsigned char)((b[8]&0x3f)|0x80);hex(b,16,h);
    snprintf(out,37,"%.8s-%.4s-%.4s-%.4s-%.12s",h,h+8,h+12,h+16,h+20);
}
/* "2026-10-19T15-02-11Z-3f1c": UTC and 4 random hex digits, as the spec's examples. */
static void new_revision(char out[64]){
    time_t now=time(NULL);struct tm tm;char tail[5];
    if(!gmtime_r(&now,&tm))memset(&tm,0,sizeof(tm));
    random_hex(tail,2);
    snprintf(out,64,"%04d-%02d-%02dT%02d-%02d-%02dZ-%s",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,tail);
}
static int ends_with_fold(const char *s,const char *tail){size_t a=strlen(s),b=strlen(tail);return a>=b&&xz_dj_fold_equal(s+a-b,tail);}

/* --- files --- */
static int write_new(const char *path,const void *data,size_t n){
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
    if(fd<0)return -1;
    if(write_all(fd,data,n)||fsync(fd)){close(fd);unlink(path);return -1;}
    if(close(fd)){unlink(path);return -1;}
    return 0;
}
/* Section 7 step 3: a dot-named temporary file in the same folder, flushed, renamed over dir/name, the folder flushed. */
static int publish(const char *root,const char *dir,const char *name,const void *data,size_t n){
    char path[4096],temporary[4096],folder[4096],tail[17],dot[32];
    random_hex(tail,8);snprintf(dot,sizeof(dot),".djprofile-%s",tail);
    if(join(folder,root,dir)||cat(path,sizeof(path),folder,name)||cat(temporary,sizeof(temporary),folder,dot))return -1;
    if(write_new(temporary,data,n))return -1;
    if(rename(temporary,path)){unlink(temporary);return -1;}
    fsync_dir(folder);
    return 0;
}
static int rel_path(char out[4096],const char *root,const char *dir,const char *name){
    char relative[2048];return cat(relative,sizeof(relative),dir,name)||join(out,root,relative);
}
static int rel_read(const char *root,const char *dir,const char *name,size_t max,unsigned char *buf,size_t *n){
    char relative[2048];
    if(cat(relative,sizeof(relative),dir,name))return XZ_DJP_READ_INVALID;
    return xz_djp_confined_read(root,relative,max,buf,n);
}
/* Any entry of dir (stick-relative) whose name equals name under ASCII folding. */
static int name_taken(const char *root,const char *dir,const char *name){
    char path[4096];DIR *d;struct dirent *e;int taken=0;
    if(join(path,root,dir)||!(d=opendir(path)))return 0;
    while(!taken&&(e=readdir(d)))taken=xz_dj_fold_equal(e->d_name,name);
    closedir(d);return taken;
}
/* The file dir/name holds exactly these bytes. */
static int holds(const char *root,const char *dir,const char *name,const unsigned char *data,size_t n,unsigned char *buf){
    size_t got;return rel_read(root,dir,name,XZ_DJ_LOGO_MAX_BYTES,buf,&got)==XZ_DJP_READ_OK&&got==n&&!memcmp(buf,data,n);
}
/* What a write did, so a profile that cannot be finished is taken back. */
struct undo { int n; struct { char path[4096], from[4096]; int dir; } item[24]; };
static void undo_add(struct undo *u,const char *path,const char *from,int dir){
    if(u->n>=24)return;
    snprintf(u->item[u->n].path,4096,"%s",path);snprintf(u->item[u->n].from,4096,"%s",from?from:"");u->item[u->n].dir=dir;u->n++;
}
static void undo_all(struct undo *u){
    for(int i=u->n-1;i>=0;i--){
        if(u->item[i].from[0])(void)rename(u->item[i].path,u->item[i].from);
        else if(u->item[i].dir)(void)rmdir(u->item[i].path);
        else (void)unlink(u->item[i].path);
    }
    u->n=0;
}
/* Puts bytes at dir/name. An identical file is kept; a missing one is written; an existing different
   one is replaced in place only when replace is set. */
static int put_file(const char *root,const char *dir,const char *name,const unsigned char *data,size_t n,int replace,struct undo *u,unsigned char *buf){
    char path[4096];struct stat st;
    if(rel_path(path,root,dir,name))return -1;
    if(!lstat(path,&st)){
        if(S_ISREG(st.st_mode)&&holds(root,dir,name,data,n,buf))return 0;
        if(!replace||!S_ISREG(st.st_mode))return -1;
        return publish(root,dir,name,data,n);
    }
    if(errno!=ENOENT||write_new(path,data,n))return -1;
    if(u)undo_add(u,path,NULL,0);
    return 0;
}
static int is_reserved(const char *name){
    static const char *const reserved[]={"logo.png","logo.jpg","logo.jpeg","profile.json","profile.previous.json","DJPROFILE.json"};
    for(size_t i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++)if(xz_dj_fold_equal(name,reserved[i]))return 1;
    return name[0]=='.';
}
/* A file name in dir for these bytes: want when it is free or already holds them, else
   want-<8 hex of their SHA-256>, then -2, -3. used lists names this write already took. */
static int asset_name(const char *root,const char *dir,const char *want,const unsigned char *data,size_t n,char used[][XZ_DJ_NAME_MAX],int nused,char out[XZ_DJ_NAME_MAX],unsigned char *buf){
    char stem[192],ext[16]="",digest[65];const char *dot=strrchr(want,'.');
    sha_hex(data,n,digest);
    size_t stem_n=dot&&dot!=want&&strlen(dot)<sizeof(ext)?(size_t)(dot-want):strlen(want);
    if(stem_n>180)stem_n=180;
    memcpy(stem,want,stem_n);stem[stem_n]=0;
    if(dot&&dot!=want&&strlen(dot)<sizeof(ext))snprintf(ext,sizeof(ext),"%s",dot);
    for(int k=0;k<50;k++){
        char candidate[XZ_DJ_NAME_MAX];int clash=0;
        if(!k)snprintf(candidate,sizeof(candidate),"%s%s",stem,ext);
        else if(k==1)snprintf(candidate,sizeof(candidate),"%s-%.8s%s",stem,digest,ext);
        else snprintf(candidate,sizeof(candidate),"%s-%.8s-%d%s",stem,digest,k,ext);
        if(is_reserved(candidate))continue;
        for(int i=0;i<nused;i++)if(xz_dj_fold_equal(used[i],candidate))clash=1;
        if(clash)continue;
        if(!name_taken(root,dir,candidate)||holds(root,dir,candidate,data,n,buf)){memcpy(out,candidate,strlen(candidate)+1);return 0;}
    }
    return -1;
}

/* --- text --- */
static size_t cp_len(unsigned char c){return c<0x80?1:c>=0xf0?4:c>=0xe0?3:2;}
/* The longest prefix of at most max bytes that ends on a character boundary. */
static size_t fit_bytes(const char *s,size_t n,size_t max){
    if(n<=max)return n;
    size_t at=0;
    while(at<n&&at+cp_len((unsigned char)s[at])<=max)at+=cp_len((unsigned char)s[at]);
    return at;
}
static uint32_t cp_at(const char *s,size_t *i){
    const unsigned char *u=(const unsigned char *)s;size_t k=cp_len(u[*i]);uint32_t v=k==1?u[*i]:u[*i]&(k==4?7u:k==3?15u:31u);
    for(size_t j=1;j<k;j++)v=v<<6|(u[*i+j]&63);
    *i+=k;return v;
}
/* Unicode whitespace or U+FEFF: a name blank to Python's strip() or JavaScript's trim() is refused. */
static int blank(const char *s){
    for(size_t i=0,n=strlen(s);i<n;){
        uint32_t c=cp_at(s,&i);
        if(!((c>=9&&c<=13)||(c>=0x1c&&c<=0x20)||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||
             c==0x202f||c==0x205f||c==0x3000||c==0xfeff))return 0;
    }return 1;
}
static int has_control(const char *s){for(size_t i=0,n=strlen(s);i<n;){uint32_t c=cp_at(s,&i);if(c<0x20||(c>=0x7f&&c<=0x9f))return 1;}return 0;}
/* Contract 0.3 step 1: the DJ name without FAT-invalid or control characters or leading dots, trailing
   spaces and dots trimmed, at most 100 UTF-16 units and room for a " 99" suffix inside 160 bytes. */
static void folder_name(const char *name,char out[160]){
    size_t n=0,units=0;
    for(size_t i=0,len=strlen(name);i<len;){
        size_t at=i;uint32_t c=cp_at(name,&i);size_t k=i-at;
        if(c<0x20||(c>=0x7f&&c<=0x9f)||(c<0x80&&strchr("<>:\"/\\|?*",(int)c))||(c=='.'&&!n))continue;
        if(units+(c>=0x10000?2:1)>96||n+k>156)break;
        memcpy(out+n,name+at,k);n+=k;units+=c>=0x10000?2:1;
    }
    while(n&&(out[n-1]==' '||out[n-1]=='.'))n--;
    out[n]=0;
    if(!n||blank(out))strcpy(out,"DJ");
}

/* --- JSON helpers --- */
static void put_text(struct xz_dj_json *j,jv *object,const char *key,const char *value){xz_dj_json_put(j,object,key,xz_dj_json_text(j,value));}
static void put_bytes(struct xz_dj_json *j,jv *object,const char *key,const char *value,size_t n){xz_dj_json_put(j,object,key,xz_dj_json_string(j,value,n));}
static void put_number(struct xz_dj_json *j,jv *object,const char *key,int64_t value){xz_dj_json_put(j,object,key,xz_dj_json_number(j,value));}
static jv *put_array(struct xz_dj_json *j,jv *object,const char *key){
    jv *a=xz_dj_json_get(object,key);
    if(a&&a->type==XZ_DJ_JARR)return a;
    a=xz_dj_json_value(j,XZ_DJ_JARR);xz_dj_json_put(j,object,key,a);return a;
}
static const char *text_of(const jv *v){return v&&v->type==XZ_DJ_JSTR?v->s:"";}
/* Section 5: numbers that are not whole within 2^53 are written as strings. */
static void whole_numbers(jv *v){
    if(!v)return;
    int64_t whole;
    if(v->type==XZ_DJ_JNUM&&(xz_dj_json_whole(v,&whole)||whole>(INT64_C(1)<<53)||whole<-(INT64_C(1)<<53)))v->type=XZ_DJ_JSTR;
    for(jv *c=v->child;c;c=c->next)whole_numbers(c);
}
/* The files a document lists (logos, renditions, the track copy). */
static int listed(const jv *d,const char *names[24]){
    int n=0;const jv *v;
    static const char *const arrays[]={"logos","renditions"};
    for(int k=0;k<2;k++)if((v=xz_dj_json_get(d,arrays[k]))&&v->type==XZ_DJ_JARR)
        for(const jv *e=v->child;e&&n<24;e=e->next)if(xz_dj_json_get(e,"file"))names[n++]=text_of(xz_dj_json_get(e,"file"));
    if((v=xz_dj_json_get(d,"track"))&&xz_dj_json_get(v,"file")&&n<24)names[n++]=text_of(xz_dj_json_get(v,"file"));
    return n;
}

/* --- images --- */
static enum xz_dj_image_result read_image(const char *path,unsigned char **data,size_t *n){
    struct stat st;*data=NULL;*n=0;
    if(!path||lstat(path,&st)||!S_ISREG(st.st_mode))return XZ_DJ_IMAGE_UNREADABLE;
    if((uint64_t)st.st_size>XZ_DJ_LOGO_MAX_BYTES)return XZ_DJ_IMAGE_FILE_TOO_BIG;
    int fd=open(path,O_RDONLY|O_NOFOLLOW);
    if(fd<0)return XZ_DJ_IMAGE_UNREADABLE;
    unsigned char *buf=malloc(XZ_DJ_LOGO_MAX_BYTES+1);size_t total=0;
    if(!buf){close(fd);return XZ_DJ_IMAGE_NO_MEMORY;}
    for(;;){
        ssize_t r=read(fd,buf+total,XZ_DJ_LOGO_MAX_BYTES+1-total);
        if(r<0){if(errno==EINTR)continue;close(fd);free(buf);return XZ_DJ_IMAGE_UNREADABLE;}
        if(!r)break;
        total+=(size_t)r;
        if(total>XZ_DJ_LOGO_MAX_BYTES){close(fd);free(buf);return XZ_DJ_IMAGE_FILE_TOO_BIG;}
    }
    close(fd);
    if(!total){free(buf);return XZ_DJ_IMAGE_NOT_IMAGE;}
    *data=buf;*n=total;return XZ_DJ_IMAGE_OK;
}
/* Resampling taps for one axis. Shrinking: exact area coverage in units of 1/dst, weights sum to src.
   Enlarging: bilinear between pixel centres, weights sum to 2*dst. */
struct taps { int *start,*count; uint32_t *weight,total; int per; };
static int taps_make(struct taps *t,int src,int dst){
    t->per=src>=dst?src/dst+2:2;
    t->start=malloc(sizeof(int)*(size_t)dst);t->count=malloc(sizeof(int)*(size_t)dst);t->weight=calloc((size_t)dst*(size_t)t->per,sizeof(uint32_t));
    if(!t->start||!t->count||!t->weight)return -1;
    for(int o=0;o<dst;o++){
        uint32_t *w=t->weight+(size_t)o*(size_t)t->per;
        if(src>=dst){
            uint64_t from=(uint64_t)o*(uint64_t)src,to=(uint64_t)(o+1)*(uint64_t)src;
            int first=(int)(from/(uint64_t)dst),last=(int)((to-1)/(uint64_t)dst);
            t->start[o]=first;t->count[o]=last-first+1;
            for(int j=first;j<=last;j++){
                uint64_t a=(uint64_t)j*(uint64_t)dst,b=a+(uint64_t)dst;
                w[j-first]=(uint32_t)((to<b?to:b)-(from>a?from:a));
            }
            t->total=(uint32_t)src;
        }else{
            int64_t num=(int64_t)(2*o+1)*src-dst;if(num<0)num=0;
            int j0=(int)(num/(2*dst));uint32_t frac=(uint32_t)(num%(2*dst));
            t->start[o]=j0;
            if(j0+1<src){t->count[o]=2;w[0]=(uint32_t)(2*dst)-frac;w[1]=frac;}
            else{t->count[o]=1;w[0]=(uint32_t)(2*dst);}
            t->total=(uint32_t)(2*dst);
        }
    }
    return 0;
}
static void taps_free(struct taps *t){free(t->start);free(t->count);free(t->weight);}
/* render_xz_tile's fitted size: ((along*160 + across/2) / across) clamped to 1..160. */
static int fit(int along,int across){
    uint64_t v=((uint64_t)along*XZ_DJ_TILE_SIDE+(uint64_t)across/2)/(uint64_t)across;
    return v<1?1:v>XZ_DJ_TILE_SIDE?XZ_DJ_TILE_SIDE:(int)v;
}
static enum xz_dj_image_result render_tile(const unsigned char *px,int width,int height,int channels,uint16_t *tile){
    const int side=XZ_DJ_TILE_SIDE;
    int fw=width>=height?side:fit(width,height),fh=width>=height?fit(height,width):side;
    struct taps tx={0},ty={0};enum xz_dj_image_result rc=XZ_DJ_IMAGE_NO_MEMORY;
    uint32_t *mid=malloc(sizeof(uint32_t)*3*(size_t)fw*(size_t)height);
    if(!mid||taps_make(&tx,width,fw)||taps_make(&ty,height,fh))goto done;
    for(int y=0;y<height;y++){
        const unsigned char *row=px+(size_t)y*(size_t)width*(size_t)channels;
        for(int x=0;x<fw;x++){
            uint32_t acc[3]={0,0,0};const uint32_t *wt=tx.weight+(size_t)x*(size_t)tx.per;
            for(int k=0;k<tx.count[x];k++){
                const unsigned char *s=row+(size_t)(tx.start[x]+k)*(size_t)channels;
                unsigned r,g,b,a;
                if(channels<3){r=g=b=s[0];a=channels==2?s[1]:255;}else{r=s[0];g=s[1];b=s[2];a=channels==4?s[3]:255;}
                /* render_xz_tile: blend over black before scaling, rounded like Rust. */
                r=(r*a+127)/255;g=(g*a+127)/255;b=(b*a+127)/255;
                acc[0]+=r*wt[k];acc[1]+=g*wt[k];acc[2]+=b*wt[k];
            }
            uint32_t *m=mid+((size_t)y*(size_t)fw+(size_t)x)*3;m[0]=acc[0];m[1]=acc[1];m[2]=acc[2];
        }
    }
    for(int i=0;i<side*side;i++)tile[i]=0;
    uint64_t total=(uint64_t)tx.total*ty.total;
    int ox=(side-fw)/2,oy=(side-fh)/2;
    for(int y=0;y<fh;y++){
        const uint32_t *wt=ty.weight+(size_t)y*(size_t)ty.per;
        for(int x=0;x<fw;x++){
            uint64_t acc[3]={0,0,0};
            for(int k=0;k<ty.count[y];k++){
                const uint32_t *m=mid+((size_t)(ty.start[y]+k)*(size_t)fw+(size_t)x)*3;
                acc[0]+=(uint64_t)m[0]*wt[k];acc[1]+=(uint64_t)m[1]*wt[k];acc[2]+=(uint64_t)m[2]*wt[k];
            }
            unsigned r=(unsigned)((acc[0]+total/2)/total),g=(unsigned)((acc[1]+total/2)/total),b=(unsigned)((acc[2]+total/2)/total);
            if(r>255)r=255;
            if(g>255)g=255;
            if(b>255)b=255;
            tile[(oy+y)*side+ox+x]=(uint16_t)((r>>3)<<11|(g>>2)<<5|b>>3);
        }
    }
    rc=XZ_DJ_IMAGE_OK;
done:
    free(mid);taps_free(&tx);taps_free(&ty);return rc;
}
static enum xz_dj_image_result tile_from_memory(const unsigned char *data,size_t n,uint16_t *tile,int *width,int *height){
    int w,h,c;
    if(n>INT_MAX||!stbi_info_from_memory(data,(int)n,&w,&h,&c)||w<=0||h<=0||c<1||c>4)return XZ_DJ_IMAGE_NOT_IMAGE;
    if(width)*width=w;
    if(height)*height=h;
    if((uint64_t)w*(uint64_t)h>XZ_DJ_IMAGE_MAX_PIXELS)return XZ_DJ_IMAGE_TOO_MANY_PIXELS;
    unsigned char *px=stbi_load_from_memory(data,(int)n,&w,&h,&c,0);
    if(!px){
        void *probe=malloc((size_t)w*(size_t)h*4u);
        free(probe);return probe?XZ_DJ_IMAGE_NOT_IMAGE:XZ_DJ_IMAGE_NO_MEMORY;
    }
    enum xz_dj_image_result rc=c>=1&&c<=4?render_tile(px,w,h,c,tile):XZ_DJ_IMAGE_NOT_IMAGE;
    stbi_image_free(px);return rc;
}
enum xz_dj_image_result xz_dj_image_tile(const char *path,uint16_t tile[XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE],int *width,int *height){
    unsigned char *data;size_t n;
    if(width)*width=0;
    if(height)*height=0;
    enum xz_dj_image_result rc=read_image(path,&data,&n);
    if(rc==XZ_DJ_IMAGE_OK)rc=tile_from_memory(data,n,tile,width,height);
    free(data);return rc;
}

/* --- image list --- */
static int ci_compare(const char *a,const char *b){
    for(;;a++,b++){
        int x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='A'&&x<='Z')x+=32;
        if(y>='A'&&y<='Z')y+=32;
        if(x!=y||!x)return x-y;
    }
}
static int sort_paths(const void *a,const void *b){int c=ci_compare(a,b);return c?c:strcmp(a,b);}
static int skipped_name(const char *name){
    static const char *const skip[]={"PIONEER","DJPROFILE","DJPROFILES","VJ.Tools","CDJMODS","System Volume Information","$RECYCLE.BIN"};
    if(name[0]=='.')return 1;
    for(size_t i=0;i<sizeof(skip)/sizeof(skip[0]);i++)if(!ci_compare(name,skip[i]))return 1;
    return 0;
}
static int image_name(const char *name){
    const char *dot=strrchr(name,'.');
    return dot&&(!ci_compare(dot,".png")||!ci_compare(dot,".jpg")||!ci_compare(dot,".jpeg"));
}
struct found { char (*item)[XZ_DJ_IMAGE_PATH]; int n,cap; };
#define LIST_LIMIT 4096
static int walk(const char *root,const char *relative,int depth,struct found *f){
    char path[4096];
    if(snprintf(path,sizeof(path),"%s%s%s",root,*relative?"/":"",relative)>=(int)sizeof(path))return -1;
    DIR *d=opendir(path);
    if(!d)return -1;
    struct dirent *e;
    while((e=readdir(d))&&f->n<LIST_LIMIT){
        char child[XZ_DJ_IMAGE_PATH],full[4096];struct stat st;
        if(skipped_name(e->d_name))continue;
        if(snprintf(child,sizeof(child),"%s%s%s",relative,*relative?"/":"",e->d_name)>=(int)sizeof(child))continue;
        if(snprintf(full,sizeof(full),"%s/%s",root,child)>=(int)sizeof(full)||lstat(full,&st)||S_ISLNK(st.st_mode))continue;
        if(S_ISDIR(st.st_mode)){if(depth<3)(void)walk(root,child,depth+1,f);continue;}
        if(!S_ISREG(st.st_mode)||!image_name(e->d_name)||st.st_size<8||(uint64_t)st.st_size>XZ_DJ_LOGO_MAX_BYTES)continue;
        if(f->n==f->cap){
            int cap=f->cap?f->cap*2:64;void *grown=realloc(f->item,(size_t)cap*sizeof(*f->item));
            if(!grown)break;
            f->item=grown;f->cap=cap;
        }
        memcpy(f->item[f->n++],child,strlen(child)+1);
    }
    closedir(d);return 0;
}
/* Images directly inside each DJ folder: DJPROFILE/<name>/ and DJPROFILES/<name>/, not VJTOOLS. */
static void dj_folders(const char *root,struct found *f){
    DIR *d=opendir(root);struct dirent *e;char tops[2][256]={"",""};
    if(!d)return;
    while((e=readdir(d))){
        int k=!ci_compare(e->d_name,"DJPROFILE")?0:!ci_compare(e->d_name,"DJPROFILES")?1:-1;
        if(k>=0&&!tops[k][0]&&strlen(e->d_name)<sizeof(tops[k]))memcpy(tops[k],e->d_name,strlen(e->d_name)+1);
    }
    closedir(d);
    for(int k=0;k<2;k++){
        char path[4096];struct stat st;
        if(!tops[k][0]||join(path,root,tops[k])||lstat(path,&st)||!S_ISDIR(st.st_mode)||!(d=opendir(path)))continue;
        while((e=readdir(d))){
            char child[XZ_DJ_IMAGE_PATH],full[4096];
            if(e->d_name[0]=='.'||!ci_compare(e->d_name,"VJTOOLS"))continue;
            if(snprintf(child,sizeof(child),"%s/%s",tops[k],e->d_name)>=(int)sizeof(child))continue;
            if(join(full,root,child)||lstat(full,&st)||!S_ISDIR(st.st_mode))continue;
            /* Depth 0 below the DJ folder: only its own files. */
            DIR *dj=opendir(full);struct dirent *g;
            if(!dj)continue;
            while((g=readdir(dj))&&f->n<LIST_LIMIT){
                char item[XZ_DJ_IMAGE_PATH],file[4096];
                if(g->d_name[0]=='.'||!image_name(g->d_name))continue;
                if(snprintf(item,sizeof(item),"%s/%s",child,g->d_name)>=(int)sizeof(item))continue;
                if(join(file,root,item)||lstat(file,&st)||!S_ISREG(st.st_mode)||st.st_size<8||(uint64_t)st.st_size>XZ_DJ_LOGO_MAX_BYTES)continue;
                if(f->n==f->cap){
                    int cap=f->cap?f->cap*2:64;void *grown=realloc(f->item,(size_t)cap*sizeof(*f->item));
                    if(!grown)break;
                    f->item=grown;f->cap=cap;
                }
                memcpy(f->item[f->n++],item,strlen(item)+1);
            }
            closedir(dj);
        }
        closedir(d);
    }
}
int xz_dj_image_list(const char *root,char out[][XZ_DJ_IMAGE_PATH],int max){
    struct found f={0};
    if(!root||!*root)return -1;
    dj_folders(root,&f);
    int folder=f.n;
    if(walk(root,"",0,&f)){free(f.item);return -1;}
    if(folder)qsort(f.item,(size_t)folder,sizeof(*f.item),sort_paths);
    if(f.n>folder)qsort(f.item+folder,(size_t)(f.n-folder),sizeof(*f.item),sort_paths);
    int n=max<0?0:f.n<max?f.n:max;
    for(int i=0;i<n;i++)memcpy(out[i],f.item[i],XZ_DJ_IMAGE_PATH);
    free(f.item);return n;
}


/* --- logos within section 5.2 --- */
struct png_out { unsigned char *b; size_t n, cap; int err; };
static void png_sink(void *context,void *data,int size){
    struct png_out *o=context;
    if(o->err||size<=0)return;
    if(o->n+(size_t)size>o->cap){
        size_t cap=o->cap?o->cap*2:65536;while(o->n+(size_t)size>cap)cap*=2;
        unsigned char *b=cap<=(size_t)XZ_DJ_LOGO_MAX_BYTES*2?realloc(o->b,cap):NULL;
        if(!b){o->err=1;return;}
        o->b=b;o->cap=cap;
    }
    memcpy(o->b+o->n,data,(size_t)size);o->n+=(size_t)size;
}
static unsigned char *encode_png(const unsigned char *px,int w,int h,int channels,size_t *n){
    struct png_out o={0};
    if(!stbi_write_png_to_func(png_sink,&o,w,h,channels,px,w*channels)||o.err){free(o.b);return NULL;}
    *n=o.n;return o.b;
}
/* Exact area average into tw x th, alpha weighted so transparent pixels do not darken edges.
   Gray becomes RGB. Streams one output row at a time. */
static unsigned char *shrink(const unsigned char *px,int w,int h,int c,int tw,int th,int *out_channels){
    int alpha=c==2||c==4,oc=alpha?4:3;
    struct taps tx={0},ty={0};unsigned char *out=malloc((size_t)tw*(size_t)th*(size_t)oc);
    uint64_t *acc=calloc((size_t)tw*4,sizeof(uint64_t));uint64_t *row=malloc((size_t)tw*4*sizeof(uint64_t));
    if(!out||!acc||!row||taps_make(&tx,w,tw)||taps_make(&ty,h,th)){free(out);out=NULL;goto done;}
    uint64_t total=(uint64_t)tx.total*ty.total;
    for(int oy=0;oy<th;oy++){
        memset(acc,0,(size_t)tw*4*sizeof(uint64_t));
        for(int k=0;k<ty.count[oy];k++){
            const unsigned char *src=px+(size_t)(ty.start[oy]+k)*(size_t)w*(size_t)c;uint64_t wy=ty.weight[(size_t)oy*(size_t)ty.per+(size_t)k];
            for(int ox=0;ox<tw;ox++){
                uint64_t sum[4]={0,0,0,0};const uint32_t *wt=tx.weight+(size_t)ox*(size_t)tx.per;
                for(int t=0;t<tx.count[ox];t++){
                    const unsigned char *s=src+(size_t)(tx.start[ox]+t)*(size_t)c;
                    unsigned r=c<3?s[0]:s[0],g=c<3?s[0]:s[1],b=c<3?s[0]:s[2],a=c==2?s[1]:c==4?s[3]:255;
                    sum[0]+=(uint64_t)r*a*wt[t];sum[1]+=(uint64_t)g*a*wt[t];sum[2]+=(uint64_t)b*a*wt[t];sum[3]+=(uint64_t)a*wt[t];
                }
                for(int ch=0;ch<4;ch++)row[ox*4+ch]=sum[ch];
            }
            for(int i=0;i<tw*4;i++)acc[i]+=row[i]*wy;
        }
        for(int ox=0;ox<tw;ox++){
            unsigned char *o=out+((size_t)oy*(size_t)tw+(size_t)ox)*(size_t)oc;uint64_t a=acc[ox*4+3];
            for(int ch=0;ch<3;ch++){uint64_t v=a?(acc[ox*4+ch]+a/2)/a:0;o[ch]=(unsigned char)(v>255?255:v);}
            if(alpha){uint64_t v=(a+total/2)/total;o[3]=(unsigned char)(v>255?255:v);}
        }
    }
done:
    taps_free(&tx);taps_free(&ty);free(acc);free(row);
    *out_channels=oc;return out;
}
/* A logo ready for a profile folder: data (malloc'd), its format and the xdj-xz tile. */
struct logo { unsigned char *data; size_t n; int png, has_tile; uint16_t tile[XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE]; };
static void logo_free(struct logo *l){free(l->data);l->data=NULL;l->n=0;}
/* Takes data (malloc'd). A usable logo is kept as it is; a larger PNG or JPEG is scaled into 4096 px a
   side and 4 MP (and smaller while the PNG passes 8 MiB) and written as PNG. */
static enum xz_dj_image_result logo_prepare(unsigned char *data,size_t n,struct logo *o){
    struct xz_dj_image_info info;int w,h,c;
    memset(o,0,sizeof(*o));
    if(!xz_dj_image_usable(data,n,&info)){
        /* The header alone is not enough to write it: it must decode. */
        enum xz_dj_image_result rc=tile_from_memory(data,n,o->tile,NULL,NULL);
        if(rc!=XZ_DJ_IMAGE_OK){free(data);return rc;}
        o->data=data;o->n=n;o->png=info.kind==XZ_DJ_IMAGE_PNG;o->has_tile=1;
        return XZ_DJ_IMAGE_OK;
    }
    enum xz_dj_image_result rc=XZ_DJ_IMAGE_NOT_IMAGE;
    if(n>INT_MAX||!stbi_info_from_memory(data,(int)n,&w,&h,&c)||w<=0||h<=0){free(data);return rc;}
    if((uint64_t)w*(uint64_t)h>XZ_DJ_IMAGE_MAX_PIXELS){free(data);return XZ_DJ_IMAGE_TOO_MANY_PIXELS;}
    unsigned char *px=stbi_load_from_memory(data,(int)n,&w,&h,&c,0);
    free(data);
    if(!px)return XZ_DJ_IMAGE_NOT_IMAGE;
    rc=XZ_DJ_IMAGE_NO_MEMORY;
    o->has_tile=render_tile(px,w,h,c,o->tile)==XZ_DJ_IMAGE_OK;
    double scale=1;
    if(w>(int)XZ_DJ_LOGO_MAX_SIDE)scale=(double)XZ_DJ_LOGO_MAX_SIDE/w;
    if(h*scale>XZ_DJ_LOGO_MAX_SIDE)scale=(double)XZ_DJ_LOGO_MAX_SIDE/h;
    for(int tries=0;tries<8&&rc!=XZ_DJ_IMAGE_OK;tries++,scale*=0.7){
        int tw=(int)(w*scale),th=(int)(h*scale),oc;
        while((uint64_t)tw*(uint64_t)th>XZ_DJ_LOGO_MAX_PIXELS){tw=tw*999/1000;th=th*999/1000;}
        if(tw<1)tw=1;
        if(th<1)th=1;
        unsigned char *small=shrink(px,w,h,c,tw,th,&oc);size_t bytes;
        unsigned char *encoded=small?encode_png(small,tw,th,oc,&bytes):NULL;
        free(small);
        if(!encoded)break;
        if(!xz_dj_image_usable(encoded,bytes,NULL)){o->data=encoded;o->n=bytes;o->png=1;rc=XZ_DJ_IMAGE_OK;}
        else free(encoded);
    }
    stbi_image_free(px);
    return rc;
}
/* The initial on the accent colour, as the MODS header draws a profile without a logo. */
static enum xz_dj_image_result logo_monogram(const char *name,uint32_t fill,struct logo *o){
    const int side=XZ_DJ_TILE_SIDE,scale=16,x0=(side-5*scale)/2,y0=(side-7*scale)/2;
    memset(o,0,sizeof(*o));
    unsigned char *px=malloc((size_t)side*side*3);
    if(!px)return XZ_DJ_IMAGE_NO_MEMORY;
    unsigned r=fill>>16&255,g=fill>>8&255,b=fill&255,luma=(r*3+g*6+b)/10,ink=luma>140?0x10:0xff;
    size_t i=0;uint32_t c=name[0]?cp_at(name,&i):'?';
    if(c>=0xc0&&c<=0xff)c=(unsigned char)xz_pixel_font_latin1[c-0xc0];
    if(c>='a'&&c<='z')c-=32;
    if(c<=' '||c>126)c='?';
    for(int y=0;y<side;y++)for(int x=0;x<side;x++){
        int gx=(x-x0)/scale,gy=(y-y0)/scale,on=x>=x0&&y>=y0&&gx<5&&gy<7&&(xz_pixel_font_row(c,(unsigned)gy)>>(4-gx)&1);
        unsigned char *p=px+((size_t)y*side+(size_t)x)*3;
        p[0]=on?(unsigned char)ink:(unsigned char)r;p[1]=on?(unsigned char)ink:(unsigned char)g;p[2]=on?(unsigned char)ink:(unsigned char)b;
    }
    o->has_tile=render_tile(px,side,side,3,o->tile)==XZ_DJ_IMAGE_OK;
    o->data=encode_png(px,side,side,3,&o->n);o->png=1;
    free(px);
    return o->data?XZ_DJ_IMAGE_OK:XZ_DJ_IMAGE_NO_MEMORY;
}
static void tile_bytes(const uint16_t *tile,unsigned char *out){
    for(int i=0;i<XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE;i++){out[i*2]=(unsigned char)(tile[i]&255);out[i*2+1]=(unsigned char)(tile[i]>>8);}
}

/* --- the writer --- */
struct ctx { const char *root; unsigned char *buf; struct xz_djp_scan s; int scanned; unsigned char *stick; size_t stick_n; int stick_rc; };
static int rescan(struct ctx *x){
    if(x->scanned)xz_djp_scan_free(&x->s);
    x->scanned=0;
    if(xz_djp_scan(x->root,&x->s,x->buf))return -1;
    x->scanned=1;return 0;
}
static int ctx_open(struct ctx *x,const char *root){
    memset(x,0,sizeof(*x));x->root=root;
    x->buf=malloc(BUF_BYTES);x->stick=malloc(XZ_DJP_MAX_STICK_JSON+1);
    return x->buf&&x->stick&&!rescan(x)?0:-1;
}
static void ctx_close(struct ctx *x){if(x->scanned)xz_djp_scan_free(&x->s);free(x->buf);free(x->stick);}
static int same_key(const char *a,const char *b){return !xz_dj_name_key_compare(a,b);}
/* DJPROFILE as the stick spells it, made when the stick has none (section 3). */
static int home(struct ctx *x,char out[XZ_DJ_NAME_MAX]){
    char path[4096];
    if(!x->s.tops[0][0]){
        if(join(path,x->root,"DJPROFILE")||(mkdir(path,0755)&&errno!=EEXIST))return -1;
        strcpy(x->s.tops[0],"DJPROFILE");
    }
    strcpy(out,x->s.tops[0]);return 0;
}
/* A folder name for name in top, unique by name key among the stick's profile folders (self excepted:
   its own name when the keys agree), never VJTOOLS. */
static int unique_folder(struct ctx *x,const char *top,const char *name,const struct xz_djp_folder *self,char out[XZ_DJ_NAME_MAX]){
    char stem[160];folder_name(name,stem);
    for(int k=1;k<100;k++){
        char candidate[XZ_DJ_NAME_MAX],key[XZ_DJ_KEY_MAX],path[4096];int clash=0;struct stat st;
        if(k==1)snprintf(candidate,sizeof(candidate),"%s",stem);else snprintf(candidate,sizeof(candidate),"%s %d",stem,k);
        if(xz_dj_valid_folder_name(candidate,strlen(candidate))||xz_dj_name_key(candidate,strlen(candidate),key,sizeof(key))<0||!strcmp(key,"vjtools"))continue;
        if(self&&!strcmp(self->c.key,key)){snprintf(out,XZ_DJ_NAME_MAX,"%s",self->c.name);return 0;}
        for(int i=0;i<x->s.folders;i++)if(&x->s.folder[i]!=self&&!strcmp(x->s.folder[i].c.key,key))clash=1;
        if(clash||rel_path(path,x->root,top,candidate)||!lstat(path,&st)||name_taken(x->root,top,candidate))continue;
        snprintf(out,XZ_DJ_NAME_MAX,"%s",candidate);return 0;
    }
    return -1;
}
/* The stick file to rewrite: the valid one the stick has (its bytes kept to notice a change), else a new one. */
static jv *stick_tree(struct ctx *x,struct xz_dj_json *j){
    x->stick_rc=XZ_DJP_READ_MISSING;x->stick_n=0;
    if(x->s.stick_file[0]){
        x->stick_rc=xz_djp_confined_read(x->root,x->s.stick_file,XZ_DJP_MAX_STICK_JSON,x->stick,&x->stick_n);
        jv *t;struct xz_dj_json *p=x->stick_rc==XZ_DJP_READ_OK?xz_dj_json_parse((const char *)x->stick,x->stick_n,XZ_DJ_JSON_PROFILE,&t):NULL;
        jv *copy=p?xz_dj_json_copy(j,t):NULL;
        xz_dj_json_free(p);
        if(copy)return copy;
    }
    jv *t=xz_dj_json_value(j,XZ_DJ_JOBJ);
    if(t)put_text(j,t,"format","djprofile-stick/1");
    return t;
}
static jv *new_entry(struct xz_dj_json *j,const char *folder,const char *id){
    jv *e=xz_dj_json_value(j,XZ_DJ_JOBJ);
    if(!e)return NULL;
    put_text(j,e,"folder",folder);
    if(id&&*id)put_text(j,e,"id",id);
    return e;
}
/* The entry of profiles that names this profile: its id, else its folder name. */
static jv *entry_of(jv *profiles,const char *id,const char *folder){
    for(jv *e=profiles?profiles->child:NULL;e;e=e->next){
        const jv *eid=xz_dj_json_get(e,"id"),*f=xz_dj_json_get(e,"folder");
        if(eid){if(id&&*id&&xz_dj_json_is(eid,id))return e;continue;}
        if(f&&f->type==XZ_DJ_JSTR&&folder&&same_key(f->s,folder))return e;
    }
    return NULL;
}
/* "profiles" as the stick shows them now: the existing array, or one entry per shown profile. */
static jv *profiles_of(struct ctx *x,struct xz_dj_json *j,jv *tree){
    jv *p=xz_dj_json_get(tree,"profiles");
    if(p&&p->type==XZ_DJ_JARR)return p;
    p=put_array(j,tree,"profiles");
    for(int i=0;i<x->s.shown&&p;i++){
        const struct xz_djp_folder *f=&x->s.folder[x->s.order[i]];
        xz_dj_json_push(p,new_entry(j,f->c.name,f->level==1?f->view.dj_id:NULL));
    }
    return p;
}
/* Section 7 step 4: DJPROFILE.json replaced last, with a new revision. */
static int write_stick(struct ctx *x,struct xz_dj_json *j,jv *tree){
    char top[XZ_DJ_NAME_MAX],name[XZ_DJ_NAME_MAX],rev[64],relative[1024];size_t n,now_n;
    if(home(x,top))return -1;
    new_revision(rev);put_text(j,tree,"revision",rev);
    if(xz_dj_fixed(x->root,top,XZ_DJP_STICK_FILE,name))strcpy(name,XZ_DJP_STICK_FILE);
    if(cat(relative,sizeof(relative),top,name))return -1;
    char *text=xz_dj_json_write(tree,XZ_DJ_JSON_PRETTY,&n);jv *check;
    struct xz_dj_json *p=text&&n<=XZ_DJP_MAX_STICK_JSON?xz_dj_json_parse(text,n,XZ_DJ_JSON_PROFILE,&check):NULL;
    int rc=-1;
    if(p&&!xz_dj_json_failed(j)){
        /* Someone else rewrote it since it was read: stop rather than lose their change. */
        int now=xz_djp_confined_read(x->root,relative,XZ_DJP_MAX_STICK_JSON,x->buf,&now_n);
        int same=!strcmp(relative,x->s.stick_file)?now==x->stick_rc&&(now!=XZ_DJP_READ_OK||(now_n==x->stick_n&&!memcmp(x->buf,x->stick,now_n))):1;
        if(!same)rc=-2;
        else if(!publish(x->root,top,name,text,n)&&xz_djp_confined_read(x->root,relative,XZ_DJP_MAX_STICK_JSON,x->buf,&now_n)==XZ_DJP_READ_OK&&now_n==n&&!memcmp(x->buf,text,n))rc=0;
    }
    xz_dj_json_free(p);free(text);return rc;
}
/* A file a document lists, read and checked against its bytes and sha256 (contract 0.3 step 2). malloc'd. */
static int read_verified(struct ctx *x,const jv *ref,const char *path_key,size_t max,unsigned char **data,size_t *n){
    const jv *path=xz_dj_json_get(ref,path_key),*sha=xz_dj_json_get(ref,"sha256");int64_t bytes;char digest[65];
    *data=NULL;
    if(!path||path->type!=XZ_DJ_JSTR||memchr(path->s,0,path->n)||xz_dj_json_whole(xz_dj_json_get(ref,"bytes"),&bytes)||!sha||sha->n!=64)return 1;
    int rc=xz_djp_confined_read(x->root,path->s,max,x->buf,n);
    if(rc==XZ_DJP_READ_IO)return -1;
    if(rc!=XZ_DJP_READ_OK||(int64_t)*n!=bytes)return 1;
    sha_hex(x->buf,*n,digest);
    for(int i=0;i<64;i++)if(digest[i]!=(sha->s[i]>='A'&&sha->s[i]<='F'?sha->s[i]+32:sha->s[i]))return 1;
    if(!(*data=malloc(*n?*n:1)))return -1;
    memcpy(*data,x->buf,*n);return 0;
}
static int is_known(const char *key,const char *const *known,size_t k){for(size_t i=0;i<k;i++)if(!strcmp(key,known[i]))return 1;return 0;}
/* Copies the members of from not in known into to (section 5.5). */
static void keep_unknown(struct xz_dj_json *j,jv *to,const jv *from,const char *const *known,size_t k){
    for(const jv *m=from?from->child:NULL;m;m=m->next)if(m->key&&!memchr(m->key,0,m->key_n)&&!is_known(m->key,known,k)&&!xz_dj_json_get(to,m->key)){
        jv *copy=xz_dj_json_copy(j,m);whole_numbers(copy);xz_dj_json_put(j,to,m->key,copy);
    }
}
static jv *logo_entry(struct xz_dj_json *j,const char *file,const unsigned char *data,size_t n,const char *role){
    jv *e=xz_dj_json_value(j,XZ_DJ_JOBJ);char digest[65];
    if(!e)return NULL;
    sha_hex(data,n,digest);
    put_text(j,e,"file",file);put_number(j,e,"bytes",(int64_t)n);put_text(j,e,"sha256",digest);put_text(j,e,"role",role);put_text(j,e,"background","any");
    return e;
}
static jv *tile_entry(struct xz_dj_json *j,const char *file,const unsigned char *bytes){
    jv *e=xz_dj_json_value(j,XZ_DJ_JOBJ);char digest[65];
    if(!e)return NULL;
    sha_hex(bytes,XZ_DJ_TILE_BYTES,digest);
    put_text(j,e,"file",file);put_number(j,e,"bytes",XZ_DJ_TILE_BYTES);put_text(j,e,"sha256",digest);
    put_number(j,e,"width",XZ_DJ_TILE_SIDE);put_number(j,e,"height",XZ_DJ_TILE_SIDE);put_text(j,e,"format","rgb565le");put_text(j,e,"for","xdj-xz");
    return e;
}
/* Writes the xdj-xz tile under a fresh name and lists it. */
static int add_tile(struct ctx *x,struct xz_dj_json *j,jv *d,const char *dir,const uint16_t *tile,char used[][XZ_DJ_NAME_MAX],int *nused,struct undo *u){
    unsigned char *bytes=malloc(XZ_DJ_TILE_BYTES);char name[XZ_DJ_NAME_MAX];
    if(!bytes)return -1;
    tile_bytes(tile,bytes);
    int rc=asset_name(x->root,dir,TILE_NAME,bytes,XZ_DJ_TILE_BYTES,used,*nused,name,x->buf)||put_file(x->root,dir,name,bytes,XZ_DJ_TILE_BYTES,0,u,x->buf)?-1:0;
    if(!rc){xz_dj_json_push(put_array(j,d,"renditions"),tile_entry(j,name,bytes));strcpy(used[(*nused)++],name);}
    free(bytes);return rc;
}
static int tile_listed(const jv *d){
    const jv *r=xz_dj_json_get(d,"renditions");
    for(const jv *e=r?r->child:NULL;e;e=e->next)if(xz_dj_json_is(xz_dj_json_get(e,"format"),"rgb565le")&&xz_dj_json_is(xz_dj_json_get(e,"for"),"xdj-xz"))return 1;
    return 0;
}
static uint32_t accent_of(const jv *d,const char *draft_accent){
    const char *s=xz_dj_json_get(d,"accent")?text_of(xz_dj_json_get(d,"accent")):draft_accent&&*draft_accent?draft_accent:"#3a3a3a";
    return (uint32_t)strtoul(s+1,NULL,16);
}
/* Contract 0.3 step 2 in a folder the DJ made: an existing logo.png, logo.jpg or logo.jpeg that differs
   from the new primary logo and that the replaced document (base) does not list becomes
   logo-original.<ext>, listed as an alternate when it is usable. */
static int keep_originals(struct ctx *x,struct xz_dj_json *j,jv *logos,const char *dir,const struct logo *primary,const jv *base,
                          char used[][XZ_DJ_NAME_MAX],int *nused,uint64_t *total,struct undo *u){
    const char *ours[24];int nours=base?listed(base,ours):0;
    static const char *const names[]={"logo.png","logo.jpg","logo.jpeg"};
    for(int k=0;k<3;k++){
        char spelled[XZ_DJ_NAME_MAX],want[32],name[XZ_DJ_NAME_MAX],from[4096],to[4096];size_t n;
        int listed_here=0;
        if(xz_dj_fixed(x->root,dir,names[k],spelled)||holds(x->root,dir,spelled,primary->data,primary->n,x->buf))continue;
        for(int i=0;i<nours;i++)if(xz_dj_fold_equal(ours[i],spelled))listed_here=1;
        if(listed_here)continue;
        int rc=rel_read(x->root,dir,spelled,XZ_DJ_LOGO_MAX_BYTES,x->buf,&n);
        if(rc==XZ_DJP_READ_IO)return -1;
        unsigned char *data=rc==XZ_DJP_READ_OK?malloc(n?n:1):NULL;
        if(data)memcpy(data,x->buf,n);
        snprintf(want,sizeof(want),"logo-original%s",strrchr(names[k],'.'));
        if(asset_name(x->root,dir,want,data?data:(const unsigned char *)"",data?n:0,used,*nused,name,x->buf)||
           rel_path(from,x->root,dir,spelled)||rel_path(to,x->root,dir,name)||(name_taken(x->root,dir,name)?0:rename(from,to))){free(data);return -1;}
        undo_add(u,to,from,0);strcpy(used[(*nused)++],name);
        if(data&&!xz_dj_image_usable(data,n,NULL)&&logos->count<8&&*total+n<=MAX_TOTAL_LOGO_BYTES){xz_dj_json_push(logos,logo_entry(j,name,data,n,"alternate"));*total+=n;}
        free(data);
    }
    return 0;
}
/* Serialises d, checks it as a reader would with the files in place, and publishes profile.json
   (profile.previous.json first when the folder has a valid profile.json). */
static int publish_document(struct ctx *x,const char *dir,const jv *d,const char *json,int json_valid){
    size_t n,old_n;char *text=xz_dj_json_write(d,XZ_DJ_JSON_PRETTY,&n),logo[1024];jv *check;
    struct xz_dj_json *p=text&&n<=MAX_JSON?xz_dj_json_parse(text,n,XZ_DJ_JSON_PROFILE,&check):NULL;
    int rc=-1;unsigned char *old=malloc(MAX_JSON+1);
    if(p&&old&&!xz_djp_check_document(x->root,dir,check,logo,NULL,NULL,x->buf)){
        rc=0;
        if(json[0]&&json_valid){
            if(rel_read(x->root,dir,json,MAX_JSON,old,&old_n)!=XZ_DJP_READ_OK)rc=-1;
            else if(old_n!=n||memcmp(old,text,n)){
                char previous[XZ_DJ_NAME_MAX];
                if(xz_dj_fixed(x->root,dir,"profile.previous.json",previous))strcpy(previous,"profile.previous.json");
                if(publish(x->root,dir,previous,old,old_n))rc=-1;
            }
        }
        if(!rc&&(publish(x->root,dir,json[0]?json:"profile.json",text,n)||rel_read(x->root,dir,json[0]?json:"profile.json",MAX_JSON,old,&old_n)!=XZ_DJP_READ_OK||old_n!=n||memcmp(old,text,n)))rc=-1;
    }
    else if(!p||!old)rc=-1;
    else rc=-3;
    xz_dj_json_free(p);free(text);free(old);return rc;
}
/* contract 0.2: the marker already in the old profile track's comment (an ID3v2.3/2.4 COMM frame),
   naming this id. 0 found. */
static int space_or_end(unsigned char c){return !c||c==' '||(c>='\t'&&c<='\r');}
static int old_marker(struct ctx *x,const jv *track,const char *id,char out[200]){
    const jv *path=xz_dj_json_get(track,"path");int fd;uint64_t size;unsigned char head[10];int found=-1;
    if(!path||path->type!=XZ_DJ_JSTR||memchr(path->s,0,path->n)||xz_djp_confined_open(x->root,path->s,64u<<20,&fd,&size)!=XZ_DJP_READ_OK)return -1;
    if(pread(fd,head,10,0)==10&&!memcmp(head,"ID3",3)&&(head[3]==3||head[3]==4)){
        size_t tag=(size_t)(head[6]&127)<<21|(size_t)(head[7]&127)<<14|(size_t)(head[8]&127)<<7|(head[9]&127);
        if(tag>16u<<20)tag=16u<<20;
        unsigned char *body=malloc(tag+1);
        if(body&&pread(fd,body,tag,10)==(ssize_t)tag){
            body[tag]=0;
            for(size_t at=0;found&&at+10<=tag&&memcmp(body+at,"\0\0\0\0",4);){
                const unsigned char *f=body+at+4;
                size_t n=head[3]==4?(size_t)(f[0]&127)<<21|(size_t)(f[1]&127)<<14|(size_t)(f[2]&127)<<7|(f[3]&127):(size_t)f[0]<<24|(size_t)f[1]<<16|(size_t)f[2]<<8|f[3];
                if(n>tag-at-10)break;
                if(!memcmp(body+at,"COMM",4))for(size_t i=0;found&&i+12<=n;i++){
                    const char *s=(const char *)body+at+10+i;size_t idn=strlen(id);
                    if(i+25+idn>n||memcmp(s,"djprofile:1:",12)||(i&&!space_or_end((unsigned char)s[-1])))continue;
                    if(strncmp(s+12,id,idn)||s[12+idn]!=':')continue;
                    int ok=1;for(size_t k=0;k<12;k++){char c=s[13+idn+k];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))ok=0;}
                    if(!ok||(i+25+idn<n&&!space_or_end((unsigned char)s[25+idn])))continue;
                    snprintf(out,200,"%.*s",(int)(25+idn),s);found=0;
                }
                at+=10+n;
            }
        }
        free(body);
    }
    close(fd);return found;
}
static const char *old_known[]={"schema","version","dj_id","revision","dj_name","note","bundle","preferred_logo","secondary_logos",
    "xz_tile","tagline","accent","aliases","links","page_url","profile_track"};
struct moved { char id[XZ_DJ_ID_MAX]; char folder[XZ_DJ_NAME_MAX]; };
/* Contract 0.2 and 0.3 for one old-store profile. 0 moved, 1 not moved (its assets do not check or the
   result would not be a valid document; it stays readable from the old store), -1 I/O error. */
static int migrate_one(struct ctx *x,const struct xz_dj_profile *old,struct moved *out){
    struct xz_dj_json *j=xz_dj_json_new(),*src=NULL;jv *o=NULL,*v;struct undo u;struct logo primary={0},alt={0};
    char dir[1024],used[24][XZ_DJ_NAME_MAX];int nused=0,result=1;uint64_t total=0;size_t n;
    u.n=0;
    if(!j)return -1;
    int rc=xz_djp_confined_read(x->root,old->document_path,MAX_JSON,x->buf,&n);
    if(rc==XZ_DJP_READ_IO){result=-1;goto done;}
    if(rc!=XZ_DJP_READ_OK||!(src=xz_dj_json_parse((const char *)x->buf,n,0,&o))||o->type!=XZ_DJ_JOBJ)goto done;
    const char *name=text_of(xz_dj_json_get(o,"dj_name"));
    /* Step 1: the profile's folder. */
    const struct xz_djp_folder *adopt=NULL;
    for(int i=0;i<x->s.folders&&!adopt;i++)if(!x->s.folder[i].json[0]&&same_key(x->s.folder[i].c.name,name))adopt=&x->s.folder[i];
    if(adopt)snprintf(dir,sizeof(dir),"%s",adopt->c.dir);
    else{
        char top[XZ_DJ_NAME_MAX],folder[XZ_DJ_NAME_MAX],path[4096];
        if(home(x,top)||unique_folder(x,top,name,NULL,folder)||cat(dir,sizeof(dir),top,folder)||join(path,x->root,dir)||mkdir(path,0755)){result=-1;goto done;}
        undo_add(&u,path,NULL,1);
    }
    /* Step 2: the assets, each checked against its recorded SHA-256 first. */
    const jv *preferred=xz_dj_json_get(o,"preferred_logo");unsigned char *data;
    if(preferred&&preferred->type==XZ_DJ_JOBJ){
        rc=read_verified(x,preferred,"path",XZ_DJ_LOGO_MAX_BYTES,&data,&n);
        if(rc){result=rc<0?-1:1;goto done;}
        if(logo_prepare(data,n,&primary))goto done;
    }else if(logo_monogram(name,accent_of(o,NULL),&primary))goto done;
    const char *primary_name=primary.png?"logo.png":"logo.jpg";
    strcpy(used[nused++],primary_name);
    jv *d=xz_dj_json_value(j,XZ_DJ_JOBJ),*logos;char rev[64];
    put_text(j,d,"format","djprofile/1");put_text(j,d,"id",old->dj_id);
    v=xz_dj_json_get(o,"revision");
    if(v&&v->type==XZ_DJ_JSTR&&v->n>=1&&v->n<=64)xz_dj_json_put(j,d,"revision",xz_dj_json_copy(j,v));else{new_revision(rev);put_text(j,d,"revision",rev);}
    /* What the 1.0 limits cut or drop is kept whole in ext["vj.tools"] (contract 0.2). */
    jv *vendor=xz_dj_json_value(j,XZ_DJ_JOBJ);
#define LOST(key) do{if(!xz_dj_json_get(vendor,key)){jv *copy_=xz_dj_json_copy(j,xz_dj_json_get(o,key));whole_numbers(copy_);xz_dj_json_put(j,vendor,key,copy_);}}while(0)
    put_bytes(j,d,"name",name,fit_bytes(name,strlen(name),160));
    if(fit_bytes(name,strlen(name),160)<strlen(name))LOST("dj_name");
    if((v=xz_dj_json_get(o,"tagline"))&&v->type==XZ_DJ_JSTR&&v->n){put_bytes(j,d,"tagline",v->s,fit_bytes(v->s,v->n,240));if(v->n>240)LOST("tagline");}
    if((v=xz_dj_json_get(o,"accent"))&&v->type==XZ_DJ_JSTR)xz_dj_json_put(j,d,"accent",xz_dj_json_copy(j,v));
    if((v=xz_dj_json_get(o,"aliases"))&&v->type==XZ_DJ_JARR&&v->count){
        jv *aliases=put_array(j,d,"aliases");
        for(const jv *a=v->child;a;a=a->next)if(a->type==XZ_DJ_JSTR){xz_dj_json_push(aliases,xz_dj_json_string(j,a->s,fit_bytes(a->s,a->n,160)));if(a->n>160)LOST("aliases");}
    }
    logos=put_array(j,d,"logos");
    static const char *const ref_known[]={"path","bytes","sha256","label","note"};
    jv *entry=logo_entry(j,primary_name,primary.data,primary.n,"primary");
    if(preferred&&preferred->type==XZ_DJ_JOBJ){
        if((v=xz_dj_json_get(preferred,"label"))&&v->type==XZ_DJ_JSTR&&v->n){put_bytes(j,entry,"label",v->s,fit_bytes(v->s,v->n,160));if(v->n>160)LOST("preferred_logo");}
        if((v=xz_dj_json_get(preferred,"note"))&&v->type==XZ_DJ_JSTR&&v->n){put_bytes(j,entry,"note",v->s,fit_bytes(v->s,v->n,1024));if(v->n>1024)LOST("preferred_logo");}
        keep_unknown(j,entry,preferred,ref_known,5);
    }
    xz_dj_json_push(logos,entry);total+=primary.n;
    if(adopt&&keep_originals(x,j,logos,dir,&primary,NULL,used,&nused,&total,&u)){result=-1;goto done;}
    if(put_file(x->root,dir,primary_name,primary.data,primary.n,0,&u,x->buf))goto done;
    const jv *secondary=xz_dj_json_get(o,"secondary_logos");
    for(const jv *s=secondary&&secondary->type==XZ_DJ_JARR?secondary->child:NULL;s;s=s->next){
        rc=read_verified(x,s,"path",XZ_DJ_LOGO_MAX_BYTES,&data,&n);
        if(rc){result=rc<0?-1:1;goto done;}
        if(logo_prepare(data,n,&alt))goto done;
        const char *path=text_of(xz_dj_json_get(s,"path")),*base=strrchr(path,'/')?strrchr(path,'/')+1:path;char want[XZ_DJ_NAME_MAX],file[XZ_DJ_NAME_MAX];
        snprintf(want,sizeof(want),"%s",base);
        if(alt.png&&!ends_with_fold(want,".png")){char *dot=strrchr(want,'.');if(dot)*dot=0;if(strlen(want)<200)strcat(want,".png");}
        if(logos->count>=8||total+alt.n>MAX_TOTAL_LOGO_BYTES||asset_name(x->root,dir,want,alt.data,alt.n,used,nused,file,x->buf)||
           put_file(x->root,dir,file,alt.data,alt.n,0,&u,x->buf))goto done;
        strcpy(used[nused++],file);total+=alt.n;
        entry=logo_entry(j,file,alt.data,alt.n,"alternate");
        if((v=xz_dj_json_get(s,"label"))&&v->type==XZ_DJ_JSTR&&v->n){put_bytes(j,entry,"label",v->s,fit_bytes(v->s,v->n,160));if(v->n>160)LOST("secondary_logos");}
        if((v=xz_dj_json_get(s,"note"))&&v->type==XZ_DJ_JSTR&&v->n){put_bytes(j,entry,"note",v->s,fit_bytes(v->s,v->n,1024));if(v->n>1024)LOST("secondary_logos");}
        keep_unknown(j,entry,s,ref_known,5);
        xz_dj_json_push(logos,entry);logo_free(&alt);
    }
    jv *links=xz_dj_json_value(j,XZ_DJ_JARR);
    if((v=xz_dj_json_get(o,"page_url"))&&v->type==XZ_DJ_JSTR){
        if(v->n<=2048){jv *page=xz_dj_json_value(j,XZ_DJ_JOBJ);put_text(j,page,"kind","page");xz_dj_json_put(j,page,"url",xz_dj_json_copy(j,v));xz_dj_json_push(links,page);}
        else LOST("page_url");
    }
    if((v=xz_dj_json_get(o,"links"))&&v->type==XZ_DJ_JARR)for(const jv *l=v->child;l;l=l->next){
        const jv *kind=xz_dj_json_get(l,"kind"),*url=xz_dj_json_get(l,"url");
        if(links->count>=12||!kind||kind->type!=XZ_DJ_JSTR||!url||url->type!=XZ_DJ_JSTR||url->n>2048||kind->n>40){LOST("links");if(links->count>=12)break;}
        if(!kind||kind->type!=XZ_DJ_JSTR||!url||url->type!=XZ_DJ_JSTR||url->n>2048)continue;
        jv *link=xz_dj_json_value(j,XZ_DJ_JOBJ);static const char *const link_known[]={"kind","url"};
        put_bytes(j,link,"kind",kind->s,fit_bytes(kind->s,kind->n,40));xz_dj_json_put(j,link,"url",xz_dj_json_copy(j,url));keep_unknown(j,link,l,link_known,2);
        xz_dj_json_push(links,link);
    }
    if(links->count)xz_dj_json_put(j,d,"links",links);
    if((v=xz_dj_json_get(o,"note"))&&v->type==XZ_DJ_JSTR&&v->n)xz_dj_json_put(j,d,"note",xz_dj_json_copy(j,v));
    const jv *xz_tile=xz_dj_json_get(o,"xz_tile");
    if(xz_tile&&xz_tile->type==XZ_DJ_JOBJ){
        static const char *const tile_known[]={"path","bytes","sha256","width","height","format"};char file[XZ_DJ_NAME_MAX];
        rc=read_verified(x,xz_tile,"path",XZ_DJ_TILE_BYTES,&data,&n);
        if(!rc&&n!=XZ_DJ_TILE_BYTES){free(data);rc=1;}
        if(rc){result=rc<0?-1:1;goto done;}
        if(asset_name(x->root,dir,TILE_NAME,data,n,used,nused,file,x->buf)||put_file(x->root,dir,file,data,n,0,&u,x->buf)){free(data);goto done;}
        strcpy(used[nused++],file);
        entry=tile_entry(j,file,data);keep_unknown(j,entry,xz_tile,tile_known,6);
        xz_dj_json_push(put_array(j,d,"renditions"),entry);free(data);
    }else if(primary.has_tile&&add_tile(x,j,d,dir,primary.tile,used,&nused,&u))goto done;
    /* profile_track: {marker} with the marker the old MP3 carries; the MP3 itself stays in the old store. */
    const jv *track=xz_dj_json_get(o,"profile_track");char mark[200];
    if(track&&track->type==XZ_DJ_JOBJ){
        if(!old_marker(x,track,old->dj_id,mark)){jv *t=xz_dj_json_value(j,XZ_DJ_JOBJ);put_text(j,t,"marker",mark);xz_dj_json_put(j,d,"track",t);}
        else LOST("profile_track");
    }
#undef LOST
    jv *ext=xz_dj_json_value(j,XZ_DJ_JOBJ);
    keep_unknown(j,vendor,o,old_known,sizeof(old_known)/sizeof(old_known[0]));
    if(vendor&&vendor->count){xz_dj_json_put(j,ext,"vj.tools",vendor);xz_dj_json_put(j,d,"ext",ext);}
    if(xz_dj_json_failed(j)){result=-1;goto done;}
    /* Step 3: profile.json; DJPROFILE.json follows once every profile has moved. */
    rc=publish_document(x,dir,d,"",0);
    if(rc==-3)goto done;
    if(rc){result=-1;goto done;}
    snprintf(out->id,sizeof(out->id),"%s",old->dj_id);snprintf(out->folder,sizeof(out->folder),"%s",strchr(dir,'/')+1);
    result=0;
done:
    if(result)undo_all(&u);
    logo_free(&primary);logo_free(&alt);xz_dj_json_free(src);xz_dj_json_free(j);
    return result;
}
/* Contract 0.3: any write on a stick that still has old-store profiles first moves all of them to profile
   folders, then lists every folder in DJPROFILE.json (moved ones first) and keeps the active profile.
   The old store stays (step 4: the XZ has nowhere to back it up). 0, or -1 on an I/O error. */
static int migrate(struct ctx *x){
    int todo=0;
    for(int k=0;k<x->s.olds;k++)if(!x->s.old[k].damaged&&x->s.old[k].document_path[0])todo++;
    if(!todo)return 0;
    struct moved moved[XZ_DJ_MAX_PROFILES+1];int nmoved=0;
    char active[XZ_DJ_ID_MAX+XZ_DJ_NAME_MAX]="";
    if(x->s.list.active>=0)snprintf(active,sizeof(active),"%s",xz_dj_profile_ref(&x->s.list.profile[x->s.list.active]));
    struct { char folder[XZ_DJ_NAME_MAX], id[XZ_DJ_ID_MAX]; } before[XZ_DJ_MAX_PROFILES];int nbefore=0;
    for(int i=0;i<x->s.shown;i++){
        const struct xz_djp_folder *f=&x->s.folder[x->s.order[i]];
        snprintf(before[nbefore].folder,XZ_DJ_NAME_MAX,"%s",f->c.name);snprintf(before[nbefore].id,XZ_DJ_ID_MAX,"%s",f->level==1?f->view.dj_id:"");nbefore++;
    }
    for(int k=0;k<x->s.olds&&nmoved<XZ_DJ_MAX_PROFILES+1;k++){
        if(x->s.old[k].damaged||!x->s.old[k].document_path[0])continue;
        int rc=migrate_one(x,&x->s.old[k],&moved[nmoved]);
        if(rc<0)return -1;
        if(!rc){
            nmoved++;
            /* Later profiles must not take this folder's name. */
            struct xz_djp_folder *grown=realloc(x->s.folder,sizeof(*grown)*(size_t)(x->s.folders+1));
            if(!grown)return -1;
            x->s.folder=grown;struct xz_djp_folder *f=&grown[x->s.folders];int seen=0;
            for(int i=0;i<x->s.folders;i++)grown[i].c.name=strchr(grown[i].c.dir,'/')+1;
            for(int i=0;i<x->s.folders;i++)if(same_key(grown[i].c.name,moved[nmoved-1].folder)){seen=1;snprintf(grown[i].json,sizeof(grown[i].json),"profile.json");}
            if(!seen){
                memset(f,0,sizeof(*f));snprintf(f->c.dir,sizeof(f->c.dir),"%s/%s",x->s.tops[0],moved[nmoved-1].folder);f->c.name=strchr(f->c.dir,'/')+1;
                if(xz_dj_name_key(f->c.name,strlen(f->c.name),f->c.key,sizeof(f->c.key))>=0){snprintf(f->json,sizeof(f->json),"profile.json");x->s.folders++;}
            }
        }
    }
    if(!nmoved)return 0;
    struct xz_dj_json *j=xz_dj_json_new();
    jv *tree=j?stick_tree(x,j):NULL,*old=tree?xz_dj_json_get(tree,"profiles"):NULL,*profiles=j?xz_dj_json_value(j,XZ_DJ_JARR):NULL;
    if(!tree||!profiles){xz_dj_json_free(j);return -1;}
    for(int i=0;i<nmoved&&profiles->count<XZ_DJ_MAX_PROFILES;i++){
        jv *e=entry_of(old,moved[i].id,moved[i].folder);
        if(e){xz_dj_json_unlink(old,e);put_text(j,e,"folder",moved[i].folder);put_text(j,e,"id",moved[i].id);}else e=new_entry(j,moved[i].folder,moved[i].id);
        xz_dj_json_push(profiles,e);
        if(!strcmp(active,moved[i].id)||same_key(active,moved[i].folder))memcpy(active,moved[i].id,strlen(moved[i].id)+1);
    }
    for(int i=0;i<nbefore&&profiles->count<XZ_DJ_MAX_PROFILES;i++){
        int adopted=0;
        for(int k=0;k<nmoved;k++)if(same_key(before[i].folder,moved[k].folder))adopted=1;
        if(adopted)continue;
        jv *e=entry_of(old,before[i].id,before[i].folder);
        if(e)xz_dj_json_unlink(old,e);else e=new_entry(j,before[i].folder,before[i].id);
        xz_dj_json_push(profiles,e);
    }
    xz_dj_json_put(j,tree,"profiles",profiles);
    if(*active)put_text(j,tree,"active",active);
    int rc=write_stick(x,j,tree);
    xz_dj_json_free(j);
    return rc||rescan(x)?-1:0;
}

/* --- device saves --- */
static enum xz_dj_write_result image_failure(enum xz_dj_image_result r){return r==XZ_DJ_IMAGE_NO_MEMORY?XZ_DJ_WRITE_IO:XZ_DJ_WRITE_IMAGE;}
enum xz_dj_write_result xz_dj_profile_write(const char *root,const struct xz_dj_draft *draft,char *out_dj_id){
    if(out_dj_id)*out_dj_id=0;
    if(!root||!*root||!draft||!bounded(draft->dj_id,sizeof(draft->dj_id))||!bounded(draft->name,sizeof(draft->name))||!bounded(draft->folder,sizeof(draft->folder))||
       !bounded(draft->tagline,sizeof(draft->tagline))||!bounded(draft->logo,sizeof(draft->logo))||!bounded(draft->accent,sizeof(draft->accent)))return XZ_DJ_WRITE_INVALID;
    if(draft->accent[0]&&(strlen(draft->accent)!=7||draft->accent[0]!='#'||strspn(draft->accent+1,"0123456789abcdef")!=6))return XZ_DJ_WRITE_INVALID;
    if(xz_dj_valid_name(draft->name,strlen(draft->name),160)||blank(draft->name)||strlen(draft->tagline)>240||xz_djp_utf8_valid(S(draft->tagline))||has_control(draft->tagline))return XZ_DJ_WRITE_INVALID;
    if(*draft->dj_id&&(strlen(draft->dj_id)>128||strspn(draft->dj_id,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=strlen(draft->dj_id)))return XZ_DJ_WRITE_INVALID;
    if(*draft->logo&&(draft->remove_logo||draft->logo[0]!='/'))return XZ_DJ_WRITE_INVALID;
    struct stat st;
    if(stat(root,&st)||!S_ISDIR(st.st_mode))return XZ_DJ_WRITE_IO;
    /* A new logo is read before anything is written. */
    struct logo fresh={0};unsigned char *data=NULL;size_t n;
    if(*draft->logo){
        enum xz_dj_image_result image=read_image(draft->logo,&data,&n);
        if(image==XZ_DJ_IMAGE_OK)image=logo_prepare(data,n,&fresh);
        else free(data);
        if(image!=XZ_DJ_IMAGE_OK)return image_failure(image);
    }
    struct ctx x;struct xz_dj_json *j=xz_dj_json_new(),*base_tree=NULL;struct undo u;u.n=0;
    enum xz_dj_write_result result=XZ_DJ_WRITE_IO;
    char dir[1024],folder[XZ_DJ_NAME_MAX],id[XZ_DJ_ID_MAX],rev[64],used[24][XZ_DJ_NAME_MAX],json[XZ_DJ_NAME_MAX]="";int nused=0,json_valid=0;
    struct logo kept={0};
    if(!j||ctx_open(&x,root)||migrate(&x))goto done;
    /* The profile being written: by id, else by folder, else a new one. */
    const struct xz_djp_folder *t=NULL;
    for(int i=0;i<x.s.folders&&!t&&*draft->dj_id;i++)if(x.s.folder[i].level==1&&!strcmp(x.s.folder[i].view.dj_id,draft->dj_id))t=&x.s.folder[i];
    if(*draft->dj_id&&!t){result=XZ_DJ_WRITE_CHANGED;goto done;}
    for(int i=0;i<x.s.folders&&!t&&*draft->folder;i++)if(!strcmp(x.s.folder[i].c.dir,draft->folder))t=&x.s.folder[i];
    if(*draft->folder&&!t){result=XZ_DJ_WRITE_CHANGED;goto done;}
    int shown=0;
    for(int i=0;i<x.s.shown;i++)if(&x.s.folder[x.s.order[i]]==t)shown=1;
    if(!shown&&x.s.shown>=XZ_DJ_MAX_PROFILES){result=XZ_DJ_WRITE_FULL;goto done;}
    /* The document it replaces, kept member by member. */
    jv *d=NULL,*base=NULL;
    if(t&&t->level==1){
        if(rel_read(root,t->c.dir,t->used,MAX_JSON,x.buf,&n)!=XZ_DJP_READ_OK||!(base_tree=xz_dj_json_parse((const char *)x.buf,n,XZ_DJ_JSON_PROFILE,&base)))goto done;
        d=xz_dj_json_copy(j,base);
    }
    if(t){snprintf(json,sizeof(json),"%s",t->json);json_valid=t->json_valid;}
    if(!d){
        char made[37];uuid4(made);d=xz_dj_json_value(j,XZ_DJ_JOBJ);
        put_text(j,d,"format","djprofile/1");put_text(j,d,"id",made);put_text(j,d,"revision","");put_text(j,d,"name","");
    }
    if(!d)goto done;
    snprintf(id,sizeof(id),"%s",text_of(xz_dj_json_get(d,"id")));
    const char *was=text_of(xz_dj_json_get(d,"name"));int renamed=strcmp(was,draft->name)!=0;
    put_text(j,d,"name",draft->name);
    if(*draft->tagline)put_text(j,d,"tagline",draft->tagline);else xz_dj_json_remove(d,"tagline");
    if(!xz_dj_json_get(d,"accent")&&*draft->accent)put_text(j,d,"accent",draft->accent);
    /* The folder: its own, or a new one named after the DJ. */
    char top[XZ_DJ_NAME_MAX];
    if(t)snprintf(dir,sizeof(dir),"%s",t->c.dir);
    else{
        char path[4096];
        if(home(&x,top)||unique_folder(&x,top,draft->name,NULL,folder)||cat(dir,sizeof(dir),top,folder)||join(path,root,dir)||mkdir(path,0755))goto done;
        undo_add(&u,path,NULL,1);
    }
    /* Logos. */
    jv *logos=xz_dj_json_get(d,"logos"),*primary_entry=NULL;
    for(jv *e=logos?logos->child:NULL;e;e=e->next)if(xz_dj_json_is(xz_dj_json_get(e,"role"),"primary"))primary_entry=e;
    uint64_t total=0;const struct logo *change=NULL;
    if(*draft->logo)change=&fresh;
    else if(draft->remove_logo||!t||t->level<0){
        if(logo_monogram(draft->name,accent_of(d,draft->accent),&kept))goto done;
        change=&kept;
    }else if(!primary_entry){
        /* Level 0, or a document without logos: the folder's own logo becomes the primary logo. */
        const char *file=t->view.logo_path+strlen(t->c.dir)+1;
        if(rel_read(root,t->c.dir,file,XZ_DJ_LOGO_MAX_BYTES,x.buf,&n)!=XZ_DJP_READ_OK||!(data=malloc(n)))goto done;
        memcpy(data,x.buf,n);
        if(logo_prepare(data,n,&kept)){result=XZ_DJ_WRITE_IMAGE;goto done;}
        change=&kept;
    }
    if(change){
        char name[XZ_DJ_NAME_MAX];snprintf(name,sizeof(name),"%s",change->png?"logo.png":"logo.jpg");
        /* The stick's own spelling of an identical file (Logo.PNG on a case-sensitive file system). */
        char spelled[XZ_DJ_NAME_MAX];
        if(!xz_dj_fixed(root,dir,name,spelled)&&holds(root,dir,spelled,change->data,change->n,x.buf))snprintf(name,sizeof(name),"%s",spelled);
        if(!logos)logos=put_array(j,d,"logos");
        if(!primary_entry){primary_entry=xz_dj_json_value(j,XZ_DJ_JOBJ);xz_dj_json_push_front(logos,primary_entry);put_text(j,primary_entry,"role","primary");}
        char digest[65];sha_hex(change->data,change->n,digest);
        int same=xz_dj_json_is(xz_dj_json_get(primary_entry,"sha256"),digest)&&xz_dj_fold_equal(text_of(xz_dj_json_get(primary_entry,"file")),name);
        if(!same){
            put_text(j,primary_entry,"file",name);put_number(j,primary_entry,"bytes",(int64_t)change->n);put_text(j,primary_entry,"sha256",digest);
            put_text(j,primary_entry,"background","any");xz_dj_json_remove(primary_entry,"label");
            /* Renditions are copies of the old primary logo. */
            xz_dj_json_remove(d,"renditions");
        }
        strcpy(used[nused++],name);
        for(jv *e=logos->child;e;e=e->next)if(e!=primary_entry&&xz_dj_json_get(e,"file")&&nused<20)snprintf(used[nused++],XZ_DJ_NAME_MAX,"%s",text_of(xz_dj_json_get(e,"file")));
        if(!same){
            /* A logo file this profile never listed is the DJ's: it moves aside, it is not overwritten. */
            if(t&&keep_originals(&x,j,logos,dir,change,base,used,&nused,&total,&u))goto done;
            if(put_file(root,dir,name,change->data,change->n,1,&u,x.buf))goto done;
        }
    }
    for(const jv *e=logos?logos->child:NULL;e;e=e->next){int64_t b;if(!xz_dj_json_whole(xz_dj_json_get(e,"bytes"),&b))total+=(uint64_t)b;}
    if(total>MAX_TOTAL_LOGO_BYTES||(logos&&logos->count>8)){result=XZ_DJ_WRITE_INVALID;goto done;}
    /* The tile the MODS header draws from; one the reader could not use is replaced. */
    jv *renditions=xz_dj_json_get(d,"renditions");
    if(!change&&tile_listed(d)&&!(t&&t->level==1&&t->view.has_thumb))
        for(jv *e=renditions?renditions->child:NULL,*next;e;e=next){
            next=e->next;
            if(xz_dj_json_is(xz_dj_json_get(e,"format"),"rgb565le")&&xz_dj_json_is(xz_dj_json_get(e,"for"),"xdj-xz"))xz_dj_json_unlink(renditions,e);
        }
    if(!tile_listed(d)){
        const struct logo *source=change;
        if(!source){
            const char *file=text_of(xz_dj_json_get(primary_entry,"file"));
            if(rel_read(root,dir,file,XZ_DJ_LOGO_MAX_BYTES,x.buf,&n)==XZ_DJP_READ_OK&&(data=malloc(n))){memcpy(data,x.buf,n);if(!logo_prepare(data,n,&kept))source=&kept;}
        }
        for(const jv *e=xz_dj_json_get(d,"renditions")?xz_dj_json_get(d,"renditions")->child:NULL;e&&nused<20;e=e->next)snprintf(used[nused++],XZ_DJ_NAME_MAX,"%s",text_of(xz_dj_json_get(e,"file")));
        if(source&&source->has_tile&&add_tile(&x,j,d,dir,source->tile,used,&nused,&u))goto done;
    }
    /* A track copy names the DJ in its tags: after a rename it no longer matches, so only the marker stays. */
    jv *track=xz_dj_json_get(d,"track");
    if(track&&renamed&&xz_dj_json_get(track,"file")){xz_dj_json_remove(track,"file");xz_dj_json_remove(track,"bytes");xz_dj_json_remove(track,"sha256");}
    /* Nothing to do when the document is what the stick holds. */
    if(base){
        put_text(j,d,"revision",text_of(xz_dj_json_get(base,"revision")));
        if(xz_dj_json_same(d,base)&&!strcmp(t->used,t->json)){result=XZ_DJ_WRITE_OK;goto stick;}
    }
    new_revision(rev);put_text(j,d,"revision",rev);
    if(xz_dj_json_failed(j))goto done;
    int published=publish_document(&x,dir,d,json,json_valid);
    if(published==-3){result=XZ_DJ_WRITE_INVALID;goto done;}
    if(published)goto done;
    u.n=0;
    /* The folder follows the name (section 7), then the files the old document listed and this one does not go. */
stick:;
    char old_folder[XZ_DJ_NAME_MAX];snprintf(old_folder,sizeof(old_folder),"%s",strchr(dir,'/')+1);
    snprintf(folder,sizeof(folder),"%s",old_folder);
    if(t&&renamed){
        char top_of[XZ_DJ_NAME_MAX];snprintf(top_of,sizeof(top_of),"%.*s",(int)(strchr(dir,'/')-dir),dir);
        if(!unique_folder(&x,top_of,draft->name,t,folder)&&strcmp(folder,old_folder)){
            char from[4096],to[4096],moved[1024];
            if(cat(moved,sizeof(moved),top_of,folder)||join(from,root,dir)||join(to,root,moved)||rename(from,to))snprintf(folder,sizeof(folder),"%s",old_folder);
            else{char parent[4096];snprintf(dir,sizeof(dir),"%s",moved);if(!join(parent,root,top_of))fsync_dir(parent);}
        }
    }
    {
        struct xz_dj_json *sj=xz_dj_json_new();jv *tree=sj?stick_tree(&x,sj):NULL;
        if(!tree){xz_dj_json_free(sj);goto done;}
        int had=x.s.stick_file[0]!=0,changed=0;
        jv *profiles=xz_dj_json_get(tree,"profiles");
        if(!profiles||profiles->type!=XZ_DJ_JARR){profiles=profiles_of(&x,sj,tree);changed=1;}
        jv *e=entry_of(profiles,t&&t->level==1?id:NULL,old_folder);
        if(!e&&profiles->count<XZ_DJ_MAX_PROFILES){xz_dj_json_push(profiles,e=new_entry(sj,folder,id));changed=1;}
        if(e&&(!xz_dj_json_is(xz_dj_json_get(e,"folder"),folder)||!xz_dj_json_is(xz_dj_json_get(e,"id"),id))){put_text(sj,e,"folder",folder);put_text(sj,e,"id",id);changed=1;}
        const jv *active=xz_dj_json_get(tree,"active");
        if(!active||active->type!=XZ_DJ_JSTR){
            const char *current=x.s.list.active>=0?xz_dj_profile_ref(&x.s.list.profile[x.s.list.active]):id;
            put_text(sj,tree,"active",t&&x.s.list.active>=0&&!strcmp(x.s.list.profile[x.s.list.active].dir,t->c.dir)?id:current);changed=1;
        }else if(t&&!xz_dj_json_is(active,id)&&same_key(active->s,old_folder)){put_text(sj,tree,"active",id);changed=1;}
        int wrote=changed||!had?write_stick(&x,sj,tree):0;
        if(wrote){xz_dj_json_free(sj);result=wrote==-2?XZ_DJ_WRITE_CHANGED:XZ_DJ_WRITE_IO;goto done;}
        xz_dj_json_free(sj);
    }
    if(base&&result!=XZ_DJ_WRITE_OK){
        const char *before[24],*now[24];int nb=listed(base,before),nn=listed(d,now);
        for(int i=0;i<nb;i++){
            int kept_file=0;char path[4096];
            for(int k=0;k<nn;k++)if(xz_dj_fold_equal(before[i],now[k]))kept_file=1;
            if(!kept_file&&!rel_path(path,root,dir,before[i]))(void)unlink(path);
        }
    }
    if(out_dj_id)snprintf(out_dj_id,XZ_DJ_ID_MAX,"%s",id);
    result=XZ_DJ_WRITE_OK;
done:
    undo_all(&u);
    logo_free(&fresh);logo_free(&kept);xz_dj_json_free(base_tree);xz_dj_json_free(j);ctx_close(&x);
    return result;
}

int xz_dj_stick_set_active(const char *root,const char *reference){
    if(!root||!*root||!reference||!*reference||strlen(reference)>=XZ_DJ_NAME_MAX)return -1;
    struct ctx x;int result=-1;struct xz_dj_json *j=NULL;
    if(ctx_open(&x,root)||migrate(&x))goto done;
    int i=xz_djp_resolve(&x.s.list,reference,strlen(reference));
    if(i<0)goto done;
    if(i==x.s.list.active){result=1;goto done;}
    const struct xz_dj_profile *p=&x.s.list.profile[i];
    jv *tree=(j=xz_dj_json_new())?stick_tree(&x,j):NULL;
    if(!tree)goto done;
    if(!x.s.stick_file[0])profiles_of(&x,j,tree);
    put_text(j,tree,"active",xz_dj_profile_ref(p));
    if(write_stick(&x,j,tree))goto done;
    /* Contract 0.3 step 5: an older runtime reads the old index. */
    if(p->kind!=XZ_DJ_LEVEL0&&x.s.old_index_valid&&xz_djp_old_index_set_active(root,p->dj_id)<0)goto done;
    result=0;
done:
    xz_dj_json_free(j);ctx_close(&x);return result;
}

int xz_dj_draft_load(const char *root,const char *reference,struct xz_dj_draft *out){
    if(!out)return -1;
    memset(out,0,sizeof(*out));
    if(!root||!*root||!reference||!*reference)return -1;
    struct xz_dj_stick *s=malloc(sizeof(*s));int rc=-1;
    if(s&&xz_dj_stick_read(root,s)>=0){
        int i=xz_djp_resolve(s,reference,strlen(reference));
        if(i>=0&&!s->profile[i].damaged){
            const struct xz_dj_profile *p=&s->profile[i];
            if(p->kind!=XZ_DJ_LEVEL0)snprintf(out->dj_id,sizeof(out->dj_id),"%s",p->dj_id);
            else snprintf(out->folder,sizeof(out->folder),"%s",p->dir);
            snprintf(out->name,sizeof(out->name),"%s",p->name);snprintf(out->tagline,sizeof(out->tagline),"%s",p->tagline);
            rc=0;
        }
    }
    free(s);return rc;
}
