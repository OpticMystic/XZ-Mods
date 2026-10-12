/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_profile_write.h"
#include "dj_profile_internal.h"
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
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "vendor/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
/* STB_IMAGE_STATIC declares functions this file never calls; that is reported at the end of the file. */
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/* Writing mirrors publish_profile in native/rust-app/crates/dj-profiles/src/profile.rs. */
#define HOME "DJPROFILE/VJTOOLS"
#define INDEX_PATH HOME "/index.json"
#define MAX_JSON 65536
#define MAX_LOGOS 8
#define MAX_TOTAL_LOGO_BYTES (24u*1024u*1024u)
#define TILE_FILE "xz-tile-160.rgb565"
#define PATH_MAX_REL 1025
typedef struct xz_djp_str str;

/* --- small helpers --- */
static str S(const char *s){str v={s,strlen(s)};return v;}
static int eq(str a,const char *b){return a.n==strlen(b)&&!memcmp(a.s,b,a.n);}
static int copy_str(char *out,size_t cap,str v){if(v.n>=cap||memchr(v.s,0,v.n))return -1;memcpy(out,v.s,v.n);out[v.n]=0;return 0;}
static int bounded(const char *s,size_t cap){return memchr(s,0,cap)!=NULL;}
static void hex(const unsigned char *b,size_t n,char *out){static const char d[]="0123456789abcdef";for(size_t i=0;i<n;i++){out[i*2]=d[b[i]>>4];out[i*2+1]=d[b[i]&15];}out[n*2]=0;}
static void sha_hex(const void *data,size_t n,char out[65]){SHA256_CTX c;unsigned char sum[32];sha256_init(&c);sha256_update(&c,data,n);sha256_final(&c,sum);hex(sum,32,out);}
static void lower(char *s){for(;*s;s++)if(*s>='A'&&*s<='Z')*s=(char)(*s-'A'+'a');}
static int join(char out[4096],const char *root,const char *relative){
    size_t a=strlen(root),b=strlen(relative);if(a+1+b>=4096)return -1;
    memcpy(out,root,a);out[a]='/';memcpy(out+a+1,relative,b+1);return 0;
}
/* out = a + "/" + b; -1 when it does not fit. */
static int path_cat(char *out,size_t cap,const char *a,const char *b){
    size_t x=strlen(a),y=strlen(b);if(x+1+y>=cap)return -1;
    memmove(out,a,x);out[x]='/';memcpy(out+x+1,b,y+1);return 0;
}
static int write_all(int fd,const void *data,size_t n){
    const char *p=data;
    while(n){ssize_t w=write(fd,p,n);if(w<0){if(errno==EINTR)continue;return -1;}p+=w;n-=(size_t)w;}
    return 0;
}
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
static void utc(struct tm *tm){time_t now=time(NULL);if(!gmtime_r(&now,tm))memset(tm,0,sizeof(*tm));}

/* --- UTF-8 code points on valid input --- */
static uint32_t cp(const char *s,size_t *i){
    const unsigned char *u=(const unsigned char *)s;unsigned c=u[*i];
    if(c<0x80){(*i)++;return c;}
    size_t k=c>=0xf0?3:c>=0xe0?2:1;uint32_t v=c&(k==3?7:k==2?15:31);
    for(size_t j=1;j<=k;j++)v=v<<6|(u[*i+j]&63);
    *i+=k+1;return v;
}
/* Rust's char::is_whitespace plus U+FEFF, which JavaScript's trim() also removes: a name blank to
   either reader is refused. */
static int blank(str v){
    for(size_t i=0;i<v.n;){
        uint32_t c=cp(v.s,&i);
        if(!((c>=9&&c<=13)||c==0x20||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||
             c==0x202f||c==0x205f||c==0x3000||c==0xfeff))return 0;
    }return 1;
}
/* profile.rs slug(): ASCII letters, digits, '-' and '_' kept, every other character '_', 80 characters. */
static void slug(const char *value,char out[81]){
    size_t n=0,i=0,len=strlen(value);int meaningful=0;
    while(i<len&&n<80){
        uint32_t c=cp(value,&i);
        char o=(c<0x80&&((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_'))?(char)c:'_';
        if(o!='_')meaningful=1;
        out[n++]=o;
    }
    out[n]=0;
    if(!meaningful)strcpy(out,"dj-logo");
}

/* --- JSON output in serde_json's format (to_vec_pretty when pretty, to_vec otherwise) --- */
struct js { char *b; size_t n,cap; int err,pretty,depth; unsigned char first[16]; };
static void js_raw(struct js *o,const char *s,size_t n){
    if(o->err)return;
    if(o->n+n+1>o->cap){
        size_t cap=o->cap?o->cap:4096;while(o->n+n+1>cap)cap*=2;
        char *b=cap<=(1u<<20)?realloc(o->b,cap):NULL;
        if(!b){o->err=1;return;}
        o->b=b;o->cap=cap;
    }
    memcpy(o->b+o->n,s,n);o->n+=n;o->b[o->n]=0;
}
static void js_s(struct js *o,const char *s){js_raw(o,s,strlen(s));}
static void js_str(struct js *o,const char *s){
    js_raw(o,"\"",1);
    for(const unsigned char *p=(const unsigned char *)s;*p;p++){
        char e[8];
        switch(*p){
        case '"':js_s(o,"\\\"");break;case '\\':js_s(o,"\\\\");break;
        case '\b':js_s(o,"\\b");break;case '\f':js_s(o,"\\f");break;case '\n':js_s(o,"\\n");break;
        case '\r':js_s(o,"\\r");break;case '\t':js_s(o,"\\t");break;
        default:
            if(*p<0x20){snprintf(e,sizeof(e),"\\u%04x",*p);js_s(o,e);}
            else js_raw(o,(const char *)p,1);
        }
    }
    js_raw(o,"\"",1);
}
static void js_nl(struct js *o){if(!o->pretty)return;js_raw(o,"\n",1);for(int i=0;i<o->depth;i++)js_raw(o,"  ",2);}
static void js_open(struct js *o,const char *c){js_s(o,c);if(o->depth>=15){o->err=1;return;}o->first[++o->depth]=1;}
static void js_close(struct js *o,const char *c){int empty=o->first[o->depth];o->depth--;if(!empty)js_nl(o);js_s(o,c);}
static void js_item(struct js *o){if(!o->first[o->depth])js_raw(o,",",1);o->first[o->depth]=0;js_nl(o);}
static void js_key(struct js *o,const char *k){js_item(o);js_str(o,k);js_s(o,o->pretty?": ":":");}
static void js_u64(struct js *o,uint64_t v){char b[24];snprintf(b,sizeof(b),"%llu",(unsigned long long)v);js_s(o,b);}
static void js_kstr(struct js *o,const char *k,const char *v){js_key(o,k);js_str(o,v);}
static void js_ku64(struct js *o,const char *k,uint64_t v){js_key(o,k);js_u64(o,v);}

/* --- the document model: every field the Rust ProfileDocument knows --- */
struct logo {
    char path[PATH_MAX_REL],sha[65],label[641],note[1025],name[256],asset[PATH_MAX_REL],ext[8];
    uint64_t bytes;
    const char *source;  /* stick-relative file to copy, or NULL for the new logo held in memory */
    char source_path[PATH_MAX_REL];
};
struct link { char kind[161],url[8193]; };
struct profile {
    char dj_id[XZ_DJ_ID_MAX],revision[129],name[641],note[8193],bundle[PATH_MAX_REL],tagline[321],accent[8],page_url[8193];
    int has_accent,has_page,nlogos,naliases,nlinks,has_tile;
    struct logo logo[MAX_LOGOS];
    char tile_path[PATH_MAX_REL],tile_sha[65];
    char alias[8][321];
    struct link link[12];
};
static int parse_logo(struct xz_djp_json *j,int t,struct logo *o){
    static const char *const names[]={"path","bytes","sha256","label","note"};int f[5];str path,sha,label,note;
    if(xz_djp_json_fields(j,t,names,5,f)||xz_djp_json_string(j,f[0],&path)||xz_djp_json_u64(j,f[1],&o->bytes)||
       xz_djp_json_string(j,f[2],&sha)||xz_djp_json_string(j,f[3],&label)||xz_djp_json_string(j,f[4],&note))return -1;
    if(copy_str(o->path,sizeof(o->path),path)||copy_str(o->sha,sizeof(o->sha),sha)||copy_str(o->label,sizeof(o->label),label)||
       copy_str(o->note,sizeof(o->note),note))return -1;
    lower(o->sha);return 0;
}
/* Parses a document xz_dj_profile_valid() accepted. */
static int parse_profile(const char *text,size_t n,struct profile *p){
    static const char *const names[]={"dj_id","revision","dj_name","note","bundle","preferred_logo","secondary_logos","xz_tile",
        "tagline","accent","aliases","links","page_url"};
    enum{ID,REVISION,NAME,NOTE,BUNDLE,PREFERRED,SECONDARY,TILE,TAGLINE,ACCENT,ALIASES,LINKS,PAGE};
    int f[13],rc=-1;str v;
    memset(p,0,sizeof(*p));
    struct xz_djp_json *j=xz_djp_json_open(text,n);
    if(!j||xz_djp_json_fields(j,0,names,13,f))goto done;
    if(xz_djp_json_string(j,f[ID],&v)||copy_str(p->dj_id,sizeof(p->dj_id),v))goto done;
    if(xz_djp_json_string(j,f[REVISION],&v)||copy_str(p->revision,sizeof(p->revision),v))goto done;
    if(xz_djp_json_string(j,f[NAME],&v)||copy_str(p->name,sizeof(p->name),v))goto done;
    if(xz_djp_json_string(j,f[NOTE],&v)||copy_str(p->note,sizeof(p->note),v))goto done;
    if(xz_djp_json_string(j,f[BUNDLE],&v)||copy_str(p->bundle,sizeof(p->bundle),v))goto done;
    if(xz_djp_json_string(j,f[TAGLINE],&v)||copy_str(p->tagline,sizeof(p->tagline),v))goto done;
    if(!xz_djp_json_null(j,f[ACCENT])){if(xz_djp_json_string(j,f[ACCENT],&v)||copy_str(p->accent,sizeof(p->accent),v))goto done;p->has_accent=1;}
    if(!xz_djp_json_null(j,f[PAGE])){if(xz_djp_json_string(j,f[PAGE],&v)||copy_str(p->page_url,sizeof(p->page_url),v))goto done;p->has_page=1;}
    if(!xz_djp_json_null(j,f[PREFERRED])&&parse_logo(j,f[PREFERRED],&p->logo[p->nlogos++]))goto done;
    int count=xz_djp_json_array(j,f[SECONDARY]);
    if(count<0||p->nlogos+count>MAX_LOGOS)goto done;
    for(int i=0,t=f[SECONDARY]+1;i<count;i++,t=xz_djp_json_next(j,t))if(parse_logo(j,t,&p->logo[p->nlogos++]))goto done;
    if(!xz_djp_json_null(j,f[TILE])){
        static const char *const tn[]={"path","sha256"};int g[2];str path,sha;
        if(xz_djp_json_fields(j,f[TILE],tn,2,g)||xz_djp_json_string(j,g[0],&path)||xz_djp_json_string(j,g[1],&sha)||
           copy_str(p->tile_path,sizeof(p->tile_path),path)||copy_str(p->tile_sha,sizeof(p->tile_sha),sha))goto done;
        lower(p->tile_sha);p->has_tile=1;
    }
    count=xz_djp_json_array(j,f[ALIASES]);
    if(count<0||count>8)goto done;
    for(int i=0,t=f[ALIASES]+1;i<count;i++,t=xz_djp_json_next(j,t))
        if(xz_djp_json_string(j,t,&v)||copy_str(p->alias[p->naliases++],sizeof(p->alias[0]),v))goto done;
    count=xz_djp_json_array(j,f[LINKS]);
    if(count<0||count>12)goto done;
    for(int i=0,t=f[LINKS]+1;i<count;i++,t=xz_djp_json_next(j,t)){
        static const char *const ln[]={"kind","url"};int g[2];str kind,url;struct link *l=&p->link[p->nlinks++];
        if(xz_djp_json_fields(j,t,ln,2,g)||xz_djp_json_string(j,g[0],&kind)||xz_djp_json_string(j,g[1],&url)||
           copy_str(l->kind,sizeof(l->kind),kind)||copy_str(l->url,sizeof(l->url),url))goto done;
    }
    rc=0;
done:
    xz_djp_json_close(j);return rc;
}
static void js_logo(struct js *o,const struct logo *l){
    js_open(o,"{");js_kstr(o,"path",l->path);js_ku64(o,"bytes",l->bytes);js_kstr(o,"sha256",l->sha);
    js_kstr(o,"label",l->label);js_kstr(o,"note",l->note);js_close(o,"}");
}
/* serde field order and skip_serializing_if rules of ProfileDocument. */
static void js_profile(struct js *o,const struct profile *p){
    js_open(o,"{");
    js_kstr(o,"schema","vj.tools.dj-profile");js_ku64(o,"version",1);js_kstr(o,"dj_id",p->dj_id);js_kstr(o,"revision",p->revision);
    js_kstr(o,"dj_name",p->name);js_kstr(o,"note",p->note);js_kstr(o,"bundle",p->bundle);
    js_key(o,"preferred_logo");if(p->nlogos)js_logo(o,&p->logo[0]);else js_s(o,"null");
    js_key(o,"secondary_logos");js_open(o,"[");for(int i=1;i<p->nlogos;i++){js_item(o);js_logo(o,&p->logo[i]);}js_close(o,"]");
    if(p->has_tile){
        js_key(o,"xz_tile");js_open(o,"{");js_kstr(o,"path",p->tile_path);js_ku64(o,"bytes",XZ_DJ_TILE_BYTES);js_kstr(o,"sha256",p->tile_sha);
        js_ku64(o,"width",XZ_DJ_TILE_SIDE);js_ku64(o,"height",XZ_DJ_TILE_SIDE);js_kstr(o,"format","rgb565le");js_close(o,"}");
    }
    if(*p->tagline)js_kstr(o,"tagline",p->tagline);
    if(p->has_accent)js_kstr(o,"accent",p->accent);
    if(p->naliases){js_key(o,"aliases");js_open(o,"[");for(int i=0;i<p->naliases;i++){js_item(o);js_str(o,p->alias[i]);}js_close(o,"]");}
    if(p->nlinks){
        js_key(o,"links");js_open(o,"[");
        for(int i=0;i<p->nlinks;i++){js_item(o);js_open(o,"{");js_kstr(o,"kind",p->link[i].kind);js_kstr(o,"url",p->link[i].url);js_close(o,"}");}
        js_close(o,"]");
    }
    if(p->has_page)js_kstr(o,"page_url",p->page_url);
    js_close(o,"}");
}

/* --- index.json --- */
struct index { int valid,count,active; char id[XZ_DJ_MAX_PROFILES+1][XZ_DJ_ID_MAX],name[XZ_DJ_MAX_PROFILES+1][641]; };
static int parse_index(const char *text,size_t n,struct index *x){
    static const char *const names[]={"active","profiles"};int f[2],rc=-1;str active;
    memset(x,0,sizeof(*x));x->active=-1;
    if(xz_dj_index_valid(text,n))return -1;
    struct xz_djp_json *j=xz_djp_json_open(text,n);
    if(!j||xz_djp_json_fields(j,0,names,2,f)||xz_djp_json_string(j,f[0],&active))goto done;
    int count=xz_djp_json_array(j,f[1]);
    if(count<1||count>XZ_DJ_MAX_PROFILES)goto done;
    for(int i=0,t=f[1]+1;i<count;i++,t=xz_djp_json_next(j,t)){
        static const char *const en[]={"dj_id","dj_name"};int g[2];str id,name;
        if(xz_djp_json_fields(j,t,en,2,g)||xz_djp_json_string(j,g[0],&id)||xz_djp_json_string(j,g[1],&name)||
           copy_str(x->id[i],sizeof(x->id[0]),id)||copy_str(x->name[i],sizeof(x->name[0]),name))goto done;
        if(eq(active,x->id[i]))x->active=i;
    }
    x->count=count;x->valid=x->active>=0;rc=x->valid?0:-1;
done:
    xz_djp_json_close(j);return rc;
}
static int find_entry(const struct index *x,const char *id){for(int i=0;i<x->count;i++)if(!strcmp(x->id[i],id))return i;return -1;}
/* ProfileIndex re-serialised as Rust write_index does; unknown fields are dropped. */
static void js_index(struct js *o,const struct index *x){
    struct tm tm;char revision[96],tail[5];utc(&tm);random_hex(tail,2);
    snprintf(revision,sizeof(revision),"%04d-%02d-%02dT%02d-%02d-%02dZ-%s",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,tail);
    js_open(o,"{");
    js_kstr(o,"schema","vjtools.dj-profile-index");js_ku64(o,"version",1);js_kstr(o,"revision",revision);js_kstr(o,"active",x->id[x->active]);
    js_key(o,"profiles");js_open(o,"[");
    for(int i=0;i<x->count;i++){js_item(o);js_open(o,"{");js_kstr(o,"dj_id",x->id[i]);js_kstr(o,"dj_name",x->name[i]);js_close(o,"}");}
    js_close(o,"]");js_close(o,"}");
}

/* --- files --- */
/* Creates each missing directory of relative below root; existing components must be real directories. */
static int ensure_dirs(const char *root,const char *relative){
    char path[4096];struct stat st;
    if(join(path,root,relative))return -1;
    for(size_t i=strlen(root)+1;;i++){
        char c=path[i];
        if(c&&c!='/')continue;
        path[i]=0;
        if(lstat(path,&st)){
            if(errno!=ENOENT||(mkdir(path,0755)&&errno!=EEXIST)||lstat(path,&st)){path[i]=c;return -1;}
        }
        path[i]=c;
        if(S_ISLNK(st.st_mode)||!S_ISDIR(st.st_mode))return -1;
        if(!c)return 0;
    }
}
static int write_new(const char *path,const void *data,size_t n){
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);
    if(fd<0)return -1;
    if(write_all(fd,data,n)||fsync(fd)){close(fd);unlink(path);return -1;}
    if(close(fd)){unlink(path);return -1;}
    return 0;
}
/* Streams a confined file, checking its size and SHA-256; with dest, also writes a fsynced copy there. */
static int stream_file(const char *root,const char *relative,uint64_t max,uint64_t bytes,const char *sha,const char *dest){
    int in,out=-1,rc=-1,created=0;uint64_t size,total=0;SHA256_CTX c;unsigned char sum[32];char got[65];
    if(xz_djp_confined_open(root,relative,max,&in,&size)!=XZ_DJP_READ_OK)return -1;
    unsigned char *buf=malloc(65536);
    if(!buf)goto done;
    if(dest){if((out=open(dest,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644))<0)goto done;created=1;}
    sha256_init(&c);
    for(;;){
        ssize_t r=read(in,buf,65536);
        if(r<0){if(errno==EINTR)continue;goto done;}
        if(!r)break;
        total+=(uint64_t)r;
        if(total>max)goto done;
        sha256_update(&c,buf,(size_t)r);
        if(out>=0&&write_all(out,buf,(size_t)r))goto done;
    }
    sha256_final(&c,sum);hex(sum,32,got);
    if(total!=bytes||strcmp(got,sha))goto done;
    if(out>=0){if(fsync(out))goto done;int closed=close(out);out=-1;if(closed)goto done;}
    rc=0;
done:
    if(out>=0)close(out);
    if(rc&&created)unlink(dest);
    close(in);free(buf);return rc;
}
/* Removes a staging directory this writer made (two levels, no links followed). */
static void remove_tree(const char *path,int depth){
    DIR *d=opendir(path);
    if(d){
        struct dirent *e;char child[4096];struct stat st;
        while((e=readdir(d))){
            if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
            if(snprintf(child,sizeof(child),"%s/%s",path,e->d_name)>=(int)sizeof(child)||lstat(child,&st))continue;
            if(S_ISDIR(st.st_mode)&&depth>0)remove_tree(child,depth-1);else unlink(child);
        }
        closedir(d);
    }
    rmdir(path);
}
/* Temporary file, fsync, rename over relative, fsync the directory. */
static int publish(const char *root,const char *relative,const char *data,size_t n){
    char path[4096],temporary[4096],directory[4096],tail[33];
    if(join(path,root,relative))return -1;
    strcpy(directory,path);*strrchr(directory,'/')=0;
    random_hex(tail,16);
    if(snprintf(temporary,sizeof(temporary),"%s/.profile-%s",directory,tail)>=(int)sizeof(temporary))return -1;
    if(write_new(temporary,data,n))return -1;
    if(rename(temporary,path)){unlink(temporary);return -1;}
    fsync_dir(directory);
    return 0;
}
static int reread_equals(const char *root,const char *relative,const char *data,size_t n,unsigned char *buf){
    size_t got;
    return xz_djp_confined_read(root,relative,MAX_JSON,buf,&got)==XZ_DJP_READ_OK&&got==n&&!memcmp(buf,data,n);
}
/* verify_bundle + verify_published: the manifest lists every logo and the tile, and the files match. */
static int verify_bundle(const char *root,const struct profile *p,unsigned char *buf){
    char relative[PATH_MAX_REL+32];size_t n;int rc=-1;
    if(snprintf(relative,sizeof(relative),"%s/manifest.json",p->bundle)>=(int)sizeof(relative))return -1;
    if(xz_djp_confined_read(root,relative,MAX_JSON,buf,&n)!=XZ_DJP_READ_OK)return -1;
    struct xz_djp_json *j=xz_djp_json_open((const char *)buf,n);
    static const char *const names[]={"schema","version","id","artist","website","created_at","files"};int f[7];
    str schema,id,artist,website,created;uint64_t version;
    if(!j||xz_djp_json_fields(j,0,names,7,f)||xz_djp_json_string(j,f[0],&schema)||xz_djp_json_u64(j,f[1],&version)||
       xz_djp_json_string(j,f[2],&id)||f[3]<0||xz_djp_json_string(j,f[3],&artist)||f[6]<0)goto done;
    if(!xz_djp_json_null(j,f[4])&&xz_djp_json_string(j,f[4],&website))goto done;
    if(xz_djp_json_string(j,f[5],&created))goto done;
    const char *last=strrchr(p->bundle,'/');
    int count=xz_djp_json_array(j,f[6]);
    if(!eq(schema,"vj.tools.dj-branding")||version!=1||count<0||count>64||!last||!eq(id,last+1))goto done;
    struct { str role,path,sha; uint64_t bytes; } file[64];
    for(int i=0,t=f[6]+1;i<count;i++,t=xz_djp_json_next(j,t)){
        static const char *const fn[]={"role","path","name","bytes","sha256"};int g[5];str name;
        if(xz_djp_json_fields(j,t,fn,5,g)||g[0]<0||g[1]<0||g[2]<0||xz_djp_json_string(j,g[0],&file[i].role)||
           xz_djp_json_string(j,g[1],&file[i].path)||xz_djp_json_string(j,g[2],&name)||xz_djp_json_u64(j,g[3],&file[i].bytes)||
           g[4]<0||xz_djp_json_string(j,g[4],&file[i].sha))goto done;
    }
    size_t bl=strlen(p->bundle);
    for(int k=0;k<=p->nlogos;k++){
        int tile=k==p->nlogos;
        if(tile&&!p->has_tile)break;
        const char *role=tile?"rendition":"logo",*path=tile?p->tile_path:p->logo[k].path,*sha=tile?p->tile_sha:p->logo[k].sha;
        uint64_t bytes=tile?XZ_DJ_TILE_BYTES:p->logo[k].bytes;int listed=0;
        for(int i=0;i<count&&!listed;i++){
            if(!eq(file[i].role,role)||file[i].bytes!=bytes||file[i].sha.n!=64||strlen(path)!=bl+1+file[i].path.n||
               memcmp(path,p->bundle,bl)||path[bl]!='/'||memcmp(path+bl+1,file[i].path.s,file[i].path.n))continue;
            listed=1;for(int h=0;h<64;h++){char a=file[i].sha.s[h];if(a>='A'&&a<='Z')a=(char)(a-'A'+'a');if(a!=sha[h])listed=0;}
        }
        if(!listed||stream_file(root,path,tile?XZ_DJ_TILE_BYTES:XZ_DJ_LOGO_MAX_BYTES,bytes,sha,NULL))goto done;
    }
    rc=0;
done:
    xz_djp_json_close(j);return rc;
}
/* The original file name the old bundle's manifest records for a logo path. */
static void manifest_name(const char *root,const char *bundle,const char *path,char out[256],unsigned char *buf){
    char relative[PATH_MAX_REL+32];size_t n,bl=strlen(bundle);
    const char *slash=strrchr(path,'/');snprintf(out,256,"%s",slash?slash+1:path);
    if(!*bundle||strncmp(path,bundle,bl)||path[bl]!='/')return;
    if(snprintf(relative,sizeof(relative),"%s/manifest.json",bundle)>=(int)sizeof(relative))return;
    if(xz_djp_confined_read(root,relative,MAX_JSON,buf,&n)!=XZ_DJP_READ_OK)return;
    struct xz_djp_json *j=xz_djp_json_open((const char *)buf,n);
    static const char *const names[]={"files"};int f[1];
    if(j&&!xz_djp_json_fields(j,0,names,1,f)){
        int count=xz_djp_json_array(j,f[0]);
        for(int i=0,t=f[0]+1;i<count;i++,t=xz_djp_json_next(j,t)){
            static const char *const fn[]={"path","name"};int g[2];str p,name;
            if(xz_djp_json_fields(j,t,fn,2,g)||xz_djp_json_string(j,g[0],&p)||xz_djp_json_string(j,g[1],&name))break;
            if(eq(p,path+bl+1)&&name.n&&!xz_djp_valid_text(name,255)){(void)copy_str(out,256,name);break;}
        }
    }
    xz_djp_json_close(j);
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

/* --- the writer --- */
struct work {
    unsigned char index_bytes[MAX_JSON+1],old_bytes[MAX_JSON+1],scratch[MAX_JSON+1];
    size_t index_n,old_n;
    int index_rc,old_rc;
    struct index index;
    struct profile old,next;
    uint16_t tile[XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE];
    unsigned char tile_bytes[XZ_DJ_TILE_BYTES];
};
static void uuid4(char out[37]){
    unsigned char b[16];char h[33];entropy(b,16);b[6]=(unsigned char)((b[6]&0x0f)|0x40);b[8]=(unsigned char)((b[8]&0x3f)|0x80);hex(b,16,h);
    snprintf(out,37,"%.8s-%.4s-%.4s-%.4s-%.12s",h,h+8,h+12,h+16,h+20);
}
/* The same bytes (or the same absence) as when the write started. */
static int unchanged(const char *root,const char *relative,int rc,const unsigned char *bytes,size_t n,unsigned char *buf){
    size_t got;int now=xz_djp_confined_read(root,relative,MAX_JSON,buf,&got);
    if(now!=rc)return 0;
    return now!=XZ_DJP_READ_OK||(got==n&&!memcmp(buf,bytes,n));
}
static const char *extension_of(const char *path){
    const char *dot=strrchr(path,'.'),*slash=strrchr(path,'/');
    if(!dot||(slash&&dot<slash)||strlen(dot+1)<1||strlen(dot+1)>5)return "bin";
    for(const char *p=dot+1;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')))return "bin";
    return dot+1;
}
/* "#rrggbb", lowercase as the Rust writer emits it. */
static int hex_color(const char *s){
    if(strlen(s)!=7||s[0]!='#')return 0;
    for(int i=1;i<7;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
enum xz_dj_write_result xz_dj_profile_write(const char *root,const struct xz_dj_draft *draft,char *out_dj_id){
    if(out_dj_id)*out_dj_id=0;
    if(!root||!*root||!draft||!bounded(draft->dj_id,sizeof(draft->dj_id))||!bounded(draft->name,sizeof(draft->name))||
       !bounded(draft->tagline,sizeof(draft->tagline))||!bounded(draft->logo,sizeof(draft->logo))||
       !bounded(draft->accent,sizeof(draft->accent)))return XZ_DJ_WRITE_INVALID;
    if(draft->accent[0]&&!hex_color(draft->accent))return XZ_DJ_WRITE_INVALID;
    if(xz_djp_valid_label(S(draft->name),160)||blank(S(draft->name))||xz_djp_valid_text(S(draft->tagline),80))return XZ_DJ_WRITE_INVALID;
    if(*draft->dj_id&&xz_djp_valid_id(S(draft->dj_id)))return XZ_DJ_WRITE_INVALID;
    if(*draft->logo&&(draft->remove_logo||draft->logo[0]!='/'))return XZ_DJ_WRITE_INVALID;
    struct stat st;
    if(stat(root,&st)||!S_ISDIR(st.st_mode))return XZ_DJ_WRITE_IO;
    struct work *w=calloc(1,sizeof(*w));
    if(!w)return XZ_DJ_WRITE_IO;
    enum xz_dj_write_result result=XZ_DJ_WRITE_IO;
    struct js doc={0},manifest={0},seed={0},list={0};
    unsigned char *logo_data=NULL;size_t logo_n=0;
    char stage[4096]="",relative[PATH_MAX_REL+64],pointer[256],previous[256];
    int pointer_written=0;

    /* 1. The index as it is now; Rust read_local_index treats a missing or invalid one as none. */
    w->index_rc=xz_djp_confined_read(root,INDEX_PATH,MAX_JSON,w->index_bytes,&w->index_n);
    if(w->index_rc==XZ_DJP_READ_IO)goto done;
    if(w->index_rc==XZ_DJP_READ_OK)(void)parse_index((const char *)w->index_bytes,w->index_n,&w->index);
    struct index *x=&w->index;
    struct profile *p=&w->next,*old=&w->old;
    /* 2. The id. */
    if(*draft->dj_id)snprintf(p->dj_id,sizeof(p->dj_id),"%s",draft->dj_id);
    else for(int tries=0;;tries++){
        char path[4096];uuid4(p->dj_id);
        snprintf(relative,sizeof(relative),HOME "/profiles/%s",p->dj_id);
        if(join(path,root,relative))goto done;
        if((!x->valid||find_entry(x,p->dj_id)<0)&&lstat(path,&st)&&errno==ENOENT)break;
        if(tries>=8)goto done;
    }
    /* 3. listed_index: refuse a 17th profile before anything is written. */
    int index_needed=1,entry=x->valid?find_entry(x,p->dj_id):-1;
    if(!x->valid){
        memset(x,0,sizeof(*x));x->count=1;x->active=0;x->valid=1;
        snprintf(x->id[0],sizeof(x->id[0]),"%s",p->dj_id);snprintf(x->name[0],sizeof(x->name[0]),"%s",draft->name);
    }else if(entry>=0){
        if(!strcmp(x->name[entry],draft->name))index_needed=0;
        else snprintf(x->name[entry],sizeof(x->name[0]),"%s",draft->name);
    }else{
        if(x->count>=XZ_DJ_MAX_PROFILES){result=XZ_DJ_WRITE_FULL;goto done;}
        snprintf(x->id[x->count],sizeof(x->id[0]),"%s",p->dj_id);snprintf(x->name[x->count],sizeof(x->name[0]),"%s",draft->name);x->count++;
    }
    /* 4. The current document, kept field by field when it is valid and belongs to this id. */
    snprintf(pointer,sizeof(pointer),HOME "/profiles/%s/profile.json",p->dj_id);
    snprintf(previous,sizeof(previous),HOME "/profiles/%s/profile.previous.json",p->dj_id);
    w->old_rc=xz_djp_confined_read(root,pointer,MAX_JSON,w->old_bytes,&w->old_n);
    if(w->old_rc==XZ_DJP_READ_IO)goto done;
    int has_old=w->old_rc==XZ_DJP_READ_OK&&!xz_dj_profile_valid((const char *)w->old_bytes,w->old_n)&&
        !parse_profile((const char *)w->old_bytes,w->old_n,old)&&!strcmp(old->dj_id,p->dj_id);
    /* 5. The next document. */
    if(has_old){*p=*old;p->has_tile=0;for(int i=0;i<p->nlogos;i++){p->logo[i].source=p->logo[i].source_path;strcpy(p->logo[i].source_path,old->logo[i].path);}}
    snprintf(p->name,sizeof(p->name),"%s",draft->name);
    snprintf(p->tagline,sizeof(p->tagline),"%s",draft->tagline);
    /* The stick colour is adopted, never over an accent the profile already has. */
    if(!p->has_accent&&draft->accent[0]){snprintf(p->accent,sizeof(p->accent),"%s",draft->accent);p->has_accent=1;}
    int tile_rendered=0,tile_copied=0;
    if(draft->remove_logo)p->nlogos=0;
    else if(*draft->logo){
        enum xz_dj_image_result image=read_image(draft->logo,&logo_data,&logo_n);
        if(image==XZ_DJ_IMAGE_OK)image=tile_from_memory(logo_data,logo_n,w->tile,NULL,NULL);
        if(image!=XZ_DJ_IMAGE_OK){result=image==XZ_DJ_IMAGE_NO_MEMORY?XZ_DJ_WRITE_IO:XZ_DJ_WRITE_IMAGE;goto done;}
        struct logo *l=&p->logo[0];
        if(!p->nlogos)p->nlogos=1;
        memset(l,0,sizeof(*l));
        strcpy(l->label,"Preferred logo");l->bytes=logo_n;sha_hex(logo_data,logo_n,l->sha);
        const char *base=strrchr(draft->logo,'/')+1;
        if(!*base||strlen(base)>=sizeof(l->name)||xz_djp_valid_text(S(base),255)){result=XZ_DJ_WRITE_IMAGE;goto done;}
        strcpy(l->name,base);
        strcpy(l->ext,logo_data[0]==0x89?"png":"jpg");
        for(int i=0;i<XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE;i++){w->tile_bytes[i*2]=(unsigned char)(w->tile[i]&255);w->tile_bytes[i*2+1]=(unsigned char)(w->tile[i]>>8);}
        sha_hex(w->tile_bytes,XZ_DJ_TILE_BYTES,p->tile_sha);p->has_tile=tile_rendered=1;
    }else if(has_old&&old->has_tile&&p->nlogos){
        strcpy(p->tile_sha,old->tile_sha);p->has_tile=tile_copied=1;
    }
    uint64_t total=0;
    for(int i=0;i<p->nlogos;i++){
        struct logo *l=&p->logo[i];char name[81];
        if(!*l->label||blank(S(l->label))){if(i)snprintf(l->label,sizeof(l->label),"Alternate %d",i);else strcpy(l->label,"Preferred logo");}
        slug(l->label,name);
        const char *ext=l->source?extension_of(l->source):l->ext;
        snprintf(l->asset,sizeof(l->asset),"assets/%03d-%s.%s",i+1,name,ext);
        if(l->source)manifest_name(root,old->bundle,l->source,l->name,w->scratch);
        total+=l->bytes;
    }
    if(total>MAX_TOTAL_LOGO_BYTES){result=XZ_DJ_WRITE_INVALID;goto done;}
    /* 6. Content revision: SHA-256 of everything the bundle and document hold except timestamps. */
    js_open(&seed,"[");
    js_item(&seed);js_str(&seed,"xz-dj-profile-write/1");
    js_item(&seed);js_str(&seed,p->dj_id);js_item(&seed);js_str(&seed,p->name);js_item(&seed);js_str(&seed,p->note);
    js_item(&seed);js_open(&seed,"[");
    for(int i=0;i<p->nlogos;i++){
        js_item(&seed);js_open(&seed,"[");js_item(&seed);js_str(&seed,p->logo[i].sha);js_item(&seed);js_str(&seed,p->logo[i].label);
        js_item(&seed);js_str(&seed,p->logo[i].note);js_item(&seed);js_str(&seed,p->logo[i].asset);js_close(&seed,"]");
    }
    js_close(&seed,"]");
    js_item(&seed);js_str(&seed,p->tagline);js_item(&seed);if(p->has_accent)js_str(&seed,p->accent);else js_s(&seed,"null");
    js_item(&seed);js_open(&seed,"[");for(int i=0;i<p->naliases;i++){js_item(&seed);js_str(&seed,p->alias[i]);}js_close(&seed,"]");
    js_item(&seed);js_open(&seed,"[");for(int i=0;i<p->nlinks;i++){js_item(&seed);js_str(&seed,p->link[i].kind);js_item(&seed);js_str(&seed,p->link[i].url);}js_close(&seed,"]");
    js_item(&seed);if(p->has_page)js_str(&seed,p->page_url);else js_s(&seed,"null");
    js_item(&seed);if(p->has_tile)js_str(&seed,p->tile_sha);else js_s(&seed,"null");
    js_close(&seed,"]");
    if(seed.err)goto done;
    sha_hex(seed.b,seed.n,p->revision);
    char bundle_id[37];
    snprintf(bundle_id,sizeof(bundle_id),"%.8s-%.4s-%.4s-%.4s-%.12s",p->revision,p->revision+8,p->revision+12,p->revision+16,p->revision+20);
    snprintf(p->bundle,sizeof(p->bundle),HOME "/profiles/%s/bundles/%s",p->dj_id,bundle_id);
    for(int i=0;i<p->nlogos;i++)
        if(path_cat(p->logo[i].path,sizeof(p->logo[i].path),p->bundle,p->logo[i].asset)){result=XZ_DJ_WRITE_INVALID;goto done;}
    if(p->has_tile&&path_cat(p->tile_path,sizeof(p->tile_path),p->bundle,"renditions/" TILE_FILE)){result=XZ_DJ_WRITE_INVALID;goto done;}
    /* 7. The document and the manifest. */
    doc.pretty=1;js_profile(&doc,p);
    if(doc.err||doc.n>MAX_JSON||xz_dj_profile_valid(doc.b,doc.n)){result=XZ_DJ_WRITE_INVALID;goto done;}
    {
        struct tm tm;char created[96];utc(&tm);
        snprintf(created,sizeof(created),"%04d-%02d-%02dT%02d:%02d:%02d+00:00",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
        manifest.pretty=1;js_open(&manifest,"{");
        js_kstr(&manifest,"schema","vj.tools.dj-branding");js_ku64(&manifest,"version",1);js_kstr(&manifest,"id",bundle_id);
        js_kstr(&manifest,"artist",p->name);js_key(&manifest,"website");js_s(&manifest,"null");js_kstr(&manifest,"created_at",created);
        js_key(&manifest,"files");js_open(&manifest,"[");
        for(int i=0;i<p->nlogos;i++){
            js_item(&manifest);js_open(&manifest,"{");js_kstr(&manifest,"role","logo");js_kstr(&manifest,"path",p->logo[i].asset);
            js_kstr(&manifest,"name",p->logo[i].name);js_ku64(&manifest,"bytes",p->logo[i].bytes);js_kstr(&manifest,"sha256",p->logo[i].sha);js_close(&manifest,"}");
        }
        if(p->has_tile){
            js_item(&manifest);js_open(&manifest,"{");js_kstr(&manifest,"role","rendition");js_kstr(&manifest,"path","renditions/" TILE_FILE);
            js_kstr(&manifest,"name",TILE_FILE);js_ku64(&manifest,"bytes",XZ_DJ_TILE_BYTES);js_kstr(&manifest,"sha256",p->tile_sha);js_close(&manifest,"}");
        }
        js_close(&manifest,"]");js_close(&manifest,"}");
        if(manifest.err||manifest.n>MAX_JSON){result=XZ_DJ_WRITE_INVALID;goto done;}
    }
    /* 8. The immutable bundle: staged next to its final name, then renamed. */
    snprintf(relative,sizeof(relative),HOME "/profiles/%s/bundles",p->dj_id);
    if(ensure_dirs(root,relative))goto done;
    char bundles[4096],bundle_path[4096];
    if(join(bundles,root,relative)||join(bundle_path,root,p->bundle))goto done;
    if(lstat(bundle_path,&st)){
        char tail[33],file[4096];
        if(errno!=ENOENT)goto done;
        random_hex(tail,16);
        if(snprintf(stage,sizeof(stage),"%s/.stage-%s",bundles,tail)>=(int)sizeof(stage)||mkdir(stage,0755)){*stage=0;goto done;}
        if(path_cat(file,sizeof(file),stage,"assets")||mkdir(file,0755))goto done;
        for(int i=0;i<p->nlogos;i++){
            const struct logo *l=&p->logo[i];
            if(path_cat(file,sizeof(file),stage,l->asset))goto done;
            if(l->source?stream_file(root,l->source,XZ_DJ_LOGO_MAX_BYTES,l->bytes,l->sha,file):write_new(file,logo_data,logo_n))goto done;
        }
        if(p->has_tile){
            if(path_cat(file,sizeof(file),stage,"renditions")||mkdir(file,0755)||path_cat(file,sizeof(file),stage,"renditions/" TILE_FILE))goto done;
            if(tile_rendered&&write_new(file,w->tile_bytes,XZ_DJ_TILE_BYTES))goto done;
            if(tile_copied&&stream_file(root,old->tile_path,XZ_DJ_TILE_BYTES,XZ_DJ_TILE_BYTES,old->tile_sha,file))goto done;
        }
        if(path_cat(file,sizeof(file),stage,"manifest.json")||write_new(file,manifest.b,manifest.n))goto done;
        fsync_dir(stage);
        if(rename(stage,bundle_path)){
            /* Another writer published the same content first: keep theirs, verified below. */
            if(lstat(bundle_path,&st)||!S_ISDIR(st.st_mode))goto done;
            remove_tree(stage,2);
        }
        *stage=0;
        fsync_dir(bundles);
    }else if(S_ISLNK(st.st_mode)||!S_ISDIR(st.st_mode))goto done;
    if(verify_bundle(root,p,w->scratch))goto done;
    /* 9. profile.previous.json, then profile.json, unless the stick changed since step 1. */
    if(!unchanged(root,INDEX_PATH,w->index_rc,w->index_bytes,w->index_n,w->scratch)||
       !unchanged(root,pointer,w->old_rc,w->old_bytes,w->old_n,w->scratch)){result=XZ_DJ_WRITE_CHANGED;goto done;}
    if(w->old_rc==XZ_DJP_READ_OK&&(w->old_n!=doc.n||memcmp(w->old_bytes,doc.b,doc.n))){
        if(publish(root,previous,(const char *)w->old_bytes,w->old_n)||!reread_equals(root,previous,(const char *)w->old_bytes,w->old_n,w->scratch))goto done;
    }
    pointer_written=1;
    if(publish(root,pointer,doc.b,doc.n)||!reread_equals(root,pointer,doc.b,doc.n,w->scratch)||verify_bundle(root,p,w->scratch))goto done;
    /* 10. index.json last. */
    if(index_needed){
        if(!unchanged(root,INDEX_PATH,w->index_rc,w->index_bytes,w->index_n,w->scratch)){result=XZ_DJ_WRITE_CHANGED;goto done;}
        list.pretty=1;js_index(&list,x);
        if(list.err||list.n>MAX_JSON||xz_dj_index_valid(list.b,list.n))goto done;
        if(publish(root,INDEX_PATH,list.b,list.n)||!reread_equals(root,INDEX_PATH,list.b,list.n,w->scratch))goto done;
    }
    pointer_written=0;
    if(out_dj_id)snprintf(out_dj_id,XZ_DJ_ID_MAX,"%s",p->dj_id);
    result=XZ_DJ_WRITE_OK;
done:
    if(*stage)remove_tree(stage,2);
    if(pointer_written){
        /* Put the previous profile.json back so the index never names a document it did not see. */
        char path[4096];
        if(w->old_rc==XZ_DJP_READ_OK)(void)publish(root,pointer,(const char *)w->old_bytes,w->old_n);
        else if(!join(path,root,pointer))unlink(path);
    }
    free(logo_data);free(doc.b);free(manifest.b);free(seed.b);free(list.b);free(w);
    return result;
}

int xz_dj_draft_load(const char *root,const char *dj_id,struct xz_dj_draft *out){
    if(!out)return -1;
    memset(out,0,sizeof(*out));
    if(!root||!*root||!dj_id||strlen(dj_id)>=XZ_DJ_ID_MAX||xz_djp_valid_id(S(dj_id)))return -1;
    struct work *w=calloc(1,sizeof(*w));char pointer[256];int rc=-1;
    if(!w)return -1;
    if(xz_djp_confined_read(root,INDEX_PATH,MAX_JSON,w->index_bytes,&w->index_n)!=XZ_DJP_READ_OK||
       parse_index((const char *)w->index_bytes,w->index_n,&w->index)||find_entry(&w->index,dj_id)<0)goto done;
    snprintf(pointer,sizeof(pointer),HOME "/profiles/%s/profile.json",dj_id);
    if(xz_djp_confined_read(root,pointer,MAX_JSON,w->old_bytes,&w->old_n)!=XZ_DJP_READ_OK||
       xz_dj_profile_valid((const char *)w->old_bytes,w->old_n)||parse_profile((const char *)w->old_bytes,w->old_n,&w->old)||
       strcmp(w->old.dj_id,dj_id))goto done;
    snprintf(out->dj_id,sizeof(out->dj_id),"%s",w->old.dj_id);
    snprintf(out->name,sizeof(out->name),"%s",w->old.name);
    snprintf(out->tagline,sizeof(out->tagline),"%s",w->old.tagline);
    rc=0;
done:
    if(rc)memset(out,0,sizeof(*out));
    free(w);return rc;
}
