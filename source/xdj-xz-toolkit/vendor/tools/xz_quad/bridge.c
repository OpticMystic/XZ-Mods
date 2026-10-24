#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "sha256.h"
#include "contract.h"

/* Firmware libc predates the C99 scanf symbol added in glibc 2.7. */
extern int legacy_sscanf(const char *, const char *, ...) __asm__("sscanf");
#define sscanf legacy_sscanf

#define RUN "/dev/shm/xz-four-deck"
#define NODE RUN "/usb-audio"
#define USB "/sys/bus/usb/devices/"
#define MODULE "/sys/module/xz_hub_depth/"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static volatile sig_atomic_t stopping;
static void stop_signal(int sig) { (void)sig; stopping = 1; }

static int text_file(const char *path, char *out, size_t capacity)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, out, capacity - 1);
    close(fd);
    if (n < 0 || (size_t)n == capacity - 1) return -1;
    out[n] = 0;
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    return (int)n;
}

static int equals(const char *path, const char *value)
{
    char actual[256];
    return text_file(path, actual, sizeof(actual)) >= 0 && !strcmp(actual, value);
}

static int attribute(const char *device, const char *key, const char *value)
{
    char path[512];
    int n = snprintf(path, sizeof(path), USB "%s/%s", device, key);
    return n > 0 && n < (int)sizeof(path) && equals(path, value);
}

static int decimal(const char *text, unsigned long *value)
{
    if (!*text) return 0;
    for (const char *p = text; *p; p++) if (!isdigit((unsigned char)*p)) return 0;
    errno = 0;
    *value = strtoul(text, NULL, 10);
    return !errno;
}

static int number_file(const char *path, unsigned long *value)
{
    char text[40];
    return text_file(path, text, sizeof(text)) >= 0 && decimal(text, value);
}

static int read_at(int fd, uint64_t address, void *out, size_t bytes)
{
    return pread(fd, out, bytes, (off_t)address) == (ssize_t)bytes;
}

static int hash_file(const char *path, const char *expected, int nofollow, struct stat *identity)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | (nofollow ? O_NOFOLLOW : 0));
    if (fd < 0) return 0;
    struct stat before, after;
    int ok = fstat(fd, &before) == 0 && S_ISREG(before.st_mode);
    SHA256_CTX ctx;
    sha256_init(&ctx);
    unsigned char block[16384], digest[32];
    ssize_t n = 0;
    while (ok && (n = read(fd, block, sizeof(block))) > 0) sha256_update(&ctx, block, (size_t)n);
    ok = ok && n == 0 && fstat(fd, &after) == 0 && before.st_ino == after.st_ino &&
        before.st_dev == after.st_dev && before.st_size == after.st_size && before.st_mtime == after.st_mtime;
    close(fd);
    sha256_final(&ctx, digest);
    char hex[65];
    for (size_t i = 0; i < sizeof(digest); i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (identity) *identity = before;
    return ok && !strcmp(hex, expected);
}

static int ram_path(const char *path)
{
    struct statfs fs;
    char resolved[PATH_MAX];
    return realpath(path, resolved) && !strcmp(path, resolved) &&
        !strncmp(path, "/dev/shm/", 9) && statfs(path, &fs) == 0 && fs.f_type == 0x01021994;
}

static unsigned long long start_time(pid_t pid)
{
    char path[64], data[2048];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    if (text_file(path, data, sizeof(data)) < 0) return 0;
    char *p = strrchr(data, ')');
    if (!p || p[1] != ' ') return 0;
    p += 2;
    for (int field = 3; field < 22; field++) {
        p = strchr(p, ' ');
        if (!p) return 0;
        p++;
    }
    return strtoull(p, NULL, 10);
}

static int process_set(pid_t pid)
{
    DIR *dir = opendir("/proc");
    if (!dir) return 0;
    struct dirent *entry;
    unsigned found = 0;
    int ok = 1;
    while ((entry = readdir(dir))) {
        unsigned long other;
        char path[80], comm[128];
        if (!decimal(entry->d_name, &other)) continue;
        snprintf(path, sizeof(path), "/proc/%lu/comm", other);
        if (text_file(path, comm, sizeof(comm)) < 0) continue;
        if (!strcmp(comm, "app")) ok = 0;
        if (!strcmp(comm, "rbp")) { found++; if (other != (unsigned long)pid) ok = 0; }
    }
    closedir(dir);
    return ok && found == 1;
}

/* Process-local, immutable boot/media ownership; never persisted or caller supplied. */
enum binding_state { BINDING_NEW, BINDING_ACTIVE, BINDING_RETIRED };
struct file_identity { uint64_t device, inode, rdev; };
struct observation {
    char boot[40], mac[32], vendor[8], product[8], serial[132];
    char source[32], mount[64], kind[16], mount_line[1024], ancestry[PATH_MAX];
    unsigned long devnum, busnum;
    struct file_identity usb, partition, block, volume;
    unsigned char sector_sha[32];
};
struct binding {
    enum binding_state state;
    struct observation identity;
    int volume_fd, block_fd;
};

static struct file_identity file_identity(const struct stat *s)
{
    return (struct file_identity){s->st_dev, s->st_ino, s->st_rdev};
}

static int same_file(int fd, const char *path, struct file_identity *identity, mode_t type)
{
    struct stat descriptor, current;
    if (fstat(fd, &descriptor) || lstat(path, &current) ||
        (descriptor.st_mode & S_IFMT) != type || (current.st_mode & S_IFMT) != type) return 0;
    *identity = file_identity(&descriptor);
    struct file_identity other = file_identity(&current);
    return !memcmp(identity, &other, sizeof(other));
}

static int hex_text(const char *p, size_t length)
{
    if (strlen(p) != length) return 0;
    for (size_t i = 0; i < length; i++)
        if (!isdigit((unsigned char)p[i]) && !(p[i] >= 'a' && p[i] <= 'f')) return 0;
    return 1;
}

static int unit_identity(struct observation *o)
{
    if (text_file("/proc/sys/kernel/random/boot_id", o->boot, sizeof(o->boot)) != 36 ||
        text_file("/sys/class/net/eth0/address", o->mac, sizeof(o->mac)) != 17) return 0;
    unsigned nonzero = 0;
    for (unsigned i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (o->boot[i] != '-') return 0; }
        else if (!isxdigit((unsigned char)o->boot[i])) return 0;
    }
    for (unsigned i = 0; i < 17; i++) {
        if (i % 3 == 2) { if (o->mac[i] != ':') return 0; }
        else { if (!isxdigit((unsigned char)o->mac[i])) return 0; nonzero |= o->mac[i] != '0'; }
    }
    /* A valid unicast MAC is observed, never used to infer firmware/model eligibility. */
    return nonzero && !(strtoul(o->mac, NULL, 16) & 1);
}

static int device_numbers(const char *text, unsigned *a, unsigned *b)
{
    char copy[40];
    if (strlen(text) >= sizeof(copy)) return 0;
    strcpy(copy, text);
    char *colon = strchr(copy, ':');
    if (!colon) return 0;
    *colon++ = 0;
    unsigned long first, second;
    if (!decimal(copy, &first) || !decimal(colon, &second) || first > 4095 || second > 1048575) return 0;
    *a = (unsigned)first; *b = (unsigned)second;
    return 1;
}

static int music_mount(struct observation *o, unsigned *mount_major, unsigned *mount_minor)
{
    FILE *file = fopen("/proc/self/mountinfo", "r");
    if (!file) { puts("UNSUPPORTED_MOUNTINFO_REQUIRED"); return 0; }
    char line[1024];
    unsigned lines = 0, matches = 0;
    int ok = 1;
    while (fgets(line, sizeof(line), file)) {
        if (++lines > 256 || !strchr(line, '\n')) { ok = 0; break; }
        char copy[1024], *fields[32], *save = NULL;
        strcpy(copy, line);
        unsigned n = 0;
        for (char *p = strtok_r(copy, " \n", &save); p; p = strtok_r(NULL, " \n", &save)) {
            if (n == COUNT(fields)) { ok = 0; break; }
            fields[n++] = p;
        }
        if (!ok || n < 10) { ok = 0; break; }
        unsigned long id, parent;
        if (!decimal(fields[0], &id) || !id || !decimal(fields[1], &parent) ||
            !device_numbers(fields[2], mount_major, mount_minor)) { ok = 0; break; }
        if (strcmp(fields[4], "/media/usb2") && strncmp(fields[4], "/media/usb2/", 12)) continue;
        unsigned dash = 6;
        while (dash < n && strcmp(fields[dash], "-")) dash++;
        if (++matches != 1 || dash + 4 != n || strcmp(fields[3], "/") ||
            (strcmp(fields[dash + 1], "vfat") && strcmp(fields[dash + 1], "fuseblk"))) { ok = 0; break; }
        const char *block = fields[dash + 2];
        /* Keep the established single sd-letter, first-partition layout. */
        if (strlen(block) != 9 || strncmp(block, "/dev/sd", 7) || block[7] < 'a' ||
            block[7] > 'z' || block[8] != '1' || strcmp(fields[4] + 12, block + 5)) { ok = 0; break; }
        strcpy(o->source, block);
        strcpy(o->mount, fields[4]);
        strcpy(o->kind, fields[dash + 1]);
        strcpy(o->mount_line, line);
    }
    if (!lines) puts("UNSUPPORTED_MOUNTINFO_REQUIRED");
    ok = ok && !ferror(file) && matches == 1;
    fclose(file);
    /* Parse the retained row, not a later unrelated row's device numbers. */
    return ok && sscanf(o->mount_line, "%*u %*u %u:%u", mount_major, mount_minor) == 2;
}

static int observe_music(struct observation *o, int *keep_volume, int *keep_block)
{
    memset(o, 0, sizeof(*o));
    unsigned mount_major, mount_minor, block_major, block_minor;
    unsigned long bus, number;
    char path[128], dev[40];
    int volume = -1, block = -1, ok = 0;
    if (!unit_identity(o) || !music_mount(o, &mount_major, &mount_minor) ||
        !attribute("2-1", "idVendor", "0451") || !attribute("2-1", "idProduct", "8027") ||
        !number_file(USB "2-1.1/devnum", &number) || number < 1 || number > 127 ||
        !number_file(USB "2-1.1/busnum", &bus) || bus != 2 ||
        text_file(USB "2-1.1/idVendor", o->vendor, sizeof(o->vendor)) != 4 || !hex_text(o->vendor, 4) ||
        text_file(USB "2-1.1/idProduct", o->product, sizeof(o->product)) != 4 || !hex_text(o->product, 4)) goto done;
    /* Serial-less devices are unsupported; there is no inferred or override serial. */
    int serial_length = text_file(USB "2-1.1/serial", o->serial, sizeof(o->serial));
    if (serial_length < 1 || serial_length > 128) {
        puts("UNSUPPORTED_MUSIC_SERIAL_REQUIRED");
        goto done;
    }
    for (const char *p = o->serial; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') goto done;
    struct stat usb, partition;
    if (stat(USB "2-1.1", &usb) || !S_ISDIR(usb.st_mode)) goto done;
    o->usb = file_identity(&usb);
    o->busnum = bus; o->devnum = number;
    snprintf(path, sizeof(path), "/sys/class/block/%s", o->source + 5);
    if (!realpath(path, o->ancestry) ||
        !strstr(o->ancestry, "/usb2/2-1/2-1.1/2-1.1:1.0/") || stat(path, &partition)) goto done;
    o->partition = file_identity(&partition);
    snprintf(path, sizeof(path), "/sys/class/block/sd%c/removable", o->source[7]);
    if (!equals(path, "1")) goto done;
    snprintf(path, sizeof(path), "/sys/class/block/%s/dev", o->source + 5);
    if (text_file(path, dev, sizeof(dev)) < 1 || !device_numbers(dev, &block_major, &block_minor)) goto done;
    volume = open(o->mount, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    block = open(o->source, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (volume < 0 || block < 0 || !same_file(volume, o->mount, &o->volume, S_IFDIR) ||
        !same_file(block, o->source, &o->block, S_IFBLK) ||
        o->volume.device != (uint64_t)makedev(mount_major, mount_minor) ||
        o->block.rdev != (uint64_t)makedev(block_major, block_minor)) goto done;
    unsigned char sector[512];
    if (!read_at(block, 0, sector, sizeof(sector)) || sector[510] != 0x55 || sector[511] != 0xaa ||
        (!strcmp(o->kind, "fuseblk") ? memcmp(sector + 3, "EXFAT   ", 8) != 0 :
         (memcmp(sector + 54, "FAT16   ", 8) && memcmp(sector + 82, "FAT32   ", 8)))) goto done;
    /* exFAT VolumeFlags and PercentInUse are mutable filesystem status,
     * excluded from its boot checksum; they are not volume identity. */
    if (!strcmp(o->kind, "fuseblk")) sector[106] = sector[107] = sector[112] = 0;
    SHA256_CTX ctx;
    sha256_init(&ctx); sha256_update(&ctx, sector, sizeof(sector)); sha256_final(&ctx, o->sector_sha);
    struct file_identity checked;
    if (!same_file(volume, o->mount, &checked, S_IFDIR) || memcmp(&checked, &o->volume, sizeof(checked)) ||
        !same_file(block, o->source, &checked, S_IFBLK) || memcmp(&checked, &o->block, sizeof(checked))) goto done;
    ok = 1;
done:
    if (ok && keep_volume && keep_block) { *keep_volume = volume; *keep_block = block; }
    else { if (volume >= 0) close(volume); if (block >= 0) close(block); }
    return ok;
}

static void binding_close(struct binding *binding)
{
    if (binding->volume_fd >= 0) close(binding->volume_fd);
    if (binding->block_fd >= 0) close(binding->block_fd);
    binding->volume_fd = binding->block_fd = -1;
    binding->state = BINDING_RETIRED;
}

static int binding_check(struct binding *binding)
{
    struct observation now, again;
    struct file_identity volume, block;
    if (binding->state == BINDING_ACTIVE &&
        same_file(binding->volume_fd, binding->identity.mount, &volume, S_IFDIR) &&
        !memcmp(&volume, &binding->identity.volume, sizeof(volume)) &&
        same_file(binding->block_fd, binding->identity.source, &block, S_IFBLK) &&
        !memcmp(&block, &binding->identity.block, sizeof(block)) &&
        observe_music(&now, NULL, NULL) && observe_music(&again, NULL, NULL) &&
        !memcmp(&now, &again, sizeof(now)) && !memcmp(&now, &binding->identity, sizeof(now))) return 1;
    binding_close(binding);
    return 0;
}

struct native {
    pid_t pid;
    int memory;
    unsigned long long start;
    uint32_t engine, begin, players[2];
};

static int native_alive(const struct native *native)
{
    return native->start && start_time(native->pid) == native->start;
}

static int native_stopped(const struct native *native)
{
    uint32_t engine, range[2], players[2];
    uint16_t flags[2];
    if (!native_alive(native) ||
        !read_at(native->memory, 0x10e9688, &engine, 4) || engine != native->engine ||
        !read_at(native->memory, engine + 12, range, 8) || range[0] != native->begin || range[1] - range[0] != 8 ||
        !read_at(native->memory, range[0], players, 8) || memcmp(players, native->players, 8)) return 0;
    for (unsigned i = 0; i < 2; i++)
        if (!read_at(native->memory, (uint64_t)players[i] + 0xe76, &flags[i], 2) || flags[i]) return 0;
    return read_at(native->memory, 0x10e9688, &engine, 4) && engine == native->engine &&
        read_at(native->memory, engine + 12, range, 8) && range[0] == native->begin && range[1] - range[0] == 8 &&
        read_at(native->memory, range[0], players, 8) && !memcmp(players, native->players, 8);
}

/* Admission has no caller readiness bit and cannot revive a retired binding. */
static int binding_admit(struct binding *binding, const struct native *native)
{
    if (binding->state != BINDING_NEW) return 0;
    binding->state = BINDING_RETIRED;
    binding->volume_fd = binding->block_fd = -1;
    struct observation second;
    if (!process_set(native->pid) || !native_stopped(native) ||
        !observe_music(&binding->identity, &binding->volume_fd, &binding->block_fd) ||
        !observe_music(&second, NULL, NULL) || memcmp(&binding->identity, &second, sizeof(second)) ||
        !process_set(native->pid) || !native_stopped(native)) {
        binding_close(binding);
        return 0;
    }
    binding->state = BINDING_ACTIVE;
    return 1;
}

static int mapped_library(pid_t pid, const char *path, const char *digest)
{
    struct stat identity;
    if (!ram_path(path) || !hash_file(path, digest, 1, &identity)) return 0;
    char filename[80], line[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    FILE *file = fopen(filename, "r");
    if (!file) return 0;
    int executable = 0, ok = 1;
    while (fgets(line, sizeof(line), file)) {
        unsigned long long inode;
        unsigned devmajor, devminor;
        char permissions[5], mapped[512];
        if (sscanf(line, "%*s %4s %*s %x:%x %llu %511[^\n]", permissions,
                   &devmajor, &devminor, &inode, mapped) != 5) continue;
        if (strcmp(mapped, path)) continue;
        if (inode != (unsigned long long)identity.st_ino || devmajor != major(identity.st_dev) ||
            devminor != minor(identity.st_dev)) ok = 0;
        if (strchr(permissions, 'x')) executable = 1;
    }
    fclose(file);
    return ok && executable;
}

static int qualify_native(struct native *native, pid_t pid)
{
    char path[80], environment[32768];
    native->pid = pid;
    native->memory = -1;
    native->start = start_time(pid);
    if (!native->start || !process_set(pid)) return 0;
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    if (!hash_file(path, RBP_SHA, 0, NULL)) return 0;
    snprintf(path, sizeof(path), "/proc/%d/environ", pid);
    int n = text_file(path, environment, sizeof(environment));
    if (n <= 0) return 0;
    int readonly = 0;
    char *preload = NULL;
    for (int i = 0; i < n; i += (int)strlen(environment + i) + 1) {
        if (!strcmp(environment + i, "XZ_MODS_SETTINGS_READONLY=1")) readonly = 1;
        if (!strncmp(environment + i, "LD_PRELOAD=", 11)) preload = environment + i + 11;
    }
    if (!readonly || !preload) return 0;
    char *separator = strchr(preload, ':');
    if (!separator || strchr(separator + 1, ':')) return 0;
    *separator = 0;
    if (!mapped_library(pid, preload, RECEIVER_SHA) || !mapped_library(pid, separator + 1, RUNTIME_SHA)) return 0;
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    native->memory = open(path, O_RDONLY | O_CLOEXEC);
    const unsigned char signature[8] = {0x0c,0x30,0x90,0xe5,0x10,0x20,0x90,0xe5};
    unsigned char actual[8];
    uint32_t range[2], field;
    if (native->memory < 0 || !read_at(native->memory, 0x494ec, actual, 8) || memcmp(actual, signature, 8) ||
        !read_at(native->memory, 0x49508, &field, 4) || field != 0x15d00e77 ||
        !read_at(native->memory, 0x10e9688, &native->engine, 4) || native->engine < 0x10000 || native->engine > UINT32_MAX - 20 || (native->engine & 3) ||
        !read_at(native->memory, native->engine + 12, range, 8) || range[0] < 0x10000 || (range[0] & 3) || range[1] - range[0] != 8 ||
        !read_at(native->memory, range[0], native->players, 8)) return 0;
    native->begin = range[0];
    for (unsigned i = 0; i < 2; i++)
        if (native->players[i] < 0x10000 || native->players[i] > UINT32_MAX - 0xe78 || (native->players[i] & 3)) return 0;
    return native_alive(native);
}

static int qualify_kernel(void)
{
    struct utsname name;
    if (uname(&name) || strcmp(name.release, KERNEL_RELEASE)) return 0;
    FILE *file = fopen("/proc/kallsyms", "r");
    int memory = open("/dev/kmem", O_RDONLY | O_CLOEXEC);
    if (!file || memory < 0) { if (file) fclose(file); if (memory >= 0) close(memory); return 0; }
    unsigned seen[COUNT(kernel_crcs)] = {0};
    char line[512], symbol[256], kind;
    unsigned long long address;
    int ok = 1;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%llx %c %255s", &address, &kind, symbol) != 3) continue;
        for (unsigned i = 0; i < COUNT(kernel_crcs); i++) if (!strcmp(symbol, kernel_crcs[i].name)) {
            uint32_t value;
            if (++seen[i] != 1 || !read_at(memory, address, &value, 4) || value != kernel_crcs[i].value) ok = 0;
        }
    }
    for (unsigned i = 0; i < COUNT(kernel_crcs); i++) if (seen[i] != 1) ok = 0;
    const unsigned char expected[8] = {0x2b,0x32,0xd5,0xe5,0x02,0x00,0x53,0xe3};
    unsigned char actual[8];
    ok = ok && read_at(memory, 0x805ffa58, actual, 8) && !memcmp(expected, actual, 8);
    fclose(file);
    close(memory);
    return ok;
}

struct device {
    char hub[16];
    unsigned long hub_number, number;
    ino_t inode, hub_inode;
    int bound;
};

static int loopback_present(void)
{
    return (attribute("2-1.1", "idVendor", "2b73") && attribute("2-1.1", "idProduct", "002e")) ||
           (attribute("2-1.2", "idVendor", "2b73") && attribute("2-1.2", "idProduct", "002e"));
}

static int scan_device(struct device *device)
{
    memset(device, 0, sizeof(*device));
    if (!attribute("2-1", "idVendor", "0451") || !attribute("2-1", "idProduct", "8027")) return -1;
    for (unsigned port = 1; port <= 2; port++) {
        char hub[16], path[256];
        snprintf(hub, sizeof(hub), "2-1.%u", port);
        if (!attribute(hub, "idVendor", "2b73") || !attribute(hub, "idProduct", "002e")) continue;
        if (*device->hub || !attribute(hub, "bDeviceClass", "09") || !attribute(hub, "speed", "480")) return -1;
        strcpy(device->hub, hub);
        snprintf(path, sizeof(path), USB "%s", hub);
        struct stat hub_identity;
        if (stat(path, &hub_identity)) return -1;
        device->hub_inode = hub_identity.st_ino;
        snprintf(path, sizeof(path), USB "%s/devnum", hub);
        if (!number_file(path, &device->hub_number)) return -1;
        snprintf(path, sizeof(path), USB "%s/%s:1.0/driver", hub, hub);
        char driver[PATH_MAX];
        if (realpath(path, driver)) {
            if (strcmp(driver, "/sys/bus/usb/drivers/hub")) return -1;
            device->bound = 1;
        }
        for (unsigned child = 1; child <= 9; child++) {
            char name[32];
            snprintf(name, sizeof(name), "%s.%u", hub, child);
            if (!attribute(name, "idVendor", "2b73") || !attribute(name, "idProduct", "002d")) continue;
            if (device->number || !attribute(name, "busnum", "2") || !attribute(name, "speed", "480")) return -1;
            snprintf(path, sizeof(path), USB "%s/devnum", name);
            if (!number_file(path, &device->number) || device->number < 1 || device->number > 127) return -1;
            snprintf(path, sizeof(path), USB "%s", name);
            struct stat identity;
            if (stat(path, &identity)) return -1;
            device->inode = identity.st_ino;
        }
    }
    return 0;
}

static int admission_loopback_safe(void)
{
    if (!loopback_present()) return 1;
    struct device device;
    if (scan_device(&device) || strcmp(device.hub, "2-1.2") || device.bound || device.number) return 0;
    char path[256];
    struct stat driver;
    snprintf(path, sizeof(path), USB "%s/%s:1.0/driver", device.hub, device.hub);
    if (lstat(path, &driver) == 0 || errno != ENOENT) return 0;
    DIR *dir = opendir(USB);
    if (!dir) return 0;
    struct dirent *entry;
    int safe = 1;
    errno = 0;
    while ((entry = readdir(dir))) {
        if (!strncmp(entry->d_name, "2-1.2.", 6)) { safe = 0; break; }
    }
    if (errno) safe = 0;
    if (closedir(dir)) safe = 0;
    return safe;
}

static int write_text(const char *path, const char *text, int flags)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW | flags, 0600);
    if (fd < 0) return 0;
    size_t n = strlen(text);
    int ok = write(fd, text, n) == (ssize_t)n;
    close(fd);
    return ok;
}

static int command(const char *program, const char *arg, const char *option, const char *extra)
{
    pid_t child = fork();
    if (child < 0) return 0;
    if (!child) {
        unsetenv("LD_PRELOAD");
        execl(program, program, arg, option, extra, (char *)NULL);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int event_socket(void)
{
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = (unsigned)getpid();
    addr.nl_groups = 1;
    if (fd >= 0 && bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
    if (fd >= 0) close(fd);
    return -1;
}

static int usb_event(const char *data, size_t size, unsigned sender)
{
    if (sender || !size || data[size - 1]) return 0;
    for (size_t i = 0; i < size;) {
        const char *end = memchr(data + i, 0, size - i);
        if (!end) return 0;
        if (!strcmp(data + i, "SUBSYSTEM=usb")) return 1;
        i = (size_t)(end - data) + 1;
    }
    return 0;
}

static void event_wait(int fd, int milliseconds)
{
    struct pollfd p = {.fd = fd, .events = POLLIN};
    if (poll(&p, 1, milliseconds) <= 0) return;
    for (unsigned i = 0; i < 64; i++) {
        char data[8192];
        struct sockaddr_nl sender;
        socklen_t length = sizeof(sender);
        ssize_t n = recvfrom(fd, data, sizeof(data), MSG_DONTWAIT, (struct sockaddr *)&sender, &length);
        if (n <= 0) break;
        if (length == sizeof(sender) && usb_event(data, (size_t)n, sender.nl_pid)) break;
    }
}

/* Use the existing Linux clock syscall to retain the firmware libc closure.
 * A debounce is a minimum monotonic delay, not a netlink event wait.
 * Polling no descriptors prevents queued USB traffic from shortening the delay.
 * Check cancellation every 50 ms, including a stop marker without a signal. */
static int debounce_wait(int milliseconds)
{
    struct timespec now;
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &now)) return 0;
    int64_t deadline = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec + (int64_t)milliseconds * 1000000;
    for (;;) {
        if (stopping || access(RUN "/stop", F_OK) == 0) return 0;
        if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &now)) return 0;
        int64_t remaining = deadline - ((int64_t)now.tv_sec * 1000000000 + now.tv_nsec);
        if (remaining <= 0) return 1;
        int slice = remaining > 50000000 ? 50 : (int)((remaining + 999999) / 1000000);
        if (poll(NULL, 0, slice) < 0 && errno != EINTR) return 0;
    }
}

enum action { WAIT, CLEAR, BIND, PUBLISH };
struct route {
    struct device bound, published;
    int bind_attempted;
};

static int same_hub(const struct device *a, const struct device *b)
{
    return !strcmp(a->hub, b->hub) && a->hub_number == b->hub_number && a->hub_inode == b->hub_inode;
}

static enum action next_action(struct route *route, const struct device *device, int permitted)
{
    if (route->published.number && (!permitted || !same_hub(&route->published, device) ||
        !device->bound || device->number != route->published.number || device->inode != route->published.inode)) return CLEAR;
    if (!same_hub(&route->bound, device)) {
        memset(&route->bound, 0, sizeof(route->bound));
        route->bind_attempted = 0;
    }
    if (!permitted || !*device->hub) return WAIT;
    if (!device->bound && !route->bind_attempted) return BIND;
    if (device->bound && *route->bound.hub && same_hub(&route->bound, device) &&
        device->number && !route->published.number) return PUBLISH;
    return WAIT;
}

static int remove_node(struct route *route)
{
    struct stat node;
    if (lstat(NODE, &node) == 0) {
        if (!route->published.number || !S_ISCHR(node.st_mode) || major(node.st_rdev) != 189 ||
            minor(node.st_rdev) != 127 + route->published.number || unlink(NODE)) return 0;
    } else if (errno != ENOENT) return 0;
    memset(&route->published, 0, sizeof(route->published));
    return 1;
}

static void state_log(const char *message)
{
    static char previous[160];
    if (!strcmp(previous, message)) return;
    snprintf(previous, sizeof(previous), "%s", message);
    int fd = open(RUN "/bridge.log", O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return;
    struct stat info;
    if (fstat(fd, &info) == 0 && info.st_size > 65536) { if (ftruncate(fd, 0)) { close(fd); return; } }
    char line[256];
    int n = snprintf(line, sizeof(line), "%ld %s\n", (long)time(NULL), message);
    if (n > 0 && n < (int)sizeof(line)) { ssize_t ignored = write(fd, line, (size_t)n); (void)ignored; }
    close(fd);
    write_text(RUN "/status", message, O_CREAT | O_TRUNC);
}

static int run_bridge(struct native *native, const char *module, struct binding *binding)
{
    if (mkdir(RUN, 0700) && errno != EEXIST) return 1;
    if (!ram_path(RUN)) return 1;
    struct stat directory;
    if (lstat(RUN, &directory) || !S_ISDIR(directory.st_mode) || directory.st_uid || (directory.st_mode & 077)) return 1;
    int lock = open(RUN "/owner.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (lock < 0) return 1;
    if (flock(lock, LOCK_EX | LOCK_NB)) { puts("ALREADY_OWNED; no changes"); close(lock); return 2; }
    const char *refusal = access(MODULE, F_OK) == 0 ? "TERMINATED_FOREIGN_MODULE" :
        access(NODE, F_OK) == 0 ? "TERMINATED_FOREIGN_NODE" :
        access(RUN "/stop", F_OK) == 0 ? "TERMINATED_STOP_MARKER" :
        !process_set(native->pid) ? "TERMINATED_PROCESS_SET_CHANGE" : NULL;
    if (refusal) {
        state_log(refusal);
        state_log("STOPPED_WITH_ERROR");
        close(lock);
        return 1;
    }
    int events = event_socket();
    if (events < 0) {
        state_log("TERMINATED_EVENT_SOCKET_FAILURE");
        state_log("STOPPED_WITH_ERROR");
        close(lock);
        return 1;
    }
    signal(SIGTERM, stop_signal);
    signal(SIGINT, stop_signal);
    signal(SIGHUP, stop_signal);
    char pid[64];
    snprintf(pid, sizeof(pid), "%d\n", getpid());
    if (!write_text(RUN "/bridge.pid", pid, O_CREAT | O_TRUNC)) {
        state_log("TERMINATED_PID_WRITE_FAILURE");
        state_log("STOPPED_WITH_ERROR");
        close(events); close(lock); return 1;
    }
    int loaded = 0, failed = 0;
    const char *reason = NULL;
    struct route route = {0};
admission:
    for (;;) {
        if (stopping) { reason = "TERMINATED_SIGNAL"; goto done; }
        if (access(RUN "/stop", F_OK) == 0) { reason = "TERMINATED_STOP_MARKER"; goto done; }
        if (!native_alive(native)) { reason = "TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE"; goto done; }
        if (!process_set(native->pid)) { reason = "TERMINATED_PROCESS_SET_CHANGE"; failed = 1; goto done; }
        if (binding->state == BINDING_NEW) {
            if (!native_stopped(native)) { state_log("WAIT_NATIVE_PAUSED"); event_wait(events, 1000); continue; }
            if (!binding_admit(binding, native)) { reason = "TERMINATED_STOPPED_MEDIA_ADMISSION"; failed = 1; goto done; }
        }
        if (!binding_check(binding)) { reason = "TERMINATED_MUSIC_IDENTITY"; failed = 1; goto done; }
        if (access(MODULE, F_OK) == 0 || access(NODE, F_OK) == 0) {
            reason = "TERMINATED_FOREIGN_MODULE_OR_NODE"; failed = 1; goto done;
        }
        if (!native_stopped(native)) state_log("WAIT_NATIVE_PAUSED");
        else if (!admission_loopback_safe()) state_log("WAIT_SAFE_LOOPBACK_DISCONNECT");
        else break;
        event_wait(events, 1000);
    }
    if (!command("/sbin/insmod", module, "apply=0", NULL)) { reason = "TERMINATED_PROBE_LOAD_FAILURE"; failed = 1; goto done; }
    loaded = 1;
    if (!command("/sbin/rmmod", "xz_hub_depth", NULL, NULL)) { reason = "TERMINATED_PROBE_UNLOAD_FAILURE"; failed = 1; goto done; }
    loaded = 0;
    if (!debounce_wait(300)) { reason = stopping ? "TERMINATED_SIGNAL" : access(RUN "/stop", F_OK) == 0 ? "TERMINATED_STOP_MARKER" : "TERMINATED_ADMISSION_DEBOUNCE_FAILURE"; goto done; }
    /* Repeat all admission guards before metadata-only existing-hub adoption. */
    if (stopping || access(RUN "/stop", F_OK) == 0 || !native_alive(native) ||
        !native_stopped(native) || !admission_loopback_safe() || !process_set(native->pid) ||
        !binding_check(binding) || access(MODULE, F_OK) == 0 || access(NODE, F_OK) == 0) goto admission;
    if (!command("/sbin/insmod", module, "apply=1", "adopt_existing=1")) { reason = "TERMINATED_APPLY_LOAD_FAILURE"; failed = 1; goto done; }
    loaded = 1;
    state_log("WAIT_LOOPBACK");
    while (!stopping && native_alive(native) && access(RUN "/stop", F_OK) != 0) {
        if (!binding_check(binding)) { reason = "TERMINATED_MUSIC_IDENTITY"; failed = 1; break; }
        struct device device;
        if (scan_device(&device)) { reason = "TERMINATED_SCAN_FAILURE"; failed = 1; break; }
        enum action action = next_action(&route, &device, 1);
        if (action == CLEAR) {
            if (!remove_node(&route)) { reason = "TERMINATED_NODE_REMOVE_FAILURE"; failed = 1; break; }
            state_log("ROUTE_DISCONNECTED");
            continue;
        }
        if (action == BIND) {
            unsigned long references;
            if (!native_stopped(native)) { state_log("WAIT_NATIVE_PAUSED"); event_wait(events, 300); continue; }
            if (!debounce_wait(300)) { reason = stopping ? "TERMINATED_SIGNAL" : access(RUN "/stop", F_OK) == 0 ? "TERMINATED_STOP_MARKER" : "TERMINATED_BIND_DEBOUNCE_FAILURE"; break; }
            struct device fresh;
            if (scan_device(&fresh) || !same_hub(&fresh, &device)) continue;
            if (!native_alive(native)) { reason = "TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE"; failed = 1; break; }
            if (!process_set(native->pid)) { reason = "TERMINATED_PROCESS_SET_CHANGE"; failed = 1; break; }
            if (!native_stopped(native)) { state_log("WAIT_NATIVE_PAUSED"); event_wait(events, 300); continue; }
            if (!binding_check(binding)) { reason = "TERMINATED_MUSIC_IDENTITY"; failed = 1; break; }
            if (!number_file(MODULE "refcnt", &references) || references != 1 ||
                !equals(MODULE "parameters/apply", "Y") || fresh.bound) { reason = "TERMINATED_MODULE_ADMISSION_FAILURE"; failed = 1; break; }
            if (stopping || access(RUN "/stop", F_OK) == 0) break;
            char interface[32];
            snprintf(interface, sizeof(interface), "%s:1.0", fresh.hub);
            route.bind_attempted = 1;
            route.bound = fresh;
            if (!write_text("/sys/bus/usb/drivers/hub/bind", interface, 0)) {
                state_log("BIND_FAILED; disconnect loopback to retry");
            } else state_log("WAIT_AUDIO_DEVICE");
        } else if (action == PUBLISH) {
            if (!binding_check(binding)) { reason = "TERMINATED_MUSIC_IDENTITY"; failed = 1; break; }
            if (mknod(NODE, S_IFCHR | 0600, makedev(189, 127 + device.number))) { reason = "TERMINATED_NODE_WRITE_FAILURE"; failed = 1; break; }
            route.published = device;
            state_log("READY_CH3_CH4");
        } else if (!*device.hub) state_log("WAIT_LOOPBACK");
        event_wait(events, 2000);
    }
done:
    binding_close(binding);
    if (!reason) reason = stopping ? "TERMINATED_SIGNAL" :
        access(RUN "/stop", F_OK) == 0 ? "TERMINATED_STOP_MARKER" :
        !native_alive(native) ? "TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE" : "TERMINATED_UNSPECIFIED";
    state_log(reason);
    if (!remove_node(&route)) { state_log("CLEANUP_NODE_REMOVE_FAILURE"); failed = 1; }
    if (loaded) {
        unsigned long references;
        if (loopback_present() || !number_file(MODULE "refcnt", &references) || references) {
            state_log("STOP_PENDING_DISCONNECT; module retained, no force unload");
            while (loopback_present() || !number_file(MODULE "refcnt", &references) || references) event_wait(events, 2000);
        }
        if (!command("/sbin/rmmod", "xz_hub_depth", NULL, NULL)) { state_log("CLEANUP_MODULE_UNLOAD_FAILURE"); failed = 1; }
    }
    state_log(failed ? "STOPPED_WITH_ERROR" : "STOPPED");
    unlink(RUN "/bridge.pid");
    close(events);
    close(lock);
    return failed;
}

static int self_test(void)
{
    unsigned long n;
    assert(decimal("127", &n) && n == 127);
    assert(!decimal("-1", &n) && !decimal("1x", &n) && !decimal("", &n));
    const char usb[] = "add@/devices/test\0SUBSYSTEM=usb\0";
    assert(usb_event(usb, sizeof(usb), 0));
    assert(!usb_event(usb, sizeof(usb), 1));
    assert(!usb_event("SUBSYSTEM=usb", 13, 0));
    const char unrelated[] = "SUBSYSTEM=block\0";
    assert(!usb_event(unrelated, sizeof(unrelated), 0));
    struct route route = {0};
    struct device absent = {0}, hub = {.hub = "2-1.2", .hub_number = 8};
    assert(next_action(&route, &absent, 1) == WAIT);
    assert(next_action(&route, &hub, 0) == WAIT);
    assert(next_action(&route, &hub, 1) == BIND);
    route.bound = hub;
    route.bind_attempted = 1;
    assert(next_action(&route, &hub, 1) == WAIT);
    hub.bound = 1;
    hub.number = 9;
    hub.inode = 100;
    assert(next_action(&route, &hub, 1) == PUBLISH);
    route.published = hub;
    for (int i = 0; i < 100; i++) assert(next_action(&route, &hub, 1) == WAIT);
    assert(next_action(&route, &hub, 0) == CLEAR);
    assert(next_action(&route, &absent, 1) == CLEAR);
    hub.inode++;
    assert(next_action(&route, &hub, 1) == CLEAR);
    memset(&route.published, 0, sizeof(route.published));
    hub.hub_number++;
    assert(next_action(&route, &hub, 1) == WAIT); /* Never adopt an already-bound foreign generation. */
    hub.bound = 0;
    assert(next_action(&route, &hub, 1) == BIND);
    puts("PASS event validation, route lifecycle, duplicate events, ownership, and reconnect policy");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return self_test();
    unsigned long pid;
    if (argc != 3 || (strcmp(argv[1], "--check") && strcmp(argv[1], "--run")) ||
        !decimal(argv[2], &pid) || pid < 1 || pid > 0x7fffffff) {
        fprintf(stderr, "usage: xz-auto-bridge --check|--run NATIVE_PID\n"); return 2;
    }
    if (!strcmp(argv[1], "--check")) alarm(30);
    char executable[512], module[512];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 1 || length == (ssize_t)sizeof(executable) - 1) return 1;
    executable[length] = 0;
    char *slash = strrchr(executable, '/');
    if (!slash) return 1;
    *slash = 0;
    int size = snprintf(module, sizeof(module), "%s/xz_hub_depth.ko", executable);
    if (size < 0 || size >= (int)sizeof(module) || !ram_path(module) || !hash_file(module, MODULE_SHA, 1, NULL)) {
        puts("REFUSED_MODULE_IDENTITY"); return 1;
    }
    struct native native = {.memory = -1};
    if (!qualify_kernel()) { puts("REFUSED_KERNEL_ABI"); return 1; }
    if (!qualify_native(&native, (pid_t)pid)) {
        if (native.memory >= 0) close(native.memory);
        puts("REFUSED_NATIVE_RUNTIME"); return 1;
    }
    struct binding binding = {.volume_fd = -1, .block_fd = -1};
    puts("QUALIFIED_EXACT_RUNTIME_AND_KERNEL");
    int result = 0;
    if (!strcmp(argv[1], "--run")) result = run_bridge(&native, module, &binding);
    else {
        if (!binding_admit(&binding, &native)) { close(native.memory); puts("REFUSED_STOPPED_UNIT_MUSIC_BINDING"); return 1; }
        printf("NATIVE_PAUSED=%d\n", native_stopped(&native));
    }
    binding_close(&binding);
    close(native.memory);
    return result;
}
