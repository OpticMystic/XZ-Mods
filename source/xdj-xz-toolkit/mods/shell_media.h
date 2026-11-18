/* SPDX-License-Identifier: MIT */
#ifndef XZ_SHELL_MEDIA_H
#define XZ_SHELL_MEDIA_H
/* XDJ-XZ 1.26 worker-side media identity. Native USB1/USB2 routing remains
 * slots 0/1; Linux mount directory ordinals are not a source identity. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

struct xz_shmedia {
 unsigned long mount_id;
 ino_t attachment_inode;
 dev_t attachment_device;
 ino_t root_inode;
 dev_t root_device;
 int probe_error;
 uint32_t generation; /* Filled atomically with this identity by the runtime worker probe. */
 char root[1024], database[1024], source[256];
 struct stat file;
};
struct xz_shmedia_paths { const char *mountinfo, *media, *sysblock, *mounts; };
static const struct xz_shmedia_paths xz_shmedia_system={"/proc/self/mountinfo","/media","/sys/block","/proc/mounts"};

static int xz_shmedia_same_file(const struct stat *a,const struct stat *b) {
 return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
        a->st_mtime==b->st_mtime&&a->st_ctime==b->st_ctime&&
        a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int xz_shmedia_same_location(const struct xz_shmedia *a,const struct xz_shmedia *b) {
 return a->mount_id&&a->mount_id==b->mount_id&&a->root_inode==b->root_inode&&a->root_device==b->root_device&&
        !strcmp(a->root,b->root)&&!strcmp(a->source,b->source);
}
static int xz_shmedia_same_mount(const struct xz_shmedia *a,const struct xz_shmedia *b) {
 return xz_shmedia_same_location(a,b)&&a->attachment_inode==b->attachment_inode&&a->attachment_device==b->attachment_device;
}
static int xz_shmedia_same(const struct xz_shmedia *a,const struct xz_shmedia *b) {
 return xz_shmedia_same_mount(a,b)&&!strcmp(a->database,b->database)&&xz_shmedia_same_file(&a->file,&b->file);
}
/* Mountinfo escapes cannot denote supported device/mount paths. Reject instead
 * of interpreting arbitrary whitespace, aliases, submounts or traversal. */
static int xz_shmedia_mountpoint(const char *point,const char *media) {
 size_t n=strlen(media);
 if(strncmp(point,media,n)||strncmp(point+n,"/usb",4)||
    (point[n+4]!='1'&&point[n+4]!='2'))return 0;
 const char *s=point+n+5;
 if(!*s)return 1;
 if(*s++!='/'||!*s)return 0;
 for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'))return 0;
 return 1;
}
/* Exact platform topology from retained XZ identity evidence: host usb2,
 * hub 2-1, physical top USB1=2-1.2, physical top USB2=2-1.1. Nested hubs
 * and other controllers are deliberately not relabelled as those slots. */
static int xz_shmedia_port(const char *path) {
 if(strstr(path,"/usb2/2-1/2-1.2/2-1.2:1.0/"))return 0;
 if(strstr(path,"/usb2/2-1/2-1.1/2-1.1:1.0/"))return 1;
 return -1;
}
static int xz_shmedia_attachment(const struct xz_shmedia_paths *paths,const char *source,
                                int slot,struct stat *identity) {
 if(strncmp(source,"/dev/sd",7))return 0;
 const char *s=source+7;while(*s>='a'&&*s<='z')s++;
 if(s==source+7)return 0;
 const char *partition=s;while(*s>='0'&&*s<='9')s++;
 if(*s)return 0;
 char disk[64],node[2048],resolved[4096];size_t n=(size_t)(partition-(source+5));
 if(n>=sizeof disk)return 0;
 memcpy(disk,source+5,n);disk[n]=0;
 if(snprintf(node,sizeof node,"%s/%s/removable",paths->sysblock,disk)>=(int)sizeof node)return 0;
 FILE *f=fopen(node,"r");if(!f)return 0;int c=fgetc(f);fclose(f);if(c!='1')return 0;
 int length=s==partition?snprintf(node,sizeof node,"%s/%s",paths->sysblock,disk):
                         snprintf(node,sizeof node,"%s/%s/%s",paths->sysblock,disk,source+5);
 if(length<0||length>=(int)sizeof node)return 0;
 if(!realpath(node,resolved)||xz_shmedia_port(resolved)!=slot||stat(resolved,identity))return 0;
 return 1;
}
/* Returns only an export on an actual mounted removable partition belonging
 * to the requested physical slot. No directory scan, no unreadable-proc trust.
 * Duplicate candidates for one physical port fail closed. */
static int xz_shmedia_candidate(const struct xz_shmedia_paths *paths,int slot,unsigned long id,
                                const char *point,const char *device,struct xz_shmedia *out) {
 if(!xz_shmedia_mountpoint(point,paths->media)||strlen(device)>=sizeof out->source)return 0;
 struct stat attachment,root,file;
 if(!xz_shmedia_attachment(paths,device,slot,&attachment)||lstat(point,&root)||!S_ISDIR(root.st_mode))return 0;
 struct xz_shmedia candidate;memset(&candidate,0,sizeof candidate);
 if(snprintf(candidate.database,sizeof candidate.database,"%s/PIONEER/rekordbox/export.pdb",point)>=(int)sizeof candidate.database)return 0;
 if(lstat(candidate.database,&file)||!S_ISREG(file.st_mode)||file.st_dev!=root.st_dev)return 0;
 candidate.mount_id=id;candidate.attachment_inode=attachment.st_ino;candidate.attachment_device=attachment.st_dev;
 candidate.root_inode=root.st_ino;candidate.root_device=root.st_dev;
 snprintf(candidate.root,sizeof candidate.root,"%s",point);
 snprintf(candidate.source,sizeof candidate.source,"%s",device);candidate.file=file;
 *out=candidate;return 1;
}
static int xz_shmedia_discover(const struct xz_shmedia_paths *paths,int slot,struct xz_shmedia *out) {
 memset(out,0,sizeof *out);if(slot<0||slot>1)return 0;
 /* Some qualified XZ kernels expose empty mountinfo. /proc/mounts is the
  * proven fallback; neither directory presence nor an unreadable table grants
  * presence. Root+attachment identities replace the unavailable mount ID. */
 for(int fallback=0;fallback<2;fallback++){
  const char *input=fallback?paths->mounts:paths->mountinfo;
  FILE *f=input?fopen(input,"r"):NULL;if(!f)continue;
  char line[4096];int found=0,records=0,ambiguous=0;
  while(fgets(line,sizeof line,f)) {
   char *fields[64],*save=NULL;int count=0,sep=-1;
   for(char *s=strtok_r(line," \n",&save);s&&count<64;s=strtok_r(NULL," \n",&save)){
    fields[count]=s;if(!strcmp(s,"-"))sep=count;count++;
   }
   const char *device,*point;unsigned long id=1;
   if(fallback){if(count<4)continue;device=fields[0];point=fields[1];}
   else{
    if(count<10||sep<6||sep+3>=count)continue;
    char *end=NULL;id=strtoul(fields[0],&end,10);if(!id||!end||*end)continue;
    if(strcmp(fields[3],"/"))continue;
    device=fields[sep+2];point=fields[4];
   }
   records++;
   struct xz_shmedia candidate;
   if(!xz_shmedia_candidate(paths,slot,id,point,device,&candidate))continue;
   if(found){ambiguous=1;break;}
   *out=candidate;found=1;
  }
  int failed=ferror(f);fclose(f);
  if(!failed&&records){
   if(ambiguous||!found){memset(out,0,sizeof *out);out->probe_error=ambiguous;return 0;}
   return 1;
  }
  memset(out,0,sizeof *out);
 }
 out->probe_error=1;return 0;
}
struct xz_shmedia_guard { struct xz_shmedia bound; };
/* Serialised by the worker-side caller. A stale fuse mount must not be rebound
 * to a new attachment merely because /dev/sdX was reused. Require a different
 * mounted-root identity or an observed absence before admitting that binding.
 * An unreadable table is not evidence of absence and cannot clear quarantine. */
static int xz_shmedia_refresh(const struct xz_shmedia_paths *paths,int slot,
                              struct xz_shmedia_guard *guard,struct xz_shmedia *out) {
 if(!xz_shmedia_discover(paths,slot,out)){
  if(!out->probe_error)memset(&guard->bound,0,sizeof guard->bound);
  return 0;
 }
 if(xz_shmedia_same_location(&guard->bound,out)&&!xz_shmedia_same_mount(&guard->bound,out)){
  memset(out,0,sizeof *out);out->probe_error=2;return 0;
 }
 guard->bound=*out;return 1;
}
#endif
