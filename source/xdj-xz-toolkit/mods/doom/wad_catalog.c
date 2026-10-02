/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "wad_catalog.h"
#include "../audio/vendor/sha256/sha256.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t little(const unsigned char *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int error(struct xz_wad_file *out,const char *reason) {
    snprintf(out->reason,sizeof(out->reason),"%s",reason);out->kind=XZ_WAD_INVALID;return -1;
}
int xz_wad_inspect(const char *path,struct xz_wad_file *out) {
    memset(out,0,sizeof(*out));
    if(!path || strlen(path)>=sizeof(out->path)) return error(out,"PATH TOO LONG");
    snprintf(out->path,sizeof(out->path),"%s",path);
    const char *name=strrchr(path,'/');name=name?name+1:path;
    snprintf(out->name,sizeof(out->name),"%.95s",name);
    size_t length=strlen(name);
    if(length>=4 && (!strcasecmp(name+length-4,".pk3") || !strcasecmp(name+length-4,".pk7"))) {
        out->kind=XZ_WAD_ADVANCED;snprintf(out->reason,sizeof(out->reason),"GZDOOM REQUIRED");return 0;
    }
    int fd=open(path,O_RDONLY|O_NOFOLLOW);struct stat stat;
    unsigned char header[12];
    if(fd<0)return error(out,"FILE UNAVAILABLE");
    if(fstat(fd,&stat) || !S_ISREG(stat.st_mode) || stat.st_size<12 || stat.st_size>128*1024*1024 || read(fd,header,12)!=12) {
        close(fd);return error(out,"INVALID WAD SIZE");
    }
    int base=!memcmp(header,"IWAD",4);
    if(!base && memcmp(header,"PWAD",4)) {close(fd);return error(out,"NOT A DOOM WAD");}
    uint32_t count=little(header+4),offset=little(header+8);
    if(!count || count>65536 || offset<12 || (uint64_t)offset+(uint64_t)count*16>(uint64_t)stat.st_size || lseek(fd,offset,SEEK_SET)<0) {
        close(fd);return error(out,"INVALID WAD DIRECTORY");
    }
    int advanced=0,episode=0,episode2=0,commercial=0,palette=0,doom_sprite=0,map=0;
    for(uint32_t i=0;i<count;i++) {
        unsigned char entry[16];char lump[9];
        if(read(fd,entry,16)!=16) {close(fd);return error(out,"SHORT WAD DIRECTORY");}
        uint32_t position=little(entry),size=little(entry+4);
        if((uint64_t)position+size>(uint64_t)stat.st_size) {close(fd);return error(out,"INVALID LUMP RANGE");}
        memcpy(lump,entry+8,8);lump[8]=0;
        for(unsigned j=0;j<8;j++)if(lump[j]>='a'&&lump[j]<='z')lump[j]=(char)(lump[j]-'a'+'A');
        if(!strcmp(lump,"TEXTMAP")||!strcmp(lump,"DECORATE")||!strcmp(lump,"ZSCRIPT")||!strcmp(lump,"BEHAVIOR"))advanced=1;
        if(lump[0]=='E'&&lump[1]>='1'&&lump[1]<='4'&&lump[2]=='M'&&lump[3]>='1'&&lump[3]<='9'&&!lump[4]) {
            episode=map=1;if(lump[1]>'1')episode2=1;
            if(!out->first_map) {out->first_episode=lump[1]-'0';out->first_map=lump[3]-'0';}
        }
        if(!strncmp(lump,"MAP",3)&&lump[3]>='0'&&lump[3]<='9'&&lump[4]>='0'&&lump[4]<='9'&&!lump[5]) {
            commercial=map=1;if(!out->first_map)out->first_map=(lump[3]-'0')*10+lump[4]-'0';
        }
        if(!strcmp(lump,"PLAYPAL"))palette=1;
        if(!strcmp(lump,"TROOA1")||!strcmp(lump,"TROOA1C1"))doom_sprite=1;
    }
    if(!base && stat.st_size==12361532) {
        static const unsigned char chex_hash[32]={0xd8,0xeb,0x52,0x77,0x91,0x88,0x83,0xf4,0x90,0xfb,0x1a,0x4b,0xe3,0xc9,0xa8,0x58,0x8d,0xf2,0xdb,0xae,0xe6,0xdc,0x4b,0xeb,0x8d,0xf4,0x92,0x91,0x48,0xbb,0xff,0xb1};
        SHA256_CTX sha;sha256_init(&sha);unsigned char data[32768],digest[32];ssize_t n;
        if(lseek(fd,0,SEEK_SET)>=0) {
            while((n=read(fd,data,sizeof(data)))>0)sha256_update(&sha,data,(size_t)n);
            sha256_final(&sha,digest);if(n==0&&!memcmp(digest,chex_hash,32)){base=1;out->chex=1;}
        }
    }
    close(fd);
    out->family=commercial?XZ_WAD_DOOM2:episode?XZ_WAD_DOOM:XZ_WAD_ANY;
    out->shareware=base&&episode&&!episode2&&!commercial;
    if(advanced) {out->kind=XZ_WAD_ADVANCED;snprintf(out->reason,sizeof(out->reason),"GZDOOM REQUIRED");}
    else if(base && (!map || !palette || !doom_sprite))return error(out,"OTHER GAME ENGINE REQUIRED");
    else {out->kind=base?XZ_WAD_BASE:XZ_WAD_MAP;snprintf(out->reason,sizeof(out->reason),"%s",out->chex?"CHEX QUEST / BASE GAME":base?(out->shareware?"DOOM SHAREWARE":"BASE GAME / IWAD"):map?"CLASSIC MAP / PWAD":"CLASSIC ADD-ON / PWAD");}
    return 0;
}
static int extension(const char *name) {
    size_t n=strlen(name);return n>=4 && (!strcasecmp(name+n-4,".wad")||!strcasecmp(name+n-4,".pk3")||!strcasecmp(name+n-4,".pk7"));
}
static void scan(struct xz_wad_catalog *c,const char *path,unsigned depth) {
    if(depth>8 || c->visited>=30000 || c->count==XZ_WAD_FILES) {c->truncated=1;return;}
    DIR *dir=opendir(path);if(!dir)return;
    struct dirent *entry;
    while((entry=readdir(dir))) {
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        if(!strcmp(entry->d_name,"Halloween Troy 3D 2026.avc"))continue;
        if(++c->visited>=30000 || c->count==XZ_WAD_FILES) {c->truncated=1;break;}
        char next[XZ_WAD_PATH];struct stat stat;
        if(snprintf(next,sizeof(next),"%s/%s",path,entry->d_name)>=(int)sizeof(next) || lstat(next,&stat) || S_ISLNK(stat.st_mode))continue;
        if(S_ISDIR(stat.st_mode))scan(c,next,depth+1);
        else if(S_ISREG(stat.st_mode)&&extension(entry->d_name)) {
            struct xz_wad_file *file=&c->files[c->count++];xz_wad_inspect(next,file);
        }
    }
    closedir(dir);
}
static int compare(const void *a,const void *b) { return strcasecmp(((const struct xz_wad_file *)a)->path,((const struct xz_wad_file *)b)->path); }
int xz_wad_scan(struct xz_wad_catalog *c,const char *root) {
    struct stat stat;if(!root || lstat(root,&stat) || !S_ISDIR(stat.st_mode) || S_ISLNK(stat.st_mode))return -1;
    scan(c,root,0);qsort(c->files,c->count,sizeof(c->files[0]),compare);return 0;
}
int xz_wad_compatible(const struct xz_wad_file *base,const struct xz_wad_file *map,char reason[96]) {
    const char *error=NULL;
    if(!base || base->kind!=XZ_WAD_BASE)error="SELECT A COMPATIBLE BASE GAME FIRST";
    else if(map && map->kind!=XZ_WAD_MAP)error="THIS MOD REQUIRES ANOTHER ENGINE";
    else if(map && base->shareware)error="CUSTOM MAPS NEED FULL DOOM OR FREEDOOM";
    else if(map && base->chex)error="CHEX QUEST USES ITS OWN GAME DATA";
    else if(map && map->family!=XZ_WAD_ANY && map->family!=base->family)error="BASE GAME DOES NOT MATCH THIS MAP";
    snprintf(reason,96,"%s",error?error:"READY TO PLAY");return error?-1:0;
}
