#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "cue/native.h"
#include "runtime.h"
#include "diary/diary.h"
#pragma weak xz_diary_start
#pragma weak xz_diary_stop
#include "audio/runtime.h"
#include "audio/trace.h"
#include "ram/runtime.h"
#include "ram/abi.h"
#include "ui_runtime.h"
#include "network/runtime.h"
#include "key/runtime.h"
#include "doom/native_bridge.h"
#include "kart/xz_kart_runtime.h"
#include "sdk/xz_sdk_host.h"
#include "deck_select.h"
#include "deck_input_trace.h"
#include "sync_link_runtime.h"
extern void xz_extra_decks_native_master_selected(unsigned native_deck);
#include "jog_display_runtime.h"
extern int xz_mods_takeover_v1(void);
extern void xz_shell_runtime_notice(const char *text) __attribute__((weak));
extern int xz_shell_runtime_owns(void) __attribute__((weak));
extern void xz_shell_runtime_native_input(int side,uint32_t monotonic_ms) __attribute__((weak));
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
static struct code_hook code_hooks[96];
static unsigned code_hook_count;
struct pad_trace_record { uint32_t key,operation_byte,channel,mode,decoded,deck,pad,operation,hotcue,ui_flags; };
struct pad_trace { uint32_t version,sequence,count,enabled;struct pad_trace_record records[64]; };
__attribute__((visibility("default"))) struct pad_trace xz_mods_pad_trace_v1 = {1,0,0,0,{{0}}};
__attribute__((visibility("default"),used)) struct xz_deck_input_trace xz_mods_deck_input_trace_v1;
__attribute__((visibility("default"),used)) struct xz_deck_input_codes xz_mods_deck_input_codes_v1;
static struct xz_deck_input_motion deck_input_motion;

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
    /* ui_com_DrawCautionStatus: push {r4-r11,lr}; sub sp,sp,#0xa4. ui_com_DrawCautionBrowse: push {r4-r11,lr};
       vpush {d8,d9}. No PC-relative operand. */
    static const unsigned char caution_status_guard[8]={0xf0,0x4f,0x2d,0xe9,0xa4,0xd0,0x4d,0xe2};
    static const unsigned char caution_browse_guard[8]={0xf0,0x4f,0x2d,0xe9,0x04,0x8b,0x2d,0xed};
    if(address==0x147f08)guard=caution_status_guard;
    if(address==0x146fcc)guard=caution_browse_guard;
    static const unsigned char key_list_guard[8]={0xf0,0x4f,0x2d,0xe9,0x01,0x60,0xa0,0xe1};
    if(address==0x236270)guard=key_list_guard;
    static const unsigned char key_traffic_guard[8]={0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
    if(address==0x17c58c)guard=key_traffic_guard;
    static const unsigned char layered_wave_guard[8] = {0xf0,0x4f,0x2d,0xe9,0x80,0x52,0xa0,0xe1};
    if(address==0x1fab28)guard=layered_wave_guard;
    /* Total_MainLoadMessageProc (classic LOAD into DECK 3/4): movw r3,#0x712; cmp r0,r3. No PC-relative operand. */
    static const unsigned char total_load_guard[8]={0x12,0x37,0x00,0xe3,0x03,0x00,0x50,0xe1};
    if(address==0xbe400)guard=total_load_guard;
    /* Native browser popup/DB reply ownership. All guards have relocatable ARM prologues. */
    static const struct {uint32_t address;unsigned char guard[8];} classic_popup_guards[]={
        {0x20594c,{0xf0,0x4f,0x2d,0xe9,0xfe,0x4b,0x80,0xe2}},
        {0x2028a0,{0x00,0x30,0x90,0xe5,0xf0,0x4f,0x2d,0xe9}},
        {0x171fd0,{0x00,0xc0,0xa0,0xe1,0x04,0x40,0x2d,0xe5}},
        {0x21addc,{0xf0,0x4f,0x2d,0xe9,0x01,0x50,0xa0,0xe1}},
        {0x172264,{0x38,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1}}
    };
    for(unsigned i=0;i<sizeof(classic_popup_guards)/sizeof(classic_popup_guards[0]);i++)
        if(address==classic_popup_guards[i].address)guard=classic_popup_guards[i].guard;

    static const unsigned char pc_receive_guard[8]={0xf0,0x4f,0x2d,0xe9,0x00,0x50,0xa0,0xe1};
    static const unsigned char pc_wave_clear_guard[8]={0x03,0x00,0x50,0xe3,0xf0,0x41,0x2d,0xe9};
    static const unsigned char pc_wave_discard_guard[8]={0x03,0x00,0x50,0xe3,0xf0,0x47,0x2d,0xe9};
    static const unsigned char pc_midi_feedback_guard[8]={0x22,0x30,0xd0,0xe5,0xf0,0x47,0x2d,0xe9};
    if(address==0xe52d0)guard=pc_receive_guard;
    if(address==0xe8194)guard=pc_wave_clear_guard;
    if(address==0xe8604)guard=pc_wave_discard_guard;
    if(address==0x27a3f4)guard=pc_midi_feedback_guard;
    static const struct {uint32_t address;unsigned char guard[8];} four_deck_runtime_guards[]={
        {0x59f70,{0xf8,0x40,0x2d,0xe9,0x14,0x50,0x80,0xe2}},
        {0x532c0,{0x00,0x30,0x91,0xe5,0x01,0x10,0x43,0xe2}},
        {0x53140,{0x00,0xc0,0x91,0xe5,0x04,0x40,0x2d,0xe5}},
        {0x531d8,{0x00,0xc0,0x91,0xe5,0x10,0x40,0x2d,0xe9}},
        {0x53090,{0x00,0x00,0x53,0xe3,0x30,0x00,0x2d,0xe9}},
        {0x597c8,{0x00,0x20,0x91,0xe5,0x08,0x40,0x2d,0xe9}},
        {0x2591e0,{0xf0,0x4f,0x2d,0xe9,0x00,0x40,0xa0,0xe1}}
    };
    for(unsigned i=0;i<sizeof(four_deck_runtime_guards)/sizeof(four_deck_runtime_guards[0]);i++)
        if(address==four_deck_runtime_guards[i].address)guard=four_deck_runtime_guards[i].guard;
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

int xz_runtime_deck_selection_snapshot(struct xz_ds_selection *out){
    if(!out||!installed||observer||pthread_mutex_trylock(&deck_mutex))return 0;
    uint32_t epoch=xz_extra_decks_epoch();
    struct xz_extra_route route=xz_extra_decks_route();
    if(epoch!=xz_extra_decks_epoch()) {pthread_mutex_unlock(&deck_mutex);return 0;}
    xz_deck_select_route_instance(&deck_select,route.route_up&&epoch,epoch);
    xz_deck_select_snapshot(&deck_select,out);
    pthread_mutex_unlock(&deck_mutex);return 1;
}

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
    if (outcome->verdict == XZ_DS_DELIVER && !xz_extra_deck_input(&outcome->event)) {
        /* The physical brake knob still belongs to native when its software copy is unavailable. */
        return !(outcome->also_stock && outcome->event.kind == XZ_EXTRA_IN_VINYL_SPEED);
    }
    return outcome->verdict != XZ_DS_PASS && !outcome->also_stock;
}

static int deck_route(void *self, const void *input) {
    struct xz_ds_key key;
    if (!xz_ds_key_decode(self, input, &key)) return 0;
    struct xz_extra_route route = xz_extra_decks_route();
    xz_deck_input_codes_write(&xz_mods_deck_input_codes_v1,&key,XZ_DECK_TRACE_FALLBACK,0,
        xz_extra_decks_control_revision(),
        xz_game_milliseconds());
    struct xz_extra_decks feedback;
    int have_feedback=route.route_up&&(key.code==XZ_DS_KEY_SHIFT||key.code==XZ_DS_KEY_TEMPO)&&xz_extra_decks_snapshot(&feedback);
    pthread_mutex_lock(&deck_mutex);
    if(have_feedback)xz_deck_select_feedback(&deck_select,&feedback);
    xz_deck_select_route_instance(&deck_select, route.route_up,xz_extra_decks_epoch());
    uint32_t input_ms=xz_game_milliseconds();
    struct xz_ds_outcome outcome = xz_deck_select_key(&deck_select, &key, input_ms);
    int left = deck_select.side[0].target == XZ_DS_EXTRA, right = deck_select.side[1].target == XZ_DS_EXTRA;
    pthread_mutex_unlock(&deck_mutex);
    if (outcome.verdict == XZ_DS_PASS && key.code == 0x4305 && key.op == 4 && key.delta && xz_shell_runtime_native_input)
        xz_shell_runtime_native_input(key.side, input_ms);
    xz_deck_input_trace_write_motion(&xz_mods_deck_input_trace_v1,&deck_input_motion,&key,XZ_DECK_TRACE_FALLBACK,outcome.verdict,
        (left?1u:0u)|(right?2u:0u)|(route.route_up?4u:0u)|(route.available?8u:0u),input_ms);
    int r = deck_outcome(&outcome, left, right, route.available, key.code);
    if (outcome.toggled) (void)xz_extra_decks_snapshot(&feedback);
    return r;
}

int xz_runtime_deck_global_key(uint16_t code, unsigned channel, unsigned operation) {
    if (!installed || observer || (channel != 1 && channel != 2)) return 0;
    {
        struct xz_ds_key seen={.side=(int)channel-1,.code=code,.op=operation};
        xz_deck_input_codes_write(&xz_mods_deck_input_codes_v1,&seen,XZ_DECK_TRACE_GLOBAL_KEY,0,
            xz_extra_decks_control_revision(),xz_game_milliseconds());
    }
    /* Every key passes the global hook: only SHIFT and the GLOBAL-path rows take the lock. */
    const struct xz_ds_row *row = code == XZ_DS_KEY_SHIFT ? NULL : xz_ds_row_for(code, NULL);
    if (code != XZ_DS_KEY_SHIFT && (!row || row->path != XZ_DS_PATH_GLOBAL)) return 0;
    struct xz_extra_route route = xz_extra_decks_route();
    struct xz_extra_decks feedback;
    int have_feedback=route.route_up&&code==XZ_DS_KEY_SHIFT&&xz_extra_decks_snapshot(&feedback);
    pthread_mutex_lock(&deck_mutex);
    if(have_feedback)xz_deck_select_feedback(&deck_select,&feedback);
    xz_deck_select_route_instance(&deck_select, route.route_up,xz_extra_decks_epoch());
    uint32_t input_ms=xz_game_milliseconds();
    struct xz_ds_key trace_key={.side=(int)channel-1,.code=code,.op=operation};
    trace_key.shift=deck_select.side[channel-1].phase==XZ_DS_DOWN_ONCE||
        deck_select.side[channel-1].phase==XZ_DS_DOWN_TWICE||deck_select.side[channel-1].phase==XZ_DS_MODIFIER;
    struct xz_ds_outcome outcome = xz_deck_select_global_key(&deck_select, channel, code, operation, input_ms);
    int left = deck_select.side[0].target == XZ_DS_EXTRA, right = deck_select.side[1].target == XZ_DS_EXTRA;
    pthread_mutex_unlock(&deck_mutex);
    xz_deck_input_trace_write_motion(&xz_mods_deck_input_trace_v1,&deck_input_motion,&trace_key,XZ_DECK_TRACE_GLOBAL_KEY,outcome.verdict,
        (left?1u:0u)|(right?2u:0u)|(route.route_up?4u:0u)|(route.available?8u:0u),input_ms);
    /* Native MASTER requests return the software group to the effective native master.
       The stock event still arbitrates ON/OFF; the pressed side is not a master identity. */
    if (code == 0x4312 && operation == 0 && outcome.verdict == XZ_DS_PASS &&
        !(channel == 1 ? left : right))
        xz_extra_decks_native_master_selected(channel-1);
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
    int r = deck_outcome(&outcome, left, right, route.available, code);
    if (outcome.toggled) (void)xz_extra_decks_snapshot(&feedback);
    return r;
}

static int deck_select_apply(int side, int want_extra) {
    if (!installed || observer || side < 0 || side > 1) return -1;
    struct xz_extra_route route = xz_extra_decks_route();
    if (!route.available) return -1;
    struct xz_extra_decks feedback;
    int have_feedback=route.route_up&&xz_extra_decks_snapshot(&feedback);
    struct xz_ds_outcome outcome;
    memset(&outcome, 0, sizeof(outcome));
    pthread_mutex_lock(&deck_mutex);
    if(have_feedback)xz_deck_select_feedback(&deck_select,&feedback);
    xz_deck_select_route_instance(&deck_select, route.route_up,xz_extra_decks_epoch());
    int already=want_extra>=0&&(deck_select.side[side].target==XZ_DS_EXTRA)==want_extra;
    if(!already)xz_deck_select_toggle(&deck_select, side, &outcome);
    int left = deck_select.side[0].target == XZ_DS_EXTRA, right = deck_select.side[1].target == XZ_DS_EXTRA;
    pthread_mutex_unlock(&deck_mutex);
    xz_extra_decks_publish_sides(left, right);
    if (outcome.toggled) (void)xz_extra_decks_snapshot(&feedback);
    if (outcome.notice == XZ_DS_NOTICE_NO_ROUTE) { xz_log(XZ_DS_NO_ROUTE_TEXT); return 0; }
    return already || outcome.toggled ? 1 : -1;
}
int xz_runtime_deck_select_toggle(int side) {
    return deck_select_apply(side,-1);
}
int xz_runtime_deck_select_target(int side, int extra) {
    if(extra!=0&&extra!=1)return -1;
    return deck_select_apply(side,extra);
}

static int physical_priority(void *self, const void *input) {
    return xz_kart_runtime_physical(self,input) || xz_ui_runtime_arcade_key(self,input) || xz_doom_physical_key(self,input);
}

int xz_runtime_deck_early_key(uint16_t code){
 const struct xz_ds_row *row=xz_ds_row_for(code,NULL);
 return row&&row->path==XZ_DS_PATH_PLAYER&&row->cls!=XZ_DS_ROW_STOCK;
}
int xz_runtime_deck_games_pending(void){
 return xz_kart_runtime_input_pending()||xz_ui_runtime_arcade_input_pending()||xz_doom_input_pending();
}
static int owned_player_release(const struct xz_ds_key *key){
 if(key->op!=2&&key->op!=3)return 0;
 unsigned slot=0;(void)xz_ds_row_for(key->code,&slot);uint64_t bit=(uint64_t)1<<slot;
 return !!((deck_select.side[key->side].owner[XZ_DS_EXTRA]|deck_select.side[key->side].owner[XZ_DS_NATIVE]|deck_select.side[key->side].early_native_down|deck_select.side[key->side].early_native_release)&bit);
}
int xz_runtime_deck_global_input(void *self, const void *input) {
    if (!installed || observer || !input) return 0;
    const unsigned char *packet=input;
    struct xz_ds_key key;
    if(xz_ds_key_decode_global(input,&key))
     xz_deck_input_codes_write(&xz_mods_deck_input_codes_v1,&key,XZ_DECK_TRACE_GLOBAL,0,
         xz_extra_decks_control_revision(),xz_game_milliseconds());
    if(!xz_ds_key_decode_global(input,&key)||!xz_runtime_deck_early_key(key.code)||
       (self&&((const unsigned char *)self)[0x26]!=packet[10]))return 0;
    pthread_mutex_lock(&deck_mutex);
    unsigned slot=0;(void)xz_ds_row_for(key.code,&slot);uint64_t bit=(uint64_t)1<<slot;
    struct xz_ds_side *side=&deck_select.side[key.side];
    /* Retire a PC-eaten release before a new game can acquire this press. */
    if(key.op==0&&(side->early_native_release&bit)){side->early_native_release&=~bit;side->owner[XZ_DS_NATIVE]&=~bit;}
    int owned=key.op<=3&&!!((side->owner[XZ_DS_EXTRA]|side->owner[XZ_DS_NATIVE]|side->early_native_down)&bit);
    int release=owned||owned_player_release(&key);
    key.shift=deck_select.side[key.side].phase!=XZ_DS_IDLE&&deck_select.side[key.side].phase!=XZ_DS_RELEASED;
    pthread_mutex_unlock(&deck_mutex);
    /* Kart must observe contacts even while inactive so a later grant cannot take a held contact. */
    if(key.op<=3&&(release||!xz_kart_runtime_input_pending())&&xz_kart_runtime_observe_key(packet[10],input)&&!release)return 1;
    if(!release&&xz_runtime_deck_games_pending()){
     if(!self)return 0;
     if(physical_priority(self,input))return 1;
    }
    struct xz_extra_route route=xz_extra_decks_route();
    pthread_mutex_lock(&deck_mutex);
    xz_deck_select_route_instance(&deck_select,route.route_up,xz_extra_decks_epoch());
    uint32_t input_ms=xz_game_milliseconds();
    struct xz_ds_outcome outcome=xz_deck_select_global_player(&deck_select,&key,input_ms);
    int left=deck_select.side[0].target==XZ_DS_EXTRA,right=deck_select.side[1].target==XZ_DS_EXTRA;
    pthread_mutex_unlock(&deck_mutex);
    if(outcome.verdict==XZ_DS_PASS&&key.code==0x4305&&key.op==4&&key.delta&&xz_shell_runtime_native_input)
     xz_shell_runtime_native_input(key.side,input_ms);
    xz_deck_input_trace_write_motion(&xz_mods_deck_input_trace_v1,&deck_input_motion,&key,XZ_DECK_TRACE_GLOBAL,outcome.verdict,
        (left?1u:0u)|(right?2u:0u)|(route.route_up?4u:0u)|(route.available?8u:0u),input_ms);
    return deck_outcome(&outcome,left,right,route.available,key.code);
}

static int physical_key(void *self, const void *input) {
    struct xz_ds_key contact;
    int release=0;uint16_t code=0;unsigned op=0;
    if(input){memcpy(&code,(const unsigned char *)input+8,2);op=((const unsigned char *)input)[11]&15u;}
    if(xz_runtime_deck_early_key(code)&&(op==2||op==3)&&xz_ds_key_decode(self,input,&contact)){
     pthread_mutex_lock(&deck_mutex);release=owned_player_release(&contact);pthread_mutex_unlock(&deck_mutex);
    }
    if(!observer && !release && physical_priority(self,input)) return 1;
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
    xz_deck_input_trace_init(&xz_mods_deck_input_trace_v1,enabled("XZ_MODS_DECK_INPUT_TRACE"));
    xz_deck_input_codes_init(&xz_mods_deck_input_codes_v1,enabled("XZ_MODS_DECK_INPUT_TRACE"));
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
    /* Install the Jog reader before players/connector can publish snapshots. */
    if (xz_jog_display_runtime_start()) xz_log("DECK 3/4 Jog LCD bridge unavailable");
    if (xz_extra_decks_start()) xz_log("DECK 3/4 players unavailable");
    if (xz_sync_link_runtime_start()) xz_log("DECK 3/4 Link master bridge unavailable");
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
    if (enabled("XZ_MODS_UI")) {
      /* Root hash-verifies the exact FD9 payload before runtime construction. */
      const struct xz_net_linux_config network_config={
        .client_path="/dev/shm/xz-network-udhcpc",
        .event_path="/dev/shm/xz-network-lease-event",
        .client_contract_verified=1
      };
      if(xz_net_runtime_start(&network_config)!=XZ_NET_OK)
          xz_log("Network worker unavailable; NETWORK setup remains visible");
      xz_ui_runtime_start(audio_ready,key_ready,1);
    }
    if(xz_diary_start&&xz_diary_start())xz_log("USB diary worker unavailable");
    { extern int xz_classic_load_start(void); if (xz_classic_load_start()) xz_log("Classic LOAD into DECK 3/4 unavailable: guard or hook slot refused"); }
}

__attribute__((destructor)) static void stop(void) {
    if(xz_diary_stop)xz_diary_stop();
    /* No UI lock. Network owns all its state; keep NODELETE/state on failure,
       while continuing UI/engine teardown in the existing order. */
    if(xz_net_runtime_stop(8000)!=XZ_NET_OK)
        xz_log("Network stop incomplete; service-owned state retained");
    xz_jog_display_runtime_stop();
    xz_sync_link_runtime_stop();
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
