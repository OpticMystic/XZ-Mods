/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "dj_folders.h"
#include "dj_profile_internal.h"
#include "dj_profile_write.h"
#include "audio/vendor/sha256/sha256.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int lower(int c){return c>='A'&&c<='Z'?c+32:c;}
static int ci_equal(const char *a,const char *b){
    for(;*a&&*b;a++,b++)if(lower((unsigned char)*a)!=lower((unsigned char)*b))return 0;
    return !*a&&!*b;
}
static int ci_compare(const char *a,const char *b){
    for(;*a&&*b;a++,b++){int d=lower((unsigned char)*a)-lower((unsigned char)*b);if(d)return d;}
    return (unsigned char)*a-(unsigned char)*b;
}
static int image_name(const char *name){
    const char *dot=strrchr(name,'.');
    return dot&&dot!=name&&(ci_equal(dot,".png")||ci_equal(dot,".jpg")||ci_equal(dot,".jpeg"));
}
/* "logo.png", "logo.jpg" or "logo.jpeg" in any case. */
static int logo_name(const char *name){
    const char *dot=strrchr(name,'.');
    return dot&&dot-name==4&&lower((unsigned char)name[0])=='l'&&lower((unsigned char)name[1])=='o'&&
        lower((unsigned char)name[2])=='g'&&lower((unsigned char)name[3])=='o'&&image_name(name);
}
static int join(char *out,size_t cap,const char *a,const char *b){return snprintf(out,cap,"%s/%s",a,b)>=(int)cap;}
static int real_dir(const char *path){struct stat st;return !lstat(path,&st)&&S_ISDIR(st.st_mode);}

/* The logo of one DJ folder, as a name inside it; 0 when it has none. */
static int folder_logo(const char *dir,char *out,size_t cap){
    DIR *d=opendir(dir);struct dirent *e;int found=0,is_logo=0;
    if(!d)return 0;
    while((e=readdir(d))){
        char path[4096];struct stat st;
        if(e->d_name[0]=='.'||!image_name(e->d_name)||strlen(e->d_name)>=cap||join(path,sizeof(path),dir,e->d_name))continue;
        if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<8||(uint64_t)st.st_size>XZ_DJ_LOGO_MAX_BYTES)continue;
        int logo=logo_name(e->d_name);
        /* logo.* wins; otherwise the first by name. */
        if(!found||(logo&&!is_logo)||(logo==is_logo&&ci_compare(e->d_name,out)<0)){
            memcpy(out,e->d_name,strlen(e->d_name)+1);found=1;is_logo=logo;
        }
    }
    closedir(d);return found;
}
static int taken(const struct xz_dj_stick *s,const char *name){
    for(int i=0;i<s->count;i++)if(ci_equal(s->profile[i].name,name))return 1;
    return 0;
}
static void folder_id(const char *name,char out[XZ_DJ_ID_MAX]){
    SHA256_CTX c;unsigned char sum[32];static const char hex[]="0123456789abcdef";
    sha256_init(&c);sha256_update(&c,(const unsigned char *)name,strlen(name));sha256_final(&c,sum);
    size_t n=strlen(XZ_DJ_FOLDER_ID);memcpy(out,XZ_DJ_FOLDER_ID,n);
    for(int i=0;i<8;i++){out[n+i*2]=hex[sum[i]>>4];out[n+i*2+1]=hex[sum[i]&15];}
    out[n+16]=0;
}
struct candidate { char name[641]; char logo[256]; };
static int by_name(const void *a,const void *b){
    const struct candidate *x=a,*y=b;int c=ci_compare(x->name,y->name);return c?c:strcmp(x->name,y->name);
}
int xz_dj_folder_profiles(const char *root,struct xz_dj_stick *stick){
    if(!root||!*root||!stick||stick->count>=XZ_DJ_MAX_PROFILES)return 0;
    /* The top folders, DJPROFILE before DJPROFILES, whatever their case on the stick. */
    char tops[2][256]={"",""};
    DIR *d=opendir(root);struct dirent *e;
    if(!d)return 0;
    while((e=readdir(d))){
        int k=ci_equal(e->d_name,"DJPROFILE")?0:ci_equal(e->d_name,"DJPROFILES")?1:-1;
        if(k>=0&&!tops[k][0]&&strlen(e->d_name)<sizeof(tops[k]))memcpy(tops[k],e->d_name,strlen(e->d_name)+1);
    }
    closedir(d);
    struct candidate *found=calloc(XZ_DJ_MAX_PROFILES*2,sizeof(*found));int n=0;
    if(!found)return 0;
    for(int k=0;k<2;k++){
        char top[4096];
        if(!tops[k][0]||join(top,sizeof(top),root,tops[k])||!real_dir(top)||!(d=opendir(top)))continue;
        while((e=readdir(d))&&n<XZ_DJ_MAX_PROFILES*2){
            char dir[4096],logo[256];struct xz_djp_str name={e->d_name,strlen(e->d_name)};
            if(e->d_name[0]=='.'||ci_equal(e->d_name,"VJTOOLS")||join(dir,sizeof(dir),top,e->d_name)||!real_dir(dir))continue;
            /* The folder name is the DJ name, held to the same rules as dj_name. */
            int blank=1;for(size_t i=0;i<name.n;i++)if(name.s[i]!=' ')blank=0;
            if(blank||xz_djp_valid_label(name,160)||name.n>=sizeof(found[n].name))continue;
            if(!folder_logo(dir,logo,sizeof(logo)))continue;
            if(snprintf(found[n].logo,sizeof(found[n].logo),"%s/%s/%s",tops[k],e->d_name,logo)>=(int)sizeof(found[n].logo))continue;
            memcpy(found[n].name,e->d_name,name.n+1);n++;
        }
        closedir(d);
    }
    /* DJPROFILE's copy of a name wins over DJPROFILES' (stable: sort keeps the earlier one first). */
    for(int i=0;i<n;i++)for(int j=i+1;j<n;j++)if(ci_equal(found[i].name,found[j].name)){found[j]=found[--n];j--;}
    if(n>1)qsort(found,(size_t)n,sizeof(*found),by_name);
    int added=0,had=stick->count;
    for(int i=0;i<n&&stick->count<XZ_DJ_MAX_PROFILES;i++){
        if(taken(stick,found[i].name))continue;
        struct xz_dj_profile *p=&stick->profile[stick->count++];
        memset(p,0,sizeof(*p));
        memcpy(p->name,found[i].name,strlen(found[i].name)+1);
        memcpy(p->logo_path,found[i].logo,strlen(found[i].logo)+1);
        folder_id(p->name,p->dj_id);p->folder=1;added++;
    }
    if(!had&&added)stick->active=0;
    free(found);return added;
}

/* --- rekordbox stick colour --- */
uint32_t xz_dj_color_rgb(int id){
    /* rekordbox's colour label palette. */
    static const uint32_t rgb[9]={0,0xf870f8,0xf87070,0xffa064,0xf8e550,0x1ee12b,0x10e4dc,0x1e50f0,0x9828f0};
    return id>=1&&id<=8?rgb[id]:0;
}
static uint32_t u32le(const unsigned char *b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static int read_at(int fd,void *buf,size_t n,uint64_t at){
    size_t got=0;
    while(got<n){ssize_t r=pread(fd,(char *)buf+got,n-got,(off_t)(at+got));if(r<=0)return -1;got+=(size_t)r;}
    return 0;
}
#define PDB_PATH "PIONEER/rekordbox/export.pdb"
#define PROPERTY_TABLE 19
int xz_dj_stick_color(const char *root,uint32_t *rgb){
    int fd=-1;uint64_t size=0;
    if(rgb)*rgb=0;
    int rc=xz_djp_confined_open(root,PDB_PATH,(uint64_t)1<<32,&fd,&size);
    if(rc==XZ_DJP_READ_MISSING)return 1;
    if(rc!=XZ_DJP_READ_OK)return -1;
    int result=-1;unsigned char head[0x1c+16*64],*page=NULL;
    if(size<sizeof(head)||read_at(fd,head,sizeof(head),0))goto done;
    uint32_t page_size=u32le(head+4),tables=u32le(head+8);
    if(u32le(head)||page_size<0x40||page_size>65536||!tables||tables>64)goto done;
    uint32_t first=0,last=0;int listed=0;
    for(uint32_t i=0;i<tables;i++){
        const unsigned char *t=head+0x1c+16*i;
        if(u32le(t)==PROPERTY_TABLE){first=u32le(t+8);last=u32le(t+12);listed=1;break;}
    }
    if(!listed||!(page=malloc(page_size)))goto done;
    /* Walk the table's pages; the property row heads the first data page. */
    uint32_t at=first;result=1;
    for(int hops=0;hops<64;hops++){
        if((uint64_t)at*page_size+page_size>size||read_at(fd,page,page_size,(uint64_t)at*page_size)){result=-1;break;}
        if(u32le(page+4)!=at||u32le(page+8)!=PROPERTY_TABLE){result=-1;break;}
        int data=!(page[0x1b]&0x40);
        if(data&&page[0x28]==0x80&&page[0x29]==0x02){
            int id=page[0x28+8];
            if(id>=1&&id<=8){if(rgb)*rgb=xz_dj_color_rgb(id);result=0;}
            break;
        }
        if(at==last)break;
        at=u32le(page+12);
    }
done:
    free(page);close(fd);return result;
}
