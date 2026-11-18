/* SPDX-License-Identifier: MIT */
/* Writer and logo decoder cases. Run by tests/verify_dj_profile_write.py, which prepares <work>:
   sticks/{created,migrated,adopted,unknown,full,errors,large,switch}, images/ and list/, then checks every
   stick with spec/djprofile/tools/check_djprofile.py. */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_profile_write.h"
#include "dj_folders.h"
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
    size_t cap=16u<<20;char *data=malloc(cap+1);*n=data?fread(data,1,cap+1,f):0;fclose(f);
    if(!data||*n>cap){free(data);return NULL;}data[*n]=0;return data;
}
static const char *at(const char *root,const char *relative){
    static char buffers[8][4096];static int next;char *out=buffers[next++&7];
    snprintf(out,4096,"%s/%s",root,relative);return out;
}
static int exists(const char *path){struct stat st;return !lstat(path,&st);}
static int same_file(const char *a,const char *b){
    size_t x,y;char *p=slurp(a,&x),*q=slurp(b,&y);int same=p&&q&&x==y&&!memcmp(p,q,x);free(p);free(q);return same;
}
static int contains(const char *path,const char *needle){size_t n;char *t=slurp(path,&n);int found=t&&strstr(t,needle);free(t);return found;}
static uint16_t rgb565(unsigned r,unsigned g,unsigned b){return (uint16_t)((r>>3)<<11|(g>>2)<<5|b>>3);}
static int solid(const uint16_t *px,int n,uint16_t v){for(int i=0;i<n;i++)if(px[i]!=v)return 0;return 1;}
static int near(uint16_t v,unsigned r,unsigned g,unsigned b,int tolerance){
    int r5=v>>11,g6=v>>5&63,b5=v&31;
    return abs((r5<<3|r5>>2)-(int)r)<=tolerance&&abs((g6<<2|g6>>4)-(int)g)<=tolerance&&abs((b5<<3|b5>>2)-(int)b)<=tolerance;
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
static int unchanged_since(const char *root,const unsigned char before[32]){unsigned char now[32];tree_hash(root,now);return !memcmp(before,now,32);}
static int is_uuid4(const char *id){
    if(strlen(id)!=36||id[8]!='-'||id[13]!='-'||id[18]!='-'||id[23]!='-'||id[14]!='4'||!strchr("89ab",id[19]))return 0;
    for(int i=0;i<36;i++)if(id[i]!='-'&&!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return 0;
    return 1;
}
static int find(const struct xz_dj_stick *s,const char *name){for(int i=0;i<s->count;i++)if(!strcmp(s->profile[i].name,name))return i;return -1;}

#define OLD_INDEX "DJPROFILE/VJTOOLS/index.json"
#define OLD_KAI "DJPROFILE/VJTOOLS/profiles/djp_kai/profile.json"
#define OLD_KAI_LOGO "DJPROFILE/VJTOOLS/profiles/djp_kai/bundles/0b7c1f2e-0000-4000-8000-00000000c0de/assets/001-logo.png"
#define OLD_KAI_TILE "DJPROFILE/VJTOOLS/profiles/djp_kai/bundles/0b7c1f2e-0000-4000-8000-00000000c0de/renditions/xz-tile-160.rgb565"
#define OLD_LENA_LOGO "DJPROFILE/VJTOOLS/profiles/djp_lena/bundles/5e1a9d40-0000-4000-8000-0000000b2b00/assets/001-logo.png"
#define MUSIC "PIONEER/rekordbox/export.pdb"
#define STICK_FILE "DJPROFILE/DJPROFILE.json"
static char images[4096];
static const char *image(const char *name){return at(images,name);}
static struct xz_dj_stick s;

/* 1. An empty stick: create, read back, write again (nothing changes), a second profile without a logo. */
static void created(const char *root){
    int before=failures;char id[XZ_DJ_ID_MAX]="x";unsigned char hash[32];
    static const char name[]="Ada \"Countess\" \\ Lov\xc3\xa9lace";
    struct xz_dj_draft d=draft("",name,"Analytical\t? no: tab refused","",0);
    CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_INVALID);CHECK(!*id);CHECK(!exists(at(root,"DJPROFILE")));
    d=draft("",name,"Analytical engines",image("green.png"),0);
    CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);CHECK(is_uuid4(id));
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==1);CHECK(s.active==0);CHECK(s.profile[0].kind==XZ_DJ_LEVEL1);
    CHECK(!strcmp(s.profile[0].dj_id,id));CHECK(!strcmp(s.profile[0].name,name));CHECK(!strcmp(s.profile[0].tagline,"Analytical engines"));
    CHECK(!strcmp(s.profile[0].dir,"DJPROFILE/Ada Countess  Lov\xc3\xa9lace"));
    CHECK(s.profile[0].has_thumb&&solid(s.profile[0].thumb,XZ_DJ_THUMB*XZ_DJ_THUMB,rgb565(30,220,60)));
    const char *dir="DJPROFILE/Ada Countess  Lov\xc3\xa9lace";char path[512];
    snprintf(path,sizeof(path),"%s/logo.png",dir);CHECK(same_file(at(root,path),image("green.png")));
    snprintf(path,sizeof(path),"%s/" "tile-160.rgb565",dir);CHECK(exists(at(root,path)));
    snprintf(path,sizeof(path),"%s/profile.previous.json",dir);CHECK(!exists(at(root,path)));
    snprintf(path,sizeof(path),"%s/profile.json",dir);
    CHECK(contains(at(root,path),"\"format\": \"djprofile/1\"")&&contains(at(root,path),"\"name\": \"Ada \\\"Countess\\\" \\\\ Lov\xc3\xa9lace\""));
    CHECK(contains(at(root,STICK_FILE),"\"format\": \"djprofile-stick/1\"")&&contains(at(root,STICK_FILE),id));
    /* The same content again writes nothing. */
    struct xz_dj_draft back;CHECK(xz_dj_draft_load(root,id,&back)==0);
    CHECK(!strcmp(back.dj_id,id)&&!strcmp(back.name,name)&&!strcmp(back.tagline,"Analytical engines")&&!*back.logo&&!back.remove_logo);
    tree_hash(root,hash);
    char again[XZ_DJ_ID_MAX];CHECK(xz_dj_profile_write(root,&back,again)==XZ_DJ_WRITE_OK);CHECK(!strcmp(again,id));
    CHECK(unchanged_since(root,hash));
    snprintf(d.dj_id,sizeof(d.dj_id),"%s",id);CHECK(xz_dj_profile_write(root,&d,again)==XZ_DJ_WRITE_OK);CHECK(unchanged_since(root,hash));
    CHECK(xz_dj_draft_load(root,"nobody",&back)==-1);CHECK(xz_dj_draft_load(root,"",&back)==-1);
    /* 160 bytes fit a name, 161 do not; a profile without a logo gets its initial; active stays. */
    char wide[400]={0};for(int i=0;i<81;i++){wide[i*2]=(char)0xc3;wide[i*2+1]=(char)0xa9;}
    d=draft("",wide,"","",0);CHECK(xz_dj_profile_write(root,&d,again)==XZ_DJ_WRITE_INVALID);
    wide[160]=0;d=draft("",wide,"","",0);CHECK(xz_dj_profile_write(root,&d,again)==XZ_DJ_WRITE_OK);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2);CHECK(s.active==0);
    int k=find(&s,wide);CHECK(k==1);
    if(k>=0){CHECK(s.profile[k].has_thumb&&!solid(s.profile[k].thumb,XZ_DJ_THUMB*XZ_DJ_THUMB,s.profile[k].thumb[0]));snprintf(path,sizeof(path),"%s/logo.png",s.profile[k].dir);CHECK(exists(at(root,path)));}
    /* A profile.json the reader cannot use (its logo no longer matches) is still that DJ: an edit keeps the id. */
    if(k>=0){
        char doc[1200],folder[512],keep_id[XZ_DJ_ID_MAX];size_t dn;snprintf(keep_id,sizeof(keep_id),"%s",s.profile[k].dj_id);
        snprintf(folder,sizeof(folder),"%s",s.profile[k].dir);snprintf(doc,sizeof(doc),"%s/profile.json",folder);
        char *text=slurp(at(root,doc),&dn);char *sha=text?strstr(text,"\"sha256\": \""):NULL;
        if(sha){sha[14]=sha[14]=='0'?'1':'0';FILE *f=fopen(at(root,doc),"wb");fwrite(text,1,dn,f);fclose(f);}
        CHECK(xz_dj_stick_read(root,&s)==0);k=-1;for(int i=0;i<s.count;i++)if(!strcmp(s.profile[i].dir,folder))k=i;
        CHECK(k>=0&&s.profile[k].kind==XZ_DJ_LEVEL0);
        d=draft("",wide,"Fixed","",0);snprintf(d.folder,sizeof(d.folder),"%s",folder);
        CHECK(xz_dj_profile_write(root,&d,again)==XZ_DJ_WRITE_OK);CHECK(!strcmp(again,keep_id));
        snprintf(doc,sizeof(doc),"%s/profile.previous.json",folder);CHECK(exists(at(root,doc)));
        CHECK(xz_dj_stick_read(root,&s)==0);k=find(&s,wide);CHECK(k>=0&&s.profile[k].kind==XZ_DJ_LEVEL1&&!strcmp(s.profile[k].tagline,"Fixed"));
        free(text);
    }
    /* NO LOGO replaces the logo with the initial; the alternate-free profile keeps its tagline. */
    d=draft(id,name,"Analytical engines","",1);CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    snprintf(path,sizeof(path),"%s/logo.png",dir);CHECK(!same_file(at(root,path),image("green.png")));
    snprintf(path,sizeof(path),"%s/profile.previous.json",dir);CHECK(exists(at(root,path)));
    printf("created: %s\n",failures>before?"FAIL":"ok");
}

/* 2. The shared pre-1.0 fixture: the first write moves both profiles to folders; the old store stays. */
struct view { char name[641],tagline[321],page[512]; uint32_t accent; int has_accent,has_thumb; uint16_t thumb[XZ_DJ_THUMB*XZ_DJ_THUMB]; };
static void snapshot(const struct xz_dj_stick *x,struct view *v,int *active){
    for(int i=0;i<x->count;i++){
        snprintf(v[i].name,sizeof(v[i].name),"%s",x->profile[i].name);snprintf(v[i].tagline,sizeof(v[i].tagline),"%s",x->profile[i].tagline);
        snprintf(v[i].page,sizeof(v[i].page),"%s",x->profile[i].page_url);v[i].accent=x->profile[i].accent;v[i].has_accent=x->profile[i].has_accent;
        v[i].has_thumb=x->profile[i].has_thumb;memcpy(v[i].thumb,x->profile[i].thumb,sizeof(v[i].thumb));
    }
    *active=x->active;
}
static int same_view(const struct view *a,const struct view *b,int n){
    for(int i=0;i<n;i++)if(strcmp(a[i].name,b[i].name)||strcmp(a[i].tagline,b[i].tagline)||strcmp(a[i].page,b[i].page)||a[i].accent!=b[i].accent||
        a[i].has_accent!=b[i].has_accent||a[i].has_thumb!=b[i].has_thumb||memcmp(a[i].thumb,b[i].thumb,sizeof(a[i].thumb))){fprintf(stderr,"profile %d differs\n",i);return 0;}
    return 1;
}
static void migrated(const char *root){
    int before=failures;size_t n,m;static struct view was[16],now[16];int was_active,now_active;
    char *music=slurp(at(root,MUSIC),&n),*index=slurp(at(root,OLD_INDEX),&m);CHECK(music&&index);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2&&s.profile[0].kind==XZ_DJ_OLD);snapshot(&s,was,&was_active);
    /* Already active: nothing switches, but the stick moves to 1.0 (contract 0.3) and reads the same. */
    CHECK(xz_dj_stick_set_active(root,"djp_lena")==1);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2);
    CHECK(s.profile[0].kind==XZ_DJ_LEVEL1&&s.profile[1].kind==XZ_DJ_LEVEL1);
    CHECK(!strcmp(s.profile[0].dir,"DJPROFILE/DJ Kai")&&!strcmp(s.profile[1].dir,"DJPROFILE/Lena B"));
    snapshot(&s,now,&now_active);CHECK(same_view(was,now,2));CHECK(was_active==now_active&&now_active==1);
    CHECK(same_file(at(root,"DJPROFILE/DJ Kai/logo.png"),at(root,OLD_KAI_LOGO)));
    CHECK(same_file(at(root,"DJPROFILE/DJ Kai/tile-160.rgb565"),at(root,OLD_KAI_TILE)));
    CHECK(same_file(at(root,"DJPROFILE/Lena B/logo.png"),at(root,OLD_LENA_LOGO)));
    const char *kai="DJPROFILE/DJ Kai/profile.json";
    static const char *const mapped[]={"\"id\": \"djp_kai\"","\"revision\": \"0b7c1f2e-0000-4000-8000-00000000c0de\"","\"accent\": \"#ff2a6d\"",
        "\"Kai Moreno\"","\"kind\": \"page\"","\"url\": \"https://vj.tools/dj#p1.fixture\"","\"kind\": \"media-kit\"","\"role\": \"primary\"",
        "\"label\": \"Preferred logo\"","\"for\": \"xdj-xz\""};
    for(size_t i=0;i<sizeof(mapped)/sizeof(mapped[0]);i++)if(!contains(at(root,kai),mapped[i])){fprintf(stderr,"migrated profile.json lacks %s\n",mapped[i]);failures++;}
    CHECK(!contains(at(root,kai),"page_url")&&!contains(at(root,kai),"VJTOOLS")&&!contains(at(root,kai),"\"ext\""));
    CHECK(contains(at(root,STICK_FILE),"\"active\": \"djp_lena\""));
    /* The old store is never deleted, and its index is untouched by a move. */
    size_t k;char *index_now=slurp(at(root,OLD_INDEX),&k);CHECK(index_now&&k==m&&!memcmp(index_now,index,m));free(index_now);
    CHECK(exists(at(root,OLD_KAI))&&exists(at(root,OLD_KAI_LOGO)));
    /* A second move finds nothing to move. */
    unsigned char hash[32];tree_hash(root,hash);
    CHECK(xz_dj_stick_set_active(root,"djp_lena")==1);CHECK(unchanged_since(root,hash));
    /* A new profile is added after the moved ones and does not change the active one. */
    char mira[XZ_DJ_ID_MAX];struct xz_dj_draft d=draft("","Mira","",image("red_blue.png"),0);
    CHECK(xz_dj_profile_write(root,&d,mira)==XZ_DJ_WRITE_OK);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==3);CHECK(s.active==1);CHECK(find(&s,"Mira")==2&&s.profile[2].has_thumb);
    /* Rename DJ Kai keeping the logo: the folder follows, profile.previous.json keeps the document it replaced. */
    char *kai_doc=slurp(at(root,kai),&m);
    struct xz_dj_draft e;CHECK(xz_dj_draft_load(root,"djp_kai",&e)==0);CHECK(!strcmp(e.name,"DJ Kai")&&!strcmp(e.tagline,"Deep tech, Berlin"));
    snprintf(e.name,sizeof(e.name),"DJ Kai Moreno");snprintf(e.tagline,sizeof(e.tagline),"Techno, Berlin");
    char id[XZ_DJ_ID_MAX];CHECK(xz_dj_profile_write(root,&e,id)==XZ_DJ_WRITE_OK);CHECK(!strcmp(id,"djp_kai"));
    CHECK(!exists(at(root,"DJPROFILE/DJ Kai"))&&exists(at(root,"DJPROFILE/DJ Kai Moreno/profile.json")));
    CHECK(same_file(at(root,"DJPROFILE/DJ Kai Moreno/profile.previous.json"),at(root,"DJPROFILE/DJ Kai Moreno/profile.previous.json")));
    size_t q;char *prev=slurp(at(root,"DJPROFILE/DJ Kai Moreno/profile.previous.json"),&q);CHECK(prev&&kai_doc&&q==m&&!memcmp(prev,kai_doc,m));free(prev);free(kai_doc);
    CHECK(contains(at(root,"DJPROFILE/DJ Kai Moreno/profile.json"),"\"Kai Moreno\"")&&contains(at(root,"DJPROFILE/DJ Kai Moreno/profile.json"),"press.zip"));
    CHECK(contains(at(root,STICK_FILE),"\"folder\": \"DJ Kai Moreno\""));
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==3);CHECK(!strcmp(s.profile[0].name,"DJ Kai Moreno"));CHECK(s.profile[0].has_thumb&&solid(s.profile[0].thumb,XZ_DJ_THUMB*XZ_DJ_THUMB,rgb565(255,42,109)));
    /* Lena gets a JPEG: logo.jpg and a new tile; the files the old document listed go. */
    d=draft("djp_lena","Lena B","",image("logo.jpg"),0);
    CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    CHECK(exists(at(root,"DJPROFILE/Lena B/logo.jpg"))&&!exists(at(root,"DJPROFILE/Lena B/logo.png")));
    CHECK(!exists(at(root,"DJPROFILE/Lena B/tile-160.rgb565")));
    CHECK(xz_dj_stick_read(root,&s)==0);int lena=find(&s,"Lena B");CHECK(lena>=0&&s.profile[lena].has_thumb);
    int close_enough=1;for(int i=0;lena>=0&&i<XZ_DJ_THUMB*XZ_DJ_THUMB;i++)if(!near(s.profile[lena].thumb[i],40,200,90,12))close_enough=0;
    CHECK(close_enough);
    /* The same save again writes nothing; back to a PNG: the replaced primary goes, nothing is set aside. */
    unsigned char lena_hash[32];tree_hash(root,lena_hash);
    CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);CHECK(unchanged_since(root,lena_hash));
    d=draft("djp_lena","Lena B","",image("green.png"),0);CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    CHECK(same_file(at(root,"DJPROFILE/Lena B/logo.png"),image("green.png"))&&!exists(at(root,"DJPROFILE/Lena B/logo.jpg")));
    CHECK(!exists(at(root,"DJPROFILE/Lena B/logo-original.jpg"))&&!exists(at(root,"DJPROFILE/Lena B/logo-original.png")));
    /* Switching: DJPROFILE.json and the old index both name the profile; only active and revision move there. */
    CHECK(xz_dj_stick_set_active(root,"djp_kai")==0);
    CHECK(contains(at(root,STICK_FILE),"\"active\": \"djp_kai\""));
    char *index_after=slurp(at(root,OLD_INDEX),&k);CHECK(index_after&&k+1==strlen(index)&&strstr(index_after,"\"active\": \"djp_kai\""));
    CHECK(index_after&&strstr(index_after,"\"dj_name\": \"Lena B\"")&&!strstr(index_after,"2026-10-08T15-00-00Z-0001"));free(index_after);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.active==find(&s,"DJ Kai Moreno"));
    CHECK(xz_dj_stick_set_active(root,"djp_kai")==1);CHECK(xz_dj_stick_set_active(root,"nobody")==-1);
    /* A profile only DJPROFILE.json knows switches without touching the index. */
    index_after=slurp(at(root,OLD_INDEX),&k);
    CHECK(xz_dj_stick_set_active(root,mira)==0);
    size_t k2;char *index_same=slurp(at(root,OLD_INDEX),&k2);CHECK(index_after&&index_same&&k2==k&&!memcmp(index_after,index_same,k));free(index_same);free(index_after);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.active==find(&s,"Mira"));
    size_t n2;char *music2=slurp(at(root,MUSIC),&n2);CHECK(music&&music2&&n2==n&&!memcmp(music,music2,n));
    free(music);free(music2);free(index);
    printf("migrated: %s\n",failures>before?"FAIL":"ok");
}

/* 3. Hand-made folders: the move adopts the DJ's folder and keeps their logos; a device edit adopts a level 0 folder. */
static void adopted(const char *root){
    int before=failures;
    CHECK(xz_dj_stick_read(root,&s)==0);
    /* Contract 0.1 step 3: the hand-made "DJ Kai" folder is listed once, as the old profile. */
    CHECK(s.count==3);CHECK(find(&s,"DJ Kai")==1&&s.profile[1].kind==XZ_DJ_OLD);CHECK(find(&s,"Zed")==0&&s.profile[0].kind==XZ_DJ_LEVEL0);
    CHECK(xz_dj_stick_set_active(root,"Zed")==0);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==3);
    int kai=find(&s,"DJ Kai");CHECK(kai>=0&&s.profile[kai].kind==XZ_DJ_LEVEL1&&!strcmp(s.profile[kai].dir,"DJPROFILE/DJ Kai"));
    CHECK(!strcmp(s.profile[s.active].name,"Zed"));
    CHECK(same_file(at(root,"DJPROFILE/DJ Kai/logo.png"),at(root,OLD_KAI_LOGO)));
    CHECK(same_file(at(root,"DJPROFILE/DJ Kai/logo-original.png"),image("green.png")));
    CHECK(same_file(at(root,"DJPROFILE/DJ Kai/logo-original.jpeg"),image("logo.jpg")));
    CHECK(exists(at(root,"DJPROFILE/DJ Kai/flyer.png"))&&!exists(at(root,"DJPROFILE/DJ Kai/Logo.PNG")));
    CHECK(contains(at(root,"DJPROFILE/DJ Kai/profile.json"),"\"file\": \"logo-original.png\"")&&contains(at(root,"DJPROFILE/DJ Kai/profile.json"),"\"role\": \"alternate\""));
    CHECK(contains(at(root,STICK_FILE),"\"active\": \"Zed\""));
    /* Editing the level 0 folder: its logo becomes logo.png beside the DJ's file, the folder gets an id. */
    int zed=find(&s,"Zed");struct xz_dj_draft d=draft("","Zed","Live","",0);
    snprintf(d.folder,sizeof(d.folder),"%s",s.profile[zed].dir);snprintf(d.accent,sizeof(d.accent),"#1e50f0");
    char id[XZ_DJ_ID_MAX];CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);CHECK(is_uuid4(id));
    CHECK(same_file(at(root,"DJPROFILE/Zed/logo.png"),at(root,"DJPROFILE/Zed/flyer.png")));
    CHECK(contains(at(root,"DJPROFILE/Zed/profile.json"),"\"accent\": \"#1e50f0\""));
    CHECK(contains(at(root,STICK_FILE),id));CHECK(!contains(at(root,STICK_FILE),"\"active\": \"Zed\""));
    CHECK(xz_dj_stick_read(root,&s)==0);zed=find(&s,"Zed");CHECK(zed>=0&&s.profile[zed].kind==XZ_DJ_LEVEL1&&s.active==zed);
    printf("adopted: %s\n",failures>before?"FAIL":"ok");
}

/* 4. A 1.0 stick made by other software: every member the XZ does not change survives, at every level. */
static void unknown(const char *root){
    int before=failures;const char *doc="DJPROFILE/DJ Kai/profile.json";
    CHECK(xz_dj_stick_read(root,&s)==0);
    struct xz_dj_draft d;CHECK(xz_dj_draft_load(root,"5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21",&d)==0);
    snprintf(d.tagline,sizeof(d.tagline),"Garage, Leeds");
    CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    static const char *const kept[]={"\"readers ignore this, writers keep it\"","\"x-entry\"","\"x-link\": true","\"x-root\"",
        "\"background\": \"light\"","\"file\": \"DJ PROFILE - DJ Kai.mp3\"","\"marker\": \"djprofile:1:5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21:76f3571a0e8c\"",
        "\"tagline\": \"Garage, Leeds\"","\"aliases\""};
    for(size_t i=0;i<sizeof(kept)/sizeof(kept[0]);i++)if(!contains(at(root,doc),kept[i])){fprintf(stderr,"lost %s\n",kept[i]);failures++;}
    CHECK(contains(at(root,STICK_FILE),"\"x-stick\": \"kept\""));
    /* A rename: the track copy's tags name the old name, so only its marker stays and the copy goes. */
    snprintf(d.name,sizeof(d.name),"DJ Kai Live");
    CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    const char *moved="DJPROFILE/DJ Kai Live/profile.json";
    CHECK(contains(at(root,moved),"\"marker\": \"djprofile:1:5f1d6f0e")&&!contains(at(root,moved),".mp3"));
    CHECK(!exists(at(root,"DJPROFILE/DJ Kai Live/DJ PROFILE - DJ Kai.mp3"))&&contains(at(root,moved),"\"x-root\""));
    CHECK(contains(at(root,STICK_FILE),"\"x-stick\": \"kept\"")&&contains(at(root,STICK_FILE),"\"folder\": \"DJ Kai Live\""));
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(find(&s,"DJ Kai Live")>=0&&find(&s,"Lena B")>=0);
    /* A JPEG primary takes logo.jpg: the alternate stored under that name moves aside, its bytes kept and listed. */
    d=draft("5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21","DJ Kai Live","Garage, Leeds",image("logo.jpg"),0);
    CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    CHECK(same_file(at(root,"DJPROFILE/DJ Kai Live/logo.jpg"),image("logo.jpg"))&&!exists(at(root,"DJPROFILE/DJ Kai Live/logo.png")));
    DIR *folder=opendir(at(root,"DJPROFILE/DJ Kai Live"));struct dirent *e;int aside=0;
    while(folder&&(e=readdir(folder)))if(!strncmp(e->d_name,"logo-",5)&&strstr(e->d_name,".jpg")){char rel[600];snprintf(rel,sizeof(rel),"DJPROFILE/DJ Kai Live/%s",e->d_name);aside=same_file(at(root,rel),image("logo.jpg"))&&contains(at(root,moved),e->d_name);}
    if(folder)closedir(folder);
    CHECK(aside);
    printf("unknown: %s\n",failures>before?"FAIL":"ok");
}

/* A logo that would pass 24 MiB of logos is refused before the primary logo is touched. */
static void heavy(const char *root){
    int before=failures;unsigned char a[32];tree_hash(root,a);
    struct xz_dj_draft d=draft("heavy","Heavy","",image("noise.png"),0);
    CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_INVALID);CHECK(unchanged_since(root,a));
    CHECK(xz_dj_stick_read(root,&s)==0&&s.count==1&&s.profile[0].kind==XZ_DJ_LEVEL1);
    printf("heavy: %s\n",failures>before?"FAIL":"ok");
}

/* 5. Sixteen profiles, then a refused 17th that writes nothing. */
static void full(const char *root){
    int before=failures;char first[XZ_DJ_ID_MAX]="",id[XZ_DJ_ID_MAX];
    for(int i=0;i<XZ_DJ_MAX_PROFILES;i++){
        char name[32];snprintf(name,sizeof(name),"DJ %02d",i+1);struct xz_dj_draft d=draft("",name,"","",0);
        CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);if(!i)strcpy(first,id);
    }
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==16);CHECK(s.active==0);CHECK(!strcmp(s.profile[0].dj_id,first));
    unsigned char a[32];tree_hash(root,a);
    struct xz_dj_draft d=draft("","One too many","",image("green.png"),0);
    strcpy(id,"x");CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_FULL);CHECK(!*id);
    CHECK(unchanged_since(root,a));
    d=draft(first,"DJ 01 renamed","","",0);CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_OK);
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==16);CHECK(!strcmp(s.profile[0].name,"DJ 01 renamed"));
    CHECK(exists(at(root,"DJPROFILE/DJ 01 renamed/logo.png"))&&!exists(at(root,"DJPROFILE/DJ 01")));
    printf("full: %s\n",failures>before?"FAIL":"ok");
}

/* 6. Refusals that leave the stick untouched. */
static void errors(const char *root){
    int before=failures;unsigned char a[32];tree_hash(root,a);
    char long_name[200];memset(long_name,'a',161);long_name[161]=0;
    char long_tag[300];memset(long_tag,'t',241);long_tag[241]=0;
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
        struct xz_dj_draft d=draft("djp_kai","DJ Kai","",image(images_bad[i]),0);
        int got=xz_dj_profile_write(root,&d,NULL);
        if(got!=XZ_DJ_WRITE_IMAGE){fprintf(stderr,"bad image %s: got %d\n",images_bad[i],got);failures++;}
    }
    CHECK(unchanged_since(root,a));
    /* An unknown id moves the old store first, then refuses. */
    struct xz_dj_draft d=draft("djp_nobody","Nobody","","",0);CHECK(xz_dj_profile_write(root,&d,NULL)==XZ_DJ_WRITE_CHANGED);
    /* DJ Kai's old document holds a member 1.0 cannot (a non-ASCII name): it stays in the old store, Lena moved. */
    CHECK(xz_dj_stick_read(root,&s)==0);int kai=find(&s,"DJ Kai"),lena=find(&s,"Lena B");
    CHECK(kai>=0&&s.profile[kai].kind==XZ_DJ_OLD&&lena>=0&&s.profile[lena].kind==XZ_DJ_LEVEL1);
    CHECK(xz_dj_stick_set_active(root,"djp_kai")==0);CHECK(xz_dj_stick_read(root,&s)==0&&!strcmp(s.profile[s.active].name,"DJ Kai"));
    CHECK(!contains(at(root,"DJPROFILE/DJPROFILE.json"),"\"active\""));
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

/* 7. Logos over 4096 px a side or 4 MP are scaled down and written as PNG. */
static void large(const char *root){
    int before=failures;char id[XZ_DJ_ID_MAX];
    struct xz_dj_draft d=draft("","Wide","",image("wide4100.png"),0);CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);
    d=draft("","Square","",image("square2100.png"),0);CHECK(xz_dj_profile_write(root,&d,id)==XZ_DJ_WRITE_OK);
    static const char *const logos[]={"DJPROFILE/Wide/logo.png","DJPROFILE/Square/logo.png"};
    for(int i=0;i<2;i++){
        size_t n;unsigned char *data=(unsigned char *)slurp(at(root,logos[i]),&n);struct xz_dj_image_info info;
        CHECK(data&&!xz_dj_image_usable(data,n,&info)&&info.kind==XZ_DJ_IMAGE_PNG);
        if(data)printf("large: %s is %ux%u\n",logos[i],info.width,info.height);
        CHECK(!i?info.width==4096&&info.height<=100:info.width==info.height&&(uint64_t)info.width*info.height<=4u*1024u*1024u&&info.width>=2040);
        free(data);
    }
    CHECK(xz_dj_stick_read(root,&s)==0);CHECK(s.count==2&&s.profile[0].has_thumb&&s.profile[1].has_thumb);
    printf("large: %s\n",failures>before?"FAIL":"ok");
}

/* 8. Picking a logo file on a stick. */
static void list(const char *root){
    int before=failures;static char out[XZ_DJ_IMAGE_LIST_MAX][XZ_DJ_IMAGE_PATH];
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
    CHECK(xz_dj_image_tile(image("stripes.png"),tile,&w,&h)==XZ_DJ_IMAGE_OK);CHECK(w==480&&h==480);
    CHECK(solid(tile,160*160,rgb565(85,85,85)));
    printf("tiles: %s\n",failures>before?"FAIL":"ok");
}

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s <work>\n",argv[0]);return 2;}
    const char *work=argv[1];char sticks[4000];
    snprintf(images,sizeof(images),"%s/images",work);snprintf(sticks,sizeof(sticks),"%s/sticks",work);
    static const char *const names[]={"created","migrated","adopted","unknown","full","errors","large","heavy"};
    char root[9][4096];
    for(int i=0;i<8;i++)snprintf(root[i],sizeof(root[i]),"%s/%s",sticks,names[i]);
    snprintf(root[8],sizeof(root[8]),"%s/list",work);
    created(root[0]);migrated(root[1]);adopted(root[2]);unknown(root[3]);full(root[4]);errors(root[5]);large(root[6]);heavy(root[7]);list(root[8]);
    tiles();
    printf("test_dj_profile_write: %s (%d failures)\n",failures?"FAIL":"ok",failures);
    return failures?1:0;
}
