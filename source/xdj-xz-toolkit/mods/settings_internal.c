/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "settings_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define ROOT "/root/settings"
#define NAMESPACE "VJ.Tools"
#define HEADER_BYTES 24u
#define PAYLOAD_BYTES 1023u
#define MIN_FREE_BYTES (256u * 1024u)
#define UBIFS_MAGIC 0x24051905

static const unsigned char magic[8] = {'X','Z','P','R','E','F','1',0};
static const char *slots[2] = {"preferences.0", "preferences.1"};
static unsigned nonce;
#ifdef XZ_SETTINGS_INTERNAL_TEST
static const char *test_root;
static int low_space;
void xz_settings_internal_test_root(const char *root) { test_root=root; }
void xz_settings_internal_test_low_space(int enabled) { low_space=enabled; }
#endif
static const char *root_path(void) {
#ifdef XZ_SETTINGS_INTERNAL_TEST
    return test_root ? test_root : ROOT;
#else
    return ROOT;
#endif
}

struct record {
    uint64_t generation;
    size_t length;
    char payload[PAYLOAD_BYTES+1];
    struct xz_settings settings;
    unsigned present;
};

static uint32_t crc_bytes(uint32_t crc,const unsigned char *data,size_t size) {
    for(size_t i=0;i<size;i++) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u & (uint32_t)-(int32_t)(crc&1u));
    }
    return crc;
}
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(unsigned char *p,uint32_t value) {
    for(unsigned n=0;n<4;n++) p[n]=(unsigned char)(value>>(8*n));
}
static int same_mount(int fd) {
    struct stat opened,current;
    return !fstat(fd,&opened)&&!lstat(root_path(),&current)&&S_ISDIR(current.st_mode)&&
        opened.st_dev==current.st_dev&&opened.st_ino==current.st_ino;
}
static int settings_volume(int fd) {
#ifdef XZ_SETTINGS_INTERNAL_TEST
    if(test_root) return same_mount(fd);
#endif
    struct statfs fs;
    if(fstatfs(fd,&fs)||(unsigned long)fs.f_type!=UBIFS_MAGIC) return 0;
    FILE *mounts=setmntent("/proc/mounts","r");
    if(!mounts) return 0;
    int found=0;struct mntent *entry;
    while((entry=getmntent(mounts))) {
        if(strcmp(entry->mnt_dir,ROOT)||strcmp(entry->mnt_type,"ubifs")) continue;
        const char *name=entry->mnt_fsname;
        if(strncmp(name,"ubi",3)) continue;
        name+=3;
        if(*name<'0'||*name>'9') continue;
        while(*name>='0'&&*name<='9') name++;
        if(!strcmp(name,":settings")) { found=1;break; }
    }
    endmntent(mounts);
    return found&&same_mount(fd);
}
static int has_space(int fd) {
#ifdef XZ_SETTINGS_INTERNAL_TEST
    if(low_space) return 0;
#endif
    struct statvfs fs;
    if(fstatvfs(fd,&fs)||!fs.f_frsize||(fs.f_flag&ST_RDONLY)) return 0;
    return fs.f_bavail >= (MIN_FREE_BYTES+fs.f_frsize-1)/fs.f_frsize;
}
static int open_store(int writing,int *root,int *folder) {
    *root=*folder=-1;
    *root=open(root_path(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(*root<0||!settings_volume(*root)) return XZ_INTERNAL_SETTINGS_UNAVAILABLE;
    if(writing&&!has_space(*root)) return XZ_INTERNAL_SETTINGS_LOW_SPACE;
    if(writing&&mkdirat(*root,NAMESPACE,0700)&&errno!=EEXIST) return XZ_INTERNAL_SETTINGS_IO;
    *folder=openat(*root,NAMESPACE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(*folder<0) return !writing&&errno==ENOENT?XZ_INTERNAL_SETTINGS_MISSING:XZ_INTERNAL_SETTINGS_IO;
    struct stat a,b;
    if(fstat(*root,&a)||fstat(*folder,&b)||a.st_dev!=b.st_dev||!same_mount(*root)) return XZ_INTERNAL_SETTINGS_UNAVAILABLE;
    return 0;
}
static void close_store(int root,int folder) {
    if(folder>=0) close(folder);
    if(root>=0) close(root);
}
static int read_slot(int folder,unsigned slot,struct record *record) {
    int fd=openat(folder,slots[slot],O_RDONLY|O_NOFOLLOW);
    if(fd<0) return errno==ENOENT?1:-1;
    unsigned char bytes[HEADER_BYTES+PAYLOAD_BYTES];
    struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<(off_t)HEADER_BYTES||st.st_size>(off_t)sizeof(bytes)) {close(fd);return -1;}
    size_t got=0;
    while(got<(size_t)st.st_size) {
        ssize_t n=read(fd,bytes+got,(size_t)st.st_size-got);
        if(n<0&&errno==EINTR) continue;
        if(n<=0) {close(fd);return -1;}
        got+=(size_t)n;
    }
    close(fd);
    uint32_t length=get32(bytes+16);
    if(memcmp(bytes,magic,sizeof(magic))||!length||length>PAYLOAD_BYTES||got!=HEADER_BYTES+length) return -1;
    uint32_t crc=crc_bytes(crc_bytes(0xffffffffu,bytes,20),bytes+HEADER_BYTES,length)^0xffffffffu;
    if(crc!=get32(bytes+20)||memchr(bytes+HEADER_BYTES,0,length)) return -1;
    record->generation=(uint64_t)get32(bytes+8)|((uint64_t)get32(bytes+12)<<32);
    if(!record->generation) return -1;
    record->length=length;
    memcpy(record->payload,bytes+HEADER_BYTES,length);record->payload[length]=0;
    return xz_settings_parse_present(record->payload,&record->settings,&record->present)?-1:0;
}
static int newest(const struct record records[2],const int status[2]) {
    if(status[0]&&status[1]) return -1;
    if(status[0]) return 1;
    if(status[1]) return 0;
    return records[1].generation>records[0].generation?1:0;
}
int xz_settings_internal_load(struct xz_settings *out, unsigned *present) {
    if(!out) return XZ_INTERNAL_SETTINGS_CORRUPT;
    int root,folder,rc=open_store(0,&root,&folder);
    if(!rc) {
        struct record records[2];
        int status[2]={read_slot(folder,0,&records[0]),read_slot(folder,1,&records[1])};
        int latest=newest(records,status);
        if(!same_mount(root)) rc=XZ_INTERNAL_SETTINGS_UNAVAILABLE;
        else if(latest>=0) { *out=records[latest].settings; if(present) *present=records[latest].present; }
        else rc=status[0]==1&&status[1]==1?XZ_INTERNAL_SETTINGS_MISSING:XZ_INTERNAL_SETTINGS_CORRUPT;
    }
    close_store(root,folder);return rc;
}
int xz_settings_internal_save(const struct xz_settings *settings) {
    const char *readonly=getenv("XZ_MODS_SETTINGS_READONLY");
    if(readonly&&!strcmp(readonly,"1")) return XZ_INTERNAL_SETTINGS_UNAVAILABLE;
    unsigned char bytes[HEADER_BYTES+PAYLOAD_BYTES];
    char text[PAYLOAD_BYTES+1];
    int length=settings?xz_settings_format(settings,text,sizeof(text)):-1;
    if(length<=0) return XZ_INTERNAL_SETTINGS_CORRUPT;
    int root,folder,lock=-1,rc=open_store(1,&root,&folder);
    if(rc) goto done;
    lock=openat(folder,"preferences.lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    struct stat lock_stat;
    if(lock<0||fstat(lock,&lock_stat)||!S_ISREG(lock_stat.st_mode)||lock_stat.st_nlink!=1||lock_stat.st_size>1||flock(lock,LOCK_EX|LOCK_NB)) {rc=XZ_INTERNAL_SETTINGS_IO;goto done;}
    struct record records[2];
    int status[2]={read_slot(folder,0,&records[0]),read_slot(folder,1,&records[1])};
    int latest=newest(records,status);
    /* Two unreadable slots hold nothing worth keeping: start over in slot 0. */
    if(latest>=0&&records[latest].length==(size_t)length&&!memcmp(records[latest].payload,text,(size_t)length)) goto done;
    uint64_t generation=latest<0?1:records[latest].generation+1;
    if(!generation) {rc=XZ_INTERNAL_SETTINGS_CORRUPT;goto done;}
    unsigned target=latest<0?0u:(unsigned)(1-latest);
    memcpy(bytes,magic,sizeof(magic));put32(bytes+8,(uint32_t)generation);put32(bytes+12,(uint32_t)(generation>>32));put32(bytes+16,(uint32_t)length);
    memcpy(bytes+HEADER_BYTES,text,(size_t)length);
    put32(bytes+20,crc_bytes(crc_bytes(0xffffffffu,bytes,20),bytes+HEADER_BYTES,(size_t)length)^0xffffffffu);
    char temporary[80];int fd=-1;
    for(unsigned attempt=0;attempt<4;attempt++) {
        snprintf(temporary,sizeof(temporary),".preferences.%ld.%u.tmp",(long)getpid(),++nonce);
        fd=openat(folder,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd>=0||errno!=EEXIST) break;
    }
    if(fd<0) {rc=XZ_INTERNAL_SETTINGS_IO;goto done;}
    size_t written=0,total=HEADER_BYTES+(size_t)length;
    while(written<total) {
        ssize_t n=write(fd,bytes+written,total-written);
        if(n<0&&errno==EINTR) continue;
        if(n<=0) break;
        written+=(size_t)n;
    }
    int synced=written==total&&fsync(fd)==0;
    if(close(fd)) synced=0;
    if(!synced||!same_mount(root)||renameat(folder,temporary,folder,slots[target])) {
        unlinkat(folder,temporary,0);rc=XZ_INTERNAL_SETTINGS_IO;goto done;
    }
    if(fsync(folder)||fsync(root)||!same_mount(root)) {rc=XZ_INTERNAL_SETTINGS_IO;goto done;}
    struct record check;
    if(read_slot(folder,target,&check)||check.generation!=generation||check.length!=(size_t)length||memcmp(check.payload,text,(size_t)length)) rc=XZ_INTERNAL_SETTINGS_IO;
done:
    if(lock>=0) close(lock);
    close_store(root,folder);return rc;
}
