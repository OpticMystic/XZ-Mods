/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "shell_media.h"
#include <assert.h>
#include <errno.h>
#include <unistd.h>

static char base[1024],media[1200],sysblock[1200],mountinfo[1200],mountsfile[1200];
static void directory(const char *path) {
 char copy[4096];snprintf(copy,sizeof copy,"%s",path);
 for(char *p=copy+1;*p;p++)if(*p=='/'){*p=0;assert(!mkdir(copy,0700)||errno==EEXIST);*p='/';}
 assert(!mkdir(copy,0700)||errno==EEXIST);
}
static void file(const char *path,const char *data) {
 FILE *f=fopen(path,"w");assert(f);assert(fputs(data,f)>=0);assert(!fclose(f));
}
static void stick(const char *disk,const char *port,const char *root) {
 char path[4096],target[4096],node[4096];
 snprintf(path,sizeof path,"%s/%s",sysblock,disk);directory(path);
 snprintf(path,sizeof path,"%s/%s/removable",sysblock,disk);file(path,"1\n");
 snprintf(target,sizeof target,"%s/devices/usb2/2-1/%s/%s:1.0/block/%s/%s1",base,port,port,disk,disk);directory(target);
 snprintf(node,sizeof node,"%s/%s/%s1",sysblock,disk,disk);assert(!symlink(target,node));
 snprintf(path,sizeof path,"%s/PIONEER/rekordbox",root);directory(path);
 snprintf(path,sizeof path,"%s/PIONEER/rekordbox/export.pdb",root);file(path,"same bytes\n");
}
static void mounts(int first,int second,unsigned id) {
 FILE *f=fopen(mountinfo,"w");assert(f);
 if(first)fprintf(f,"41 1 8:1 / %s/usb2/sda1 rw - fuseblk /dev/sda1 rw\n",media);
 if(second)fprintf(f,"%u 1 8:17 / %s/usb1/sdb1 rw - fuseblk /dev/sdb1 rw\n",id,media);
 assert(!fclose(f));
}
int main(void) {
 char temp[]="/tmp/xz-shell-media-XXXXXX";char *created=mkdtemp(temp);assert(created);
 snprintf(base,sizeof base,"%s",created);snprintf(media,sizeof media,"%s/media",base);
 snprintf(sysblock,sizeof sysblock,"%s/sys/block",base);snprintf(mountinfo,sizeof mountinfo,"%s/mountinfo",base);
 char one[1600],two[1600];snprintf(one,sizeof one,"%s/usb2/sda1",media);snprintf(two,sizeof two,"%s/usb1/sdb1",media);
 stick("sda","2-1.2",one);stick("sdb","2-1.1",two);
 snprintf(mountsfile,sizeof mountsfile,"%s/mounts",base);
 struct xz_shmedia_paths paths={mountinfo,media,sysblock,mountsfile};struct xz_shmedia a,b,held,now;
 mounts(0,1,42);
 assert(!xz_shmedia_discover(&paths,0,&a));assert(xz_shmedia_discover(&paths,1,&b));
 assert(!strcmp(b.root,two));held=b;
 puts("PASS physical USB2-only follows source topology despite usb1 mount namespace");
 mounts(1,1,42);assert(xz_shmedia_discover(&paths,0,&a));assert(xz_shmedia_discover(&paths,1,&b));
 mounts(0,1,42);assert(!xz_shmedia_discover(&paths,0,&a));assert(xz_shmedia_discover(&paths,1,&b));assert(xz_shmedia_same(&held,&b));
 puts("PASS two sticks/remove USB1 retains physical USB2 unchanged");
 mounts(0,0,0);assert(!xz_shmedia_discover(&paths,0,&a));assert(!xz_shmedia_discover(&paths,1,&b));
 puts("PASS removed USB2 cannot serve stale export still present in directory");
 mounts(0,1,43);assert(xz_shmedia_discover(&paths,1,&now));assert(!xz_shmedia_same(&held,&now));
 assert(!xz_shmedia_same_mount(&held,&now));assert(xz_shmedia_same_file(&held.file,&now.file));
 puts("PASS reinsert same device/path/export stat changes mount identity and rejects in-flight old result");
 char export[2048];snprintf(export,sizeof export,"%s/PIONEER/rekordbox/export.pdb",two);assert(!unlink(export));
 assert(!xz_shmedia_discover(&paths,1,&b));file(export,"same bytes\n");
 assert(xz_shmedia_discover(&paths,1,&b));
 struct stat changed=b.file;changed.st_mtim.tv_nsec=(changed.st_mtim.tv_nsec+1)%1000000000L;
 assert(!xz_shmedia_same_file(&b.file,&changed));
 char away[1800];snprintf(away,sizeof away,"%s-away",two);assert(!rename(two,away));
 assert(!xz_shmedia_discover(&paths,1,&b));assert(!rename(away,two));
 assert(!unlink(mountinfo));assert(!xz_shmedia_discover(&paths,1,&b));assert(b.probe_error);
 puts("PASS missing export/mount directory/unreadable mount table fail closed");
 file(mountinfo,"");
 FILE *fallback=fopen(mountsfile,"w");assert(fallback);
 fprintf(fallback,"rootfs / rootfs rw 0 0\n/dev/sdb1 %s fuseblk rw 0 0\n",two);assert(!fclose(fallback));
 assert(xz_shmedia_discover(&paths,1,&held));assert(!xz_shmedia_discover(&paths,0,&a));
 assert(!a.probe_error);
 struct xz_shmedia_guard guard={0};assert(xz_shmedia_refresh(&paths,1,&guard,&held));
 /* Same source/namespace/export stats, but a new sysfs attachment generation. */
 char node[1800],resolved[4096],moved[4100];snprintf(node,sizeof node,"%s/sdb/sdb1",sysblock);
 assert(realpath(node,resolved));snprintf(moved,sizeof moved,"%s-old",resolved);
 assert(!rename(resolved,moved));directory(resolved);
 assert(xz_shmedia_discover(&paths,1,&now));assert(!xz_shmedia_same_mount(&held,&now));
 assert(!xz_shmedia_refresh(&paths,1,&guard,&now));assert(now.probe_error==2);
 assert(!xz_shmedia_refresh(&paths,1,&guard,&now));assert(now.probe_error==2);
 file(mountsfile,"rootfs / rootfs rw 0 0\n");assert(!xz_shmedia_discover(&paths,1,&b));assert(!b.probe_error);
 assert(!xz_shmedia_refresh(&paths,1,&guard,&b));assert(!guard.bound.mount_id);
 fallback=fopen(mountsfile,"w");assert(fallback);fprintf(fallback,"rootfs / rootfs rw 0 0\n/dev/sdb1 %s fuseblk rw 0 0\n",two);assert(!fclose(fallback));
 assert(xz_shmedia_refresh(&paths,1,&guard,&b));
 assert(!unlink(mountsfile));assert(!xz_shmedia_discover(&paths,1,&b));assert(b.probe_error);
 assert(!xz_shmedia_refresh(&paths,1,&guard,&b));assert(b.probe_error&&guard.bound.mount_id);
 puts("PASS empty mountinfo uses proven mounts fallback, attachment generation invalidates reuse, dual failure is explicit");
 assert(!xz_shmedia_mountpoint(media,media));assert(xz_shmedia_port("/other/usb2/2-1/2-1.1.2/2-1.1.2:1.0/")==-1);
 mounts(0,1,44);char removable[1800];snprintf(removable,sizeof removable,"%s/sdb/removable",sysblock);file(removable,"0\n");
 assert(!xz_shmedia_discover(&paths,1,&b));
 puts("PASS foreign ancestry/nonremovable storage cannot impersonate native source slots");
 /* Function address only: never probe the host's actual mount table in tests. */
 (void)&xz_shmedia_system;
 puts("PASS all offline shell media identity regressions");return 0;
}
