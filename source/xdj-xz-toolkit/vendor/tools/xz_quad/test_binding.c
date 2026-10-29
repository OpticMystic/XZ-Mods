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
static struct mock_file files[192];
static unsigned file_count;
struct mock_fd { struct mock_file *file; size_t pos; struct stat opened; int used; };
static struct mock_fd fds[128];
static int fault, reads, unexpected;
static unsigned bind_writes, music_stat_reads, module_music_reads, native_flag_reads;
static char last_bind[32];
static uint32_t fixture_engine, fixture_begin, fixture_end, fixture_players[2];
static uint16_t fixture_flags[2];
static unsigned slot_reads, range_reads, torn_read, lifecycle, event_waits, ready_waits, node_creates, node_removes;
static unsigned lifecycle_port=1, settle_waits, retry_waits;
static unsigned native_deck_waits, native_pause_waits;
static int m_command(const char *,const char *,const char *,const char *,const char *,const char *);
static int m_event_socket(void);
static void m_event_wait(int,int);
static int m_debounce_wait(int);
static void mock_loopback(unsigned,int,int);
static unsigned fixture_music_port=2;
static struct mock_file *find(const char *path) {
    for (unsigned i=0;i<file_count;i++) if (!strcmp(files[i].path,path)) return files+i;
    return NULL;
}
static struct mock_file *put(const char *path, const void *bytes, size_t size, mode_t mode, ino_t ino, dev_t dev, dev_t rdev) {
    struct mock_file *f=find(path);
    if (!f) { assert(file_count < 192); f=&files[file_count++]; memset(f,0,sizeof(*f)); strcpy(f->path,path); }
    assert(size<=sizeof(f->bytes)); memcpy(f->bytes,bytes,size); f->size=size;
    f->st=(struct stat){.st_mode=mode,.st_ino=ino,.st_dev=dev,.st_rdev=rdev,.st_size=(off_t)size}; return f;
}
static void txt(const char *path,const char *value) { put(path,value,strlen(value),S_IFREG|0600,10,1,0); }
static int m_open(const char *path,int flags,...) {
    assert(!(flags & (O_WRONLY|O_RDWR|O_CREAT|O_TRUNC)) ||
        (lifecycle&&!strncmp(path,"/dev/shm/xz-four-deck/",22)) ||
        (!strcmp(path,"/sys/bus/usb/drivers/hub/bind") && flags==(O_WRONLY|O_CLOEXEC|O_NOFOLLOW)));
    struct mock_file *f=find(path);
    if(f&&(flags&O_CREAT)&&(flags&O_EXCL)){errno=EEXIST;return -1;}
    if(!f&&(flags&O_CREAT))f=put(path,"",0,S_IFREG|0600,20,1,0);
    if (!f) { errno=ENOENT; return -1; }
    if(flags&O_TRUNC)f->size=0;
    for(int i=3;i<128;i++) if(!fds[i].used) {
        fds[i]=(struct mock_fd){f,(flags&O_APPEND)?f->size:0,f->st,1};
        if(fault==4&&!strcmp(path,"/dev/sdc1"))f->st.st_ino++;
        return i;
    }
    assert(0); return -1;
}
static int m_close(int fd) { assert(fd>=3&&fd<128&&fds[fd].used); fds[fd].used=0; return 0; }
static ssize_t m_read(int fd,void *data,size_t size) {
    assert(fd>=3&&fd<128&&fds[fd].used); struct mock_fd *d=fds+fd;
    if(size>d->file->size-d->pos)size=d->file->size-d->pos;
    if(fault==11&&!strcmp(d->file->path,"/sys/module/xz_hub_depth/parameters/music_hub_devnum")&&++module_music_reads==3)d->file->bytes[1]='2';
    memcpy(data,d->file->bytes+d->pos,size); d->pos+=size; return (ssize_t)size;
}
static ssize_t m_write(int fd,const void *data,size_t size) {
    assert(fd>=3&&fd<128&&fds[fd].used);
    struct mock_fd *d=fds+fd;
    if(lifecycle&&!strncmp(d->file->path,"/dev/shm/xz-four-deck/",22)) {
        assert(d->pos+size<sizeof(d->file->bytes));memcpy(d->file->bytes+d->pos,data,size);d->pos+=size;
        if(d->pos>d->file->size)d->file->size=d->pos;
        d->file->bytes[d->file->size]=0;d->file->st.st_size=(off_t)d->file->size;return (ssize_t)size;
    }
    assert(!strcmp(d->file->path,"/sys/bus/usb/drivers/hub/bind"));
    assert(size<sizeof(last_bind));memcpy(last_bind,data,size);last_bind[size]=0;bind_writes++;
    if(fault==9){errno=EIO;return -1;}
    if(lifecycle)mock_loopback(3-fixture_music_port,1,1);
    return (ssize_t)size;
}
static int m_stat(const char *path,struct stat *s) {
    struct mock_file *f=find(path);if(!f){errno=ENOENT;return -1;}
    const char *music=fixture_music_port==1?"/sys/bus/usb/devices/2-1.2":"/sys/bus/usb/devices/2-1.1";
    if(fault==10&&!strcmp(path,music)&&++music_stat_reads==2)f->st.st_ino++;
    *s=f->st;return 0;
}
static int m_fstat(int fd,struct stat *s) { assert(fd>=3&&fd<128&&fds[fd].used); *s=fds[fd].opened; return 0; }
static char *m_realpath(const char *path,char *out) { struct mock_file *f=find(path); if(!f){errno=ENOENT;return NULL;} strcpy(out,*f->resolved?f->resolved:path);return out; }
static int m_statfs(const char *path,struct statfs *fs) { if(!find(path))return -1; memset(fs,0,sizeof(*fs));fs->f_type=fault==8?0:0x01021994;return 0; }
static FILE *m_fopen(const char *path,const char *mode) { assert(!strcmp(mode,"r"));struct mock_file *f=find(path);if(!f)return NULL;return fmemopen(f->bytes,f->size,"r"); }
static unsigned directory_cursor[3];
static DIR *m_opendir(const char *path) {
    unsigned id=!strcmp(path,"/proc")?1:!strcmp(path,"/sys/bus/usb/devices/")?2:0;
    assert(id);directory_cursor[id]=0;return (DIR *)(uintptr_t)id;
}
static struct dirent *m_readdir(DIR *dir) {
    static struct dirent d;unsigned id=(unsigned)(uintptr_t)dir;assert(id==1||id==2);
    if(id==1) {
        unsigned n=directory_cursor[id]++;
        if(n==0){strcpy(d.d_name,"42");return &d;}
        if(n==1&&fault==5){strcpy(d.d_name,"43");return &d;}
    } else while(directory_cursor[id]<file_count) {
        const char *path=files[directory_cursor[id]++].path,*prefix="/sys/bus/usb/devices/";
        if(strncmp(path,prefix,strlen(prefix)))continue;
        const char *name=path+strlen(prefix);if(!*name||strchr(name,'/'))continue;
        assert(strlen(name)<sizeof(d.d_name));strcpy(d.d_name,name);return &d;
    }
    return NULL;
}
static int m_closedir(DIR *dir) { assert(dir==(DIR *)(uintptr_t)1||dir==(DIR *)(uintptr_t)2);return 0; }
static int m_access(const char *path,int mode) { assert(mode==F_OK);if(find(path))return 0;errno=ENOENT;return -1; }
static int m_mkdir(const char *path,mode_t mode) {
    assert(lifecycle&&!strcmp(path,"/dev/shm/xz-four-deck"));
    if(find(path)){errno=EEXIST;return -1;}put(path,"",0,S_IFDIR|mode,30,1,0);return 0;
}
static int m_flock(int fd,int operation) { (void)operation;assert(lifecycle&&fds[fd].used);return 0; }
static int m_unlink(const char *path) {
    assert(lifecycle&&!strncmp(path,"/dev/shm/xz-four-deck/",22));
    struct mock_file *f=find(path);if(!f){errno=ENOENT;return -1;}
    if(!strcmp(path,"/dev/shm/xz-four-deck/usb-audio"))node_removes++;
    f->path[0]=0;return 0;
}
static int m_rename(const char *from,const char *to) {
    assert(lifecycle&&!strcmp(from,"/dev/shm/xz-four-deck/status.json.tmp")&&!strcmp(to,"/dev/shm/xz-four-deck/status.json"));
    struct mock_file *f=find(from),*old=find(to);assert(f);if(old)old->path[0]=0;strcpy(f->path,to);return 0;
}
static int m_mknod(const char *path,mode_t mode,dev_t device) {
    assert(lifecycle&&!strcmp(path,"/dev/shm/xz-four-deck/usb-audio"));
    assert(!find(path));put(path,"",0,mode,40,1,device);node_creates++;return 0;
}
static ssize_t m_readlink(const char *path,char *out,size_t size) {
    assert(!strcmp(path,"/proc/self/exe"));const char *exe="/dev/shm/helper/xz-auto-bridge";
    assert(strlen(exe)<size);memcpy(out,exe,strlen(exe));return (ssize_t)strlen(exe);
}
static int m_uname(struct utsname *u);
static ssize_t m_pread(int fd,void *out,size_t size,off_t offset);
/* Guard accidental entry into bridge execution or live side effects. */
static int forbidden(void) { unexpected++; abort(); }
static int __attribute__((unused)) m_execl(const char *program,const char *arg,...) { (void)program;(void)arg;return forbidden(); }
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
#define mknod m_mknod
#define unlink m_unlink
#define rename m_rename
#define access m_access
#define readlink m_readlink
#define write m_write
#define ftruncate(...) forbidden()
#define mkdir m_mkdir
#define flock m_flock
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
    if(!strncmp(path,"/dev/sd",7)) {
        assert(offset>=0&&size==512&&(size_t)offset+size<=fds[fd].file->size);memcpy(out,fds[fd].file->bytes+offset,size);
        if(fault==1&&++reads==1) {
            char name[128];snprintf(name,sizeof(name),USB "%s",xz_quad_port(fixture_music_port)->usb);find(name)->st.st_ino++;
        }
        if(fault==2&&++reads==1) {
            char line[160];snprintf(line,sizeof(line),"51 20 0:42 / /media/usb%u/sdc1 rw - fuseblk /dev/sdc1 rw\n",fixture_music_port);txt("/proc/self/mountinfo",line);
        }
        return (ssize_t)size;
    }
    if(!strcmp(path,"/proc/42/mem")) {
        uint32_t word=0;
        if(offset==0x494ec&&size==8) { const unsigned char sig[]={0x0c,0x30,0x90,0xe5,0x10,0x20,0x90,0xe5};memcpy(out,sig,8);if(fault==13)((unsigned char *)out)[0]^=1;return 8; }
        if(offset==0x49508)word=0x15d00e77+(fault==14);
        else if(offset==0x10e9688)word=fixture_engine;
        else if(offset==(off_t)fixture_engine+12&&size==8) {
            uint32_t r[]={fixture_begin,fixture_end};memcpy(out,r,8);range_reads++;
            if(torn_read==4)fixture_end=fixture_end==fixture_begin?fixture_begin+4:fixture_begin;
            if(torn_read==5)fixture_engine+=0x1000;
            if(torn_read==6)txt("/proc/42/stat","42 (rbp) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 778 0");
            return 8;
        }
        else if(offset==(off_t)fixture_begin&&(size==4||size==8)) {
            if(fault==16)return -1;
            assert(size<=fixture_end-fixture_begin);
            memcpy(out,fixture_players,size);slot_reads++;
            if(torn_read==1)fixture_players[0]=fixture_players[0]?0:0x40000;
            if(torn_read==2)fixture_engine+=0x1000;
            if(torn_read==3){fixture_begin+=0x100;fixture_end+=0x100;}
            if(torn_read==7&&slot_reads==1)fixture_flags[0]=0x100;
            if(torn_read==8&&slot_reads==2)fixture_end+=4;
            return (ssize_t)size;
        }
        else if(offset==0x40e76||offset==0x50e76||offset==0x40e77||offset==0x50e77) {
            if(fault==15)return -1;
            uint16_t flags=fixture_flags[offset>=0x50000];
            if(fault==3||(fault==12&&++native_flag_reads>=3))flags|=0x100;
            if((offset&1)==0){assert(size==2);memcpy(out,&flags,2);}
            else {assert(size==1);*(unsigned char *)out=(unsigned char)(flags>>8);}
            return (ssize_t)size;
        }
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
    memset(files,0,sizeof(files));file_count=0;fault=reads=0;fixture_music_port=2;bind_writes=music_stat_reads=module_music_reads=native_flag_reads=0;last_bind[0]=0;
    fixture_engine=0x20000;fixture_begin=0x30000;fixture_end=0x30008;
    fixture_players[0]=0x40000;fixture_players[1]=0x50000;fixture_flags[0]=fixture_flags[1]=0;
    slot_reads=range_reads=torn_read=lifecycle=event_waits=ready_waits=node_creates=node_removes=0;
    native_deck_waits=native_pause_waits=0;stopping=0;
    txt("/dev/shm/helper/xz_hub_depth.ko","synthetic module");
    txt("/proc/sys/kernel/random/boot_id",unit?"22222222-2222-4222-8222-222222222222":"11111111-1111-4111-8111-111111111111");
    txt("/sys/class/net/eth0/address",unit?"02:11:22:33:44:66":"02:11:22:33:44:55");
    txt(USB "2-1/idVendor","0451");txt(USB "2-1/idProduct","8027");
    txt(USB "2-1.1/idVendor",unit?"abcd":"1234");txt(USB "2-1.1/idProduct",unit?"5678":"4321");
    txt(USB "2-1.1/serial",unit?"STICK_B":"STICK_A");txt(USB "2-1.1/devnum","7");txt(USB "2-1.1/busnum","2");
    put(USB "2-1.1","",0,S_IFDIR|0700,100+unit,1,0);
    struct mock_file *partition=put("/sys/class/block/sdc1","",0,S_IFDIR|0700,200+unit,1,0);
    strcpy(partition->resolved,"/sys/devices/platform/ehci/usb2/2-1/2-1.1/2-1.1:1.0/host0/target0/block/sdc/sdc1");
    txt("/sys/class/block/sdc1/partition","1");
    struct mock_file *by_number=put("/sys/dev/block/8:33","",0,S_IFDIR,200+unit,1,0);
    strcpy(by_number->resolved,partition->resolved);
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
static struct native native_fixture(void) { struct native n={.memory=-1};CHECK(qualify_kernel());CHECK(qualify_native(&n,42));CHECK(native_decks_ready(&n));CHECK(native_stopped(&n));return n; }
static void finish(struct binding *b,struct native *n) { binding_close(b);if(n->memory>=0)m_close(n->memory); }

static void select_fixture_port(unsigned port) {
    if(port==2)return;
    CHECK(port==1);
    fixture_music_port=1;
    for(unsigned i=0;i<file_count;i++) {
        struct mock_file *f=files+i;
        for(char *p=f->path;(p=strstr(p,"2-1.1"));p+=5)p[4]='2';
        for(char *p=f->resolved;(p=strstr(p,"2-1.1"));p+=5)p[4]='2';
        char *p=strstr(f->path,"/media/usb2");if(p)p[10]='1';
    }
    struct mock_file *mount=find("/proc/self/mountinfo");
    char *p=strstr((char *)mount->bytes,"/media/usb2");CHECK(p!=NULL);p[10]='1';
}
static void fixture_port(unsigned port) {
    fixture(0);select_fixture_port(port);
}
static void port_tests(void) {
    for(unsigned port=1;port<=2;port++) {
        fixture_port(port);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n)==ADMIT_READY);CHECK(b.identity.music_port==port);CHECK(binding_check(&b));finish(&b,&n);
        for(unsigned change=0;change<7;change++) {
            fixture_port(port);n=native_fixture();b=(struct binding){.volume_fd=-1,.block_fd=-1};
            CHECK(binding_admit(&b,&n)==ADMIT_READY);
            char path[128],line[256];
            snprintf(path,sizeof(path),USB "%s",xz_quad_port(port)->usb);
            if(change==0)find(path)->st.st_ino++;
            if(change==1)strcpy(find("/sys/class/block/sdc1")->resolved,"/sys/wrong");
            if(change==2){snprintf(path,sizeof(path),USB "%s/devnum",xz_quad_port(port)->usb);txt(path,"8");}
            if(change==3){snprintf(line,sizeof(line),"51 20 0:42 / /media/usb%u/sdc1 rw - fuseblk /dev/sdc1 rw\n",port);txt("/proc/self/mountinfo",line);}
            if(change==4){snprintf(line,sizeof(line),"50 20 0:42 / /media/usb%u/sdc1 rw - fuseblk /dev/sdc1 rw\n",3-port);txt("/proc/self/mountinfo",line);}
            if(change==5)find("/dev/sdc1")->bytes[100]^=1;
            if(change==6)strcpy(find("/proc/self/mountinfo")->path,"/missing");
            CHECK(!binding_check(&b));CHECK(b.state==BINDING_RETIRED);
            CHECK(binding_admit(&b,&n)==ADMIT_FATAL);finish(&b,&n);
        }
        for(unsigned wrong=0;wrong<3;wrong++) {
            fixture_port(port);n=native_fixture();b=(struct binding){.volume_fd=-1,.block_fd=-1};
            char path[128];
            if(wrong==0)snprintf(find("/sys/class/block/sdc1")->resolved,512,"/sys/devices%sblock/sdc/sdc1",xz_quad_port(3-port)->ancestry);
            if(wrong==1){snprintf(path,sizeof(path),USB "%s/busnum",xz_quad_port(port)->usb);txt(path,"1");}
            if(wrong==2)txt("/proc/self/mountinfo","50 20 0:42 / /media/usb1/sdc1 rw - fuseblk /dev/sdc1 rw\n51 20 0:43 / /media/usb2/sdd1 rw - vfat /dev/sdd1 rw\n");
            CHECK(binding_admit(&b,&n)==ADMIT_UNSUPPORTED);CHECK(b.state==BINDING_NEW);
            CHECK(b.volume_fd==-1&&b.block_fd==-1);finish(&b,&n);
        }
        for(int f=1;f<=4;f++) {
            fixture_port(port);n=native_fixture();b=(struct binding){.volume_fd=-1,.block_fd=-1};fault=f;
            CHECK(binding_admit(&b,&n)!=ADMIT_READY);CHECK(b.state==BINDING_NEW);
            CHECK(b.volume_fd==-1&&b.block_fd==-1);fault=0;
            CHECK(binding_admit(&b,&n)==ADMIT_READY);finish(&b,&n);
        }
    }
    puts("PASS both music ports; exact ancestry, ambiguity refusal and permanent identity retirement");
}

static void mock_loopback(unsigned port,int bound,int audio) {
    const char *hub=xz_quad_port(port)->usb;char path[256];
    snprintf(path,sizeof(path),USB "%s",hub);put(path,"",0,S_IFDIR|0700,700+port,1,0);
    const char *keys[]={"idVendor","idProduct","bDeviceClass","speed","devnum"};
    const char *values[]={"2b73","002e","09","480","9"};
    for(unsigned i=0;i<COUNT(keys);i++){snprintf(path,sizeof(path),USB "%s/%s",hub,keys[i]);txt(path,values[i]);}
    if(bound){snprintf(path,sizeof(path),USB "%s/%s:1.0/driver",hub,hub);txt(path,"");strcpy(find(path)->resolved,"/sys/bus/usb/drivers/hub");}
    if(audio) {
        snprintf(path,sizeof(path),USB "%s.1",hub);put(path,"",0,S_IFDIR|0700,800+port,1,0);
        const char *child_keys[]={"idVendor","idProduct","busnum","speed","devnum"};
        const char *child_values[]={"2b73","002d","2","480","10"};
        for(unsigned i=0;i<COUNT(child_keys);i++){snprintf(path,sizeof(path),USB "%s.1/%s",hub,child_keys[i]);txt(path,child_values[i]);}
    }
}
static void loopback_port_tests(void) {
    for(unsigned music=1;music<=2;music++)for(unsigned shape=0;shape<7;shape++) {
        fixture_port(music);unsigned cable=3-music;struct device d;struct route r={0};
        if(shape==0){CHECK(admission_loopback_safe(cable));CHECK(!scan_device(&d,cable)&&!*d.hub);continue;}
        mock_loopback(shape==4?music:cable,shape==2,shape==3);
        if(shape==5)mock_loopback(music,0,0);
        if(shape==6){char path[128];snprintf(path,sizeof(path),USB "%s.7",xz_quad_port(cable)->usb);put(path,"",0,S_IFDIR,900,1,0);}
        CHECK(admission_loopback_safe(cable)==(shape==1));
        if(shape==4||shape==5){CHECK(scan_device(&d,cable)!=0);continue;}
        CHECK(!scan_device(&d,cable));CHECK(!strcmp(d.hub,xz_quad_port(cable)->usb));
        if(shape==1) {
            CHECK(next_action(&r,&d,0)==WAIT);CHECK(next_action(&r,&d,1)==BIND);
            r.bound=d;r.bind_attempted=1;mock_loopback(cable,1,1);
            CHECK(!scan_device(&d,cable));CHECK(next_action(&r,&d,1)==PUBLISH);
            r.published=d;CHECK(next_action(&r,&d,1)==WAIT);
            d.hub_inode++;CHECK(next_action(&r,&d,1)==CLEAR);
        }
    }
    fixture(0);CHECK(!admission_loopback_safe(0));CHECK(!admission_loopback_safe(3));
    puts("PASS both loopback ports; opposite-port guard, wrong/duplicate/bound/child refusals, route publish/clear");
}

static void hfs_fixture(unsigned x) {
    fixture(0);unsigned char data[1536]={0},*h=data+1024;
    h[0]='H';h[1]=x?'X':'+';h[3]=x?5:4;h[19]=7;
    h[42]=0x10;h[46]=0x10;h[111]=1;
    put("/dev/sdc1",data,sizeof(data),S_IFBLK|0600,400,2,makedev(8,33));
    txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdc1 rw - hfsplus /dev/sdc1 rw\n");
}
static void new_media_tests(void) {
    for(unsigned x=0;x<2;x++) {
        unsigned mutable[]={4,8,12,20,24,28,32,36,48,52,56,60,64,68,72,80,96,112,128,208,288,368,448};
        for(unsigned i=0;i<COUNT(mutable);i++) {
            hfs_fixture(x);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
            CHECK(binding_admit(&b,&n)==ADMIT_READY);find("/dev/sdc1")->bytes[1024+mutable[i]]^=1;
            CHECK(binding_check(&b));finish(&b,&n);
        }
        unsigned immutable[]={0,1,2,3,16,19,40,41,42,43,44,45,46,47,104,105,106,107,108,109,110,111};
        for(unsigned i=0;i<COUNT(immutable);i++) {
            hfs_fixture(x);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
            CHECK(binding_admit(&b,&n)==ADMIT_READY);find("/dev/sdc1")->bytes[1024+immutable[i]]^=1;
            CHECK(!binding_check(&b));CHECK(b.state==BINDING_RETIRED);
            CHECK(binding_admit(&b,&n)==ADMIT_FATAL);finish(&b,&n);
        }
        hfs_fixture(x);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        find("/dev/sdc1")->bytes[1024]='B';CHECK(binding_admit(&b,&n)==ADMIT_UNSUPPORTED);
        CHECK(b.state==BINDING_NEW);find("/dev/sdc1")->bytes[1024]='H';
        CHECK(binding_admit(&b,&n)==ADMIT_READY);finish(&b,&n);
    }
    puts("PASS HFS+/HFSX header identity; mutable dates/counters/forks; wrong signature; permanent active retirement");
    for(unsigned part=0;part<2;part++) {
        fixture(0);const char *name=part?"sdaa15":"sdc12",*disk=part?"sdaa":"sdc";
        char path[128],line[256],ancestry[256];
        snprintf(ancestry,sizeof(ancestry),"/sys/devices/platform/ehci/usb2/2-1/2-1.1/2-1.1:1.0/host0/target0/block/%s/%s",disk,name);
        struct mock_file *p=find("/sys/class/block/sdc1");snprintf(p->path,sizeof(p->path),"/sys/class/block/%s",name);strcpy(p->resolved,ancestry);
        strcpy(find("/sys/dev/block/8:33")->resolved,ancestry);
        snprintf(find("/sys/class/block/sdc1/dev")->path,512,"/sys/class/block/%s/dev",name);
        snprintf(find("/sys/class/block/sdc1/partition")->path,512,"/sys/class/block/%s/partition",name);
        snprintf(path,sizeof(path),"/sys/class/block/%s/partition",name);txt(path,part?"15":"12");
        snprintf(path,sizeof(path),"/sys/class/block/%s/removable",disk);txt(path,"0");
        snprintf(find("/dev/sdc1")->path,512,"/dev/%s",name);
        snprintf(find("/media/usb2/sdc1")->path,512,"/media/usb2/%s",name);
        snprintf(line,sizeof(line),"50 20 0:42 / /media/usb2/%s rw - fuseblk /dev/%s rw\n",name,name);txt("/proc/self/mountinfo",line);
        struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n)==ADMIT_READY);CHECK(binding_check(&b));finish(&b,&n);
    }
    puts("PASS later partitions, multi-letter sd names and fixed-bit USB2 storage");
    for(unsigned variant=0;variant<3;variant++) {
        fixture(0);if(variant==0)strcpy(find(USB "2-1.1/serial")->path,"/absent");
        else txt(USB "2-1.1/serial",variant==1?"":"Unusual USB / serial: \"quoted\"");
        struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n)==ADMIT_READY);CHECK((b.identity.serial_length==0)==(variant<2));
        CHECK(binding_check(&b));find(USB "2-1.1")->st.st_ino++;
        CHECK(!binding_check(&b));find(USB "2-1.1")->st.st_ino--;
        CHECK(!binding_check(&b));CHECK(binding_admit(&b,&n)==ADMIT_FATAL);finish(&b,&n);
    }
    puts("PASS absent/empty/unusual serial; clone with identical header still retires by USB generation");
    for(unsigned variant=0;variant<5;variant++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        if(variant==0)strcpy(find("/sys/dev/block/8:33")->resolved,"/sys/wrong");
        if(variant==1)txt("/sys/class/block/sdc1/dev","8:34");
        if(variant==2)txt("/sys/class/block/sdc1/partition","2");
        if(variant==3)txt("/sys/class/block/sdc/removable","unknown");
        if(variant==4)txt(USB "2-1.1/busnum","1");
        CHECK(binding_admit(&b,&n)==ADMIT_UNSUPPORTED);finish(&b,&n);
    }
    for(unsigned kind=0;kind<2;kind++) {
        fixture(0);txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdc1 rw - vfat /dev/sdc1 rw\n");
        memset(find("/dev/sdc1")->bytes+3,0,8);memcpy(find("/dev/sdc1")->bytes+(kind?82:54),kind?"FAT32   ":"FAT16   ",8);
        struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n)==ADMIT_READY);find("/dev/sdc1")->bytes[kind?65:37]^=1;
        CHECK(binding_check(&b));find("/dev/sdc1")->bytes[kind?67:39]^=1;
        CHECK(!binding_check(&b));finish(&b,&n);
    }
    puts("PASS sysfs major/minor/partition/topology negatives; FAT mutable state versus volume ID");
}
static void port_format_tests(void) {
    for(unsigned port=1;port<=2;port++)for(unsigned format=0;format<5;format++) {
        if(format>=3){hfs_fixture(format==4);select_fixture_port(port);}
        else {
            fixture_port(port);
            if(format<2) {
                char line[160];snprintf(line,sizeof(line),"50 20 0:42 / /media/usb%u/sdc1 rw - vfat /dev/sdc1 rw\n",port);txt("/proc/self/mountinfo",line);
                memset(find("/dev/sdc1")->bytes+3,0,8);memcpy(find("/dev/sdc1")->bytes+(format?82:54),format?"FAT32   ":"FAT16   ",8);
            }
        }
        struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n)==ADMIT_READY);CHECK(b.identity.music_port==port);CHECK(binding_check(&b));finish(&b,&n);
    }
    puts("PASS unchanged FAT16/FAT32/exFAT/HFS+/HFSX admission on both music ports");
}
static const char no_music_mounts[] = "1 0 0:1 / / rw - rootfs rootfs rw\n";
static void hide_prefix(const char *prefix) {
    size_t length=strlen(prefix);
    for(unsigned i=0;i<file_count;i++)if(!strncmp(files[i].path,prefix,length))
        snprintf(files[i].path,sizeof(files[i].path),"/removed/%u",i);
}
static void setup_music_hub(unsigned top,int bound) {
    const struct xz_quad_port *port=xz_quad_port(top);char path[256];
    snprintf(path,sizeof(path),USB "%s",port->usb);put(path,"",0,S_IFDIR|0700,900+top,1,0);
    const char *keys[]={"idVendor","idProduct","bDeviceClass","speed","busnum","devnum","devpath","bNumConfigurations","bNumInterfaces"};
    const char *values[]={"05e3","0610","09","480","2","11",port->devpath,"1"," 1"};
    for(unsigned i=0;i<COUNT(keys);i++){snprintf(path,sizeof(path),USB "%s/%s",port->usb,keys[i]);txt(path,values[i]);}
    if(bound){snprintf(path,sizeof(path),USB "%s/%s:1.0/driver",port->usb,port->usb);txt(path,"");strcpy(find(path)->resolved,"/sys/bus/usb/drivers/hub");}
}
static void hub_media_fixture(unsigned music,unsigned count) {
    fixture_port(music);
    const struct xz_quad_port *port=xz_quad_port(music);char prefix[128],path[512],ancestry[512],line[512];
    snprintf(prefix,sizeof(prefix),USB "%s",port->usb);
    unsigned original_count=file_count;
    for(unsigned i=0;i<original_count;i++)if(!strncmp(files[i].path,prefix,strlen(prefix))) {
        struct mock_file copy=files[i];
        snprintf(path,sizeof(path),"%s.1%s",prefix,copy.path+strlen(prefix));
        put(path,copy.bytes,copy.size,copy.st.st_mode,copy.st.st_ino,copy.st.st_dev,copy.st.st_rdev);
    }
    snprintf(ancestry,sizeof(ancestry),"/sys/devices/platform/ehci/usb2/2-1/%s/%s.1/%s.1:1.0/host0/target0/block/sdc/sdc1",port->usb,port->usb,port->usb);
    strcpy(find("/sys/class/block/sdc1")->resolved,ancestry);strcpy(find("/sys/dev/block/8:33")->resolved,ancestry);
    setup_music_hub(music,1);
    if(count>1) {
        original_count=file_count;
        for(unsigned i=0;i<original_count;i++) {
            struct mock_file copy=files[i];char *pos;
            if((pos=strstr(copy.path,"sdc")))pos[2]='d';
            else if(!strcmp(copy.path,"/sys/dev/block/8:33"))strcpy(copy.path,"/sys/dev/block/8:49");
            else {
                char child[64];snprintf(child,sizeof(child),USB "%s.1",port->usb);
                if(strncmp(copy.path,child,strlen(child)))continue;
                copy.path[strlen(child)-1]='2';
            }
            if((pos=strstr(copy.resolved,"sdc")))while((pos=strstr(copy.resolved,"sdc")))pos[2]='d';
            char child[32];snprintf(child,sizeof(child),"%s.1",port->usb);
            for(pos=copy.resolved;(pos=strstr(pos,child));pos+=strlen(child))pos[strlen(child)-1]='2';
            copy.st.st_ino+=1000;
            if(copy.st.st_rdev==makedev(8,33))copy.st.st_rdev=makedev(8,49);
            if(copy.st.st_dev==makedev(0,42))copy.st.st_dev=makedev(0,43);
            struct mock_file *made=put(copy.path,copy.bytes,copy.size,copy.st.st_mode,copy.st.st_ino,copy.st.st_dev,copy.st.st_rdev);
            strcpy(made->resolved,copy.resolved);
        }
        txt("/sys/class/block/sdd1/dev","8:49");
        snprintf(path,sizeof(path),USB "%s.2/devnum",port->usb);txt(path,"12");
        snprintf(line,sizeof(line),"50 20 0:42 / /media/usb%u/sdc1 rw - fuseblk /dev/sdc1 rw\n51 20 0:43 / /media/usb%u/sdd1 rw - fuseblk /dev/sdd1 rw\n",music,music);
        txt("/proc/self/mountinfo",line);
    }
}
static void module_fixture(unsigned loopback,unsigned music_number) {
    char value[32];
    txt(MODULE "parameters/apply","Y");txt(MODULE "parameters/music_hubs","Y");
    snprintf(value,sizeof(value),"%u",loopback);txt(MODULE "parameters/loopback_port",value);
    txt(MODULE "parameters/loopback_hub_devnum","9");
    snprintf(value,sizeof(value),"%u",music_number);txt(MODULE "parameters/music_hub_devnum",value);
    txt(MODULE "refcnt",music_number?"2":"1");
    txt("/sys/bus/usb/drivers/hub/bind","");
}
static void route_media_tests(void) {
    for(unsigned music=1;music<=2;music++) {
        unsigned cable=3-music;char path[256],line[256];
        fixture_port(music);txt("/proc/self/mountinfo",no_music_mounts);
        snprintf(path,sizeof(path),USB "%s",xz_quad_port(music)->usb);hide_prefix(path);
        mock_loopback(cable,0,0);
        struct native n=native_fixture();struct route_binding r={0};
        CHECK(route_binding_admit(&r,&n)==ADMIT_READY);CHECK(r.loopback_port==cable);CHECK(route_binding_check(&r));m_close(n.memory);
        fixture_port(music);mock_loopback(cable,0,0);n=native_fixture();r=(struct route_binding){0};
        txt("/proc/self/mountinfo",no_music_mounts);CHECK(route_binding_admit(&r,&n)==ADMIT_READY);
        snprintf(line,sizeof(line),"50 20 0:42 / /media/usb%u/sdc1 rw - fuseblk /dev/sdc1 rw\n",music);
        txt("/proc/self/mountinfo",line);CHECK(route_binding_check(&r));
        txt("/proc/self/mountinfo",no_music_mounts);CHECK(route_binding_check(&r));
        txt("/proc/self/mountinfo",line);CHECK(route_binding_check(&r));
        txt("/proc/sys/kernel/random/boot_id","22222222-2222-4222-8222-222222222222");
        CHECK(!route_binding_check(&r));CHECK(r.state==BINDING_RETIRED);CHECK(route_binding_admit(&r,&n)==ADMIT_FATAL);m_close(n.memory);
        for(unsigned count=1;count<=2;count++) {
            hub_media_fixture(music,count);mock_loopback(cable,0,0);n=native_fixture();r=(struct route_binding){0};
            CHECK(route_binding_admit(&r,&n)==ADMIT_READY);CHECK(route_binding_check(&r));
            /* Sysfs removal can precede the native mount-table update. */
            strcpy(find("/sys/class/block/sdc1")->path,"/removed/block/sdc1");
            CHECK(route_binding_check(&r));CHECK(r.state==BINDING_ACTIVE);
            const char *pending_reason;
            CHECK(!music_inventory(music,&pending_reason));
            m_close(n.memory);
        }
        for(unsigned change=0;change<7;change++) {
            hub_media_fixture(music,2);mock_loopback(cable,0,0);n=native_fixture();r=(struct route_binding){0};
            if(change==0)strcpy(find("/sys/class/block/sdd1")->resolved,"/sys/wrong");
            if(change==1)txt("/proc/self/mountinfo","");
            if(change==2)strcpy(find("/proc/self/mountinfo")->path,"/missing");
            if(change==3)strcpy(find("/sys/class/block/sdd1")->resolved,"/sys/devices/platform/ehci/usb2/2-1/2-1.1/2-1.1.2/2-1.1.2.3/2-1.1.2.3:1.0/host0/block/sdd/sdd1");
            if(change==4){snprintf(line,sizeof(line),"50 20 0:42 / /media/usb%u/sdc1 rw - fuseblk /dev/sdc1 rw\n",cable);txt("/proc/self/mountinfo",line);}
            if(change==5){snprintf(path,sizeof(path),USB "%s/speed",xz_quad_port(music)->usb);txt(path,"12");}
            if(change==6)fault=2;
            CHECK(route_binding_admit(&r,&n)!=ADMIT_READY);CHECK(r.state==BINDING_NEW);m_close(n.memory);
        }
        fixture_port(music);txt("/proc/self/mountinfo",no_music_mounts);n=native_fixture();r=(struct route_binding){0};
        CHECK(route_binding_admit(&r,&n)==ADMIT_WAIT);CHECK(!strcmp(r.reason,"WAIT_LOOPBACK"));
        mock_loopback(cable,0,0);fault=3;CHECK(route_binding_admit(&r,&n)==ADMIT_WAIT);fault=0;
        CHECK(route_binding_admit(&r,&n)==ADMIT_READY);
        snprintf(path,sizeof(path),USB "%s",xz_quad_port(cable)->usb);hide_prefix(path);CHECK(route_binding_check(&r));
        mock_loopback(cable,0,0);CHECK(route_binding_check(&r));
        hide_prefix(path);mock_loopback(music,0,0);CHECK(!route_binding_check(&r));CHECK(r.state==BINDING_RETIRED);m_close(n.memory);
    }
    puts("PASS actual route no-stick start, late media/removal, 1/2 hub drives, bad/racy inventory, stopped admission, same-port reconnect and permanent wrong-port retirement");
}
static void music_hub_service_tests(void) {
    for(unsigned music=1;music<=2;music++)for(unsigned change=0;change<14;change++) {
        fixture_port(music);txt("/proc/self/mountinfo",no_music_mounts);setup_music_hub(music,change==5);mock_loopback(3-music,0,0);
        struct native n=native_fixture();struct route_binding r={0};CHECK(route_binding_admit(&r,&n)==ADMIT_READY);
        module_fixture(3-music,11);struct music_hub attempted={0};const char *reason;char path[256];
        if(change==1)fault=3;
        if(change==2)txt(MODULE "parameters/music_hub_devnum","12");
        if(change==3)txt(MODULE "parameters/loopback_port",music==1?"1":"2");
        if(change==4)txt(MODULE "parameters/music_hubs","N");
        if(change==6){snprintf(path,sizeof(path),USB "%s.7",xz_quad_port(music)->usb);put(path,"",0,S_IFDIR,999,1,0);}
        if(change==7)fault=9;
        if(change==8){snprintf(path,sizeof(path),USB "%s/speed",xz_quad_port(music)->usb);txt(path,"12");}
        if(change==9)fault=10;
        if(change==10)fault=11;
        if(change==11)fault=12;
        /* KOBJ_ADD precedes configuration, so this attribute is initially empty. */
        if(change==12){snprintf(path,sizeof(path),USB "%s/bNumInterfaces",xz_quad_port(music)->usb);txt(path,"");}
        if(change==13){txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdz1 rw - fuseblk /dev/sdz1 rw\n");}
        int ok=service_music_hub(&n,&r,&attempted,&reason);
        CHECK(ok);
        CHECK(bind_writes==(change==0||change==7));
        CHECK(r.state==BINDING_ACTIVE);
        if(change==0) {
            snprintf(path,sizeof(path),"%s:1.0",xz_quad_port(music)->usb);CHECK(!strcmp(last_bind,path));
            CHECK(service_music_hub(&n,&r,&attempted,&reason));CHECK(bind_writes==1);
        }
        if(change==1)CHECK(!strcmp(reason,"WAIT_MUSIC_HUB_PAUSED"));
        if(change==7) {
            CHECK(!strcmp(reason,"MUSIC_HUB_BIND_FAILED"));
            fault=3; /* A retry must still wait while a native deck plays. */
            CHECK(service_music_hub(&n,&r,&attempted,&reason));
            CHECK(reason&&!strcmp(reason,"WAIT_MUSIC_HUB_PAUSED")&&bind_writes==1);
            fault=0;
            CHECK(service_music_hub(&n,&r,&attempted,&reason));
            CHECK(bind_writes==2); /* Same hub must recover without another unplug. */
            CHECK(service_music_hub(&n,&r,&attempted,&reason));CHECK(bind_writes==2);
        }
        m_close(n.memory);
    }
    for(unsigned music=1;music<=2;music++) {
        fixture_port(music);mock_loopback(3-music,0,0);
        struct native n=native_fixture();struct route_binding r={0};
        CHECK(route_binding_admit(&r,&n)==ADMIT_READY);
        struct music_hub attempted={0};const char *reason;fault=3;
        CHECK(service_music_hub(&n,&r,&attempted,&reason));
        CHECK(!reason&&!bind_writes); /* A direct stick never takes the hub paused gate. */
        m_close(n.memory);
    }
    puts("PASS actual music-hub bind path: both ports, owned generation only, stopped native only, no duplicate bind, bound/child/speed refusals and write failure");
}


/* The verifier replaces only these four side-effect bodies in its staged copy. */
static int m_event_socket(void) { txt("/fixture/events","");return m_open("/fixture/events",O_RDONLY); }
static int m_debounce_wait(int ms) { CHECK(ms==300);return 1; }
static int m_command(const char *program,const char *arg,const char *option,const char *extra,const char *selection,const char *music) {
    CHECK(lifecycle);
    if(!strcmp(program,"/sbin/insmod")) {
        CHECK(!strcmp(arg,"/dev/shm/helper/xz_hub_depth.ko"));CHECK(!find(MODULE));
        char port[32];snprintf(port,sizeof(port),"loopback_port=%u",lifecycle_port);
        if(!strcmp(option,"apply=0")){CHECK(!strcmp(extra,port));CHECK(!strcmp(selection,"music_hubs=1"));CHECK(!music);}
        else {CHECK(!strcmp(option,"apply=1"));CHECK(!strcmp(extra,"adopt_existing=1"));CHECK(!strcmp(selection,port));CHECK(!strcmp(music,"music_hubs=1"));}
        txt(MODULE,"");txt(MODULE "refcnt","0");
        if(!strcmp(option,"apply=1"))module_fixture(lifecycle_port,0);
    } else {
        CHECK(!strcmp(program,"/sbin/rmmod")&&!strcmp(arg,"xz_hub_depth"));
        CHECK(!option&&!extra&&!selection&&!music);CHECK(find(MODULE)!=NULL);hide_prefix(MODULE);
    }
    return 1;
}
static void m_event_wait(int fd,int ms) {
    CHECK(lifecycle&&fds[fd].used&&++event_waits<40);
    if(*status_terminal) { hide_prefix(USB "2-1.2");if(lifecycle==17)hide_prefix(USB "2-1.1");txt(MODULE "refcnt","0");return; }
    if(equals(RUN "/status","WAIT_LOOPBACK_SETTLING")) {
        /* Scenario 18: the replugged hub reports full speed for one pass, then settles. */
        CHECK(lifecycle==18&&++settle_waits==1&&ms==300&&!find(NODE));txt(USB "2-1.2/speed","480");return;
    }
    if(equals(RUN "/status","LOOPBACK_MOVED")) { CHECK(0); }
    if(equals(RUN "/status","BIND_FAILED; press RETRY or reconnect loopback")) {
        /* Scenario 19: nothing happens until RETRY writes its marker; then the bind runs again. */
        CHECK(lifecycle==19&&!find(NODE));
        if(++retry_waits==2){fault=0;txt(RUN "/retry","");}
        return;
    }
    if(equals(RUN "/status","WAIT_NATIVE_DECKS")) {
        native_deck_waits++;
        CHECK(ms==500&&!bind_writes&&!find(MODULE));
        struct mock_file *json=find(RUN "/status.json");CHECK(json!=NULL);
        CHECK(strstr((char *)json->bytes,"\"retryable\":true")!=NULL);
        CHECK(!node_creates);fixture_engine=0x20000;fixture_begin=0x30000;fixture_end=0x30008;return;
    }
    if(equals(RUN "/status","WAIT_NATIVE_PAUSED")) {
        native_pause_waits++;
        CHECK(lifecycle==17?!find(NODE):(!bind_writes&&!node_creates));
        fixture_flags[0]=fixture_flags[1]=0;fixture_players[0]=fixture_players[1]=0;return;
    }
    if(!status_route_ready)return;
    CHECK(find(NODE)!=NULL&&node_creates==1u+(unsigned)(ready_waits&&(lifecycle==17||lifecycle==18))&&node_removes==node_creates-1);
    ready_waits++;
    if(ready_waits==1) {
        fixture_players[0]=0x40000;fixture_flags[0]=0x100;
        if(lifecycle==17) {
            /* The cable moves from USB 1 to USB 2; its old hub and its module references are gone. */
            hide_prefix(USB "2-1.2");mock_loopback(2,0,0);txt(MODULE "refcnt","0");
            lifecycle_port=2;fixture_music_port=1;
        }
        if(lifecycle==18)txt(USB "2-1.2/speed","12");
        if(lifecycle==9||(lifecycle>=11&&lifecycle<=13)){fixture_begin=0x30100;fixture_end=fixture_begin+8;}
    } else if(ready_waits==2) {
        /* A loaded, now playing deck keeps the already-published route. */
        if(lifecycle==4)fixture_engine+=0x1000;
        else if(lifecycle==5){fixture_begin+=0x100;fixture_end+=0x100;}
        else if(lifecycle==6)txt("/proc/42/stat","42 (rbp) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 778 0");
        else if(lifecycle==7)fault=5;
        else txt(RUN "/stop","");
    } else CHECK(0);
}
static void native_empty_vector_tests(void) {
    for(unsigned shape=0;shape<4;shape++) {
        fixture(0);unsigned count=shape?shape-1:0;
        if(!shape)fixture_begin=0;
        fixture_end=fixture_begin+count*4;
        struct native n={.memory=-1};struct route_binding b={0};mock_loopback(1,0,0);
        CHECK(qualify_native(&n,42));
        CHECK(native_decks_ready(&n));CHECK(n.count==count&&range_reads>=2);CHECK(native_stopped(&n));
        CHECK(route_binding_admit(&b,&n)==ADMIT_READY);m_close(n.memory);
    }
    puts("PASS native vectors with zero, one and two players admit without tracks");
    for(unsigned relocate=0;relocate<2;relocate++) {
        fixture(0);fixture_end=fixture_begin;
        struct native n=native_fixture();struct route_binding b={0};mock_loopback(1,0,0);
        CHECK(route_binding_admit(&b,&n)==ADMIT_READY);
        for(unsigned count=1;count<=2;count++) {
            if(relocate)fixture_begin+=0x100;
            fixture_end=fixture_begin+count*4;
            fixture_flags[count-1]=0x101;
            CHECK(native_alive(&n));CHECK(n.count==count);CHECK(!native_stopped(&n));
            CHECK(route_binding_check(&b));
            fixture_flags[count-1]=1;CHECK(native_stopped(&n));
        }
        fixture_begin+=0x100;fixture_end+=0x100;CHECK(!native_alive(&n));m_close(n.memory);
    }
    for(unsigned count=1;count<=2;count++) {
        fixture(0);fixture_end=fixture_begin+count*4;struct native n=native_fixture();
        fixture_end-=4;CHECK(!native_alive(&n));m_close(n.memory);
    }
    puts("PASS stable short-vector growth retains the route; shrink or full-vector replacement ends native identity");
    for(unsigned torn=4;torn<=6;torn++) {
        fixture(0);fixture_end=fixture_begin;struct native n={.memory=-1};
        CHECK(qualify_native(&n,42));torn_read=torn;
        CHECK(!native_decks_ready(&n));CHECK(!n.engine&&!slot_reads);m_close(n.memory);
    }
    puts("PASS torn empty-vector capture and process restart do not capture native identity");
}
static void native_adjacent_flag_tests(void) {
    const unsigned adjacent[]={0,1,0x80,0xff};
    for(unsigned slot=0;slot<2;slot++)for(unsigned i=0;i<COUNT(adjacent);i++) {
        fixture(0);fixture_flags[slot]=(uint16_t)adjacent[i];
        struct native n={.memory=-1};struct route_binding b={0};mock_loopback(1,0,0);
        CHECK(qualify_native(&n,42));CHECK(native_decks_ready(&n));
        CHECK(native_stopped(&n));CHECK(route_binding_admit(&b,&n)==ADMIT_READY);
        fixture_flags[slot]|=0x100;
        CHECK(!native_stopped(&n));m_close(n.memory);
    }
    puts("PASS adjacent +0xe76 values do not override the +0xe77 playing byte");
}
static void native_slot_tests(void) {
    for(unsigned zero=0;zero<3;zero++) {
        fixture(0);fixture_players[0]=zero==2?0x40000:0;fixture_players[1]=zero==1?0x50000:0;
        struct native n=native_fixture();struct route_binding b={0};mock_loopback(1,0,0);
        CHECK(route_binding_admit(&b,&n)==ADMIT_READY);CHECK(native_alive(&n));
        fixture_players[0]=0x50000;fixture_players[1]=0x40000;
        CHECK(native_stopped(&n)&&route_binding_check(&b));m_close(n.memory);
    }
    puts("PASS both empty slots and either empty slot admit; player creation/replacement keeps binding");
    const uint32_t invalid[]={1,0xfffc,0x40001,UINT32_MAX,UINT32_MAX-0xe77};
    for(unsigned slot=0;slot<2;slot++)for(unsigned i=0;i<COUNT(invalid)+2;i++) {
        fixture(0);struct native n=native_fixture();struct route_binding b={0};mock_loopback(1,0,0);
        if(i<COUNT(invalid))fixture_players[slot]=invalid[i];
        else fixture_flags[slot]=(i==COUNT(invalid)?0x100:0x8000);
        CHECK(!native_stopped(&n));CHECK(route_binding_admit(&b,&n)==ADMIT_WAIT);
        CHECK(b.state==BINDING_NEW);CHECK(!strcmp(b.reason,"WAIT_NATIVE_PAUSED"));m_close(n.memory);
    }
    puts("PASS playing flags and nonzero invalid slots wait without dereferencing invalid addresses");
    for(int failed_read=15;failed_read<=16;failed_read++) {
        fixture(0);struct native n=native_fixture();struct route_binding b={0};mock_loopback(1,0,0);
        fault=failed_read;CHECK(!native_stopped(&n));CHECK(route_binding_admit(&b,&n)==ADMIT_WAIT);
        CHECK(b.state==BINDING_NEW);m_close(n.memory);
    }
    for(unsigned kind=1;kind<=8;kind++)for(unsigned admit=0;admit<2;admit++) {
        fixture(0);struct native n=native_fixture();struct route_binding b={0};mock_loopback(1,0,0);
        if(kind<=3)fixture_players[0]=fixture_players[1]=0;
        if(kind==8){fixture_end=fixture_begin+4;n.count=1;}
        torn_read=kind;slot_reads=0;
        if(admit)CHECK(route_binding_admit(&b,&n)!=ADMIT_READY);
        else CHECK(!native_stopped(&n));
        m_close(n.memory);
    }
    puts("PASS torn player, engine and range reads cannot admit");
    for(unsigned kind=0;kind<10;kind++) {
        fixture(0);struct native n={.memory=-1};
        if(kind==0)fixture_engine=0;
        if(kind==1)fixture_engine=1;
        if(kind==2)fixture_engine=UINT32_MAX;
        if(kind==3)fixture_begin=0;
        if(kind==4)fixture_end+=4;
        if(kind==5){fixture_begin=UINT32_MAX-3;fixture_end=4;}
        if(kind==6){fixture_begin++;fixture_end++;}
        if(kind==7)fixture_end=fixture_begin-4;
        if(kind==8)fixture_end=fixture_begin+2;
        if(kind==9){fixture_begin=1;fixture_end=1;}
        CHECK(qualify_native(&n,42));CHECK(!slot_reads&&!n.engine);
        CHECK(!native_decks_ready(&n));CHECK(native_alive(&n));
        fixture_engine=0x20000;fixture_begin=0x30000;fixture_end=0x30008;
        CHECK(native_decks_ready(&n)&&native_stopped(&n));m_close(n.memory);
    }
    puts("PASS unavailable/invalid engine and range wait, then capture without requalification");
    for(unsigned f=13;f<=14;f++) {
        fixture(0);fault=(int)f;char *args[]={"xz-auto-bridge","--run","42",NULL};
        CHECK(bridge_unused_main(3,args)==1);CHECK(!find(RUN "/bridge.pid")&&!slot_reads);
    }
    puts("PASS wrong runtime signature/field still REFUSED_NATIVE_RUNTIME before deck admission");
}
static void native_lifecycle_tests(void) {
    for(unsigned scenario=1;scenario<=19;scenario++) {
        fixture_port(2);txt("/proc/self/mountinfo",no_music_mounts);mock_loopback(1,0,0);
        lifecycle=scenario;fixture_players[0]=fixture_players[1]=0;
        lifecycle_port=1;settle_waits=retry_waits=0;
        if(scenario==19)fault=9;
        if(scenario==2)fixture_engine=0;
        if(scenario==3){fixture_players[1]=0x50000;fixture_flags[1]=0x100;}
        if(scenario==8)fixture_players[0]=1;
        if(scenario==9)fixture_end=fixture_begin;
        if(scenario==10)fixture_players[1]=0x50000;
        if(scenario==11)fixture_begin=fixture_end=0;
        if(scenario==12){fixture_end=fixture_begin+4;fixture_players[0]=0x40000;}
        if(scenario==13){fixture_end=fixture_begin+4;fixture_players[0]=0x40000;fixture_flags[0]=1;}
        if(scenario>=14){fixture_players[0]=0x40000;fixture_players[1]=0x50000;fixture_flags[0]=fixture_flags[1]=1;}
        if(scenario==15)fixture_flags[0]|=0x100;
        if(scenario==16)fixture_flags[1]|=0x100;
        char *args[]={"xz-auto-bridge","--run","42",NULL};
        int result=bridge_unused_main(3,args);
        if(result!=(scenario==7))fprintf(stderr,"scenario=%u result=%d terminal=%s\n",scenario,result,status_terminal);
        CHECK(result==(scenario==7));
        unsigned republished=scenario==17||scenario==18;
        CHECK(ready_waits==2&&node_creates==1+republished&&node_removes==1+republished);
        CHECK(bind_writes==1u+(unsigned)(scenario==17||scenario==19));
        CHECK(settle_waits==(unsigned)(scenario==18)&&retry_waits==2u*(unsigned)(scenario==19));
        CHECK(native_deck_waits==(scenario==2));
        CHECK(native_pause_waits==(scenario==3||scenario==8||scenario==15||scenario==16||scenario==17));
        CHECK(!find(NODE)&&!find(RUN "/bridge.pid")&&!find(MODULE));
        const char *reason=scenario>=4&&scenario<=6?"TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE":
            scenario==7?"TERMINATED_PROCESS_SET_CHANGE":"TERMINATED_STOP_MARKER";
        CHECK(!strcmp(status_terminal,reason));
        CHECK(strstr((char *)find(RUN "/status.json")->bytes,reason)!=NULL);
        for(unsigned i=3;i<128;i++)CHECK(!fds[i].used);
    }
    fault=0;lifecycle_port=1;
    puts("PASS real main/run_bridge: boot cable + empty decks publishes; delayed decks retry; playing/invalid wait; loading keeps route; process/engine/range restart terminates; cable moved to the other port, settling replug and RETRY after a failed bind recover");
}

int main(void) {
    native_adjacent_flag_tests();native_empty_vector_tests();
    native_slot_tests();native_lifecycle_tests();
    CHECK(self_test()==0);
    route_media_tests();
    music_hub_service_tests();
    port_tests();
    loopback_port_tests();
    port_format_tests();
    new_media_tests();
    for(unsigned unit=0;unit<2;unit++) {
        fixture(unit);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        CHECK(binding_admit(&b,&n) == ADMIT_READY);CHECK(binding_check(&b));
        for(unsigned i=0;i<32;i++)CHECK(binding_check(&b));
        CHECK(binding_admit(&b,&n) != ADMIT_READY);finish(&b,&n);CHECK(!binding_check(&b));
    }
    puts("PASS two synthetic controllers/sticks independently admitted; immutable stable lifetime");
    for(unsigned change=0;change<10;change++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};CHECK(binding_admit(&b,&n) == ADMIT_READY);
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
        CHECK(!binding_check(&b));CHECK(b.state==BINDING_RETIRED);CHECK(binding_admit(&b,&n) != ADMIT_READY);finish(&b,&n);
    }
    puts("PASS devnum/path reuse, mount/descriptor/volume/topology/unit/boot changes retire");
    for(int f=1;f<=4;f++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};fault=f;
        CHECK(binding_admit(&b,&n) != ADMIT_READY);CHECK(b.state==BINDING_NEW);
        CHECK(b.volume_fd==-1&&b.block_fd==-1);fault=0;
        CHECK(binding_admit(&b,&n)==ADMIT_READY);finish(&b,&n);
    }
    puts("PASS identity/mount/descriptor change during capture and nonstopped admission refuse");
    const char *bad[]={"", "50 20 -1:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n",
        "50 20 0:42 / /media/usb2/sdc1 rw - ntfs /dev/sdc1 rw\n", "50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw",
        "50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw extra\n",
        "50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n51 20 0:43 / /media/usb2/sdd1 rw - fuseblk /dev/sdd1 rw\n",
        "50 20 0:42 / /media/usb2/sdc1\\040x rw - fuseblk /dev/sdc1 rw\n"};
    for(unsigned i=0;i<COUNT(bad);i++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};txt("/proc/self/mountinfo",bad[i]);
        CHECK(binding_admit(&b,&n) != ADMIT_READY);finish(&b,&n);
    }
    for(unsigned i=0;i<4;i++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        if(i==0)txt(USB "2-1.1/serial","");
        if(i==1)txt(USB "2-1.1/idVendor","zzzz");
        if(i==2)memcpy(find("/dev/sdc1")->bytes+3,"NTFS    ",8);
        if(i==3)strcpy(find(USB "2-1.1/serial")->path,"/removed/serial");
        CHECK((binding_admit(&b,&n)==ADMIT_READY)==(i==0||i==3));finish(&b,&n);
    }
    puts("PASS absent serial captured without invention explicitly unsupported, malformed parsers and non-exFAT fuseblk refuse");
    for(unsigned kind=0;kind<2;kind++) {
        fixture(0);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};
        txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdc1 rw - vfat /dev/sdc1 rw\n");
        memset(find("/dev/sdc1")->bytes+3,0,8);memcpy(find("/dev/sdc1")->bytes+(kind?82:54),kind?"FAT32   ":"FAT16   ",8);
        CHECK(binding_admit(&b,&n) == ADMIT_READY);CHECK(binding_check(&b));finish(&b,&n);
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
        CHECK(binding_admit(&b,&n) == ADMIT_READY);
        if(mutation==0)find("/dev/sdc1")->bytes[106]^=2;
        if(mutation==1)find("/dev/sdc1")->bytes[107]^=1;
        if(mutation==2)find("/dev/sdc1")->bytes[112]=57;
        if(mutation==3)find("/dev/sdc1")->bytes[100]^=1;
        if(mutation==4)find("/dev/sdc1")->bytes[108]^=1;
        CHECK(binding_check(&b)==(mutation<3));
        finish(&b,&n);
    }
    puts("PASS exFAT mutable status retained; serial and geometry changes retire");
    fixture(0);struct native old=native_fixture();struct binding retired={.volume_fd=-1,.block_fd=-1};CHECK(binding_admit(&retired,&old) == ADMIT_READY);
    txt("/proc/self/mountinfo","51 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n");CHECK(!binding_check(&retired));
    txt("/proc/self/mountinfo","50 20 0:42 / /media/usb2/sdc1 rw - fuseblk /dev/sdc1 rw\n");CHECK(!binding_check(&retired));CHECK(binding_admit(&retired,&old) != ADMIT_READY);finish(&retired,&old);
    fixture(1);struct native n=native_fixture();struct binding b={.volume_fd=-1,.block_fd=-1};CHECK(binding_admit(&b,&n) == ADMIT_READY);finish(&b,&n);
    CHECK(!unexpected);for(unsigned i=3;i<128;i++)CHECK(!fds[i].used);
    printf("PASS %u checks; fresh boot qualification; all boundary descriptors closed; no live I/O\n",checks);
    return 0;
}
#endif
