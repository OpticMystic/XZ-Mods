#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* In-memory boundary adapter. No real path, process, kernel, USB or network I/O. */
struct mock_file { char path[512], resolved[512]; unsigned char bytes[8192]; size_t size; struct stat st; };
static struct mock_file files[96];
static unsigned file_count;
struct mock_fd { struct mock_file *file; size_t pos; struct stat opened; int used; };
static struct mock_fd fds[128];
static int fault, reads, unexpected;
static struct mock_file *find(const char *path) {
    for (unsigned i=0;i<file_count;i++) if (!strcmp(files[i].path,path)) return files+i;
    return NULL;
}
static struct mock_file *put(const char *path, const void *bytes, size_t size, mode_t mode, ino_t ino, dev_t dev, dev_t rdev) {
    struct mock_file *f=find(path);
    if (!f) { assert(file_count < 96); f=&files[file_count++]; memset(f,0,sizeof(*f)); strcpy(f->path,path); }
    assert(size<=sizeof(f->bytes)); memcpy(f->bytes,bytes,size); f->size=size;
    f->st=(struct stat){.st_mode=mode,.st_ino=ino,.st_dev=dev,.st_rdev=rdev,.st_size=(off_t)size}; return f;
}
static void txt(const char *path,const char *value) { put(path,value,strlen(value),S_IFREG|0600,10,1,0); }
static int m_open(const char *path,int flags,...) {
    assert(!(flags & (O_WRONLY|O_RDWR|O_CREAT|O_TRUNC)));
    struct mock_file *f=find(path); if (!f) { errno=ENOENT; return -1; }
    for(int i=3;i<128;i++) if(!fds[i].used) {
        fds[i]=(struct mock_fd){f,0,f->st,1};
        if(fault==4&&!strcmp(path,"/dev/sdc1"))f->st.st_ino++;
        return i;
    }
    assert(0); return -1;
}
static int m_close(int fd) { assert(fd>=3&&fd<128&&fds[fd].used); fds[fd].used=0; return 0; }
static ssize_t m_read(int fd,void *data,size_t size) {
    assert(fd>=3&&fd<128&&fds[fd].used); struct mock_fd *d=fds+fd;
    if(size>d->file->size-d->pos)size=d->file->size-d->pos;
    memcpy(data,d->file->bytes+d->pos,size); d->pos+=size; return (ssize_t)size;
}
static int m_stat(const char *path,struct stat *s) { struct mock_file *f=find(path); if(!f){errno=ENOENT;return -1;} *s=f->st;return 0; }
static int m_fstat(int fd,struct stat *s) { assert(fd>=3&&fd<128&&fds[fd].used); *s=fds[fd].opened; return 0; }
static char *m_realpath(const char *path,char *out) { struct mock_file *f=find(path); if(!f){errno=ENOENT;return NULL;} strcpy(out,*f->resolved?f->resolved:path);return out; }
static int m_statfs(const char *path,struct statfs *fs) { if(!find(path))return -1; memset(fs,0,sizeof(*fs));fs->f_type=fault==8?0:0x01021994;return 0; }
static FILE *m_fopen(const char *path,const char *mode) { assert(!strcmp(mode,"r"));struct mock_file *f=find(path);if(!f)return NULL;return fmemopen(f->bytes,f->size,"r"); }
static DIR *m_opendir(const char *path) { assert(!strcmp(path,"/proc"));return (DIR *)(uintptr_t)1; }
static struct dirent *m_readdir(DIR *dir) { static struct dirent d; static int n; assert(dir==(DIR *)(uintptr_t)1); if(n++==0){strcpy(d.d_name,"42");return &d;}if(n==2&&fault==5){strcpy(d.d_name,"43");return &d;}n=0;return NULL; }
static int m_closedir(DIR *dir) { assert(dir==(DIR *)(uintptr_t)1);return 0; }
static int m_uname(struct utsname *u);
static ssize_t m_pread(int fd,void *out,size_t size,off_t offset);
/* Guard accidental entry into bridge execution or live side effects. */
static int forbidden(void) { unexpected++; abort(); }
static int m_execl(const char *program,const char *arg,...) { (void)program;(void)arg;return forbidden(); }
#define open m_open
#define close m_close
#define read m_read
#define pread m_pread
#define stat(...) m_stat(__VA_ARGS__)
#define lstat m_stat
#define fstat m_fstat
#define realpath m_realpath
#define statfs(...) m_statfs(__VA_ARGS__)
#define fopen m_fopen
#define opendir m_opendir
#define readdir m_readdir
#define closedir m_closedir
#define uname(...) m_uname(__VA_ARGS__)
#define socket(...) forbidden()
#define bind(...) forbidden()
#define fork(...) forbidden()
#define execl m_execl
#define mknod(...) forbidden()
#define unlink(...) forbidden()
#define write(...) forbidden()
#define ftruncate(...) forbidden()
#define mkdir(...) forbidden()
#define flock(...) forbidden()
#define main bridge_unused_main
#include "bridge.c"
#undef main
#undef stat
#undef statfs
#undef uname

static int m_uname(struct utsname *u) { memset(u,0,sizeof(*u));strcpy(u->release,fault==6?"wrong":KERNEL_RELEASE);return 0; }
static ssize_t m_pread(int fd,void *out,size_t size,off_t offset) {
    assert(fd>=3&&fd<128&&fds[fd].used); const char *path=fds[fd].file->path;
    memset(out,0,size);
    if(!strcmp(path,"/dev/sdc1")) {
        assert(offset==0&&size==512);memcpy(out,fds[fd].file->bytes,512);
        if(fault==1&&++reads==1) find(USB "2-1.1")->st.st_ino++;
        if(fault==2&&++reads==1) txt("/proc/self/mountinfo","51 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n");
        return (ssize_t)size;
    }
    if(!strcmp(path,"/proc/42/mem")) {
        uint32_t word=0;
        if(offset==0x494ec&&size==8) { const unsigned char sig[]={0x0c,0x30,0x90,0xe5,0x10,0x20,0x90,0xe5};memcpy(out,sig,8);return 8; }
        if(offset==0x49508)word=0x15d00e77;
        else if(offset==0x10e9688)word=0x20000;
        else if(offset==0x2000c&&size==8) { uint32_t r[]={0x30000,0x30008};memcpy(out,r,8);return 8; }
        else if(offset==0x30000&&size==8) { uint32_t p[]={0x40000,0x50000};memcpy(out,p,8);return 8; }
        else if(offset==0x40e76||offset==0x50e76) { uint16_t flags=fault==3;memcpy(out,&flags,2);return 2; }
        else assert(0);
        memcpy(out,&word,size); return (ssize_t)size;
    }
    if(!strcmp(path,"/dev/kmem")) {
        if(offset==0x805ffa58&&size==8){const unsigned char sig[]={0x2b,0x32,0xd5,0xe5,0x02,0x00,0x53,0xe3};memcpy(out,sig,8);return 8;}
        for(unsigned i=0;i<COUNT(kernel_crcs);i++)if(offset==(off_t)(0x1000+i*4)){
            uint32_t v=kernel_crcs[i].value+(fault==7&&i==0);memcpy(out,&v,4);return 4;
        }
        assert(0);
    }
    assert(0);return -1;
}
static unsigned checks;
#define CHECK(x) do { checks++; if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static void fixture(unsigned unit) {
    for(unsigned i=3;i<128;i++)assert(!fds[i].used);
    memset(files,0,sizeof(files));file_count=0;fault=reads=0;
    txt("/proc/sys/kernel/random/boot_id",unit?"22222222-2222-4222-8222-222222222222":"11111111-1111-4111-8111-111111111111");
    txt("/sys/class/net/eth0/address",unit?"02:11:22:33:44:66":"02:11:22:33:44:55");
    txt(USB "2-1/idVendor","0451");txt(USB "2-1/idProduct","8027");
    txt(USB "2-1.1/idVendor",unit?"abcd":"1234");txt(USB "2-1.1/idProduct",unit?"5678":"4321");
    txt(USB "2-1.1/serial",unit?"STICK_B":"STICK_A");txt(USB "2-1.1/devnum","7");txt(USB "2-1.1/busnum","2");
    put(USB "2-1.1","",0,S_IFDIR|0700,100+unit,1,0);
    struct mock_file *partition=put("/sys/class/block/sdc1","",0,S_IFDIR|0700,200+unit,1,0);
    strcpy(partition->resolved,"/sys/devices/platform/ehci/usb2/2-1/2-1.1/2-1.1:1.0/host0/target0/block/sdc/sdc1");
    txt("/sys/class/block/sdc/removable","1");txt("/sys/class/block/sdc1/dev","8:33");
    txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n");
    put("/media/usb2/sdc1","",0,S_IFDIR|0700,300+unit,makedev(0,42),0);
    unsigned char sector[512]={0};memcpy(sector+3,"EXFAT   ",8);sector[510]=0x55;sector[511]=0xaa;sector[100]=(unsigned char)unit;
    put("/dev/sdc1",sector,512,S_IFBLK|0600,400+unit,2,makedev(8,33));
    txt("/proc/42/stat","42 (rbp) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 777 0");
    txt("/proc/42/comm","rbp");txt("/proc/43/comm","rbp");
    txt("/proc/42/exe","synthetic application");txt("/dev/shm/receiver","synthetic receiver");txt("/dev/shm/runtime","synthetic runtime");
    const char env[]="XZ_MODS_SETTINGS_READONLY=1\0LD_PRELOAD=/dev/shm/receiver:/dev/shm/runtime\0";
    put("/proc/42/environ",env,sizeof(env),S_IFREG|0600,11,1,0);
    txt("/proc/42/maps","1000-2000 r-xp 0 00:01 10 /dev/shm/receiver\n2000-3000 r-xp 0 00:01 10 /dev/shm/runtime\n");
    txt("/proc/42/mem","");txt("/dev/kmem","");
    char symbols[4096]={0};size_t at=0;
    for(unsigned i=0;i<COUNT(kernel_crcs);i++)at+=(size_t)snprintf(symbols+at,sizeof(symbols)-at,"%x r %s\n",0x1000+i*4,kernel_crcs[i].name);
    txt("/proc/kallsyms",symbols);
}
#ifdef BASELINE
int main(void) { fixture(0); CHECK(qualify_music()); fixture(1); CHECK(qualify_music()); return 0; }
#else
static struct native native_fixture(void) { struct native n={.memory=-1};CHECK(qualify_kernel());CHECK(qualify_native(&n,42));CHECK(native_stopped(&n));return n; }
static void finish(struct binding *b,struct native *n) { binding_close(b);if(n->memory>=0)m_close(n->memory); }
int main(void) {
    CHECK(self_test()==0);
    for(unsigned unit=0;unit<2;unit++) {
        fixture(unit);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n));CHECK(binding_check(&b));
        for(unsigned i=0;i<32;i++)CHECK(binding_check(&b));
        CHECK(!binding_admit(&b,&n));finish(&b,&n);CHECK(!binding_check(&b));
    }
    puts("PASS two synthetic controllers/sticks independently admitted; immutable stable lifetime");
    for(unsigned change=0;change<10;change++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};CHECK(binding_admit(&b,&n));
        switch(change) {
        case 0: txt(USB "2-1.1/devnum","8");break;
        case 1: find(USB "2-1.1")->st.st_ino++;break;
        case 2: find("/dev/sdc1")->st.st_ino++;break;
        case 3: txt("/proc/self/mountinfo","51 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n");break;
        case 4: find("/media/usb2/sdc1")->st.st_ino++;break;
        case 5: find("/dev/sdc1")->bytes[100]++;break;
        case 6: txt(USB "2-1.1/serial","OTHER");break;
        case 7: txt("/sys/class/net/eth0/address","02:aa:bb:cc:dd:ee");break;
        case 8: txt("/proc/sys/kernel/random/boot_id","22222222-2222-4222-8222-222222222222");break;
        case 9: strcpy(find("/sys/class/block/sdc1")->resolved,"/sys/devices/usb2/2-1/2-1.2/2-1.2:1.0/block/sdc/sdc1");break;
        }
        CHECK(!binding_check(&b));CHECK(b.state==BINDING_RETIRED);CHECK(!binding_admit(&b,&n));finish(&b,&n);
    }
    puts("PASS devnum/path reuse, mount/descriptor/volume/topology/unit/boot changes retire");
    for(int f=1;f<=4;f++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};fault=f;
        CHECK(!binding_admit(&b,&n));CHECK(b.state==BINDING_RETIRED);finish(&b,&n);
    }
    puts("PASS identity/mount/descriptor change during capture and nonstopped admission refuse");
    const char *bad[]={"", "50 20 -1:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n",
        "50 20 0:42 / /media/usb2/sdc1 rw - ntfs /dev/sdc1 rw\n", "50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw",
        "50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw extra\n",
        "50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n51 20 0:43 / /media/usb2/sdd1 rw - fuseblk /dev/sdd1 rw\n",
        "50 20 0:42 / /media/usb2/sdc1\\040x rw - fuseblk /dev/sdc1 rw\n"};
    for(unsigned i=0;i<COUNT(bad);i++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};txt("/proc/self/mountinfo",bad[i]);
        CHECK(!binding_admit(&b,&n));finish(&b,&n);
    }
    for(unsigned i=0;i<4;i++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        if(i==0)txt(USB "2-1.1/serial","");
        if(i==1)txt(USB "2-1.1/idVendor","zzzz");
        if(i==2)memcpy(find("/dev/sdc1")->bytes+3,"NTFS    ",8);
        if(i==3)strcpy(find(USB "2-1.1/serial")->path,"/removed/serial");
        CHECK(!binding_admit(&b,&n));finish(&b,&n);
    }
    puts("PASS missing serial explicitly unsupported, malformed parsers and non-exFAT fuseblk refuse");
    for(unsigned kind=0;kind<2;kind++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdc1 rw - vfat /dev/sdc1 rw\n");
        memset(find("/dev/sdc1")->bytes+3,0,8);memcpy(find("/dev/sdc1")->bytes+(kind?82:54),kind?"FAT32   ":"FAT16   ",8);
        CHECK(binding_admit(&b,&n));CHECK(binding_check(&b));finish(&b,&n);
    }
    puts("PASS loader-supported FAT16/FAT32 volume capture");
    for(unsigned fence=0;fence<10;fence++) {
        fixture(0);struct native n={.memory=-1};
        if(fence==0)txt("/proc/42/exe","wrong app");
        if(fence==1)txt("/dev/shm/runtime","wrong runtime");
        if(fence==2)txt("/dev/shm/receiver","wrong receiver");
        if(fence==3)txt("/proc/42/environ","XZ_MODS_SETTINGS_READONLY=0");
        if(fence==4)fault=5;
        if(fence==5)txt("/proc/42/comm","app");
        if(fence==6)fault=6;
        if(fence==7)fault=7;
        if(fence==8)fault=8;
        if(fence==9)strcpy(find("/dev/shm/receiver")->resolved,"/tmp/receiver");
        if(fence==6||fence==7)CHECK(!qualify_kernel());else CHECK(!qualify_native(&n,42));
        if(n.memory>=0)m_close(n.memory);
    }
    puts("PASS actual own C exact-app/runtime/receiver/readonly/process/kernel ABI and RAM-only fences");
    for(unsigned mutation=0;mutation<5;mutation++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n));
        if(mutation==0)find("/dev/sdc1")->bytes[106]^=2;
        if(mutation==1)find("/dev/sdc1")->bytes[107]^=1;
        if(mutation==2)find("/dev/sdc1")->bytes[112]=57;
        if(mutation==3)find("/dev/sdc1")->bytes[100]^=1;
        if(mutation==4)find("/dev/sdc1")->bytes[108]^=1;
        CHECK(binding_check(&b)==(mutation<3));
        finish(&b,&n);
    }
    puts("PASS exFAT mutable status retained; serial and geometry changes retire");
    fixture(0);struct native old=native_fixture();struct binding retired={.volume_fd=-1,.block_fd=-1};CHECK(binding_admit(&retired,&old));
    txt("/proc/self/mountinfo","51 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n");CHECK(!binding_check(&retired));
    txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n");CHECK(!binding_check(&retired));CHECK(!binding_admit(&retired,&old));finish(&retired,&old);
    fixture(1);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};CHECK(binding_admit(&b,&n));finish(&b,&n);
    CHECK(!unexpected);for(unsigned i=3;i<128;i++)CHECK(!fds[i].used);
    printf("PASS %u checks; fresh boot qualification; all boundary descriptors closed; no live I/O\n",checks);
    return 0;
}
#endif
