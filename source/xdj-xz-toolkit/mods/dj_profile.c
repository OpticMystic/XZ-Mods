/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_profile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define JSMN_STATIC
#include "audio/vendor/jsmn/jsmn.h"
#include "audio/vendor/sha256/sha256.h"
#include "dj_profile_internal.h"
#include "dj_json.h"

/* The store written before 1.0 (contract section 0.1). Rules mirror native/rust-app/crates/dj-profiles/src/profile.rs. JSON acceptance mirrors
   serde_json 1.0.151 from_slice into the derived structs: strict grammar, no trailing bytes,
   duplicate known fields rejected, unknown fields ignored (their strings are not UTF-8 checked),
   known strings decoded with paired surrogates and valid UTF-8. */
#define MAX_JSON 65536
#define INDEX_PATH XZ_DJP_OLD_INDEX
#define LEGACY_PATH XZ_DJP_OLD_CARD

struct str { const char *s; size_t n; };
struct doc { const char *text; size_t n; jsmntok_t *tok; int count; char *arena; size_t used; int *off; size_t *len; };

static int hexv(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int digit(unsigned char c){return c>='0'&&c<='9';}
static size_t ws(const char *s,size_t n,size_t p){while(p<n&&(s[p]==' '||s[p]=='\t'||s[p]=='\n'||s[p]=='\r'))p++;return p;}
/* p at the opening quote; returns the index after the closing quote, 0 on error. */
static size_t scan_string(const char *s,size_t n,size_t p){
    for(p++;p<n;){
        unsigned char c=(unsigned char)s[p++];
        if(c=='"')return p;
        if(c<0x20)return 0;
        if(c=='\\'){
            if(p>=n)return 0;
            c=(unsigned char)s[p++];
            if(c=='u'){if(p+4>n)return 0;for(int k=0;k<4;k++)if(hexv((unsigned char)s[p++])<0)return 0;}
            else if(!c||!memchr("\"\\/bfnrt",c,8))return 0;
        }
    }return 0;
}
static size_t scan_number(const char *s,size_t n,size_t p){
    if(s[p]=='-')p++;
    if(p>=n||!digit((unsigned char)s[p]))return 0;
    if(s[p]=='0')p++;else while(p<n&&digit((unsigned char)s[p]))p++;
    if(p<n&&s[p]=='.'){p++;if(p>=n||!digit((unsigned char)s[p]))return 0;while(p<n&&digit((unsigned char)s[p]))p++;}
    if(p<n&&(s[p]=='e'||s[p]=='E')){p++;if(p<n&&(s[p]=='+'||s[p]=='-'))p++;if(p>=n||!digit((unsigned char)s[p]))return 0;while(p<n&&digit((unsigned char)s[p]))p++;}
    return p;
}
/* Strict RFC 8259 grammar with an object at the root. Iterative: depth costs heap, not stack. */
static int json_strict(const char *s,size_t n){
    enum{VALUE,AFTER,KEY} state=VALUE;
    char *stack=malloc(n+1);size_t depth=0,p=ws(s,n,0);int ok=0;
    if(!stack)return -1;
    if(p>=n||s[p]!='{')goto done;
    for(;;){
        p=ws(s,n,p);
        if(state==KEY){
            if(p>=n||s[p]!='"'||!(p=scan_string(s,n,p)))goto done;
            p=ws(s,n,p);if(p>=n||s[p]!=':')goto done;p++;state=VALUE;continue;
        }
        if(state==VALUE){
            if(p>=n)goto done;
            char c=s[p];
            if(c=='{'||c=='['){
                stack[depth++]=c;p=ws(s,n,p+1);
                if(p<n&&s[p]==(c=='{'?'}':']')){depth--;p++;state=AFTER;}
                else state=c=='{'?KEY:VALUE;
                continue;
            }
            if(c=='"'){if(!(p=scan_string(s,n,p)))goto done;}
            else if(c=='-'||digit((unsigned char)c)){if(!(p=scan_number(s,n,p)))goto done;}
            else if(n-p>=4&&!memcmp(s+p,"true",4))p+=4;
            else if(n-p>=5&&!memcmp(s+p,"false",5))p+=5;
            else if(n-p>=4&&!memcmp(s+p,"null",4))p+=4;
            else goto done;
            state=AFTER;continue;
        }
        if(!depth){ok=p==n;goto done;}
        if(p>=n)goto done;
        char top=stack[depth-1];
        if(s[p]==','){p++;state=top=='{'?KEY:VALUE;continue;}
        if(s[p]==(top=='{'?'}':']')){p++;depth--;continue;}
        goto done;
    }
done:
    free(stack);return ok?0:-1;
}
static int utf8_valid(const unsigned char *s,size_t n){
    for(size_t i=0;i<n;){
        unsigned c=s[i];size_t k;uint32_t v,min;
        if(c<0x80){i++;continue;}
        if(c>=0xc2&&c<=0xdf){k=1;v=c&0x1f;min=0x80;}
        else if(c>=0xe0&&c<=0xef){k=2;v=c&0x0f;min=0x800;}
        else if(c>=0xf0&&c<=0xf4){k=3;v=c&0x07;min=0x10000;}
        else return -1;
        if(i+k>=n)return -1;
        for(size_t j=1;j<=k;j++){if((s[i+j]&0xc0)!=0x80)return -1;v=v<<6|(s[i+j]&0x3f);}
        if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return -1;
        i+=k+1;
    }return 0;
}
/* Decoded (validate=true) string for token t, cached in the arena. -1 when not a valid string. */
static int str_get(struct doc *d,int t,struct str *o){
    if(t<0||t>=d->count||d->tok[t].type!=JSMN_STRING)return -1;
    if(d->off[t]==-2)return -1;
    if(d->off[t]>=0){o->s=d->arena+d->off[t];o->n=d->len[t];return 0;}
    {
        const char *s=d->text;int p=d->tok[t].start,end=d->tok[t].end;char *out=d->arena+d->used;size_t n=0;
        while(p<end){
            unsigned c=(unsigned char)s[p++];
            if(c!='\\'){out[n++]=(char)c;continue;}
            c=(unsigned char)s[p++];
            switch(c){
            case 'b':out[n++]='\b';continue;case 'f':out[n++]='\f';continue;case 'n':out[n++]='\n';continue;
            case 'r':out[n++]='\r';continue;case 't':out[n++]='\t';continue;
            case 'u':break;
            default:out[n++]=(char)c;continue;
            }
            uint32_t v=0;for(int k=0;k<4;k++)v=v*16+(uint32_t)hexv((unsigned char)s[p++]);
            if(v>=0xdc00&&v<=0xdfff){d->off[t]=-2;return -1;}
            if(v>=0xd800&&v<=0xdbff){
                if(p+6>end||s[p]!='\\'||s[p+1]!='u'){d->off[t]=-2;return -1;}
                uint32_t low=0;for(int k=0;k<4;k++){int h=hexv((unsigned char)s[p+2+k]);if(h<0){d->off[t]=-2;return -1;}low=low*16+(uint32_t)h;}
                if(low<0xdc00||low>0xdfff){d->off[t]=-2;return -1;}
                p+=6;v=0x10000+((v-0xd800)<<10)+(low-0xdc00);
            }
            /* \u0000 decodes to NUL. Lengths are explicit; every checked field rejects controls. */
            if(v<0x80)out[n++]=(char)v;
            else if(v<0x800){out[n++]=(char)(0xc0|v>>6);out[n++]=(char)(0x80|(v&63));}
            else if(v<0x10000){out[n++]=(char)(0xe0|v>>12);out[n++]=(char)(0x80|(v>>6&63));out[n++]=(char)(0x80|(v&63));}
            else{out[n++]=(char)(0xf0|v>>18);out[n++]=(char)(0x80|(v>>12&63));out[n++]=(char)(0x80|(v>>6&63));out[n++]=(char)(0x80|(v&63));}
        }
        if(utf8_valid((const unsigned char *)out,n)){d->off[t]=-2;return -1;}
        out[n]=0;d->off[t]=(int)d->used;d->len[t]=n;d->used+=n+1;o->s=out;o->n=n;return 0;
    }
}
static int doc_open(struct doc *d,const char *text,size_t n){
    memset(d,0,sizeof(*d));
    if(!text||n>MAX_JSON||json_strict(text,n))return -1;
    jsmn_parser parser;jsmn_init(&parser);
    int count=jsmn_parse(&parser,text,n,NULL,0);if(count<=0)return -1;
    d->tok=calloc((size_t)count,sizeof(*d->tok));d->off=malloc((size_t)count*sizeof(int));d->len=calloc((size_t)count,sizeof(size_t));
    d->arena=malloc(n+1);if(!d->tok||!d->off||!d->len||!d->arena)return -1;
    for(int i=0;i<count;i++)d->off[i]=-1;
    jsmn_init(&parser);d->count=jsmn_parse(&parser,text,n,d->tok,(unsigned)count);
    d->text=text;d->n=n;
    return d->count==count&&d->tok[0].type==JSMN_OBJECT?0:-1;
}
static void doc_close(struct doc *d){free(d->tok);free(d->off);free(d->len);free(d->arena);memset(d,0,sizeof(*d));}
static int skip(const struct doc *d,int t){int end=d->tok[t].end;while(++t<d->count&&d->tok[t].start<end){}return t;}
static int eq(struct str a,const char *b){return a.n==strlen(b)&&!memcmp(a.s,b,a.n);}
static int same(struct str a,struct str b){return a.n==b.n&&!memcmp(a.s,b.s,a.n);}
/* A derived struct: every key is decoded; a known key twice is serde's "duplicate field". */
static int fields(struct doc *d,int object,const char *const *names,int k,int *out){
    for(int i=0;i<k;i++)out[i]=-1;
    if(object<0||object>=d->count||d->tok[object].type!=JSMN_OBJECT)return -1;
    int t=object+1;
    for(int f=0;f<d->tok[object].size;f++){
        struct str key;if(str_get(d,t,&key))return -1;
        int value=t+1;if(value>=d->count)return -1;
        for(int i=0;i<k;i++)if(eq(key,names[i])){if(out[i]>=0)return -1;out[i]=value;}
        t=skip(d,value);
    }return 0;
}
static int is_null(const struct doc *d,int t){return t>=0&&d->tok[t].type==JSMN_PRIMITIVE&&d->text[d->tok[t].start]=='n';}
static int s_req(struct doc *d,int t,struct str *o){return str_get(d,t,o)?-1:0;}
static int s_def(struct doc *d,int t,struct str *o){if(t<0){o->s="";o->n=0;return 0;}return s_req(d,t,o);}
static int s_opt(struct doc *d,int t,struct str *o,int *has){*has=0;o->s="";o->n=0;if(t<0||is_null(d,t))return 0;*has=1;return s_req(d,t,o);}
/* A u32/u64 field: serde_json hands negatives, -0, fractions and exponents over as floats. */
static int u_req(const struct doc *d,int t,uint64_t max,uint64_t *o){
    if(t<0||d->tok[t].type!=JSMN_PRIMITIVE)return -1;
    const char *s=d->text+d->tok[t].start;int n=d->tok[t].end-d->tok[t].start;uint64_t v=0;
    if(n<1)return -1;
    for(int i=0;i<n;i++){if(!digit((unsigned char)s[i]))return -1;unsigned dd=(unsigned)(s[i]-'0');if(v>(UINT64_MAX-dd)/10)return -1;v=v*10+dd;}
    if(v>max)return -1;
    *o=v;return 0;
}
/* Vec<T> with #[serde(default)]: absent is empty, null is an error. Returns element count or -1. */
static int v_def(const struct doc *d,int t){if(t<0)return 0;if(d->tok[t].type!=JSMN_ARRAY)return -1;return d->tok[t].size;}

/* --- Unicode helpers on decoded, valid UTF-8 --- */
static uint32_t cp(const char *s,size_t *i){
    const unsigned char *u=(const unsigned char *)s;unsigned c=u[*i];
    if(c<0x80){(*i)++;return c;}
    size_t k=c>=0xf0?3:c>=0xe0?2:1;uint32_t v=c&(k==3?7:k==2?15:31);
    for(size_t j=1;j<=k;j++)v=v<<6|(u[*i+j]&63);
    *i+=k+1;return v;
}
static int is_control(uint32_t c){return c<0x20||(c>=0x7f&&c<=0x9f);}
static int is_space(uint32_t c){return (c>=9&&c<=13)||c==0x20||c==0x85||c==0xa0||c==0x1680||(c>=0x2000&&c<=0x200a)||
    c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
static size_t chars(struct str v){size_t n=0;for(size_t i=0;i<v.n;n++)cp(v.s,&i);return n;}
static int any_control(struct str v){for(size_t i=0;i<v.n;)if(is_control(cp(v.s,&i)))return 1;return 0;}

/* --- profile.rs validators --- */
static int v_id(struct str v){
    if(!v.n||v.n>128)return -1;
    for(size_t i=0;i<v.n;i++){unsigned char c=(unsigned char)v.s[i];if(!(digit(c)||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_'))return -1;}
    return 0;
}
static int v_label(struct str v,size_t max){
    int blank=1;size_t n=0;
    for(size_t i=0;i<v.n;n++){uint32_t c=cp(v.s,&i);if(is_control(c))return -1;if(!is_space(c))blank=0;}
    return blank||n>max?-1:0;
}
static int v_https(struct str v){
    if(v.n<8||memcmp(v.s,"https://",8))return -1;
    size_t host=0;while(8+host<v.n&&v.s[8+host]!='/'&&v.s[8+host]!='?'&&v.s[8+host]!='#')host++;
    if(!host||chars(v)>2048)return -1;
    for(size_t i=0;i<v.n;){uint32_t c=cp(v.s,&i);if(is_space(c)||is_control(c))return -1;}
    return 0;
}
static int v_note(struct str v,size_t max){
    if(v.n>max)return -1;
    for(size_t i=0;i<v.n;){uint32_t c=cp(v.s,&i);if(is_control(c)&&c!='\n'&&c!='\r'&&c!='\t')return -1;}
    return 0;
}
static int v_digest(struct str v){if(v.n!=64)return -1;for(size_t i=0;i<64;i++)if(hexv((unsigned char)v.s[i])<0)return -1;return 0;}
#define MAX_PARTS 520
struct parts { int count; struct str part[MAX_PARTS]; };
static int relative_parts(struct str v,struct parts *o){
    if(!v.n||v.n>1024||v.s[0]=='/'||memchr(v.s,'\\',v.n)||memchr(v.s,':',v.n)||memchr(v.s,0,v.n))return -1;
    o->count=0;
    for(size_t start=0;;){
        size_t end=start;while(end<v.n&&v.s[end]!='/')end++;
        struct str p={v.s+start,end-start};
        if(!p.n||eq(p,".")||eq(p,"..")||p.n>255||p.s[p.n-1]==' '||p.s[p.n-1]=='.'||o->count>=MAX_PARTS)return -1;
        for(size_t i=0;i<p.n;){uint32_t c=cp(p.s,&i);if(is_control(c)||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return -1;}
        o->part[o->count++]=p;
        if(end==v.n)break;
        start=end+1;
    }return 0;
}
static int validate_bundle(struct str bundle,struct str dj_id){
    struct parts p;if(relative_parts(bundle,&p))return -1;
    if(p.count==4&&eq(p.part[0],"VJ.Tools")&&eq(p.part[1],"Branding")&&eq(p.part[2],"bundles"))return v_id(p.part[3]);
    if(p.count==6&&eq(p.part[0],"DJPROFILE")&&eq(p.part[1],"VJTOOLS")&&eq(p.part[2],"profiles")&&same(p.part[3],dj_id)&&eq(p.part[4],"bundles"))
        return v_id(p.part[5]);
    return -1;
}
/* The bundle holding path as <bundle>/<directory>/<file>; the joined parts equal the raw prefix. */
static int bundle_member(struct str path,const char *directory,struct str dj_id,struct str *bundle){
    struct parts p;if(relative_parts(path,&p)||p.count<3||!eq(p.part[p.count-2],directory))return -1;
    bundle->s=path.s;bundle->n=(size_t)(p.part[p.count-2].s-path.s)-1;
    return validate_bundle(*bundle,dj_id);
}

struct logo { struct str path,sha,label,note; uint64_t bytes; };
static int logo_parse(struct doc *d,int t,struct logo *o){
    static const char *const names[]={"path","bytes","sha256","label","note"};int f[5];
    if(fields(d,t,names,5,f)||s_req(d,f[0],&o->path)||u_req(d,f[1],UINT64_MAX,&o->bytes)||s_req(d,f[2],&o->sha)||
       s_def(d,f[3],&o->label)||s_def(d,f[4],&o->note))return -1;
    return 0;
}
struct parsed_profile { struct str dj_id,name,tagline,accent,page_url,tile_path,tile_sha,logo_path; int has_accent,has_page,has_tile; };
static int profile_parse(struct doc *d,struct parsed_profile *o){
    static const char *const names[]={"schema","version","dj_id","revision","dj_name","note","bundle","preferred_logo",
        "secondary_logos","xz_tile","tagline","accent","aliases","links","page_url"};
    enum{SCHEMA,VERSION,ID,REVISION,NAME,NOTE,BUNDLE,PREFERRED,SECONDARY,TILE,TAGLINE,ACCENT,ALIASES,LINKS,PAGE};
    int f[15];struct str schema,revision,note,bundle;uint64_t version;
    memset(o,0,sizeof(*o));
    if(fields(d,0,names,15,f)||s_req(d,f[SCHEMA],&schema)||u_req(d,f[VERSION],UINT32_MAX,&version)||s_req(d,f[ID],&o->dj_id)||
       s_req(d,f[REVISION],&revision)||s_req(d,f[NAME],&o->name)||s_def(d,f[NOTE],&note)||s_def(d,f[BUNDLE],&bundle)||
       s_def(d,f[TAGLINE],&o->tagline)||s_opt(d,f[ACCENT],&o->accent,&o->has_accent)||s_opt(d,f[PAGE],&o->page_url,&o->has_page))return -1;
    /* Logos: preferred first, then the secondaries, as in validate_document. */
    struct logo logos[8];int nlogos=0,has_preferred=f[PREFERRED]>=0&&!is_null(d,f[PREFERRED]);
    if(has_preferred&&logo_parse(d,f[PREFERRED],&logos[nlogos++]))return -1;
    if(has_preferred)o->logo_path=logos[0].path;
    int secondaries=v_def(d,f[SECONDARY]);if(secondaries<0)return -1;
    if(nlogos+secondaries>8)return -1;
    for(int i=0,t=f[SECONDARY]+1;i<secondaries;i++,t=skip(d,t))if(logo_parse(d,t,&logos[nlogos++]))return -1;
    /* xz_tile: Option<RenditionReference>. */
    struct str tile_format;uint64_t tile_bytes=0,tile_w=0,tile_h=0;
    if(f[TILE]>=0&&!is_null(d,f[TILE])){
        static const char *const tn[]={"path","bytes","sha256","width","height","format"};int g[6];
        if(fields(d,f[TILE],tn,6,g)||s_req(d,g[0],&o->tile_path)||u_req(d,g[1],UINT64_MAX,&tile_bytes)||s_req(d,g[2],&o->tile_sha)||
           u_req(d,g[3],UINT32_MAX,&tile_w)||u_req(d,g[4],UINT32_MAX,&tile_h)||s_req(d,g[5],&tile_format))return -1;
        o->has_tile=1;
    }
    int aliases=v_def(d,f[ALIASES]),links=v_def(d,f[LINKS]);if(aliases<0||links<0)return -1;
    struct str alias[8];
    if(aliases>8)return -1;
    for(int i=0,t=f[ALIASES]+1;i<aliases;i++,t=skip(d,t))if(s_req(d,t,&alias[i]))return -1;
    struct str kind[12],url[12];
    if(links>12)return -1;
    for(int i=0,t=f[LINKS]+1;i<links;i++,t=skip(d,t)){
        static const char *const ln[]={"kind","url"};int g[2];
        if(fields(d,t,ln,2,g)||s_req(d,g[0],&kind[i])||s_req(d,g[1],&url[i]))return -1;
    }
    /* validate_document */
    if(!eq(schema,"vj.tools.dj-profile")||version!=1||v_id(o->dj_id)||v_id(revision)||v_label(o->name,160)||v_note(note,8*1024))return -1;
    /* validate_details */
    if(chars(o->tagline)>80||any_control(o->tagline))return -1;
    if(o->has_accent){
        if(o->accent.n!=7||o->accent.s[0]!='#')return -1;
        for(int i=1;i<7;i++)if(hexv((unsigned char)o->accent.s[i])<0)return -1;
    }
    for(int i=0;i<aliases;i++)if(v_label(alias[i],80))return -1;
    for(int i=0;i<links;i++)if(v_label(kind[i],40)||v_https(url[i]))return -1;
    if(o->has_page&&v_https(o->page_url))return -1;
    if(!has_preferred&&secondaries)return -1;
    /* document_bundle */
    struct str home={0,0};int has_bundle=0;
    if(bundle.n){if(validate_bundle(bundle,o->dj_id))return -1;home=bundle;has_bundle=1;}
    else if(has_preferred){if(bundle_member(logos[0].path,"assets",o->dj_id,&home))return -1;has_bundle=1;}
    uint64_t total=0;
    for(int i=0;i<nlogos;i++){
        struct str member;
        if(bundle_member(logos[i].path,"assets",o->dj_id,&member)||!has_bundle||!same(member,home))return -1;
        for(int j=0;j<i;j++)if(same(logos[j].path,logos[i].path))return -1;
        if(!logos[i].bytes||logos[i].bytes>8u*1024*1024||v_digest(logos[i].sha))return -1;
        if(chars(logos[i].label)>160||any_control(logos[i].label)||v_note(logos[i].note,1024))return -1;
        total+=logos[i].bytes;
    }
    if(total>24u*1024*1024)return -1;
    if(o->has_tile){
        struct str member;
        if(!has_bundle||bundle_member(o->tile_path,"renditions",o->dj_id,&member)||!same(member,home))return -1;
        if(tile_w!=XZ_DJ_TILE_SIDE||tile_h!=XZ_DJ_TILE_SIDE||!eq(tile_format,"rgb565le")||tile_bytes!=XZ_DJ_TILE_BYTES||v_digest(o->tile_sha))return -1;
    }
    return 0;
}
struct parsed_index { int count,active; struct str id[XZ_DJ_MAX_PROFILES],name[XZ_DJ_MAX_PROFILES],active_id; int active_token,revision_token; };
static int index_parse(struct doc *d,struct parsed_index *o){
    static const char *const names[]={"schema","version","revision","active","profiles"};int f[5];
    struct str schema,revision;uint64_t version;
    memset(o,0,sizeof(*o));o->active=-1;
    if(fields(d,0,names,5,f)||s_req(d,f[0],&schema)||u_req(d,f[1],UINT32_MAX,&version)||s_req(d,f[2],&revision)||
       s_req(d,f[3],&o->active_id)||f[4]<0||d->tok[f[4]].type!=JSMN_ARRAY)return -1;
    int count=d->tok[f[4]].size;
    if(!eq(schema,"vjtools.dj-profile-index")||version!=1||v_id(revision)||count<1||count>XZ_DJ_MAX_PROFILES)return -1;
    for(int i=0,t=f[4]+1;i<count;i++,t=skip(d,t)){
        static const char *const en[]={"dj_id","dj_name"};int g[2];
        if(fields(d,t,en,2,g)||s_req(d,g[0],&o->id[i])||s_req(d,g[1],&o->name[i]))return -1;
    }
    for(int i=0;i<count;i++){
        if(v_id(o->id[i])||v_label(o->name[i],160))return -1;
        for(int j=0;j<i;j++)if(same(o->id[j],o->id[i]))return -1;
        if(same(o->id[i],o->active_id))o->active=i;
    }
    if(o->active<0)return -1;
    o->count=count;o->active_token=f[3];o->revision_token=f[2];return 0;
}
int xz_dj_index_valid(const char *json,size_t n){
    struct doc d;struct parsed_index p;int rc=doc_open(&d,json,n)||index_parse(&d,&p)?-1:0;
    doc_close(&d);return rc;
}
int xz_dj_profile_valid(const char *json,size_t n){
    struct doc d;struct parsed_profile p;int rc=doc_open(&d,json,n)||profile_parse(&d,&p)?-1:0;
    doc_close(&d);return rc;
}

/* --- Files: fixed relative paths below root, no links, bounded regular files --- */
enum { READ_OK=0, READ_MISSING=1, READ_INVALID=2, READ_IO=-1 };
static int join(char out[4096],const char *root,const char *relative){
    size_t a=strlen(root),b=strlen(relative);if(a+1+b>=4096)return -1;
    memcpy(out,root,a);out[a]='/';memcpy(out+a+1,relative,b+1);return 0;
}
/* Opens root/relative read-only when no component is a link, every ancestor is a directory and the
   file is a regular file of at most max bytes. READ_OK sets *fd (caller closes) and *size. */
static int confined_open(const char *root,const char *relative,uint64_t max,int *fd,uint64_t *size){
    char path[4096];struct stat st;*fd=-1;*size=0;
    if(join(path,root,relative))return READ_INVALID;
    size_t base=strlen(root)+1;
    for(size_t i=base;;i++){
        char c=path[i];
        if(c!='/'&&c)continue;
        path[i]=0;int failed=lstat(path,&st);path[i]=c;
        if(failed)return errno==ENOENT||errno==ENOTDIR?READ_MISSING:READ_IO;
        if(S_ISLNK(st.st_mode))return READ_INVALID;
        if(c&&!S_ISDIR(st.st_mode))return READ_INVALID;
        if(!c)break;
    }
    if(!S_ISREG(st.st_mode)||(uint64_t)st.st_size>max)return READ_INVALID;
    int f=open(path,O_RDONLY|O_NOFOLLOW);
    if(f<0)return errno==ENOENT?READ_MISSING:errno==ELOOP?READ_INVALID:READ_IO;
    struct stat opened;
    if(fstat(f,&opened)||!S_ISREG(opened.st_mode)||opened.st_dev!=st.st_dev||opened.st_ino!=st.st_ino){close(f);return READ_INVALID;}
    *fd=f;*size=(uint64_t)opened.st_size;return READ_OK;
}
static int confined_read(const char *root,const char *relative,size_t max,unsigned char *buf,size_t *len){
    int fd;uint64_t size;*len=0;
    int rc=confined_open(root,relative,max,&fd,&size);
    if(rc!=READ_OK)return rc;
    size_t total=0;
    for(;;){
        ssize_t r=read(fd,buf+total,max+1-total);
        if(r<0){if(errno==EINTR)continue;close(fd);return READ_IO;}
        if(!r)break;
        total+=(size_t)r;
        if(total>max){close(fd);return READ_INVALID;}
    }
    close(fd);*len=total;return READ_OK;
}
static void display_copy(char *out,size_t cap,struct str v){
    size_t n=v.n<cap?v.n:cap-1;
    if(n<v.n)while(n&&((unsigned char)v.s[n]&0xc0)==0x80)n--;
    if(n)memcpy(out,v.s,n);
    out[n]=0;
}
static void hash(const void *data,size_t n,unsigned char out[32]){SHA256_CTX c;sha256_init(&c);sha256_update(&c,data,n);sha256_final(&c,out);}
static int digest_matches(const unsigned char sum[32],struct str hexstr){
    if(hexstr.n!=64)return 0;
    for(unsigned i=0;i<32;i++)if(sum[i]!=(unsigned)(hexv((unsigned char)hexstr.s[i*2])*16+hexv((unsigned char)hexstr.s[i*2+1])))return 0;
    return 1;
}
/* out = a + "/" + b; -1 (out "") when it does not fit. */
static int cat(char *out,size_t cap,const char *a,const char *b){
    size_t x=strlen(a),y=strlen(b);
    if(x+1+y>=cap){out[0]=0;return -1;}
    memmove(out,a,x);out[x]='/';memcpy(out+x+1,b,y+1);return 0;
}
static void thumb_from_tile(const unsigned char *bytes,uint16_t *thumb){
    const int scale=XZ_DJ_TILE_SIDE/XZ_DJ_THUMB;
    for(int y=0;y<XZ_DJ_THUMB;y++)for(int x=0;x<XZ_DJ_THUMB;x++){
        unsigned r=0,g=0,b=0;
        for(int dy=0;dy<scale;dy++)for(int dx=0;dx<scale;dx++){
            size_t at=((size_t)(y*scale+dy)*XZ_DJ_TILE_SIDE+(size_t)(x*scale+dx))*2;
            unsigned v=bytes[at]|(unsigned)bytes[at+1]<<8,r5=v>>11,g6=v>>5&63,b5=v&31;
            r+=r5<<3|r5>>2;g+=g6<<2|g6>>4;b+=b5<<3|b5>>2;
        }
        unsigned cells=(unsigned)(scale*scale);r=(r+cells/2)/cells;g=(g+cells/2)/cells;b=(b+cells/2)/cells;
        thumb[y*XZ_DJ_THUMB+x]=(uint16_t)((r>>3)<<11|(g>>2)<<5|b>>3);
    }
}
static void load_thumb(const char *root,const struct parsed_profile *p,struct xz_dj_profile *out,unsigned char *buf){
    char relative[1100];size_t n;unsigned char sum[32];
    if(!p->has_tile||p->tile_path.n>=sizeof(relative)||p->tile_path.n>=sizeof(out->tile_path))return;
    memcpy(relative,p->tile_path.s,p->tile_path.n);relative[p->tile_path.n]=0;
    if(confined_read(root,relative,XZ_DJ_TILE_BYTES,buf,&n)!=READ_OK||n!=XZ_DJ_TILE_BYTES)return;
    hash(buf,n,sum);if(!digest_matches(sum,p->tile_sha))return;
    thumb_from_tile(buf,out->thumb);
    out->has_thumb=1;memcpy(out->tile_path,relative,p->tile_path.n+1);
}
static void fill(struct xz_dj_profile *out,const struct parsed_profile *p){
    out->kind=XZ_DJ_OLD;
    display_copy(out->dj_id,sizeof(out->dj_id),p->dj_id);
    display_copy(out->name,sizeof(out->name),p->name);
    display_copy(out->tagline,sizeof(out->tagline),p->tagline);
    if(p->has_page&&p->page_url.n<sizeof(out->page_url))display_copy(out->page_url,sizeof(out->page_url),p->page_url);
    if(p->has_accent){
        uint32_t v=0;for(int i=1;i<7;i++)v=v*16+(uint32_t)hexv((unsigned char)p->accent.s[i]);
        out->accent=v;out->has_accent=1;
    }
    if(p->logo_path.n<sizeof(out->logo_path))display_copy(out->logo_path,sizeof(out->logo_path),p->logo_path);
}
/* Reads one JSON file into a parsed profile. READ_OK only when it parsed and validated. */
static int read_profile(const char *root,const char *relative,unsigned char *buf,struct doc *d,struct parsed_profile *p){
    size_t n;memset(d,0,sizeof(*d));int rc=confined_read(root,relative,MAX_JSON,buf,&n);
    if(rc!=READ_OK)return rc;
    if(doc_open(d,(const char *)buf,n)||profile_parse(d,p)){doc_close(d);return READ_INVALID;}
    return READ_OK;
}
static int has_id(const char *const *ids,int n,const char *id){for(int i=0;i<n;i++)if(!strcmp(ids[i],id))return 1;return 0;}
/* Contract 0.1 step 2: the VJTOOLS profiles in index order, then the VJ.Tools/Branding card, skipping ids
   a profile folder already holds. buf holds MAX_JSON + 1 + XZ_DJ_TILE_BYTES. 0, or -1 on an I/O error. */
static int read_old(const char *root,struct xz_djp_scan *s,const char *const *ids,int nids,unsigned char *buf){
    size_t n;struct doc index;struct parsed_index list;
    unsigned char *text=malloc(MAX_JSON+1),*tile=buf+MAX_JSON+1;
    s->old=calloc(XZ_DJ_MAX_PROFILES+1,sizeof(*s->old));
    if(!s->old||!text){free(text);return -1;}
    int rc=confined_read(root,INDEX_PATH,MAX_JSON,buf,&n);
    if(rc==READ_IO){free(text);return -1;}
    memset(&index,0,sizeof(index));
    if(rc==READ_OK&&!doc_open(&index,(const char *)buf,n)&&!index_parse(&index,&list)){
        s->old_index_valid=1;display_copy(s->old_active,sizeof(s->old_active),list.active_id);
        for(int i=0;i<list.count;i++){
            struct xz_dj_profile *card=&s->old[s->olds];char relative[256];struct doc d;struct parsed_profile p;
            memset(card,0,sizeof(*card));card->kind=XZ_DJ_OLD;
            display_copy(card->dj_id,sizeof(card->dj_id),list.id[i]);
            if(has_id(ids,nids,card->dj_id))continue;
            snprintf(relative,sizeof(relative),"DJPROFILE/VJTOOLS/profiles/%s/profile.json",card->dj_id);
            rc=read_profile(root,relative,text,&d,&p);
            if(rc==READ_IO){doc_close(&d);doc_close(&index);free(text);return -1;}
            if(rc==READ_OK&&same(p.dj_id,list.id[i])){
                fill(card,&p);load_thumb(root,&p,card,tile);snprintf(card->document_path,sizeof(card->document_path),"%s",relative);
            }else{card->damaged=1;display_copy(card->name,sizeof(card->name),list.name[i]);}
            doc_close(&d);s->olds++;
        }
    }
    doc_close(&index);
    struct doc d;struct parsed_profile p;
    rc=read_profile(root,LEGACY_PATH,text,&d,&p);
    if(rc==READ_IO){doc_close(&d);free(text);return -1;}
    if(rc==READ_OK){
        struct xz_dj_profile *card=&s->old[s->olds];int dup=0;
        memset(card,0,sizeof(*card));fill(card,&p);
        for(int i=0;i<s->olds;i++)if(!strcmp(s->old[i].dj_id,card->dj_id))dup=1;
        if(!dup&&!has_id(ids,nids,card->dj_id)){
            load_thumb(root,&p,card,tile);snprintf(card->document_path,sizeof(card->document_path),"%s",LEGACY_PATH);s->olds++;
        }
    }
    doc_close(&d);free(text);
    return 0;
}

/* --- DJ Profile 1.0, as tools/check_djprofile.py judges a stick --- */
typedef struct xz_dj_jv jv;
static int is_object(const jv *v){return v&&v->type==XZ_DJ_JOBJ;}
static int is_text(const jv *v,size_t min,size_t max){return v&&v->type==XZ_DJ_JSTR&&v->n>=min&&v->n<=max;}
/* Every byte in set, or in lo..hi when set is NULL. */
static int all_of(const jv *v,size_t min,size_t max,const char *set,int lo,int hi){
    if(!is_text(v,min,max))return 0;
    for(size_t i=0;i<v->n;i++){unsigned char c=(unsigned char)v->s[i];if(set?!(c&&strchr(set,c)):(c<lo||c>hi))return 0;}
    return 1;
}
#define ID_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
#define HEX_CHARS "0123456789abcdefABCDEF"
static int valid_id(const jv *v){return all_of(v,1,128,ID_CHARS,0,0);}
static int valid_name(const jv *v){return v&&v->type==XZ_DJ_JSTR&&!xz_dj_valid_name(v->s,v->n,160);}
static int valid_colour(const jv *v){return is_text(v,7,7)&&v->s[0]=='#'&&strspn(v->s+1,HEX_CHARS)==6;}
/* Section 5.1 paths: at most one subfolder, no segment starting with '.', no leading '/', drive letter or backslash. */
static int safe_relative(const jv *v){
    if(!is_text(v,1,512)||memchr(v->s,0,v->n)||v->s[0]=='/'||memchr(v->s,'\\',v->n))return 0;
    if(v->n>=2&&v->s[1]==':'&&((v->s[0]|32)>='a'&&(v->s[0]|32)<='z'))return 0;
    const char *slash=memchr(v->s,'/',v->n);
    if(!slash)return v->s[0]!='.';
    size_t rest=v->n-(size_t)(slash-v->s)-1;
    return slash>v->s&&v->s[0]!='.'&&rest&&slash[1]!='.'&&!memchr(slash+1,'/',rest);
}
enum { ENTRY_OK=0, ENTRY_BAD=-1, ENTRY_IO=-2 };
/* _read_entry: {file, bytes, sha256} names a file of exactly that size and digest. keep, which holds
   cap + 1 bytes, receives the content when it fits; *got its size. */
static int read_entry(const char *root,const char *dir,const jv *entry,uint64_t limit,unsigned char *keep,size_t cap,size_t *got){
    const jv *file=xz_dj_json_get(entry,"file"),*size=xz_dj_json_get(entry,"bytes"),*digest=xz_dj_json_get(entry,"sha256");
    int64_t bytes;char relative[2048];unsigned char sum[32];
    if(got)*got=0;
    if(!is_object(entry)||!safe_relative(file)||xz_dj_json_whole(size,&bytes)||bytes<0||(uint64_t)bytes>limit||!all_of(digest,64,64,HEX_CHARS,0,0))return ENTRY_BAD;
    if(snprintf(relative,sizeof(relative),"%s/%s",dir,file->s)>=(int)sizeof(relative))return ENTRY_BAD;
    if(keep&&(uint64_t)bytes<=cap){
        size_t n;int rc=confined_read(root,relative,(size_t)bytes,keep,&n);
        if(rc==READ_IO)return ENTRY_IO;
        if(rc!=READ_OK||n!=(size_t)bytes)return ENTRY_BAD;
        hash(keep,n,sum);
    }else{
        int fd;uint64_t actual,total=0;unsigned char chunk[16384];SHA256_CTX c;
        int rc=confined_open(root,relative,(uint64_t)bytes,&fd,&actual);
        if(rc==READ_IO)return ENTRY_IO;
        if(rc!=READ_OK)return ENTRY_BAD;
        sha256_init(&c);
        for(;;){
            ssize_t r=read(fd,chunk,sizeof(chunk));
            if(r<0){if(errno==EINTR)continue;close(fd);return ENTRY_IO;}
            if(!r)break;
            sha256_update(&c,chunk,(size_t)r);total+=(uint64_t)r;
        }
        close(fd);sha256_final(&c,sum);
        if(total!=(uint64_t)bytes)return ENTRY_BAD;
    }
    struct str want={digest->s,digest->n};
    if(!digest_matches(sum,want))return ENTRY_BAD;
    if(got)*got=(size_t)bytes;
    return ENTRY_OK;
}
static int marker_ok(const jv *v,const jv *id){
    static const char head[]="djprofile:1:";const size_t h=sizeof(head)-1;
    if(!is_text(v,h+1+1+12,h+128+1+12)||memcmp(v->s,head,h))return 0;
    const char *rest=v->s+h;size_t n=v->n-h,idn=n-13;
    if(rest[idn]!=':')return 0;
    for(size_t i=idn+1;i<n;i++)if(!((rest[i]>='0'&&rest[i]<='9')||(rest[i]>='a'&&rest[i]<='f')))return 0;
    for(size_t i=0;i<idn;i++)if(!strchr(ID_CHARS,rest[i]))return 0;
    return id&&id->type==XZ_DJ_JSTR&&id->n==idn&&!memcmp(id->s,rest,idn);
}
static int raw_bytes_per_pixel(const jv *format){
    static const struct{const char *name;int bytes;}raw[]={{"rgb565le",2},{"rgb565be",2},{"rgb888",3},{"rgba8888",4},{"bgra8888",4}};
    for(size_t i=0;i<sizeof(raw)/sizeof(raw[0]);i++)if(xz_dj_json_is(format,raw[i].name))return raw[i].bytes;
    return 0;
}
int xz_djp_check_document(const char *root,const char *dir,const jv *d,char logo[1024],char tile_file[1024],unsigned char *tile,unsigned char *buf){
    logo[0]=0;if(tile_file)tile_file[0]=0;
    if(!is_object(d)||!xz_dj_json_is(xz_dj_json_get(d,"format"),"djprofile/1"))return ENTRY_BAD;
    const jv *id=xz_dj_json_get(d,"id"),*v;
    if(!valid_id(id)||!all_of(xz_dj_json_get(d,"revision"),1,64,NULL,0x21,0x7e)||!valid_name(xz_dj_json_get(d,"name")))return ENTRY_BAD;
    if((v=xz_dj_json_get(d,"tagline"))&&!is_text(v,0,240))return ENTRY_BAD;
    if((v=xz_dj_json_get(d,"accent"))&&!valid_colour(v))return ENTRY_BAD;
    if((v=xz_dj_json_get(d,"aliases"))){
        if(v->type!=XZ_DJ_JARR||v->count>8)return ENTRY_BAD;
        for(const jv *a=v->child;a;a=a->next)if(!valid_name(a))return ENTRY_BAD;
    }
    if((v=xz_dj_json_get(d,"links"))){
        if(v->type!=XZ_DJ_JARR||v->count>12)return ENTRY_BAD;
        for(const jv *l=v->child;l;l=l->next){
            const jv *url=xz_dj_json_get(l,"url");
            if(!is_object(l)||!is_text(xz_dj_json_get(l,"kind"),1,40)||!is_text(url,0,2048)||url->n<8||memcmp(url->s,"https://",8))return ENTRY_BAD;
        }
    }
    if((v=xz_dj_json_get(d,"note"))&&!is_text(v,0,8192))return ENTRY_BAD;
    if((v=xz_dj_json_get(d,"ext"))){
        if(v->type!=XZ_DJ_JOBJ)return ENTRY_BAD;
        for(const jv *e=v->child;e;e=e->next)if(!is_object(e))return ENTRY_BAD;
    }
    if((v=xz_dj_json_get(d,"logos"))){
        if(v->type!=XZ_DJ_JARR||v->count<1||v->count>8)return ENTRY_BAD;
        const jv *primary=NULL;int primaries=0;uint64_t total=0;
        for(const jv *e=v->child;e;e=e->next)if(is_object(e)&&xz_dj_json_is(xz_dj_json_get(e,"role"),"primary")){if(!primaries)primary=e;primaries++;}
        if(primaries!=1)return ENTRY_BAD;
        const jv *file=xz_dj_json_get(primary,"file");
        if(!file||file->type!=XZ_DJ_JSTR||memchr(file->s,0,file->n)||!(xz_dj_fold_equal(file->s,"logo.png")||xz_dj_fold_equal(file->s,"logo.jpg")))return ENTRY_BAD;
        for(const jv *e=v->child;e;e=e->next){
            if(is_object(e)){
                const jv *role=xz_dj_json_get(e,"role"),*back=xz_dj_json_get(e,"background"),*label=xz_dj_json_get(e,"label");
                if(!xz_dj_json_is(role,"primary")&&!xz_dj_json_is(role,"alternate"))return ENTRY_BAD;
                if(back&&!xz_dj_json_is(back,"dark")&&!xz_dj_json_is(back,"light")&&!xz_dj_json_is(back,"any"))return ENTRY_BAD;
                if(label&&!is_text(label,0,160))return ENTRY_BAD;
                if(xz_dj_json_get(e,"note")&&!is_text(xz_dj_json_get(e,"note"),0,1024))return ENTRY_BAD;
            }
            size_t got;int rc=read_entry(root,dir,e,XZ_DJ_LOGO_MAX_BYTES,buf,XZ_DJ_LOGO_MAX_BYTES,&got);
            if(rc)return rc;
            if(xz_dj_image_usable(buf,got,NULL))return ENTRY_BAD;
            total+=got;
        }
        if(total>24u*1024u*1024u)return ENTRY_BAD;
        snprintf(logo,1024,"%s",file->s);
    }
    if((v=xz_dj_json_get(d,"renditions"))){
        if(v->type!=XZ_DJ_JARR||v->count>8)return ENTRY_BAD;
        for(const jv *r=v->child;r;r=r->next){
            const jv *format=xz_dj_json_get(r,"format"),*target=xz_dj_json_get(r,"for");
            int64_t width,height,bytes;size_t got=0;
            int wanted=tile&&tile_file&&!tile_file[0]&&xz_dj_json_is(format,"rgb565le")&&xz_dj_json_is(target,"xdj-xz");
            /* Section 5.4: a broken rendition is ignored; the document still counts. */
            int rc=read_entry(root,dir,r,4u*1024u*1024u,wanted?tile:NULL,XZ_DJ_TILE_BYTES,&got);
            if(rc==ENTRY_IO)return rc;
            if(rc)continue;
            if(xz_dj_json_whole(xz_dj_json_get(r,"width"),&width)||xz_dj_json_whole(xz_dj_json_get(r,"height"),&height)||width<1||width>4096||
               height<1||height>4096||!is_text(format,1,40)||!is_text(target,1,80))continue;
            int per=raw_bytes_per_pixel(format);
            xz_dj_json_whole(xz_dj_json_get(r,"bytes"),&bytes);
            if(per&&(uint64_t)bytes!=(uint64_t)width*(uint64_t)height*(uint64_t)per)continue;
            if(wanted&&width==XZ_DJ_TILE_SIDE&&height==XZ_DJ_TILE_SIDE&&got==XZ_DJ_TILE_BYTES)snprintf(tile_file,1024,"%s",xz_dj_json_get(r,"file")->s);
        }
    }
    if((v=xz_dj_json_get(d,"track"))){
        if(!is_object(v)||!marker_ok(xz_dj_json_get(v,"marker"),id))return ENTRY_BAD;
        if(xz_dj_json_get(v,"file")){int rc=read_entry(root,dir,v,64u*1024u*1024u,NULL,0,NULL);if(rc)return rc;}
    }
    return ENTRY_OK;
}
static void folder_id(const char *name,char out[XZ_DJ_ID_MAX]){
    unsigned char sum[32];static const char digits[]="0123456789abcdef";
    hash(name,strlen(name),sum);
    size_t n=strlen(XZ_DJ_FOLDER_ID);memcpy(out,XZ_DJ_FOLDER_ID,n);
    for(int i=0;i<8;i++){out[n+i*2]=digits[sum[i]>>4];out[n+i*2+1]=digits[sum[i]&15];}
    out[n+16]=0;
}
static void copy_text(char *out,size_t cap,const jv *v){
    if(!v||v->type!=XZ_DJ_JSTR){out[0]=0;return;}
    struct str s={v->s,v->n};display_copy(out,cap,s);
}
/* f->view at level 1, from a valid document. */
static void show_document(struct xz_djp_folder *f,const jv *d,const char *file,const char *logo,const char *tile_file,const unsigned char *tile){
    struct xz_dj_profile *p=&f->view;const jv *v;
    p->kind=XZ_DJ_LEVEL1;
    copy_text(p->dj_id,sizeof(p->dj_id),xz_dj_json_get(d,"id"));
    copy_text(p->name,sizeof(p->name),xz_dj_json_get(d,"name"));
    copy_text(p->tagline,sizeof(p->tagline),xz_dj_json_get(d,"tagline"));
    if((v=xz_dj_json_get(d,"accent"))){uint32_t c=0;for(int i=1;i<7;i++)c=c*16+(uint32_t)hexv((unsigned char)v->s[i]);p->accent=c;p->has_accent=1;}
    if((v=xz_dj_json_get(d,"links")))for(const jv *l=v->child;l;l=l->next){
        const jv *url=xz_dj_json_get(l,"url");
        if(xz_dj_json_is(xz_dj_json_get(l,"kind"),"page")){if(url->n<sizeof(p->page_url))copy_text(p->page_url,sizeof(p->page_url),url);break;}
    }
    cat(p->logo_path,sizeof(p->logo_path),f->c.dir,logo);
    cat(p->document_path,sizeof(p->document_path),f->c.dir,file);
    if(tile_file[0]&&!cat(p->tile_path,sizeof(p->tile_path),f->c.dir,tile_file)){thumb_from_tile(tile,p->thumb);p->has_thumb=1;}
}
/* check_djprofile.py check() for one folder: profile.json, then profile.previous.json, then level 0. */
static int judge(const char *root,struct xz_djp_folder *f,unsigned char *buf){
    static const char *const names[2]={"profile.json","profile.previous.json"};
    unsigned char tile[XZ_DJ_TILE_BYTES+1];
    f->level=-1;memset(&f->view,0,sizeof(f->view));
    memcpy(f->view.dir,f->c.dir,sizeof(f->view.dir));
    for(int k=0;k<2;k++){
        char file[XZ_DJ_NAME_MAX],relative[1024];size_t n;
        if(xz_dj_fixed(root,f->c.dir,names[k],file))continue;
        if(!k)snprintf(f->json,sizeof(f->json),"%s",file);
        if(cat(relative,sizeof(relative),f->c.dir,file))continue;
        int rc=confined_read(root,relative,MAX_JSON,buf,&n);
        if(rc==READ_IO)return -1;
        jv *d;struct xz_dj_json *j=rc==READ_OK?xz_dj_json_parse((const char *)buf,n,XZ_DJ_JSON_PROFILE,&d):NULL;
        if(!j)continue;
        char logo[1024],tile_file[1024];
        rc=xz_djp_check_document(root,f->c.dir,d,logo,tile_file,tile,buf);
        if(rc==ENTRY_IO){xz_dj_json_free(j);return -1;}
        if(rc!=ENTRY_OK){xz_dj_json_free(j);continue;}
        if(!k)f->json_valid=1;
        int found=logo[0]?0:xz_dj_json_get(d,"logos")?1:xz_dj_level0_logo(root,f->c.dir,logo,buf);
        if(found<0){xz_dj_json_free(j);return -1;}
        if(!found){f->level=1;snprintf(f->used,sizeof(f->used),"%s",file);show_document(f,d,file,logo,tile_file,tile);}
        xz_dj_json_free(j);
        break;
    }
    if(f->level<0){
        char logo[XZ_DJ_NAME_MAX];int found=xz_dj_level0_logo(root,f->c.dir,logo,buf);
        if(found<0)return -1;
        if(!found){
            struct xz_dj_profile *p=&f->view;f->level=0;p->kind=XZ_DJ_LEVEL0;
            snprintf(p->name,sizeof(p->name),"%s",f->c.name);folder_id(f->c.name,p->dj_id);
            cat(p->logo_path,sizeof(p->logo_path),f->c.dir,logo);
        }
    }
    return 0;
}
static int key_equal(const char *a,size_t an,const char *b){
    char x[XZ_DJ_NAME_KEY_CAP(640)],y[XZ_DJ_NAME_KEY_CAP(640)];size_t bn=strlen(b);
    if(an>640||bn>640)return 0;
    return xz_dj_name_key(a,an,x,sizeof(x))>=0&&xz_dj_name_key(b,bn,y,sizeof(y))>=0&&!strcmp(x,y);
}
const char *xz_dj_profile_ref(const struct xz_dj_profile *p){
    if(p->kind!=XZ_DJ_LEVEL0)return p->dj_id;
    const char *slash=strchr(p->dir,'/');return slash?slash+1:p->dir;
}
/* A profile reference resolved as section 6 says: ids first, then folder name keys. -1 when nothing matches. */
static int resolve(const struct xz_dj_stick *list,const char *ref,size_t n){
    for(int i=0;i<list->count;i++)if(list->profile[i].kind!=XZ_DJ_LEVEL0&&strlen(list->profile[i].dj_id)==n&&!memcmp(list->profile[i].dj_id,ref,n))return i;
    for(int i=0;i<list->count;i++){
        const char *slash=strchr(list->profile[i].dir,'/');
        if(slash&&key_equal(ref,n,slash+1))return i;
    }
    return -1;
}
void xz_djp_scan_free(struct xz_djp_scan *s){free(s->folder);free(s->old);s->folder=NULL;s->old=NULL;}
static int scan(const char *root,struct xz_djp_scan *s,unsigned char *buf,struct xz_dj_json **sj){
    jv *stick=NULL;
    xz_dj_tops(root,s->tops);
    /* The stick file sits in the first top folder there is. */
    const char *top=s->tops[0][0]?s->tops[0]:s->tops[1];
    char name[XZ_DJ_NAME_MAX];
    if(top[0]&&!xz_dj_fixed(root,top,XZ_DJP_STICK_FILE,name)){
        char relative[1024];size_t n;
        cat(relative,sizeof(relative),top,name);s->stick_file_present=1;
        int rc=confined_read(root,relative,XZ_DJP_MAX_STICK_JSON,buf,&n);
        if(rc==READ_IO)return -1;
        if(rc==READ_OK&&(*sj=xz_dj_json_parse((const char *)buf,n,XZ_DJ_JSON_PROFILE,&stick))&&
           is_object(stick)&&xz_dj_json_is(xz_dj_json_get(stick,"format"),"djprofile-stick/1"))memcpy(s->stick_file,relative,strlen(relative)+1);
        else{xz_dj_json_free(*sj);*sj=NULL;stick=NULL;}
    }
    int count=0;
    struct xz_dj_candidate *c=xz_dj_candidates(root,&count);
    if(count&&!(s->folder=calloc((size_t)count,sizeof(*s->folder)))){free(c);return -1;}
    s->folders=count;
    for(int i=0;i<count;i++)s->folder[i].c=c[i],s->folder[i].c.name=strchr(s->folder[i].c.dir,'/')+1;
    free(c);
    for(int i=0;i<count;i++)if(judge(root,&s->folder[i],buf))return -1;
    /* Profile order: the stick file's profiles, then the rest; a repeated id counts once; 16 at most. */
    int *ordered=malloc(sizeof(int)*((size_t)count+1)),n=0;char *taken=calloc((size_t)count+1,1);
    if(!ordered||!taken){free(ordered);free(taken);return -1;}
    const jv *entries=xz_dj_json_get(stick,"profiles");
    if(entries&&entries->type==XZ_DJ_JARR&&entries->count<=XZ_DJ_MAX_PROFILES)for(const jv *e=entries->child;e;e=e->next){
        const jv *folder=xz_dj_json_get(e,"folder"),*id=xz_dj_json_get(e,"id");
        if(!is_object(e)||!folder||folder->type!=XZ_DJ_JSTR||xz_dj_valid_folder_name(folder->s,folder->n))continue;
        for(int i=0;i<count;i++){
            const struct xz_djp_folder *f=&s->folder[i];
            if(f->level<0)continue;
            int match=id?f->level==1&&id->type==XZ_DJ_JSTR&&strlen(f->view.dj_id)==id->n&&!memcmp(f->view.dj_id,id->s,id->n):key_equal(folder->s,folder->n,f->c.name);
            if(!match)continue;
            if(!taken[i]){taken[i]=1;ordered[n++]=i;}
            break;
        }
    }
    for(int i=0;i<count;i++)if(s->folder[i].level>=0&&!taken[i])ordered[n++]=i;
    free(taken);
    for(int i=0;i<n&&s->shown<XZ_DJ_MAX_PROFILES;i++){
        const struct xz_djp_folder *f=&s->folder[ordered[i]];int dup=0;
        for(int k=0;k<s->shown&&f->level==1;k++){const struct xz_djp_folder *g=&s->folder[s->order[k]];if(g->level==1&&!strcmp(g->view.dj_id,f->view.dj_id))dup=1;}
        if(!dup)s->order[s->shown++]=ordered[i];
    }
    free(ordered);
    /* The old store, then the merged list (contract 0.1 steps 2 to 5). */
    const char **ids=malloc(sizeof(char *)*((size_t)count+1));int nids=0;
    if(!ids)return -1;
    for(int i=0;i<count;i++)if(s->folder[i].level==1)ids[nids++]=s->folder[i].view.dj_id;
    int rc=read_old(root,s,ids,nids,buf);
    free(ids);
    if(rc)return -1;
    struct xz_dj_stick *list=&s->list;
    for(int i=0;i<s->shown;i++){
        const struct xz_djp_folder *f=&s->folder[s->order[i]];int dropped=0;
        for(int k=0;k<s->olds&&f->level==0;k++)if(key_equal(f->c.name,strlen(f->c.name),s->old[k].name))dropped=1;
        if(!dropped)list->profile[list->count++]=f->view;
    }
    for(int k=0;k<s->olds&&list->count<XZ_DJ_MAX_PROFILES;k++)list->profile[list->count++]=s->old[k];
    const jv *active=xz_dj_json_get(stick,"active");
    if(active&&active->type==XZ_DJ_JSTR)list->active=resolve(list,active->s,active->n);
    if(list->active<0&&s->old_index_valid)for(int i=0;i<list->count&&list->active<0;i++)if(list->profile[i].kind!=XZ_DJ_LEVEL0&&!strcmp(list->profile[i].dj_id,s->old_active))list->active=i;
    if(list->active<0&&list->count)list->active=0;
    /* The 1.0 active profile alone, as check_djprofile.py reports it. */
    struct xz_dj_stick *ten=malloc(sizeof(*ten));
    if(!ten)return -1;
    ten->count=0;
    for(int i=0;i<s->shown;i++)ten->profile[ten->count++]=s->folder[s->order[i]].view;
    s->active=active&&active->type==XZ_DJ_JSTR?resolve(ten,active->s,active->n):-1;
    if(s->active<0&&s->shown)s->active=0;
    free(ten);
    return 0;
}
int xz_djp_scan(const char *root,struct xz_djp_scan *s,unsigned char *buf){
    memset(s,0,sizeof(*s));s->active=-1;s->list.active=-1;
    if(!root||!*root||!buf)return -1;
    struct xz_dj_json *sj=NULL;
    int rc=scan(root,s,buf,&sj);
    xz_dj_json_free(sj);
    if(rc){xz_djp_scan_free(s);memset(&s->list,0,sizeof(s->list));s->list.active=-1;}
    return rc;
}
int xz_dj_stick_read(const char *root,struct xz_dj_stick *out){
    if(!out)return -1;
    memset(out,0,sizeof(*out));out->active=-1;
    unsigned char *buf=malloc(XZ_DJ_LOGO_MAX_BYTES+MAX_JSON+2);
    struct xz_djp_scan *s=calloc(1,sizeof(*s));int result=-1;
    if(buf&&s&&!xz_djp_scan(root,s,buf)){*out=s->list;result=out->count?0:1;xz_djp_scan_free(s);}
    free(buf);free(s);
    return result;
}

/* Contract 0.3 step 5: the old index.json's "active" rewritten in place for an older runtime. */
static void revision(char out[64]){
    static unsigned counter;unsigned char sum[32];char seed[128];
    time_t now=time(NULL);struct tm tm;
    if(!gmtime_r(&now,&tm))memset(&tm,0,sizeof(tm));
    int k=snprintf(seed,sizeof(seed),"%ld-%ld-%u-%ld-%p",(long)now,(long)getpid(),++counter,(long)clock(),(void *)seed);
    hash(seed,k>0&&(size_t)k<sizeof(seed)?(size_t)k:strlen(seed),sum);
    snprintf(out,64,"%04d-%02d-%02dT%02d-%02d-%02dZ-%02x%02x",tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec,sum[0],sum[1]);
}
static int write_all(int fd,const char *data,size_t n){
    while(n){ssize_t w=write(fd,data,n);if(w<0){if(errno==EINTR)continue;return -1;}data+=w;n-=(size_t)w;}
    return 0;
}
int xz_djp_old_index_set_active(const char *root,const char *dj_id){
    if(!root||!*root||!dj_id)return -1;
    struct str wanted={dj_id,strlen(dj_id)};
    if(v_id(wanted))return -1;
    char *old=malloc(MAX_JSON+1),*next=malloc(MAX_JSON+1),*check=malloc(MAX_JSON+1);
    struct doc d={0},verify={0};struct parsed_index list,again;int result=-1;size_t n;
    char path[4096],temporary[4096],directory[4096];
    if(!old||!next||!check)goto done;
    if(confined_read(root,INDEX_PATH,MAX_JSON,(unsigned char *)old,&n)!=READ_OK)goto done;
    if(doc_open(&d,old,n)||index_parse(&d,&list))goto done;
    int listed=0;for(int i=0;i<list.count;i++)if(same(list.id[i],wanted))listed=1;
    if(!listed){result=2;goto done;}
    if(same(list.active_id,wanted)){result=1;goto done;}
    char stamp[64];revision(stamp);
    /* Splice the raw bytes between the quotes of "active" and "revision", later offset first. */
    struct edit { size_t start,end; const char *with; } edit[2]={
        {(size_t)d.tok[list.active_token].start,(size_t)d.tok[list.active_token].end,dj_id},
        {(size_t)d.tok[list.revision_token].start,(size_t)d.tok[list.revision_token].end,stamp}};
    if(edit[0].start>edit[1].start){struct edit swap=edit[0];edit[0]=edit[1];edit[1]=swap;}
    size_t size=n;for(int i=0;i<2;i++)size=size-(edit[i].end-edit[i].start)+strlen(edit[i].with);
    if(size>MAX_JSON)goto done;
    size_t at=0,from=0;
    for(int i=0;i<2;i++){
        memcpy(next+at,old+from,edit[i].start-from);at+=edit[i].start-from;
        size_t w=strlen(edit[i].with);memcpy(next+at,edit[i].with,w);at+=w;from=edit[i].end;
    }
    memcpy(next+at,old+from,n-from);at+=n-from;
    if(doc_open(&verify,next,at)||index_parse(&verify,&again)||!same(again.active_id,wanted)||again.count!=list.count)goto done;
    struct str rev;if(str_get(&verify,again.revision_token,&rev)||!eq(rev,stamp))goto done;
    if(join(path,root,INDEX_PATH)||join(directory,root,"DJPROFILE/VJTOOLS")||join(temporary,root,INDEX_PATH ".tmp"))goto done;
    int fd=open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0644);
    if(fd<0)goto done;
    if(write_all(fd,next,at)||fsync(fd)){close(fd);unlink(temporary);goto done;}
    if(close(fd)||rename(temporary,path)){unlink(temporary);goto done;}
    int dir=open(directory,O_RDONLY);
    /* FAT and some USB stacks reject fsync on a directory; the rename has already happened. */
    if(dir>=0){(void)fsync(dir);close(dir);}
    size_t got;
    if(confined_read(root,INDEX_PATH,MAX_JSON,(unsigned char *)check,&got)!=READ_OK||got!=at||memcmp(check,next,at))goto done;
    result=0;
done:
    doc_close(&d);doc_close(&verify);free(old);free(next);free(check);
    return result;
}

/* --- Shared with dj_profile_write.c and dj_folders.c through dj_profile_internal.h --- */
int xz_djp_utf8_valid(struct xz_djp_str v){return utf8_valid((const unsigned char *)v.s,v.n);}
int xz_djp_confined_open(const char *root,const char *relative,uint64_t max,int *fd,uint64_t *size){return confined_open(root,relative,max,fd,size);}
int xz_djp_confined_read(const char *root,const char *relative,size_t max,unsigned char *buf,size_t *len){return confined_read(root,relative,max,buf,len);}
int xz_djp_resolve(const struct xz_dj_stick *list,const char *ref,size_t n){return resolve(list,ref,n);}
