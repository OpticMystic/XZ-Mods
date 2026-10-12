/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "runtime.h"
#include "cache.h"
#include "abi.h"
#include "removal.h"
#include "presentation.h"
#include "../runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

struct binding {
    uint32_t impl;
    void *stream;
    int decoder_fd;
    int decoder_flags;
    unsigned device, node;
    unsigned char music_id[8];
    struct stat identity;
    char path[790];
};
struct deck {
    uint32_t epoch, player, reader;
    unsigned device, node;
    int bound, pending, auto_blocked;
    unsigned stock_users;
    struct binding source;
    struct xz126_deck_source source_snapshot;
    struct xz_ram_status status;
    unsigned char *data;
    uint32_t bytes, data_epoch;
    struct stat ram_identity;
    uint32_t music_epoch,music[7];
    uint32_t presentation_epoch;
    uintptr_t ui_player;
    int music_valid;
};
static struct deck decks[2];
static struct binding captures[8];
static unsigned capture_next;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int running, installed, worker_created;
static size_t allocated;
static uint64_t assets_pending,assets_held;
static void *(*stock_create)(void *, const void *);
static int (*stock_read)(void *, void *, int);
int xz_ram_assets_reserve(uint64_t bytes){
    struct sysinfo info;if(!bytes||sysinfo(&info))return 0;
    uint64_t free_bytes=(uint64_t)info.freeram*info.mem_unit;int ok=0;
    pthread_mutex_lock(&mutex);
    uint64_t headroom=XZ_RAM_RESERVE+(allocated<XZ_RAM_TOTAL_LIMIT?XZ_RAM_TOTAL_LIMIT-allocated:0);
    if(running&&assets_pending<=free_bytes&&headroom<=free_bytes-assets_pending&&bytes<=free_bytes-assets_pending-headroom){assets_pending+=bytes;ok=1;}
    pthread_mutex_unlock(&mutex);return ok;
}
void xz_ram_assets_publish(uint64_t bytes){pthread_mutex_lock(&mutex);assets_pending-=bytes;assets_held+=bytes;pthread_mutex_unlock(&mutex);}
void xz_ram_assets_cancel(uint64_t bytes){pthread_mutex_lock(&mutex);assets_pending-=bytes;pthread_mutex_unlock(&mutex);}
void xz_ram_assets_release(uint64_t bytes){pthread_mutex_lock(&mutex);assets_held-=bytes;pthread_mutex_unlock(&mutex);}

static int local_usb(const char *path)
{
    return path && (!strncmp(path, "/media/usb1/", 12) ||
                    !strncmp(path, "/media/usb2/", 12));
}
static int same_file(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
        a->st_size == b->st_size && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
        a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
static void message(struct deck *deck, enum xz_ram_state state, const char *text)
{
    deck->status.state = state;
    snprintf(deck->status.message, sizeof(deck->status.message), "%s", text);
}

static void *create_hook(void *impl, const void *track)
{
    void *stream = stock_create(impl, track);
    if (!stream || !__atomic_load_n(&running, __ATOMIC_ACQUIRE)) return stream;
    /* createSourceInputStream returns LocalInputStream for local media. Its
     * path, FILE*, length and cursor are grounded in the qualified constructor. */
    uint32_t vptr;
    memcpy(&vptr, stream, sizeof(vptr));
    struct binding source = {0};
    if (vptr != UINT32_C(0x003b7bc8)) return stream;
    /* FileReader's StTrackInfo copy uses +0x318 for node and +0x319 for
     * device. Capture the load request, not the player's later UI metadata. */
    source.node = ((const unsigned char *)track)[0x318];
    source.device = ((const unsigned char *)track)[0x319];
    memcpy(source.music_id,(const unsigned char *)track+0x318,8);
    if (source.node || (source.device != 2 && source.device != 3)) return stream;
    memcpy(source.path, (unsigned char *)stream + 4, sizeof(source.path));
    if (!memchr(source.path, 0, sizeof(source.path)) || !local_usb(source.path)) return stream;
    FILE *file;
    memcpy(&file, (unsigned char *)stream + 0x31c, sizeof(file));
    if (!file || fstat(fileno(file), &source.identity) || !S_ISREG(source.identity.st_mode)) return stream;
    source.decoder_fd = fileno(file);
    source.decoder_flags = fcntl(source.decoder_fd, F_GETFD);
    if (source.decoder_flags < 0) return stream;
    source.impl = (uint32_t)(uintptr_t)impl;
    source.stream = stream;
    pthread_mutex_lock(&mutex);
    captures[capture_next++ % 8] = source;
    pthread_mutex_unlock(&mutex);
    return stream;
}

static int read_hook(void *stream, void *output, int count)
{
    int result = -1;
    struct deck *stock_owner = NULL;
    pthread_mutex_lock(&mutex);
    for (unsigned i = 0; i < 2; ++i) {
        struct deck *deck = &decks[i];
        if (!running || !deck->bound ||
            deck->source.stream != stream) continue;
        if (!deck->data || deck->data_epoch != deck->epoch) {
            stock_owner = deck;
            ++deck->stock_users;
            break;
        }
        int64_t position;
        memcpy(&position, (unsigned char *)stream + 0x320, sizeof(position));
        result = (int)xz_ram_read(deck->data, deck->bytes, &position, output,
                                  count > 0 ? (size_t)count : 0);
        memcpy((unsigned char *)stream + 0x320, &position, sizeof(position));
        /* Stock seeks remain virtual. If RAM is released, force the next
         * stock read to seek its FILE* to the virtual cursor before reading. */
        *((unsigned char *)stream + 0x330) = 1;
        break;
    }
    pthread_mutex_unlock(&mutex);
    if (result >= 0) return result;
    result = stock_read(stream, output, count);
    if (stock_owner) {
        pthread_mutex_lock(&mutex);
        --stock_owner->stock_users;
        if (!stock_owner->stock_users && stock_owner->data && stock_owner->status.state == XZ_RAM_LOADING)
            pthread_cond_signal(&changed);
        pthread_mutex_unlock(&mutex);
    }
    return result;
}

int xz_ram_preserve_usb(int index, unsigned device)
{
    int result = 0;
    if (index < 0 || index >= 2) return 0;
    pthread_mutex_lock(&mutex);
        struct deck *deck = &decks[index];
        if (running && (device == 2 || device == 3) && deck->bound && deck->data &&
            (deck->status.state == XZ_RAM_READY || deck->status.state == XZ_RAM_DETACHED) &&
            deck->data_epoch == deck->epoch &&
            deck->device == device && deck->node == 0) {
            message(deck, XZ_RAM_DETACHED, "USB REMOVED / ORIGINAL TRACK RETAINED IN RAM");
            result = 1;
        }
    pthread_mutex_unlock(&mutex);
    return result;
}
int xz_ram_preserve_player(int index,unsigned device,uintptr_t ui_player){
    if(!xz_ram_preserve_usb(index,device))return 0;
    pthread_mutex_lock(&mutex);struct deck *d=&decks[index];
    int kept=d->bound&&d->data_epoch==d->epoch&&d->status.state==XZ_RAM_DETACHED;
    if(kept)d->ui_player=ui_player;
    pthread_mutex_unlock(&mutex);return kept;
}
static int native_word(uint32_t address,uint32_t *value){
    return address>=0x10000&&!(address&3)&&!xz_read_memory(address,value,4);
}
int xz_ram_preserve_sentinel(uintptr_t ui_player,unsigned device,unsigned node){
    if(node||(device!=2&&device!=3)||ui_player<0x10000||ui_player>UINT32_MAX)return 0;
    uint32_t manager,array,count,engine,begin,end,innards,track_manager,track,core;
    unsigned char channel,id[8];
    if(!native_word(0x1c8a308,&manager)||!native_word(manager+0x40,&array)||
       !native_word(manager+0x48,&count)||count<2||
       !native_word((uint32_t)ui_player+0x138,&innards)||
       xz_read_memory(innards+0x26,&channel,1)||channel<1||channel>2)return 0;
    unsigned deck=channel-1;uint32_t expected;
    if(!native_word(array+deck*4,&expected)||expected!=(uint32_t)ui_player||
       !native_word(0x10e9688,&engine)||!native_word(engine+12,&begin)||!native_word(engine+16,&end)||end-begin!=8||
       !native_word(begin+deck*4,&core)||!native_word((uint32_t)ui_player+0x13c,&track_manager)||
       !native_word(track_manager+4,&track)||xz_read_memory(track+0x314,id,8))return 0;
    int kept=0;struct stat current;
    pthread_mutex_lock(&mutex);struct deck *d=&decks[deck];
    if(running&&d->bound&&d->data&&d->data_epoch==d->epoch&&
       (d->status.state==XZ_RAM_READY||d->status.state==XZ_RAM_DETACHED)&&d->player==core&&
       d->device==device&&!d->node&&!memcmp(id,d->source.music_id,3)&&!memcmp(id+4,d->source.music_id+4,4)&&
       !fstat(d->source.decoder_fd,&current)&&same_file(&current,&d->ram_identity)){
        d->ui_player=ui_player;d->presentation_epoch=d->epoch;
        message(d,XZ_RAM_DETACHED,"USB REMOVED / ORIGINAL TRACK RETAINED IN RAM");kept=1;
    }
    pthread_mutex_unlock(&mutex);return kept;
}
int xz_ram_ready_player(int index,unsigned device,uintptr_t ui_player){
    if(index<0||index>=2)return 0;
    pthread_mutex_lock(&mutex);struct deck *d=&decks[index];
    int ready=running&&d->bound&&d->data&&d->data_epoch==d->epoch&&d->device==device&&!d->node&&
        (d->status.state==XZ_RAM_READY||d->status.state==XZ_RAM_DETACHED);
    if(ready){d->ui_player=ui_player;d->presentation_epoch=d->epoch;}
    pthread_mutex_unlock(&mutex);return ready;
}
void xz_ram_forget_player(uintptr_t ui_player,unsigned device,unsigned node){
    for(int i=0;i<2;i++){
        pthread_mutex_lock(&mutex);int matched=decks[i].ui_player==ui_player&&decks[i].bound&&
            (!device||(device==decks[i].device&&node==decks[i].node));pthread_mutex_unlock(&mutex);
        if(matched)xz_ram_invalidate(i);
    }
}
int xz_ram_music_snapshot(int index,uint32_t epoch,const uint32_t record[7]){
    if(index<0||index>=2||!record)return 0;
    const unsigned char *id=(const unsigned char *)record;int result=0;
    pthread_mutex_lock(&mutex);struct deck *d=&decks[index];
    if(running&&d->bound&&d->epoch==epoch&&(d->status.state==XZ_RAM_READY||d->status.state==XZ_RAM_LOADING)&&record[1]&&record[1]!=UINT32_MAX&&
       !memcmp(id,d->source.music_id,3)&&!memcmp(id+4,d->source.music_id+4,4)){
        memcpy(d->music,record,sizeof(d->music));d->music_epoch=epoch;d->music_valid=1;result=1;
    }
    pthread_mutex_unlock(&mutex);return result;
}
static int music_for_display(unsigned index,uint32_t record[7],uint32_t *epoch,int ready){
    if(index>=2||!record)return 0;int result=0;pthread_mutex_lock(&mutex);struct deck *d=&decks[index];
    int retained=d->status.state==XZ_RAM_DETACHED||(d->status.state==XZ_RAM_READY&&(ready||d->presentation_epoch==d->epoch));
    if(running&&d->bound&&d->data&&retained&&d->data_epoch==d->epoch&&d->music_valid&&d->music_epoch==d->epoch){
        memcpy(record,d->music,sizeof(d->music));if(epoch)*epoch=d->epoch;result=1;
    }
    pthread_mutex_unlock(&mutex);return result;
}
int xz_ram_music_retained_epoch(unsigned index,uint32_t record[7],uint32_t *epoch){return music_for_display(index,record,epoch,0);}
int xz_ram_music_owned_epoch(unsigned index,uint32_t record[7],uint32_t *epoch){return music_for_display(index,record,epoch,1);}
int xz_ram_music_retained(unsigned index,uint32_t record[7]){return xz_ram_music_retained_epoch(index,record,NULL);}
int xz_ram_retained_source(unsigned index,struct xz126_deck_source *out){
    if(index>=2||!out)return 0;int result=0;pthread_mutex_lock(&mutex);struct deck *d=&decks[index];
    if(running&&d->bound&&d->data&&d->data_epoch==d->epoch&&d->status.state==XZ_RAM_DETACHED){*out=d->source_snapshot;result=1;}
    pthread_mutex_unlock(&mutex);return result;
}

void xz_ram_invalidate(int index)
{
    if (index < 0 || index >= 2) return;
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[index];
    ++deck->epoch;
    deck->bound = 0;
    deck->pending = 0;
    deck->status.copied = deck->status.bytes = 0;
    message(deck, running ? XZ_RAM_EMPTY : XZ_RAM_UNAVAILABLE, "LOAD A USB TRACK, THEN LOAD IT INTO RAM");
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&mutex);
}

void xz_ram_bind(int index, const struct xz126_deck_source *source)
{
    if (index < 0 || index >= 2 || !source) return;
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[index];
    if (running && local_usb(source->path)) {
        unsigned count = capture_next < 8 ? capture_next : 8;
        for (unsigned i = 0; i < count; ++i) {
            struct binding *binding = &captures[(capture_next - 1 - i) % 8];
            if (binding->impl != source->reader_impl || strcmp(binding->path, source->path)) continue;
            if (deck->bound && deck->source.impl == binding->impl && deck->source.stream == binding->stream &&
                deck->source.decoder_fd == binding->decoder_fd && same_file(&deck->source.identity, &binding->identity) &&
                deck->player == source->player && deck->reader == source->reader &&
                deck->device == binding->device && deck->node == binding->node) break;
            if (deck->bound) { ++deck->epoch; deck->pending = 0; }
            deck->source = *binding;
            deck->source_snapshot=*source;
            deck->player = source->player;
            deck->reader = source->reader;
            deck->device = binding->device;
            deck->node = binding->node;
            deck->bound = 1;
            deck->ui_player=0;
            deck->auto_blocked = 0;
            deck->status.copied = deck->status.bytes = 0;
            message(deck, XZ_RAM_EMPTY, "PLAY TRACK FOR AUTO RAM / OR LOAD NOW");
            if(binding->identity.st_size<=0||(uint64_t)binding->identity.st_size>XZ_RAM_TRACK_LIMIT)
                message(deck,XZ_RAM_ERROR,"TRACK EXCEEDS 64 MB / KEEP USB CONNECTED");
            pthread_cond_signal(&changed);
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
}

struct copy_context { int index, fd; uint32_t epoch; };
static int cancelled(void *opaque)
{
    struct copy_context *context = opaque;
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[context->index];
    int result = !running || !deck->bound || deck->epoch != context->epoch ||
        deck->status.state != XZ_RAM_LOADING;
    pthread_mutex_unlock(&mutex);
    return result;
}
static int copy_read(void *opaque, uint32_t offset, void *output, uint32_t count)
{
    struct copy_context *context = opaque;
    ssize_t result;
    do { result = pread(context->fd, output, count, offset); } while (result < 0 && errno == EINTR);
    /* Give stock USB decoding priority during a long RAM load. */
    struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
    return (int)result;
}
static void progress(void *opaque, uint32_t copied)
{
    struct copy_context *context = opaque;
    pthread_mutex_lock(&mutex);
    if (decks[context->index].epoch == context->epoch)
        decks[context->index].status.copied = copied;
    pthread_mutex_unlock(&mutex);
}

static void dispose(unsigned char *data, uint32_t bytes)
{
    if (!data) return;
    munlock(data, bytes);
    munmap(data, bytes);
    pthread_mutex_lock(&mutex);
    allocated -= bytes;
    pthread_mutex_unlock(&mutex);
}

static void load(int index, uint32_t epoch, struct binding source)
{
    struct copy_context context = {index, -1, epoch};
    struct stat before, after;
    struct sysinfo memory;
    unsigned char *data = NULL;
    uint32_t bytes = 0;
    int ram_fd = -1;
    const char *error = "USB TRACK UNAVAILABLE / RECONNECT AND RETRY";
    context.fd = open(source.path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (context.fd < 0 || fstat(context.fd, &before) || !same_file(&before, &source.identity)) goto finish;
    error = "TRACK EXCEEDS 64 MB / KEEP USB CONNECTED";
    if (before.st_size <= 0 || (uint64_t)before.st_size > XZ_RAM_TRACK_LIMIT) goto finish;
    bytes = (uint32_t)before.st_size;
    error = "NOT ENOUGH FREE RAM / KEEP USB CONNECTED";
    if (sysinfo(&memory)) goto finish;
    pthread_mutex_lock(&mutex);
    uint64_t free_bytes=(uint64_t)memory.freeram*memory.mem_unit;
    int budget = xz_ram_budget(bytes, allocated, free_bytes>assets_pending?free_bytes-assets_pending:0);
    if (budget) {
        allocated += bytes;
        if (decks[index].epoch == epoch) decks[index].status.bytes = bytes;
    }
    pthread_mutex_unlock(&mutex);
    if (!budget) goto finish;
    char name[] = "/dev/shm/xz-ram-track-XXXXXX";
    ram_fd = mkstemp(name);
    if (ram_fd >= 0) { unlink(name); fcntl(ram_fd, F_SETFD, FD_CLOEXEC); }
    /* Reserve tmpfs pages before mapping, so a full RAM filesystem cannot
     * produce SIGBUS halfway through a copy. The file is never published. */
    int reserved = ram_fd >= 0 && !posix_fallocate(ram_fd, 0, bytes);
    data = reserved ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, ram_fd, 0) : MAP_FAILED;
    if (data == MAP_FAILED) {
        data = NULL;
        pthread_mutex_lock(&mutex);
        allocated -= bytes;
        pthread_mutex_unlock(&mutex);
        goto finish;
    }
    error = "CANNOT PIN TRACK IN RAM / KEEP USB CONNECTED";
    if (mlock(data, bytes)) goto finish;
    error = "RAM LOAD INTERRUPTED / RECONNECT USB AND RETRY";
    struct xz_ram_copy copy = {&context, copy_read, cancelled, progress};
    if (xz_ram_copy_all(data, bytes, &copy) || fstat(context.fd, &after) ||
        !same_file(&before, &after)) goto finish;
    close(context.fd);
    context.fd = -1;
    xz_ram_presentation_snapshot(index,epoch);
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[index];
    if (running && deck->bound && deck->epoch == epoch && deck->status.state == XZ_RAM_LOADING) {
        deck->data = data;
        deck->bytes = bytes;
        deck->data_epoch = epoch;
        deck->status.copied = bytes;
        data = NULL;
        /* New reads now use immutable RAM while any older USB read drains.
         * Keep LOADING until the stock FILE descriptor also points at RAM. */
        while (running && deck->bound && deck->epoch == epoch &&
               deck->status.state == XZ_RAM_LOADING && deck->stock_users)
            pthread_cond_wait(&changed, &mutex);
        struct stat decoder;
        int active = running && deck->bound && deck->epoch == epoch && deck->status.state == XZ_RAM_LOADING;
        if (active && !fstat(source.decoder_fd, &decoder) && same_file(&decoder, &source.identity) &&
            !fstat(ram_fd, &deck->ram_identity) && dup2(ram_fd, source.decoder_fd) >= 0) {
            fcntl(source.decoder_fd, F_SETFD, source.decoder_flags | FD_CLOEXEC);
            *((unsigned char *)source.stream + 0x330) = 1;
            message(deck, XZ_RAM_READY, "ORIGINAL TRACK IN RAM / PREPARED STEMS STILL NEED USB");
            error = NULL;
        } else error = "RAM FILE HANDOFF FAILED / USB PLAYBACK RETAINED";
    }
    pthread_mutex_unlock(&mutex);
finish:
    if (context.fd >= 0) close(context.fd);
    if (ram_fd >= 0) close(ram_fd);
    dispose(data, bytes);
    if (error) {
        pthread_mutex_lock(&mutex);
        if (decks[index].epoch == epoch && decks[index].status.state == XZ_RAM_LOADING)
            message(&decks[index], XZ_RAM_ERROR, error);
        pthread_mutex_unlock(&mutex);
    }
}

static void release_copy(int index, uint32_t epoch, struct binding source)
{
    struct stat identity;
    int fd = open(source.path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int present = fd >= 0 && !fstat(fd, &identity) && same_file(&identity, &source.identity);
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[index];
    if (running && deck->bound && deck->epoch == epoch && deck->status.state == XZ_RAM_READY) {
        struct stat decoder;
        if (present && !fstat(source.decoder_fd, &decoder) && same_file(&decoder, &deck->ram_identity) &&
            dup2(fd, source.decoder_fd) >= 0) {
            fcntl(source.decoder_fd, F_SETFD, source.decoder_flags);
            *((unsigned char *)source.stream + 0x330) = 1;
            ++deck->epoch;
            message(deck, XZ_RAM_EMPTY, "RAM RELEASED / USB PLAYBACK RETAINED");
        } else message(deck, XZ_RAM_READY, "USB SOURCE UNAVAILABLE / RAM COPY RETAINED");
    }
    pthread_mutex_unlock(&mutex);
    if (fd >= 0) close(fd);
}

static void *work(void *unused)
{
    (void)unused;
    if (sched_setscheduler(0, SCHED_IDLE, &(struct sched_param){0}))
        setpriority(PRIO_PROCESS, 0, 10);
    for (;;) {
        pthread_mutex_lock(&mutex);
        int index = -1;
        unsigned char *old = NULL;
        uint32_t old_bytes = 0;
        for (int i = 0; i < 2; ++i) {
            struct deck *deck = &decks[i];
            if (deck->data && (!deck->bound || deck->data_epoch != deck->epoch || (deck->status.state != XZ_RAM_READY &&
                deck->status.state != XZ_RAM_DETACHED))) {
                old = deck->data;
                old_bytes = deck->bytes;
                deck->data = NULL;
                deck->bytes = 0;
                break;
            }
            if(!deck->pending&&deck->bound&&!deck->auto_blocked&&deck->status.state==XZ_RAM_EMPTY&&
               deck->source.identity.st_size>0&&(uint64_t)deck->source.identity.st_size<=XZ_RAM_TRACK_LIMIT){
                unsigned char playing=0;
                if(!xz_read_memory(deck->player+0xe77,&playing,1)&&playing==1){
                    ++deck->epoch;deck->pending=1;deck->status.copied=deck->status.bytes=0;
                    message(deck,XZ_RAM_LOADING,"AUTO LOADING PLAYING TRACK / KEEP USB CONNECTED");
                }
            }
            if (deck->pending) index = i;
        }
        if (old) { pthread_mutex_unlock(&mutex); dispose(old, old_bytes); continue; }
        if (!running) { pthread_mutex_unlock(&mutex); break; }
        if (index < 0) {
            struct timeval now;gettimeofday(&now,NULL);
            struct timespec wake={now.tv_sec,now.tv_usec*1000+250000000};
            if(wake.tv_nsec>=1000000000){wake.tv_sec++;wake.tv_nsec-=1000000000;}
            pthread_cond_timedwait(&changed,&mutex,&wake);pthread_mutex_unlock(&mutex);continue;
        }
        struct deck *deck = &decks[index];
        int kind = deck->pending;
        deck->pending = 0;
        uint32_t epoch = deck->epoch;
        struct binding source = deck->source;
        pthread_mutex_unlock(&mutex);
        if (kind == 2) release_copy(index, epoch, source);
        else load(index, epoch, source);
    }
    return NULL;
}

void xz_ram_status(int index, struct xz_ram_status *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    status->state = XZ_RAM_UNAVAILABLE;
    snprintf(status->message, sizeof(status->message), "RAM ENGINE UNAVAILABLE / REBUILD LOADER IN XZ MODS");
    if (index < 0 || index >= 2) return;
    pthread_mutex_lock(&mutex);
    if (running) *status = decks[index].status;
    pthread_mutex_unlock(&mutex);
}
int xz_ram_request(int index)
{
    if (index < 0 || index >= 2) return -1;
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[index];
    int result = -1;
    if (running && deck->bound) {
        result = 0;
        if (deck->status.state != XZ_RAM_READY && deck->status.state != XZ_RAM_DETACHED &&
            deck->status.state != XZ_RAM_LOADING) {
            ++deck->epoch;
            deck->status.copied = deck->status.bytes = 0;
            message(deck, XZ_RAM_LOADING, "LOADING ORIGINAL TRACK / KEEP USB CONNECTED");
            deck->pending = 1;
            pthread_cond_signal(&changed);
        }
    }
    pthread_mutex_unlock(&mutex);
    return result;
}
int xz_ram_cancel(int index)
{
    if (index < 0 || index >= 2) return -1;
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[index];
    int result = -1;
    if (running && deck->status.state == XZ_RAM_LOADING) {
        deck->auto_blocked=1;
        ++deck->epoch;
        deck->pending = 0;
        message(deck, XZ_RAM_EMPTY, "RAM LOAD CANCELLED / USB PLAYBACK RETAINED");
        pthread_cond_signal(&changed);
        result = 0;
    }
    pthread_mutex_unlock(&mutex);
    return result;
}
int xz_ram_release(int index)
{
    if (index < 0 || index >= 2) return -1;
    pthread_mutex_lock(&mutex);
    struct deck *deck = &decks[index];
    int result = -1;
    if (running && deck->bound && deck->status.state == XZ_RAM_READY) {
        deck->auto_blocked=1;
        deck->pending = 2;
        message(deck, XZ_RAM_READY, "CHECKING USB SOURCE BEFORE RELEASING RAM");
        pthread_cond_signal(&changed);
        result = 0;
    }
    pthread_mutex_unlock(&mutex);
    return result;
}

int xz_ram_start(void)
{
    if (worker_created) return 0;
    if (!installed) {
        if ((!stock_create && xz_hook_arm(0x39840, xz_ram_hook_guard(0x39840), (void *)create_hook, (void **)&stock_create)) ||
            (!stock_read && xz_hook_arm(0x326e8, xz_ram_hook_guard(0x326e8), (void *)read_hook, (void **)&stock_read)) ||
            xz_ram_removal_start()||xz_ram_presentation_start()) return -1;
        installed = 1;
    }
    pthread_mutex_lock(&mutex);
    __atomic_store_n(&running, 1, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < 2; ++i) message(&decks[i], XZ_RAM_EMPTY, "LOAD A USB TRACK / AUTO CACHE WHEN PLAYING");
    pthread_mutex_unlock(&mutex);
    if (pthread_create(&worker, NULL, work, NULL)) {
        __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
        return -1;
    }
    worker_created = 1;
    return 0;
}
void xz_ram_stop(void)
{
    if (!worker_created) return;
    pthread_mutex_lock(&mutex);
    __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < 2; ++i) { ++decks[i].epoch; decks[i].bound = decks[i].pending = 0; }
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&mutex);
    pthread_join(worker, NULL);
    worker_created = 0;
}
