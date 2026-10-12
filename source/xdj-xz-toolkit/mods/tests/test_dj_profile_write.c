/* SPDX-License-Identifier: MIT */
/* Writer and logo decoder cases. Run by tests/verify_dj_profile_write.py, which prepares <work>:
   sticks/{created,fixture-edited,logo-removed,full,errors,conflict}, images/ and list/. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_profile_write.h"
#include "audio/vendor/sha256/sha256.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#x);failures++;}}while(0)

static char *slurp(const char *path,size_t *n){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    size_t cap=1<<20;char *data=malloc(cap+1);*n=data?fread(data,1,cap+1,f):0;fclose(f);
    if(!data||*n>cap){free(data);return NULL;}data[*n]=0;return data;
}
static const char *at(const char *root,const char *relative){
    static char buffers[8][4096];static int next;char *out=buffers[next++&7];
    snprintf(out,4096,"%s/%s",root,relative);return out;
}
static int exists(const char *path){struct stat st;return !lstat(path,&st);}
static uint16_t rgb565(unsigned r,unsigned g,unsigned b){return (uint16_t)((r>>3)<<11|(g>>2)<<5|b>>3);}
static int solid(const uint16_t *px,int n,uint16_t v){for(int i=0;i<n;i++)if(px[i]!=v)return 0;return 1;}
static int near(uint16_t v,unsigned r,unsigned g,unsigned b,int tolerance){
    int r5=v>>11,g6=v>>5&63,b5=v&31;
    return abs((r5<<3|r5>>2)-(int)r)<=tolerance&&abs((g6<<2|g6>>4)-(int)g)<=tolerance&&abs((b5<<3|b5>>2)-(int)b)<=tolerance;
}
static int count_entries(const char *path){
    DIR *d=opendir(path);int n=0;struct dirent *e;if(!d)return -1;
    while((e=readdir(d)))if(e->d_name[0]!='.')n++;
    closedir(d);return n;
}
static struct xz_dj_draft draft(const char *id,const char *name,const char *tagline,const char *logo,int remove){
    struct xz_dj_draft d;memset(&d,0,sizeof(d));
    snprintf(d.dj_id,sizeof(d.dj_id),"%s",id);snprintf(d.name,sizeof(d.name),"%s",name);
    snprintf(d.tagline,sizeof(d.tagline),"%s",tagline);snprintf(d.logo,sizeof(d.logo),"%s",logo);d.remove_logo=remove;return d;
}

/* SHA-256 over every path, type and file content below root, in sorted order. */
struct entries { char **path; int n,cap; };
static void collect(const char *root,const char *relative,struct entries *e){
    char full[4096];snprintf(full,sizeof(full),"%s%s%s",root,*relative?"/":"",relative);
    DIR *d=opendir(full);struct dirent *x;if(!d)return;
    while((x=readdir(d))){
        if(!strcmp(x->d_name,".")||!strcmp(x->d_name,".."))continue;
        char child[4096];snprintf(child,sizeof(child),"%s%s%s",relative,*relative?"/":"",x->d_name);
        if(e->n==e->cap){e->cap=e->cap?e->cap*2:64;e->path=realloc(e->path,sizeof(char *)*(size_t)e->cap);}
        e->path[e->n++]=strdup(child);
        struct stat st;if(!lstat(at(root,child),&st)&&S_ISDIR(st.st_mode))collect(root,child,e);
    }
    closedir(d);
}
static int by_name(const void *a,const void *b){return strcmp(*(char *const *)a,*(char *const *)b);}
static void tree_hash(const char *root,unsigned char out[32]){
    struct entries e={0};SHA256_CTX c;collect(root,"",&e);
    if(e.n)qsort(e.path,(size_t)e.n,sizeof(char *),by_name);
    sha256_init(&c);
    for(int i=0;i<e.n;i++){
        struct stat st;size_t n=0;lstat(at(root,e.path[i]),&st);
        sha256_update(&c,(const unsigned char *)e.path[i],strlen(e.path[i])+1);
        sha256_update(&c,(const unsigned char *)(S_ISDIR(st.st_mode)?"d":"f"),1);
        if(S_ISREG(st.st_mode)){char *data=slurp(at(root,e.path[i]),&n);if(data)sha256_update(&c,(unsigned char *)data,n);free(data);}
        free(e.path[i]);
    }
    free(e.path);sha256_final(&c,out);
}

#define KAI_JSON "DJPROFILE/VJTOOLS/profiles/djp_kai/profile.json"
#define KAI_PREVIOUS "DJPROFILE/VJTOOLS/profiles/djp_kai/profile.previous.json"
#define KAI_OLD_BUNDLE "DJPROFILE/VJTOOLS/profiles/djp_kai/bundles/0b7c1f2e-0000-4000-8000-00000000c0de"
#define LENA_JSON "DJPROFILE/VJTOOLS/profiles/djp_lena/profile.json"
#define INDEX_JSON "DJPROFILE/VJTOOLS/index.json"
#define MUSIC "PIONEER/rekordbox/export.pdb"
static char images[4096];
static const char *image(const char *name){return at(images,name);}
static struct xz_dj_stick s;

static int is_uuid4(const char *id){
    if(strlen(id)!=36||id[8]!='-'||id[13]!='-'||id[18]!='-'||id[23]!='-'||id[14]!='4'||!strchr("89ab",id[19]))return 0;
    for(int i=0;i<36;i++)if(id[i]!='-'&&!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return 0;
    return 1;
}
static char *profile_json(const char *root,const char *id,size_t *n){
    char relative[256];snprintf(relative,sizeof(relative),"DJPROFILE/VJTOOLS/profiles/%s/profile.json",id);return slurp(at(root,relative),n);
}

/* 1. An empty stick. */
static void created(const char *root){
    char id[XZ_DJ_ID_MAX]="x";size_t n,m;
    static const char name[]="Ada \"Countess\" \\ Lov\xc3\xa9lace";
    struct xz_dj_draft d=draft("",name,"Analytical\t? no: tab refused","",0);
    CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_INVALID);CHECK(!*id);CHECK(count_entries(root)==0);
    d=draft("",name,"Analytical engines","",0);snprintf(d.logo,sizeof(d.logo),"%s",image("green.png"));
    CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);CHECK(is_uuid4(id));
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==1);CHECK(s.active==0);CHECK(!s.legacy);
    CHECK(!strcmp(s.profile[0].dj_id,id));CHECK(!strcmp(s.profile[0].name,name));CHECK(!strcmp(s.profile[0].tagline,"Analytical engines"));
    CHECK(s.profile[0].has_thumb&&solid(s.profile[0].thumb,XZ_DJ_THUMB*XZ_DJ_THUMB,rgb565(30,220,60)));
    char *index=slurp(at(root,INDEX_JSON),&n);CHECK(index&&!xz_dj_index_valid(index,n));
    char *doc=profile_json(root,id,&m);CHECK(doc&&!xz_dj_profile_valid(doc,m));
    if(doc){
        CHECK(strstr(doc,"\"dj_name\": \"Ada \\\"Countess\\\" \\\\ Lov\xc3\xa9lace\""));
        CHECK(strstr(doc,"/assets/001-Preferred_logo.png\""));CHECK(strstr(doc,"\"label\": \"Preferred logo\""));
        CHECK(strstr(doc,"\"format\": \"rgb565le\""));CHECK(!strstr(doc,"accent"));CHECK(!strstr(doc,"aliases"));
    }
    char relative[256];snprintf(relative,sizeof(relative),"DJPROFILE/VJTOOLS/profiles/%s/profile.previous.json",id);
    CHECK(!exists(at(root,relative)));
    snprintf(relative,sizeof(relative),"DJPROFILE/VJTOOLS/profiles/%s/bundles",id);CHECK(count_entries(at(root,relative))==1);
    /* The same content again: same bundle, verified, nothing new; draft_load round trip. */
    struct xz_dj_draft back;CHECK(xz_dj_draft_load(root,id,&back)==0);
    CHECK(!strcmp(back.dj_id,id)&&!strcmp(back.name,name)&&!strcmp(back.tagline,"Analytical engines")&&!*back.logo&&!back.remove_logo);
    char again[XZ_DJ_ID_MAX];CHECK(xz_dj_profile_write(root,&back,again)==XZ_DJ_WRITE_OK);CHECK(!strcmp(again,id));
    CHECK(count_entries(at(root,relative))==1);
    snprintf(relative,sizeof(relative),"DJPROFILE/VJTOOLS/profiles/%s/profile.previous.json",id);CHECK(!exists(at(root,relative)));
    size_t k;char *same=profile_json(root,id,&k);CHECK(same&&doc&&k==m&&!memcmp(same,doc,m));free(same);
    char *index2=slurp(at(root,INDEX_JSON),&k);CHECK(index2&&index&&k==n&&!memcmp(index2,index,n));free(index2);
    CHECK(xz_dj_draft_load(root,"nobody",&back)==-1);CHECK(xz_dj_draft_load(root,"../x",&back)==-1);
    /* 160 two-byte characters fit the name; a second profile leaves active alone. */
    char wide[641]={0};for(int i=0;i<160;i++){wide[i*2]=(char)0xc3;wide[i*2+1]=(char)0xa9;}
    d=draft("",wide,"","",0);CHECK(xz_dj_profile_write(root,&d,again)==XZ_DJ_WRITE_OK);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2);CHECK(s.active==0);CHECK(!strcmp(s.profile[1].name,wide));CHECK(!s.profile[1].has_thumb);
    free(index);free(doc);
    printf("created: %s\n",failures?"FAIL":"ok");
}

/* 2-4 and 10. The shared fixture: add, rename keeping the logo, replace the logo with a JPEG. */
static void fixture_edited(const char *root){
    size_t n,m;int before=failures;
    char *music=slurp(at(root,MUSIC),&n),*kai=slurp(at(root,KAI_JSON),&m);CHECK(music&&kai);
    char mira[XZ_DJ_ID_MAX];struct xz_dj_draft d=draft("","Mira","","",0);snprintf(d.logo,sizeof(d.logo),"%s",image("red_blue.png"));
    CHECK(xz_dj_profile_write(root,&d,mira)==XZ_DJ_WRITE_OK);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==3);CHECK(s.active==1);
    CHECK(!strcmp(s.profile[2].dj_id,mira));CHECK(!strcmp(s.profile[2].name,"Mira"));CHECK(s.profile[2].has_thumb);
    /* Rename DJ Kai, logo kept. */
    struct xz_dj_draft k;CHECK(xz_dj_draft_load(root,"djp_kai",&k)==0);
    CHECK(!strcmp(k.name,"DJ Kai")&&!strcmp(k.tagline,"Deep tech, Berlin"));
    snprintf(k.name,sizeof(k.name),"DJ Kai Moreno");snprintf(k.tagline,sizeof(k.tagline),"Techno, Berlin");
    char id[XZ_DJ_ID_MAX];CHECK(xz_dj_profile_write(root,&k,id)==XZ_DJ_WRITE_OK);CHECK(!strcmp(id,"djp_kai"));
    size_t q;char *doc=slurp(at(root,KAI_JSON),&q),*previous=slurp(at(root,KAI_PREVIOUS),&n);
    CHECK(doc&&!xz_dj_profile_valid(doc,q));CHECK(previous&&kai&&n==m&&!memcmp(previous,kai,m));
    if(doc){
        static const char *const kept[]={"\"accent\": \"#ff2a6d\"","\"KAI\"","\"Kai Moreno\"","\"url\": \"https://example.com/kai\"",
            "\"kind\": \"media-kit\"","\"url\": \"https://example.com/kai/press.zip\"","\"page_url\": \"https://vj.tools/dj#p1.fixture\"",
            "\"dj_name\": \"DJ Kai Moreno\"","\"tagline\": \"Techno, Berlin\"","/assets/001-Preferred_logo.png\"",
            "\"sha256\": \"fcfa43c4535e2da5368c911731a9b468c15d1344f34115eddc242bb437a8904b\"",
            "\"sha256\": \"d8f6e024917b25fbf77719fa7f8cd6a0105c421c97e260d302a1cf1a9465b1c0\""};
        for(size_t i=0;i<sizeof(kept)/sizeof(kept[0]);i++)if(!strstr(doc,kept[i])){fprintf(stderr,"missing %s\n",kept[i]);failures++;}
        CHECK(!strstr(doc,"0b7c1f2e"));
        const char *b=strstr(doc,"\"bundle\": \"");char manifest[512];
        if(b){b+=11;snprintf(manifest,sizeof(manifest),"%.*s/manifest.json",(int)(strchr(b,'"')-b),b);
            char *text=slurp(at(root,manifest),&n);CHECK(text&&strstr(text,"\"name\": \"logo.png\"")&&strstr(text,"\"website\": null")&&strstr(text,"\"artist\": \"DJ Kai Moreno\""));free(text);}
    }
    CHECK(exists(at(root,KAI_OLD_BUNDLE "/manifest.json")));
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==3);CHECK(s.active==1);
    CHECK(!strcmp(s.profile[0].name,"DJ Kai Moreno"));CHECK(s.profile[0].has_thumb&&solid(s.profile[0].thumb,XZ_DJ_THUMB*XZ_DJ_THUMB,rgb565(255,42,109)));
    char *index=slurp(at(root,INDEX_JSON),&n);
    CHECK(index&&strstr(index,"\"dj_name\": \"DJ Kai Moreno\"")&&strstr(index,"\"active\": \"djp_lena\"")&&!strstr(index,"2026-10-08T15-00-00Z-0001"));
    free(index);free(doc);free(previous);
    /* Lena gets a JPEG logo: a new tile from it. */
    size_t ln;char *lena=slurp(at(root,LENA_JSON),&ln);
    d=draft("djp_lena","Lena B","","",0);snprintf(d.logo,sizeof(d.logo),"%s",image("logo.jpg"));
    CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    doc=slurp(at(root,LENA_JSON),&q);CHECK(doc&&strstr(doc,"/assets/001-Preferred_logo.jpg\"")&&!strstr(doc,"1973be00f56f"));
    previous=slurp(at(root,"DJPROFILE/VJTOOLS/profiles/djp_lena/profile.previous.json"),&n);CHECK(previous&&lena&&n==ln&&!memcmp(previous,lena,ln));
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.profile[1].has_thumb);
    int close_enough=1;for(int i=0;i<XZ_DJ_THUMB*XZ_DJ_THUMB;i++)if(!near(s.profile[1].thumb[i],40,200,90,12))close_enough=0;
    CHECK(close_enough);
    free(doc);free(previous);free(lena);
    size_t n2;char *music2=slurp(at(root,MUSIC),&n2);CHECK(music&&music2&&n2==strlen(music)&&!memcmp(music,music2,n2));
    free(music);free(music2);free(kai);
    printf("fixture-edited: %s\n",failures>before?"FAIL":"ok");
}

/* 4b. Remove every logo. */
static void logo_removed(const char *root){
    int before=failures;size_t n,q;char *music=slurp(at(root,MUSIC),&n);
    struct xz_dj_draft k;CHECK(xz_dj_draft_load(root,"djp_kai",&k)==0);k.remove_logo=1;
    CHECK(xz_dj_profile_write(root,&k,NULL)==XZ_DJ_WRITE_OK);
    char *doc=slurp(at(root,KAI_JSON),&q);
    CHECK(doc&&!xz_dj_profile_valid(doc,q)&&strstr(doc,"\"preferred_logo\": null")&&strstr(doc,"\"secondary_logos\": []")&&!strstr(doc,"xz_tile"));
    CHECK(doc&&strstr(doc,"\"Kai Moreno\"")&&strstr(doc,"\"tagline\": \"Deep tech, Berlin\""));
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2);CHECK(!s.profile[0].has_thumb);CHECK(!s.profile[0].damaged);CHECK(!strcmp(s.profile[0].name,"DJ Kai"));
    /* The index did not need to change: same name. */
    char *index=slurp(at(root,INDEX_JSON),&q);CHECK(index&&strstr(index,"2026-10-08T15-00-00Z-0001"));free(index);
    /* A logo cannot be both new and removed. */
    k.remove_logo=1;snprintf(k.logo,sizeof(k.logo),"%s",image("green.png"));CHECK(xz_dj_profile_write(root,&k,NULL)==XZ_DJ_WRITE_INVALID);
    size_t n2;char *music2=slurp(at(root,MUSIC),&n2);CHECK(music&&music2&&n2==n&&!memcmp(music,music2,n));
    free(music);free(music2);free(doc);
    printf("logo-removed: %s\n",failures>before?"FAIL":"ok");
}

/* 5. Sixteen profiles, then a refused 17th that writes nothing. */
static void full(const char *root){
    int before=failures;char first[XZ_DJ_ID_MAX]="",id[XZ_DJ_ID_MAX];
    for(int i=0;i<XZ_DJ_MAX_PROFILES;i++){
        char name[32];snprintf(name,sizeof(name),"DJ %02d",i+1);struct xz_dj_draft d=draft("",name,"","",0);
        CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);if(!i)strcpy(first,id);
    }
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==16);CHECK(s.active==0);CHECK(!strcmp(s.profile[0].dj_id,first));
    unsigned char a[32],b[32];tree_hash(root,a);
    struct xz_dj_draft d=draft("","One too many","","",0);snprintf(d.logo,sizeof(d.logo),"%s",image("green.png"));
    strcpy(id,"x");CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_FULL);CHECK(!*id);
    d=draft("djp_new","One too many","","",0);CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_FULL);
    tree_hash(root,b);CHECK(!memcmp(a,b,32));
    /* Renaming one of the sixteen still works. */
    d=draft(first,"DJ 01 renamed","","",0);CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==16);CHECK(!strcmp(s.profile[0].name,"DJ 01 renamed"));
    printf("full: %s\n",failures>before?"FAIL":"ok");
}

/* 6-7. Refusals that leave the stick untouched. */
static void errors(const char *root){
    int before=failures;unsigned char a[32],b[32];tree_hash(root,a);
    char long_name[200];memset(long_name,'a',161);long_name[161]=0;
    char long_tag[100];memset(long_tag,'t',81);long_tag[81]=0;
    struct { const char *id,*name,*tagline,*logo; int remove; } bad[]={
        {"","","","",0},{""," \t ","","",0},{"",long_name,"","",0},{"","a\x01" "b","","",0},{"","a\x7f","","",0},
        {"","\xc3(","","",0},{"","\xef\xbb\xbf","","",0},{"","\xe3\x80\x80","","",0},{"","A",long_tag,"",0},{"","A","a\nb","",0},
        {"","A","\xff","",0},{"../x","A","","",0},{"a b","A","","",0},{"","A","","relative.png",0},{"","A","","/x.png",1},
    };
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        struct xz_dj_draft d=draft(bad[i].id,bad[i].name,bad[i].tagline,bad[i].logo,bad[i].remove);
        int got=xz_dj_profile_write(root,&d,NULL);
        if(got!=XZ_DJ_WRITE_INVALID){fprintf(stderr,"bad draft %zu: got %d\n",i,got);failures++;}
    }
    CHECK(xz_dj_profile_write(NULL,NULL,NULL)==XZ_DJ_WRITE_INVALID);
    struct xz_dj_draft unterminated;memset(&unterminated,'a',sizeof(unterminated));CHECK(xz_dj_profile_write(root,&unterminated,NULL)==XZ_DJ_WRITE_INVALID);
    static const char *const images_bad[]={"notimage.png","truncated.png","huge.png","link.png","big.png","empty.png","missing.png"};
    for(size_t i=0;i<sizeof(images_bad)/sizeof(images_bad[0]);i++){
        struct xz_dj_draft d=draft("djp_kai","DJ Kai","","",0);snprintf(d.logo,sizeof(d.logo),"%s",image(images_bad[i]));
        int got=xz_dj_profile_write(root,&d,NULL);
        if(got!=XZ_DJ_WRITE_IMAGE){fprintf(stderr,"bad image %s: got %d\n",images_bad[i],got);failures++;}
    }
    tree_hash(root,b);CHECK(!memcmp(a,b,32));
    static uint16_t tile[XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE];int w,h;
    CHECK(xz_dj_image_tile(image("notimage.png"),tile,&w,&h)==XZ_DJ_IMAGE_NOT_IMAGE);
    CHECK(xz_dj_image_tile(image("truncated.png"),tile,&w,&h)==XZ_DJ_IMAGE_NOT_IMAGE);
    CHECK(xz_dj_image_tile(image("huge.png"),tile,&w,&h)==XZ_DJ_IMAGE_TOO_MANY_PIXELS);CHECK(w==5000&&h==5000);
    CHECK(xz_dj_image_tile(image("wide.png"),tile,&w,&h)==XZ_DJ_IMAGE_NOT_IMAGE);  /* 9000 px side, over STBI_MAX_DIMENSIONS */
    CHECK(xz_dj_image_tile(image("link.png"),tile,&w,&h)==XZ_DJ_IMAGE_UNREADABLE);
    CHECK(xz_dj_image_tile(image("big.png"),tile,&w,&h)==XZ_DJ_IMAGE_FILE_TOO_BIG);
    CHECK(xz_dj_image_tile(image("empty.png"),tile,&w,&h)==XZ_DJ_IMAGE_NOT_IMAGE);
    CHECK(xz_dj_image_tile(image("missing.png"),tile,&w,&h)==XZ_DJ_IMAGE_UNREADABLE);
    CHECK(xz_dj_image_tile(images,tile,&w,&h)==XZ_DJ_IMAGE_UNREADABLE);
    CHECK(xz_dj_image_tile(image("gray_alpha.png"),tile,NULL,NULL)==XZ_DJ_IMAGE_OK);
    printf("errors: %s\n",failures>before?"FAIL":"ok");
}

/* An existing bundle with the same id but different bytes is verified, refused, and nothing else moves. */
static void conflict(const char *root){
    int before=failures;char id[XZ_DJ_ID_MAX];size_t n,m;
    struct xz_dj_draft d=draft("djp_c","C","","",0);snprintf(d.logo,sizeof(d.logo),"%s",image("green.png"));
    CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);
    char *doc=profile_json(root,"djp_c",&n),*index=slurp(at(root,INDEX_JSON),&m);
    const char *t=doc?strstr(doc,"\"path\": \""):NULL;char asset[512]="";
    if(t){t+=9;snprintf(asset,sizeof(asset),"%.*s",(int)(strchr(t,'"')-t),t);}
    char *bytes=slurp(at(root,asset),&n);CHECK(bytes!=NULL);
    if(bytes){bytes[n-1]^=1;FILE *f=fopen(at(root,asset),"wb");fwrite(bytes,1,n,f);fclose(f);}
    d.tagline[0]=0;CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_IO);
    size_t k;char *now=profile_json(root,"djp_c",&k);CHECK(now&&doc&&!strcmp(now,doc));free(now);
    now=slurp(at(root,INDEX_JSON),&k);CHECK(now&&index&&!strcmp(now,index));free(now);
    if(bytes){bytes[n-1]^=1;FILE *f=fopen(at(root,asset),"wb");fwrite(bytes,1,n,f);fclose(f);}
    /* A new name makes a new bundle; the stage directory never stays behind. */
    d=draft("djp_c","C2","","",0);CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);
    DIR *dir=opendir(at(root,"DJPROFILE/VJTOOLS/profiles/djp_c/bundles"));struct dirent *e;int stray=0,bundles=0;
    while(dir&&(e=readdir(dir))){if(!strncmp(e->d_name,".stage-",7))stray++;else if(e->d_name[0]!='.')bundles++;}
    if(dir)closedir(dir);
    CHECK(!stray);CHECK(bundles==2);
    dir=opendir(at(root,"DJPROFILE/VJTOOLS"));stray=0;
    while(dir&&(e=readdir(dir)))if(!strncmp(e->d_name,".profile-",9))stray++;
    if(dir)closedir(dir);
    CHECK(!stray);
    free(doc);free(index);free(bytes);
    printf("conflict: %s\n",failures>before?"FAIL":"ok");
}

/* 8. Picking a logo file on a stick. */
static void list(const char *root){
    int before=failures;static char out[XZ_DJ_IMAGE_LIST_MAX][XZ_DJ_IMAGE_PATH];
    /* Logos in DJ folders come first, then the rest of the stick. */
    static const char *const want[]={"DJPROFILE/DJ Kai/logo.png","DJPROFILES/Mia/m.jpg",
        "a.jpg","Alpha.png","b.PNG","c.JpEg","l1/l2/l3/deep.png","l1/one.jpeg","Zed.png"};
    int n=xz_dj_image_list(root,out,XZ_DJ_IMAGE_LIST_MAX);CHECK(n==9);
    for(int i=0;i<n&&i<9;i++)if(strcmp(out[i],want[i])){fprintf(stderr,"list[%d]=%s want %s\n",i,out[i],want[i]);failures++;}
    for(int i=9;i<n;i++)fprintf(stderr,"extra %s\n",out[i]);
    CHECK(xz_dj_image_list(root,out,2)==2);CHECK(!strcmp(out[1],"DJPROFILES/Mia/m.jpg"));
    CHECK(xz_dj_image_list(root,out,5)==5);CHECK(!strcmp(out[4],"b.PNG"));
    CHECK(xz_dj_image_list(root,out,0)==0);
    CHECK(xz_dj_image_list(at(root,"no-such-dir"),out,4)==-1);
    printf("list: %s\n",failures>before?"FAIL":"ok");
}

/* 9. Tile geometry and blending. */
static void tiles(void){
    int before=failures;static uint16_t tile[XZ_DJ_TILE_SIDE*XZ_DJ_TILE_SIDE];int w=0,h=0;
    CHECK(xz_dj_image_tile(image("red_blue.png"),tile,&w,&h)==XZ_DJ_IMAGE_OK);CHECK(w==320&&h==160);
    int good=1;
    for(int y=0;y<160;y++)for(int x=0;x<160;x++){
        uint16_t want=y<40||y>=120?0:x<80?rgb565(255,0,0):rgb565(0,0,255);
        if(tile[y*160+x]!=want)good=0;
    }
    CHECK(good);
    CHECK(xz_dj_image_tile(image("half_alpha.png"),tile,&w,&h)==XZ_DJ_IMAGE_OK);CHECK(w==16&&h==16);
    CHECK(solid(tile,160*160,rgb565(128,0,0)));
    CHECK(xz_dj_image_tile(image("tall.png"),tile,&w,&h)==XZ_DJ_IMAGE_OK);CHECK(w==20&&h==40);
    good=1;
    for(int y=0;y<160;y++)for(int x=0;x<160;x++)if(tile[y*160+x]!=(x>=40&&x<120?0xffff:0))good=0;
    CHECK(good);
    /* 3x1 shrink of a black-white-black stripe pattern: exact area averages. */
    CHECK(xz_dj_image_tile(image("stripes.png"),tile,&w,&h)==XZ_DJ_IMAGE_OK);CHECK(w==480&&h==480);
    CHECK(solid(tile,160*160,rgb565(85,85,85)));
    printf("tiles: %s\n",failures>before?"FAIL":"ok");
}

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s <work>\n",argv[0]);return 2;}
    const char *work=argv[1];char sticks[4000];
    snprintf(images,sizeof(images),"%s/images",work);snprintf(sticks,sizeof(sticks),"%s/sticks",work);
    /* at() reuses its buffers; every root gets its own copy. */
    char root[7][4096];
    static const char *const names[]={"created","fixture-edited","logo-removed","full","errors","conflict"};
    for(int i=0;i<6;i++)snprintf(root[i],sizeof(root[i]),"%s/%s",sticks,names[i]);
    snprintf(root[6],sizeof(root[6]),"%s/list",work);
    created(root[0]);fixture_edited(root[1]);logo_removed(root[2]);full(root[3]);errors(root[4]);conflict(root[5]);list(root[6]);
    tiles();
    printf("test_dj_profile_write: %s (%d failures)\n",failures?"FAIL":"ok",failures);
    return failures?1:0;
}
