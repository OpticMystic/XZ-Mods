#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "cue/native.h"
#include "runtime.h"
#include "audio/runtime.h"
#include "audio/trace.h"
#include "ram/runtime.h"
#include "ram/abi.h"
#include "ui_runtime.h"
#include "key/runtime.h"
#include "doom/native_bridge.h"
#include "kart/xz_kart_runtime.h"
#include "sdk/xz_sdk_host.h"
#include "deck_select.h"
extern int xz_mods_takeover_v1(void);
extern void xz_shell_runtime_notice(const char *text) __attribute__((weak));
extern int xz_shell_runtime_owns(void) __attribute__((weak));
/* classic_load.c: arms a one-shot divert of stock's next load for that side to DECK 3 / 4. 1 when armed. */
extern int xz_classic_load_divert_arm(int side) __attribute__((weak));
static int kart_display_ready(void) { return xz_sdk_host_foreground() && xz_mods_takeover_v1(); }
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Development runtime. No constructor action unless explicitly enabled.
 * Hardware acceptance and the on-deck settings UI are still required. */
static struct xz_cue cues;
static struct xz_cue_native native;
static int (*stock_key)(void *, const void *);
static uint32_t *const key_slot = (uint32_t *)(uintptr_t)0x479090;
static int slot_protection;
static int installed;
static int observer;
static int application_verified;
static void *led_replacement;
static void *skin_event_replacement;
static int memory_fd = -1;
static pthread_mutex_t cue_mutex = PTHREAD_MUTEX_INITIALIZER;
struct code_hook {
    uint32_t address;
    unsigned char saved[8];
    void *trampoline;
    int protection;
};
/* Qualified audio, UI, and RAM-track entrypoints share this bounded table.
 * The complete guarded set currently contains 51 entrypoints; keep room for
 * every enabled adapter so later UI hooks cannot lose their repaint owner. */
static struct code_hook code_hooks[64];
static unsigned code_hook_count;
struct pad_trace_record { uint32_t key,operation_byte,channel,mode,decoded,deck,pad,operation,hotcue,ui_flags; };
struct pad_trace { uint32_t version,sequence,count,enabled;struct pad_trace_record records[64]; };
__attribute__((visibility("default"))) struct pad_trace xz_mods_pad_trace_v1 = {1,0,0,0,{{0}}};

static int enabled(const char *name) {
    const char *value = getenv(name);
    return value && strcmp(value, "1") == 0;
}



void xz_log(const char *message) {
    FILE *file = fopen("/dev/shm/xz-mods.log", "a");
    if (file) { fprintf(file, "%s\n", message); fclose(file); }
}

int xz_read_memory(uint32_t address, void *out, size_t length) {
    if (!application_verified || memory_fd < 0 || !address || !out ||
        !length || length > UINT32_MAX - address) return -1;
    return pread(memory_fd, out, length, (off_t)address) == (ssize_t)length ? 0 : -1;
}

static int range_protection(uintptr_t address, size_t length) {
    char line[512], *cursor;
    const char *permissions;
    unsigned long start, end;
    FILE *maps = fopen("/proc/self/maps", "r");
    int result = -1;
    if (!maps) return -1;
    while (fgets(line, sizeof(line), maps)) {
        start = strtoul(line, &cursor, 16);
        if (*cursor != '-') continue;
        end = strtoul(cursor + 1, &cursor, 16);
        if (*cursor != ' ') continue;
        while (*cursor == ' ') cursor++;
        permissions = cursor;
        if (strlen(permissions) < 4) continue;
        if (address < start || address >= end || length > end - address) continue;
        result = (permissions[0] == 'r' ? PROT_READ : 0)
               | (permissions[1] == 'w' ? PROT_WRITE : 0)
               | (permissions[2] == 'x' ? PROT_EXEC : 0);
        break;
    }
    fclose(maps);
    return result;
}

static int known_application(void) {
    char path[256], command[96], digest[80];
    ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
    FILE *result;
    if (size < 0 || size >= (ssize_t)sizeof(path) - 1) return 0;
    path[size] = 0;
    if (strcmp(path, "/root/pdj/rbp") != 0) return 0;
    /* Resolve the parent's executable, not /proc/self/exe in the checksum child.
     * The path contains only our numeric PID, never an environment string. */
    snprintf(command, sizeof(command), "/bin/busybox md5sum /proc/%ld/exe", (long)getpid());
    result = popen(command, "r");
    if (!result) return 0;
    int read_ok = fgets(digest, sizeof(digest), result) != NULL;
    int exit_status = pclose(result);
    if (!read_ok || exit_status != 0) return 0;
    return strncmp(digest, "627f39fbabb0b47f2d45f4aeac7e5899 ", 33) == 0
        || strncmp(digest, "6a7ccb454e52afa26a73f3380706c9ca ", 33) == 0;
}

int xz_hook_slot(uint32_t address, uint32_t expected, void *replacement, void **original) {
    void **owner = address==0x478d3c&&expected==0x27f074 ? &led_replacement :
        address==0x3ea45c&&expected==0x18194c ? &skin_event_replacement : NULL;
    if (!application_verified || !owner || !replacement || !original || *owner) return -1;
    int protection = range_protection(address,4);
    if (protection < 0 || !(protection & PROT_READ)) return -1;
    uint32_t *slot = (uint32_t *)(uintptr_t)address;
    if (*slot != expected) return -1;
    long size = sysconf(_SC_PAGESIZE);
    if (size <= 0) return -1;
    uintptr_t page = address - address % (uintptr_t)size;
    if (mprotect((void *)page,(size_t)size,protection|PROT_WRITE)) return -1;
    *original = (void *)(uintptr_t)expected;
    *slot = (uint32_t)(uintptr_t)replacement;
    if (mprotect((void *)page,(size_t)size,protection)) {
        *slot = expected; mprotect((void *)page,(size_t)size,protection); return -1;
    }
    *owner = replacement;
    return 0;
}

int xz_hook_arm(uint32_t address, const unsigned char expected[8],
                void *replacement, void **original) {
    static const unsigned char encoder_guard[8]={0x08,0x40,0x2d,0xe9,0x02,0x30,0x80,0xe2};
    static const unsigned char source_guard[8] = {0xf8,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char load_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x5c,0xd0,0x4d,0xe2};
    static const unsigned char unload_guard[8] = {0x70,0x40,0x2d,0xe9,0x28,0x50,0x80,0xe2};
    static const unsigned char touch_guard[8] = {0xf0,0x45,0x2d,0xe9,0x02,0x70,0xa0,0xe1};
    static const unsigned char output_guard[8] = {0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char link_guard[8] = {0x70,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char rekordbox_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char wave_lock_guard[8] = {0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    static const unsigned char wave_unlock_guard[8] = {0x10,0x40,0x2d,0xe9,0x58,0xd0,0x4d,0xe2};
    static const unsigned char mixer_receive_guard[8] = {0x40,0x32,0xd0,0xe5,0xf0,0x47,0x2d,0xe9};
    static const unsigned char mixer_midi_cc_guard[8] = {0x80,0x00,0x13,0xe3,0xb0,0x10,0x81,0xe3};
    static const unsigned char skin_image_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x5c,0xd0,0x4d,0xe2};
    static const unsigned char skin_fill_rect_guard[8] = {0xf0,0x41,0x2d,0xe9,0x08,0xd0,0x4d,0xe2};
    static const unsigned char skin_draw_text_guard[8] = {0xf0,0x45,0x2d,0xe9,0x14,0xd0,0x4d,0xe2};
    static const unsigned char skin_wstring_guard[8] = {0x08,0xd0,0x4d,0xe2,0xba,0xc0,0xd0,0xe1};
    static const unsigned char skin_glyph_guard[8] = {0x30,0x40,0x2d,0xe9,0x14,0xd0,0x4d,0xe2};
    static const unsigned char skin_gamma_guard[8] = {0x02,0x20,0x42,0xe2,0x07,0x00,0x52,0xe3};
    static const unsigned char skin_color_guard[8] = {0x10,0x40,0x2d,0xe9,0x02,0x00,0x51,0xe3};
    static const unsigned char skin_option_guard[8] = {0x70,0x40,0x2d,0xe9,0x01,0x60,0xa0,0xe1};
    const unsigned char *guard = address == 0x8fd5c ? source_guard :
                                 address == 0x34ba0 ? load_guard :
                                 address == 0x348dc ? unload_guard :
                                 address == 0x2628b4 ? touch_guard :
                                 address == 0x76284 ? output_guard :
                                 address == 0xdf994 ? link_guard :
                                 address == 0xe0a50 ? rekordbox_guard :
                                 address == 0x156958 ? wave_lock_guard :
                                 address == 0x1656c4 ? wave_unlock_guard :
                                 address == 0x25d7f4 ? mixer_receive_guard :
                                 address == 0x2bde78 ? mixer_midi_cc_guard : NULL;
    static const unsigned char menu_back_guard[8]={0xf0,0x4f,0x2d,0xe9,0x01,0x40,0xa0,0xe1};
    if(address==0x2eae50)guard=menu_back_guard;
    static const unsigned char master_tempo_guard[8]={0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    if(address==0x28bac4)guard=master_tempo_guard;
    if(address==0xe15b0)guard=encoder_guard;
    /* UiKey_EncoderPush: push {r4,r5,r6,lr}; mov r4,r0. No PC-relative operand. */
    static const unsigned char encoder_push_guard[8]={0x70,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    if(address==0xdec08)guard=encoder_push_guard;
    if(address==0x159ad4)guard=skin_image_guard;
    if(address==0x1594d8)guard=skin_fill_rect_guard;
    if(address==0x15a62c)guard=skin_draw_text_guard;
    if(address==0x15df68)guard=skin_wstring_guard;
    if(address==0x207a70)guard=skin_glyph_guard;
    if(address==0x1576a0)guard=wave_lock_guard;
    if(address==0x157cc0)guard=skin_gamma_guard;
    if(address==0x15cca8)guard=skin_color_guard;
    if(address==0x15c92c)guard=skin_option_guard;
    static const unsigned char key_list_guard[8]={0xf0,0x4f,0x2d,0xe9,0x01,0x60,0xa0,0xe1};
    if(address==0x236270)guard=key_list_guard;
    static const unsigned char key_traffic_guard[8]={0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    if(address==0x17c58c)guard=key_traffic_guard;
    static const unsigned char layered_wave_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x80,0x52,0xa0,0xe1};
    if(address==0x1fab28)guard=layered_wave_guard;
    /* Total_MainLoadMessageProc (classic LOAD into DECK 3/4): movw r3,#0x712; cmp r0,r3. No PC-relative operand. */
    static const unsigned char total_load_guard[8]={0x12,0x37,0x00,0xe3,0x03,0x00,0x50,0xe1};
    if(address==0xbe400)guard=total_load_guard;
    static const unsigned char pc_receive_guard[8]={0xf0,0x4f,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char pc_wave_clear_guard[8]={0x03,0x00,0x50,0xe3,0xf0,0x41,0x2d,0xe9};
    static const unsigned char pc_wave_discard_guard[8]={0x03,0x00,0x50,0xe3,0xf0,0x47,0x2d,0xe9};
    static const unsigned char pc_midi_feedback_guard[8]={0x22,0x30,0xd0,0xe5,0xf0,0x47,0x2d,0xe9};
    if(address==0xe52d0)guard=pc_receive_guard;
    if(address==0xe8194)guard=pc_wave_clear_guard;
    if(address==0xe8604)guard=pc_wave_discard_guard;
    if(address==0x27a3f4)guard=pc_midi_feedback_guard;
    if(!guard)guard=xz_ram_hook_guard(address);
    if (!application_verified || !guard || !expected || !replacement || !original ||
        code_hook_count >= sizeof(code_hooks)/sizeof(code_hooks[0]) || memcmp(expected, guard, 8) != 0) return -1;
    int protection = range_protection(address, 8);
    if (protection < 0 || !(protection & PROT_READ) ||
        memcmp((void *)(uintptr_t)address, guard, 8) != 0) return -1;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return -1;
    uint32_t *trampoline = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED) return -1;
    memcpy(trampoline, guard, 8);
    trampoline[2] = 0xe51ff004; /* ARM ldr pc,[pc,#-4], followed by destination. */
    trampoline[3] = address + 8;
    __builtin___clear_cache((char *)trampoline, (char *)trampoline + 16);
    if (mprotect(trampoline, (size_t)page_size, PROT_READ | PROT_EXEC) != 0) {
        munmap(trampoline, (size_t)page_size); return -1;
    }
    uintptr_t page = address - address % (uintptr_t)page_size;
    if (mprotect((void *)page, (size_t)page_size, protection | PROT_WRITE) != 0) {
        munmap(trampoline, (size_t)page_size); return -1;
    }
    uint32_t patch[2] = {0xe51ff004, (uint32_t)(uintptr_t)replacement};
    *original = trampoline;
    memcpy((void *)(uintptr_t)address, patch, 8);
    __builtin___clear_cache((char *)(uintptr_t)address, (char *)(uintptr_t)address + 8);
    if (mprotect((void *)page, (size_t)page_size, protection) != 0) {
        memcpy((void *)(uintptr_t)address, guard, 8);
        __builtin___clear_cache((char *)(uintptr_t)address, (char *)(uintptr_t)address + 8);
        mprotect((void *)page, (size_t)page_size, protection);
        *original = NULL;
        munmap(trampoline, (size_t)page_size);
        return -1;
    }
    struct code_hook *record = &code_hooks[code_hook_count++];
    record->address = address; record->protection = protection; record->trampoline = trampoline;
    memcpy(record->saved, guard, 8);
    return 0;
}

int xz_runtime_beat_jump(int deck,int beats) {
    pthread_mutex_lock(&cue_mutex);
    int result=xz_cue_native_beat_jump(&native,deck,beats);
    pthread_mutex_unlock(&cue_mutex);
    return result;
}

/* DECK 3 / DECK 4 (deck_select.h). The firmware's key task and the global key hook both route here. */
static struct xz_deck_select deck_select;
static pthread_mutex_t deck_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Publishes the sides and carries out one outcome with no lock held. 1: stock must not see the key. */
static int deck_outcome(const struct xz_ds_outcome *outcome, int left, int right, int available, uint16_t code) {
    xz_extra_decks_publish_sides(left, right);
    if (outcome->notice == XZ_DS_NOTICE_NO_ROUTE && available) {
        xz_log(XZ_DS_NO_ROUTE_TEXT);
        if (xz_shell_runtime_notice) xz_shell_runtime_notice(XZ_DS_NO_ROUTE_TEXT);
    }
    if (outcome->unknown_first) {
        char line[96];
        snprintf(line, sizeof(line), "Deck select: unknown key 0x%04x swallowed on a side driving DECK 3/4", code);
        xz_log(line);
    }
    if (outcome->verdict == XZ_DS_DELIVER) (void)xz_extra_deck_input(&outcome->event);
    return outcome->verdict != XZ_DS_PASS;
}

static int deck_route(void *self, const void *input) {
    struct xz_ds_key key;
    if (!xz_ds_key_decode(self, input, &key)) return 0;
    struct xz_extra_route route = xz_extra_decks_route();
    pthread_mutex_lock(&deck_mutex);
    xz_deck_select_route(&deck_select, route.route_up);
    struct xz_ds_outcome outcome = xz_deck_select_key(&deck_select, &key, xz_game_milliseconds());
    int left = deck_select.side[0].target == XZ_DS_EXTRA, right = deck_select.side[1].target == XZ_DS_EXTRA;
    pthread_mutex_unlock(&deck_mutex);
    return deck_outcome(&outcome, left, right, route.available, key.code);
}

int xz_runtime_deck_global_key(uint16_t code, unsigned channel, unsigned operation) {
    if (!installed || observer || (channel != 1 && channel != 2)) return 0;
    /* Every key passes the global hook: only SHIFT and the GLOBAL-path rows take the lock. */
    const struct xz_ds_row *row = code == XZ_DS_KEY_SHIFT ? NULL : xz_ds_row_for(code, NULL);
    if (code != XZ_DS_KEY_SHIFT && (!row || row->path != XZ_DS_PATH_GLOBAL)) return 0;
    struct xz_extra_route route = xz_extra_decks_route();
    pthread_mutex_lock(&deck_mutex);
    xz_deck_select_route(&deck_select, route.route_up);
    struct xz_ds_outcome outcome = xz_deck_select_global_key(&deck_select, channel, code, operation, xz_game_milliseconds());
    int left = deck_select.side[0].target == XZ_DS_EXTRA, right = deck_select.side[1].target == XZ_DS_EXTRA;
    pthread_mutex_unlock(&deck_mutex);
    if (outcome.verdict == XZ_DS_LOAD) {
        /* The interface on screen owns the browser cursor, so it decides where the track comes from. */
        int side = outcome.event.deck;
        enum xz_ds_load_route to = XZ_DS_LOAD_SWALLOW;
        if (xz_shell_runtime_owns && xz_shell_runtime_owns()) to = XZ_DS_LOAD_SHELL;
        else if (xz_classic_load_divert_arm && xz_classic_load_divert_arm(side)) to = XZ_DS_LOAD_STOCK;
        pthread_mutex_lock(&deck_mutex);
        xz_deck_select_load_route(&deck_select, side, to);
        pthread_mutex_unlock(&deck_mutex);
        xz_extra_decks_publish_sides(left, right);
        if (to == XZ_DS_LOAD_SWALLOW) {
            xz_log(XZ_DS_LOAD_REFUSED_TEXT);
            if (xz_shell_runtime_notice) xz_shell_runtime_notice(XZ_DS_LOAD_REFUSED_TEXT);
        }
        return to == XZ_DS_LOAD_SHELL ? XZ_RUNTIME_KEY_SHELL_LOAD + side : to == XZ_DS_LOAD_SWALLOW;
    }
    return deck_outcome(&outcome, left, right, route.available, code);
}

int xz_runtime_deck_select_toggle(int side) {
    if (!installed || observer || side < 0 || side > 1) return -1;
    struct xz_extra_route route = xz_extra_decks_route();
    if (!route.available) return -1;
    struct xz_ds_outcome outcome;
    memset(&outcome, 0, sizeof(outcome));
    pthread_mutex_lock(&deck_mutex);
    xz_deck_select_route(&deck_select, route.route_up);
    xz_deck_select_toggle(&deck_select, side, &outcome);
    int left = deck_select.side[0].target == XZ_DS_EXTRA, right = deck_select.side[1].target == XZ_DS_EXTRA;
    pthread_mutex_unlock(&deck_mutex);
    xz_extra_decks_publish_sides(left, right);
    if (outcome.notice == XZ_DS_NOTICE_NO_ROUTE) { xz_log(XZ_DS_NO_ROUTE_TEXT); return 0; }
    return outcome.toggled ? 1 : -1;
}

static int physical_key(void *self, const void *input) {
    if(!observer && xz_kart_runtime_physical(self,input)) return 1;
    if(!observer && xz_ui_runtime_arcade_key(self,input)) return 1;
    if(!observer && xz_doom_physical_key(self,input)) return 1;
    struct xz_cue_event event = {0};
    pthread_mutex_lock(&cue_mutex);
    int decoded = xz_cue_native_decode(&native, self, input, &event);
    pthread_mutex_unlock(&cue_mutex);
    int routed = !observer && deck_route(self, input);
    unsigned flags = 0;
    int pad_consumed = !routed && decoded && !observer && xz_ui_runtime_pad(&event, &flags);
    if (xz_mods_pad_trace_v1.enabled && self && input) {
        uint16_t key; uint32_t mode;
        memcpy(&key,(const unsigned char *)input+8,2);
        memcpy(&mode,(const unsigned char *)self+0x80,4);
        pthread_mutex_lock(&cue_mutex);
        struct pad_trace *t=&xz_mods_pad_trace_v1;
        __atomic_add_fetch(&t->sequence,1,__ATOMIC_SEQ_CST);
        t->records[t->count%64]=(struct pad_trace_record){key,((const unsigned char *)input)[11],
            ((const unsigned char *)self)[0x26],mode,(uint32_t)decoded,(uint32_t)event.deck,
            (uint32_t)event.pad,(uint32_t)event.operation,(uint32_t)event.hotcue_mode,
            /* bit 30: SHIFT held (+0x34), bit 31: deck select took the key */
            flags|(((const unsigned char *)self)[0x34]?1u<<30:0u)|(routed?1u<<31:0u)};
        t->count++;
        __atomic_add_fetch(&t->sequence,1,__ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&cue_mutex);
    }
    if (routed || pad_consumed) return 1;
    pthread_mutex_lock(&cue_mutex);
    int consumed = decoded && !observer && xz_cue_before(&cues, &event);
    pthread_mutex_unlock(&cue_mutex);
    if (consumed) return 1;
    int result = stock_key(self, input);
    if (observer && decoded && (event.pad >= 0 || event.play)) {
        char line[160];
        snprintf(line, sizeof(line), "key deck=%d pad=%d op=%d hotcue=%d play=%d stock=%d",
                 event.deck, event.pad, event.operation, event.hotcue_mode, event.play, result);
        xz_log(line);
    }
    if (decoded && !observer) {
        pthread_mutex_lock(&cue_mutex);
        xz_cue_after(&cues, &event, result);
        pthread_mutex_unlock(&cue_mutex);
    }
    return result;
}

int xz_runtime_set_cues(int gate, int smart) {
    if (!installed || observer) return -1;
    pthread_mutex_lock(&cue_mutex);
    xz_cue_settings(&cues, gate, smart);
    pthread_mutex_unlock(&cue_mutex);
    return 0;
}

unsigned xz_runtime_cue_flags(void) {
    pthread_mutex_lock(&cue_mutex);
    unsigned result = (cues.gate ? 1u : 0u) | (cues.smart ? 2u : 0u);
    pthread_mutex_unlock(&cue_mutex);
    return result;
}

__attribute__((constructor)) static void start(void) {
    if (!enabled("XZ_MODS_ENABLE") || !known_application()) return;
    application_verified = 1;
    memory_fd = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    int protection = range_protection((uintptr_t)key_slot, sizeof(*key_slot));
    if (protection < 0 || !(protection & PROT_READ) || *key_slot != 0x28e88c) {
        xz_log("REFUSED: physical-key vtable differs from verified firmware");
        return;
    }
    observer = enabled("XZ_MODS_OBSERVER");
    xz_mods_pad_trace_v1.enabled = enabled("XZ_MODS_PAD_TRACE");
    xz_cue_init(&cues, xz_cue_native_api(&native));
    xz_cue_settings(&cues, 0, 0);
    xz_deck_select_init(&deck_select);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return;
    uintptr_t page = (uintptr_t)key_slot - (uintptr_t)key_slot % (uintptr_t)page_size;
    if (mprotect((void *)page, (size_t)page_size, protection | PROT_WRITE) != 0) {
        xz_log("REFUSED: cannot change vtable protection");
        return;
    }
    stock_key = (int (*)(void *, const void *))(uintptr_t)*key_slot;
    slot_protection = protection;
    native.verified = 1;
    *key_slot = (uint32_t)(uintptr_t)physical_key;
    if (mprotect((void *)page, (size_t)page_size, protection) != 0) {
        *key_slot = (uint32_t)(uintptr_t)stock_key;
        mprotect((void *)page, (size_t)page_size, protection);
        native.verified = 0;
        xz_log("REFUSED: could not restore original page protection");
        return;
    }
    installed = 1;
    xz_log(observer ? "OBSERVER installed; stock dispatch retained" : "EXPERIMENTAL gate/smart cue adapter installed");
    if (observer) return;
    if (xz_extra_decks_start()) xz_log("DECK 3/4 players unavailable");
    if(xz_doom_bridge_start()) xz_log("Doom: native input bridge unavailable");
    xz_kart_runtime_display_probe(kart_display_ready);
    if(xz_kart_runtime_start()) xz_log("Kart: explicit physical-input bridge unavailable");
    (void)xz_audio_trace_start();
    if(memory_fd < 0 || xz_ram_start()) xz_log("RAM tracks: guarded adapter unavailable");
    int audio_ready = memory_fd >= 0 && xz_audio_start() == 0;
    /* Engines are available; persisted MODS settings own behavior. Neutral key
       processing leaves audio unchanged until a performance action sets cents. */
    xz_key_runtime_observe(0);
    int key_ready = audio_ready && xz_key_runtime_start()==0;
    if (enabled("XZ_MODS_UI")) xz_ui_runtime_start(audio_ready, key_ready, 1);
    { extern int xz_classic_load_start(void); if (xz_classic_load_start()) xz_log("Classic LOAD into DECK 3/4 unavailable: guard or hook slot refused"); }
}

__attribute__((destructor)) static void stop(void) {
    xz_extra_decks_stop();
    xz_audio_trace_stop();
    xz_ui_runtime_stop();
    xz_kart_runtime_stop();
    xz_key_runtime_stop();
    xz_audio_stop();
    xz_ram_stop();
    if (memory_fd >= 0) { close(memory_fd); memory_fd = -1; }
    if (!installed || *key_slot != (uint32_t)(uintptr_t)physical_key) return;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return;
    uintptr_t page = (uintptr_t)key_slot - (uintptr_t)key_slot % (uintptr_t)page_size;
    if (mprotect((void *)page, (size_t)page_size, slot_protection | PROT_WRITE) != 0) return;
    *key_slot = (uint32_t)(uintptr_t)stock_key;
    mprotect((void *)page, (size_t)page_size, slot_protection);
    native.verified = 0;
}
