/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#ifdef NDEBUG
#error RAM acceptance requires active assertions
#endif
#include <assert.h>
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
#include <time.h>
#include <unistd.h>
static char test_path[128];
static int test_open(const char *path, int flags) { (void)path; return open(test_path, flags); }
#define open(path, flags) test_open(path, flags)
#include "runtime.c"
#undef open
#include "removal.c"
#include "presentation.c"

static unsigned char stream[2][0x338], player[2][0xf00], manager[0x400];
static FILE *files[2];
static unsigned stock_reads, stock_unloads, stock_emergencies, acknowledgements;
static int removed, pending_acks_seen;
static unsigned stock_mutes,stock_disconnects;
static uint32_t fixture_music[2][7];
static uint32_t fixture_max_time[2],native_total_ms[2]={125000,130000};
static uint32_t fake_max_time(unsigned d){return fixture_max_time[d];}
static unsigned fixture_selection[2];
static unsigned fake_source_select(unsigned d){return fixture_selection[d];}
static int retire_during_music_get;
static void fake_music_get(unsigned d,void *out){assert(d<2);memcpy(out,fixture_music[d],28);if(retire_during_music_get){retire_during_music_get=0;xz_ram_invalidate((int)d);}}
static unsigned fake_node_get(unsigned d){return ((unsigned char *)fixture_music[d])[0];}
static unsigned fake_device_get(unsigned d){return ((unsigned char *)fixture_music[d])[1];}
static unsigned fake_browse_get(unsigned d){return ((unsigned char *)fixture_music[d])[2];}
static void fake_mute(void *p,int value,unsigned kind,unsigned node){(void)p;(void)kind;(void)node;if(value)stock_mutes++;}
static void fake_disconnect(void *p,int value,unsigned kind,unsigned node){(void)p;(void)kind;(void)node;if(value)stock_disconnects++;}
static int fake_stop_key(void *m,const void *input){
    (void)m;(void)input;
    mute_hook((void *)(uintptr_t)0x11000,1,2,0);disconnect_hook((void *)(uintptr_t)0x11000,1,2,0);
    return 1;
}
static unsigned char native_music[2][8],native_wave_music[2][8],title_data[2][0x224],key_data[2][0x224];
static unsigned title_sets,wave_clears,ex_wave_clears,info_converts,ex_wave_writes;
static unsigned char native_info_rows[9*0x224];
static uint32_t native_staging[2];
static void fake_info_to_deck(unsigned deck){info_converts++;memset(title_data[deck],0,0x224);memset(key_data[deck],0,0x224);}
static uint32_t native_wave_valid[2]={1,1},native_rgb_valid[2]={1,1},native_cache_flags[2]={0,0};
static int fake_ex_wave_write(unsigned deck,const void *id,unsigned format){(void)id;(void)format;ex_wave_writes++;if(!native_staging[deck])native_wave_valid[deck]=0;return 1;}
static int native_bad_core;
static void fake_title_set(const void *rows,unsigned count,unsigned deck){
 assert(deck<2);title_sets++;
 memset(title_data[deck],count?*(const unsigned char *)rows:0,sizeof(title_data[deck]));
 memset(key_data[deck],count?*(const unsigned char *)rows:0,sizeof(key_data[deck]));
}
static void fake_wave_clear(unsigned deck,unsigned request){assert(deck<2);wave_clears++;if(request){native_wave_valid[deck]=0;native_rgb_valid[deck]=0;}}
static void fake_ex_wave_clear(unsigned deck,unsigned request){assert(deck<2);ex_wave_clears++;if(request){native_wave_valid[deck]=0;native_cache_flags[deck]=0;}}
static int native_memory(uint32_t a,void *out,size_t n){
 uint32_t value=0;int found=1;
 if(a==0x224e7f0&&n==sizeof(native_info_rows)){memcpy(out,native_info_rows,n);return 0;}
 if(n==4){
  if(a==0x1c8981c)value=0xc0000;else if(a==0xc0038)value=0xc1000;
  else if(a==0xc100c)value=0xd0000;else if(a==0xc1010)value=0xd1000;
  else if(a==0xd0008)value=native_total_ms[0];else if(a==0xd1008)value=native_total_ms[1];
  else if(a==0x1c8a308)value=0x90000;else if(a==0x90040)value=0x91000;else if(a==0x90048)value=2;
  else if(a==0x10e9688)value=0xa0000;else if(a==0xa000c)value=0xa1000;else if(a==0xa0010)value=0xa1008;
  else {found=0;for(unsigned d=0;d<2;d++){
   if(a==0x91000+d*4){value=0x11000+d*0x1000;found=1;}
   else if(a==0x11138+d*0x1000){value=0x51000+d*0x1000;found=1;}
   else if(a==0x1113c+d*0x1000){value=0x61000+d*0x1000;found=1;}
   else if(a==0x61004+d*0x1000){value=0x71000+d*0x1000;found=1;}
   else if(a==0xa1000+d*4){value=(uint32_t)(uintptr_t)player[d]+(native_bad_core?4:0);found=1;}
   else if(a==0x227080c+d*0x1e898){value=native_wave_valid[d];found=1;}
   else if(a==0x2205d20+d*0x114d8+0x10cf4||a==0x2205d20+d*0x114d8+0x10ce4){value=native_staging[d];found=1;}
   else if(a==0xb0010+d*0x1000||a==0xb0024+d*0x1000){value=native_wave_valid[d];found=1;}
   else if(a==0xb0b10+d*0x1000||a==0xb0e44+d*0x1000){value=0xf0000;found=1;}
   else if(a==0xb0b14+d*0x1000||a==0xb0e48+d*0x1000){value=1000;found=1;}
   else if(a==0xb0004+d*0x1000){value=native_cache_flags[d];found=1;}
  }}
  if(found){memcpy(out,&value,4);return 0;}
 }
 for(unsigned d=0;d<2;d++){
  if(n==1&&a==0x51026+d*0x1000){*(unsigned char *)out=d+1;return 0;}
  if(n==8&&a==0x71314+d*0x1000){memcpy(out,native_music[d],8);return 0;}
  if(n==8&&a==0xb002c+d*0x1000){memcpy(out,native_wave_music[d],8);return 0;}
  if(n==28&&a==0x2251f78+d*0x1e898){uint32_t head[7]={0};memcpy(head+1,native_wave_music[d],8);head[4]=0xb0000+d*0x1000;memcpy(out,head,28);return 0;}
  if(n==16&&a==0x2335048+d*0x394){uint32_t dto[4]={1,0,0,0};memcpy(dto+2,native_wave_music[d],8);memcpy(out,dto,16);return 0;}
  if(n==16&&a==0x2335770+d*0x718){uint32_t dto[4]={1,native_rgb_valid[d],0,0};memcpy(dto+2,native_wave_music[d],8);memcpy(out,dto,16);return 0;}
 }
 return -1;
}
static int block_stock, stock_entered;
static unsigned char fixture[262144];
static void put(void *object, unsigned offset, uint32_t value) { memcpy((unsigned char *)object + offset, &value, 4); }
static void seek_stream(int deck, int64_t position) { memcpy(stream[deck] + 0x320, &position, 8); }
static void *fake_create(void *impl, const void *track) { (void)track; return stream[((uintptr_t)impl - 0x30000) / 0x1000]; }
static int fake_read(void *self, void *out, int count)
{
    ++stock_reads;
    __atomic_store_n(&stock_entered, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&block_stock, __ATOMIC_ACQUIRE)) usleep(1000);
    if (removed || count <= 0) return 0;
    int index = self == stream[0] ? 0 : 1;
    int64_t position;
    memcpy(&position, (unsigned char *)self + 0x320, 8);
    assert(!fseek(files[index], (long)position, SEEK_SET));
    int read = (int)fread(out, 1, (size_t)count, files[index]);
    seek_stream(index, position + read);
    return read;
}
static void fake_unload(void *self, unsigned device, unsigned node) { (void)self; (void)device; (void)node; ++stock_unloads; }
static void fake_emergency(void *self, unsigned device, unsigned node) { (void)self; (void)device; (void)node; ++stock_emergencies; }
static void fake_complete(void *self, int index, unsigned kind)
{
    assert(scope == NULL && kind == 2 && removing(self));
    assert(((unsigned char *)self)[0x95 + index] == 2);
    ((unsigned char *)self)[0x95 + index] = 0;
    ++acknowledgements;
    /* A reentrant unrelated unload must not inherit the removal scope. */
    unload_hook((void *)(uintptr_t)0x1234, 2, 0);
}
static void fake_dispatch(void *self, const void *unused)
{
    (void)unused;
    unsigned before = acknowledgements;
    put(self, 0x88, 4);
    ((unsigned char *)self)[0x95] = ((unsigned char *)self)[0x96] = 1;
    ((unsigned char *)self)[0x95] = 2;
    unload_hook((void *)(uintptr_t)0x11000, 2, 0);
    assert(acknowledgements == before);
    ((unsigned char *)self)[0x96] = 2;
    emergency_hook((void *)(uintptr_t)0x12000, 0, 0);
    assert(acknowledgements == before);
    ++pending_acks_seen;
}
static void fake_stop_no_args(void *self) { fake_dispatch(self, NULL); }
static void fake_rec(void *self, unsigned message, const char *path) { (void)message; fake_dispatch(self, path); }
int xz_read_memory(uint32_t address, void *out, size_t bytes)
{
    if(!native_memory(address,out,bytes))return 0;
    if (address == 0x29f054 && bytes == 8) { memcpy(out, "\x38\x40\x2d\xe9\x00\x40\xa0\xe1", 8); return 0; }
    for (unsigned i = 0; i < 2; ++i) if (address == (uint32_t)(uintptr_t)player[i] + 0x44c && bytes == 2) {
        memcpy(out, player[i] + 0x44c, 2); return 0;
    }
    for(unsigned i=0;i<2;i++)if(address==(uint32_t)(uintptr_t)player[i]+0xe77&&bytes==1){
        memcpy(out,player[i]+0xe77,1);return 0;
    }
    return -1;
}
void xz_log(const char *message) { (void)message; }
int xz_hook_arm(uint32_t address, const unsigned char expected[8], void *replacement, void **original)
{
    assert(replacement && !memcmp(expected, xz_ram_hook_guard(address), 8));
    switch (address) {
    case 0xd1d3c:*original=(void *)fake_info_to_deck;break;
    case 0xa761c:*original=(void *)fake_ex_wave_write;break;
    case 0xa904c:*original=(void *)fake_max_time;break;
    case 0xd174c:*original=(void *)fake_title_set;break;
    case 0x13a470:*original=(void *)fake_wave_clear;break;
    case 0x13a7f4:*original=(void *)fake_ex_wave_clear;break;
    case 0x13951c:*original=(void *)fake_music_get;break;
    case 0x139bf0:*original=(void *)fake_node_get;break;
    case 0x139c0c:*original=(void *)fake_device_get;break;
    case 0x139c28:*original=(void *)fake_browse_get;break;
    case 0xa9374:*original=(void *)fake_source_select;break;
    case 0x29fd9c:*original=(void *)fake_stop_key;break;
    case 0x255210:*original=(void *)fake_mute;break;
    case 0x2555a8:*original=(void *)fake_disconnect;break;
    case 0x39840: *original = (void *)fake_create; break;
    case 0x326e8: *original = (void *)fake_read; break;
    case 0x254628: *original = (void *)fake_unload; break;
    case 0x2547f8: *original = (void *)fake_emergency; break;
    case 0x29c5a0: case 0x29d6f4: *original = (void *)fake_dispatch; break;
    case 0x29e8b4: *original = (void *)fake_stop_no_args; break;
    case 0x29e248: *original = (void *)fake_rec; break;
    default: assert(0);
    }
    return 0;
}
static void wait_state(int index, enum xz_ram_state expected)
{
    struct xz_ram_status status;
    for (unsigned retry = 0; retry < 10000; ++retry) {
        xz_ram_status(index, &status);
        if (status.state == expected) return;
        if (status.state == XZ_RAM_ERROR && expected != XZ_RAM_ERROR) {
            fprintf(stderr, "RAM failure: %s\n", status.message); assert(0);
        }
        usleep(1000);
    }
    fprintf(stderr, "State %d expected, got %d\n", expected, status.state); assert(0);
}
static void fixture_write(size_t bytes)
{
    FILE *file = fopen(test_path, "wb"); assert(file);
    assert(fwrite(fixture, 1, bytes, file) == bytes); assert(!fclose(file)); removed = 0;
}
static void *blocked_read(void *unused)
{
    (void)unused; unsigned char out[32];
    assert(read_hook(stream[0], out, sizeof(out)) == sizeof(out));
    return NULL;
}
static pthread_t start_blocked_read(void)
{
    pthread_t thread;
    __atomic_store_n(&stock_entered, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&block_stock, 1, __ATOMIC_RELEASE);
    assert(!pthread_create(&thread, NULL, blocked_read, NULL));
    while (!__atomic_load_n(&stock_entered, __ATOMIC_ACQUIRE)) usleep(1000);
    return thread;
}
static void wait_full_copy(void)
{
    struct xz_ram_status status;
    for (unsigned i = 0; i < 10000; ++i) {
        xz_ram_status(0, &status);
        pthread_mutex_lock(&mutex);
        int waiting = decks[0].data && decks[0].stock_users == 1;
        pthread_mutex_unlock(&mutex);
        if (waiting && status.bytes && status.copied == status.bytes) break;
        usleep(1000);
    }
    assert(status.state == XZ_RAM_LOADING && status.copied == 65536);
}
static void bind_deck(int index)
{
    if (files[index]) fclose(files[index]);
    files[index] = fopen(test_path, "rb"); assert(files[index]);
    memset(stream[index], 0, sizeof(stream[index]));
    put(stream[index], 0, 0x3b7bc8);
    strcpy((char *)stream[index] + 4, "/media/usb1/fixture.mp3");
    if (sizeof(void *) == 4) {
        memcpy(stream[index] + 0x31c, &files[index], sizeof(files[index]));
        unsigned char track_info[0x324] = {0}; track_info[0x319] = 2;
        memcpy(track_info+0x318,fixture_music[index],8);track_info[0x319]=2;
        assert(create_hook((void *)(uintptr_t)(0x30000 + index * 0x1000), track_info) == stream[index]);
    } else {
        struct binding capture = {.impl = 0x30000 + (unsigned)index * 0x1000, .stream = stream[index], .decoder_fd = fileno(files[index]), .device = 2};
        memcpy(capture.music_id,fixture_music[index],8);
        strcpy(capture.path, "/media/usb1/fixture.mp3"); assert(!fstat(fileno(files[index]), &capture.identity));
        pthread_mutex_lock(&mutex); captures[capture_next++ % 8] = capture; pthread_mutex_unlock(&mutex);
    }
    memcpy(native_music[index],fixture_music[index],8);memcpy(native_wave_music[index],fixture_music[index],8);
    /* Player UI metadata can lag the decoder's actual load request. */
    player[index][0x44c] = player[index][0x44d] = 0;
    xz_ram_invalidate(index);
    struct xz126_deck_source source = {.player = (uint32_t)(uintptr_t)player[index],
        .reader = 0x20000 + (unsigned)index * 0x1000, .reader_impl = 0x30000 + (unsigned)index * 0x1000};
    strcpy(source.path, "/media/usb1/fixture.mp3"); xz_ram_bind(index, &source);
    wait_state(index, XZ_RAM_EMPTY);
}
struct fault { int mode; uint32_t copied; };
static int fault_read(void *opaque, uint32_t offset, void *out, uint32_t count)
{
    struct fault *fault = opaque;
    if (fault->mode == 1 && offset >= 128) return -1;
    if (count > 31) count = 31;
    memcpy(out, fixture + offset, count);
    return (int)count;
}
static int fault_cancelled(void *opaque) { struct fault *fault = opaque; return fault->mode == 2 && fault->copied >= 128; }
static void fault_progress(void *opaque, uint32_t copied) { ((struct fault *)opaque)->copied = copied; }
int main(int argc, char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--display-assets")){
        snprintf(test_path,sizeof(test_path),"/tmp/xz-ram-display-assets-%ld",(long)getpid());
        for(unsigned d=0;d<2;d++){fixture_music[d][0]=2u<<8|1u<<16;fixture_music[d][1]=2+d;}
        assert(!xz_ram_start());fixture_write(65536);
        for(unsigned d=0;d<2;d++){bind_deck(d);assert(!xz_ram_request(d));wait_state(d,XZ_RAM_READY);}
        assert(!decks[0].ui_player&&!decks[1].ui_player);
        unsigned char early_row=0x5a;fake_title_set(&early_row,1,0);unsigned early_titles=title_sets;
        title_set_for_caller(NULL,0,0,0xf56a8);assert(title_sets==early_titles&&title_data[0][0]==0x5a);
        info_to_deck_for_caller(0,0xf5be0);assert(!info_converts&&title_data[0][0]==0x5a);
        assert(ex_wave_write_for_caller(0,native_music[0],1,0xa4404)==1&&!ex_wave_writes&&native_wave_valid[0]);
        title_set_for_caller(NULL,0,0,0);assert(title_sets==early_titles+1&&!title_data[0][0]);

        unsigned before=stock_emergencies;
        native_bad_core=1;emergency_for_caller((void *)0x11000,2,0,0xb0af0);native_bad_core=0;assert(stock_emergencies==++before);
        native_music[0][4]^=1;emergency_for_caller((void *)0x11000,2,0,0xb0af0);native_music[0][4]^=1;assert(stock_emergencies==++before);
        emergency_for_caller((void *)0x11000,2,1,0xb0af0);assert(stock_emergencies==++before);
        emergency_for_caller((void *)0x11000,3,0,0xb0af0);assert(stock_emergencies==++before);
        int fd=decks[0].source.decoder_fd;decks[0].source.decoder_fd=-1;emergency_for_caller((void *)0x11000,2,0,0xb0af0);decks[0].source.decoder_fd=fd;assert(stock_emergencies==++before);
        decks[0].music_valid=0;emergency_for_caller((void *)0x11000,2,0,0xb0af0);decks[0].music_valid=1;
        emergency_for_caller((void *)0x12000,2,0,0xb4c24);
        assert(stock_emergencies==before&&decks[0].status.state==XZ_RAM_DETACHED&&decks[1].status.state==XZ_RAM_DETACHED);
        for(unsigned d=0;d<2;d++){
            unsigned char row=0x5a;fake_title_set(&row,1,d);unsigned title_before=title_sets;
            memset(fixture_music[d],0,28);title_set_for_caller(NULL,0,d,0xf56a8);
            assert(title_sets==title_before&&title_data[d][0]==0x5a&&key_data[d][0]==0x5a);
            native_info_rows[0x20]=11;memcpy(native_info_rows+0x22,"N\0o\0t\0 \0L\0o\0a\0d\0e\0d\0.\0",22);
            unsigned converts=info_converts;info_to_deck_for_caller(d,0xf5be0);assert(info_converts==converts&&title_data[d][0]==0x5a);
            native_info_rows[8]=4;info_to_deck_for_caller(d,0xf5be0);assert(info_converts==converts+1&&!title_data[d][0]);native_info_rows[8]=0;
            fake_title_set(&row,1,d);
            unsigned writes=ex_wave_writes;
            for(unsigned format=0;format<2;format++)assert(ex_wave_write_for_caller(d,native_music[d],format,0xa4404)==1&&ex_wave_writes==writes&&native_wave_valid[d]);
            native_staging[d]=0xf1000;ex_wave_write_for_caller(d,native_music[d],1,0xa4404);assert(ex_wave_writes==writes+1);native_staging[d]=0;
            native_wave_valid[d]=0;ex_wave_write_for_caller(d,native_music[d],1,0xa4404);assert(ex_wave_writes==writes+2&&!native_wave_valid[d]);native_cache_flags[d]=0;native_wave_valid[d]=1;
            row=0x77;title_set_for_caller(&row,1,d,0xf56a8);assert(title_data[d][0]==0x77);
            assert(max_time_for_caller(d,0xec440)==native_total_ms[d]);
            assert(max_time_for_caller(d,0xb1fb4)==0);fixture_max_time[d]=UINT32_MAX;assert(max_time_for_caller(d,0xec440)==UINT32_MAX);fixture_max_time[d]=0;
            uint32_t duration=native_total_ms[d];native_total_ms[d]=UINT32_MAX;assert(!max_time_for_caller(d,0xec440));native_total_ms[d]=duration;
            unsigned wave_before=wave_clears;wave_clear_for_caller(d,1,0xb2c48);
            assert(wave_clears==wave_before&&native_wave_valid[d]&&native_rgb_valid[d]);
            unsigned ex_before=ex_wave_clears;ex_wave_clear_for_caller(d,1,0xa24c8);
            assert(ex_wave_clears==ex_before&&native_wave_valid[d]&&native_cache_flags[d]==0);
            native_wave_music[d][4]^=1;ex_wave_clear_for_caller(d,1,0xa24c8);native_wave_music[d][4]^=1;
            assert(ex_wave_clears==ex_before+1&&!native_wave_valid[d]);native_wave_valid[d]=1;native_cache_flags[d]=0;
            ex_wave_clear_for_caller(d,0,0xa24c8);assert(ex_wave_clears==ex_before+2);
            fixture_music[d][0]=2u<<8|1u<<16;fixture_music[d][1]=99;
            title_set_for_caller(NULL,0,d,0xf56a8);assert(!title_data[d][0]&&!key_data[d][0]);
            wave_clear_for_caller(d,1,0xb2c48);assert(!native_rgb_valid[d]);
            memset(fixture_music[d],0,28);
        }
        emergency_for_caller((void *)0x11000,2,0,0x256cc4);assert(stock_emergencies==before+1&&!decks[0].bound);
        unsigned char row=0x5a;fake_title_set(&row,1,1);retire_during_music_get=1;title_set_for_caller(NULL,0,1,0xf56a8);assert(!title_data[1][0]&&!decks[1].bound);
        xz_ram_stop();fclose(files[0]);fclose(files[1]);unlink(test_path);
        puts("PASS actual raw-eject bypass, native UI/Core/ID/RAM-fd gates, zero-row metadata, owned wave preservation and replacement/error passthrough");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--scalars")){
        snprintf(test_path,sizeof(test_path),"/tmp/xz-ram-scalars-%ld",(long)getpid());
        fixture_music[0][0]=2u<<8|1u<<16;fixture_music[0][1]=24;fixture_music[0][2]=43;
        assert(!xz_ram_start());fixture_write(65536);bind_deck(0);assert(!xz_ram_request(0));wait_state(0,XZ_RAM_READY);
        assert(xz_ram_preserve_player(0,2,0x11000));memset(fixture_music[0],0,28);
        uint32_t full[7];music_get_hook(0,full);assert(full[1]==24);
        if(device_get_hook(0)!=2||browse_get_hook(0)!=1){fprintf(stderr,"FAIL native display scalars say device=%u browse=%u while retained full identity is device2/browse1\n",device_get_hook(0),browse_get_hook(0));assert(0);}
        assert(node_get_hook(0)==0&&device_get_hook(1)==0);
        assert(source_select_for_caller(1,0xefef8)==0);
        /* SetPlayInfo derives the no-source mode5 from this independent getter. */
        for(unsigned i=0;i<4;i++){const uintptr_t cleanup[]={0xc2f48,0xc2f58,0xc36c4,0xc36d4};assert(source_select_for_caller(0,cleanup[i])==0);}
        assert(source_select_for_caller(0,0)==0);
        if(source_select_for_caller(0,0xefef8)!=3){fprintf(stderr,"FAIL retained full/scalar identity but native source selection0 still hides waveform/controls\n");assert(0);}
        unload_hook((void *)(uintptr_t)0x11000,3,0);assert(device_get_hook(0)==2);
        emergency_hook((void *)(uintptr_t)0x11000,2,1);assert(device_get_hook(0)==2);
        fixture_music[0][2]=999;music_get_hook(0,full);assert(full[1]==24&&full[2]==43);
        fixture_music[0][0]=3u<<8|4u<<16;fixture_music[0][1]=25;
        assert(device_get_hook(0)==3&&browse_get_hook(0)==4);
        assert(source_select_for_caller(0,0xefef8)==0);
        fixture_selection[0]=4;assert(source_select_for_caller(0,0xefef8)==4);
        fixture_selection[0]=0;memset(fixture_music[0],0,28);xz_ram_invalidate(0);assert(source_select_for_caller(0,0xefef8)==0);
        xz_ram_stop();fclose(files[0]);unlink(test_path);
        puts("PASS native display scalar/full identity coherence, other deck and replacement passthrough");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--presentation")){
        snprintf(test_path,sizeof(test_path),"/tmp/xz-ram-ui-%ld",(long)getpid());
        fixture_music[0][0]=2u<<8;fixture_music[0][1]=42;fixture_music[0][2]=77;
        uint32_t retained[7];memcpy(retained,fixture_music[0],28);
        assert(!xz_ram_start());fixture_write(65536);bind_deck(0);assert(!xz_ram_request(0));wait_state(0,XZ_RAM_READY);
        assert(xz_ram_ready_player(0,2,0x11000));memset(fixture_music[0],0,28);
        uint32_t transition[7];music_get_hook(0,transition);
        if(memcmp(transition,retained,28)){fprintf(stderr,"FAIL confirmed RAM deck lost native identity during USB STOP before phase-2 detach\n");assert(0);}
        assert(xz_ram_preserve_player(0,2,0x11000));memset(fixture_music[0],0,28);
        uint32_t shown[7];music_get_hook(0,shown);assert(!memcmp(shown,retained,28));
        fixture_music[0][0]=3u<<8;fixture_music[0][1]=43;music_get_hook(0,shown);assert(!memcmp(shown,fixture_music[0],28));
        memset(fixture_music[0],0,28);unload_hook((void *)(uintptr_t)0x11000,2,0);
        music_get_hook(0,shown);for(unsigned i=0;i<7;i++)assert(!shown[i]);
        xz_ram_stop();fclose(files[0]);unlink(test_path);
        puts("PASS detached native music identity, nonempty replacement and explicit unload invalidation");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--stop-key")){
        snprintf(test_path,sizeof(test_path),"/tmp/xz-ram-stop-%ld",(long)getpid());
        memset(fixture,0x5a,sizeof(fixture));assert(!xz_ram_start());fixture_write(65536);bind_deck(0);
        assert(!xz_ram_request(0));wait_state(0,XZ_RAM_READY);
        put(manager,0x88,2);put(manager,0x3b0,2);put(manager,0x3e0,0x11000);unsigned char input[12]={0};
        assert(stop_key_hook(manager,input)==1);
        if(stock_mutes||stock_disconnects){fprintf(stderr,"FAIL USB STOP forwarded early mute=%u disconnect=%u on READY deck\n",stock_mutes,stock_disconnects);assert(0);}
        mute_hook((void *)(uintptr_t)0x11000,1,2,0);assert(stock_mutes==1);
        xz_ram_stop();fclose(files[0]);unlink(test_path);
        puts("PASS USB STOP early retention, native return and ordinary mute passthrough");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--auto")){
        snprintf(test_path,sizeof(test_path),"/tmp/xz-ram-auto-%ld",(long)getpid());
        memset(fixture,0x5a,sizeof(fixture));assert(!xz_ram_start());fixture_write(65536);bind_deck(0);
        usleep(300000);struct xz_ram_status s;xz_ram_status(0,&s);assert(s.state==XZ_RAM_EMPTY);
        player[0][0xe77]=1;pthread_mutex_lock(&mutex);pthread_cond_signal(&changed);pthread_mutex_unlock(&mutex);
        wait_state(0,XZ_RAM_READY);assert(!xz_ram_release(0));wait_state(0,XZ_RAM_EMPTY);
        usleep(400000);xz_ram_status(0,&s);assert(s.state==XZ_RAM_EMPTY);
        xz_ram_stop();fclose(files[0]);unlink(test_path);
        puts("PASS automatic playing-track retention, paused deck exclusion and explicit release remains released");return 0;
    }
    if (argc > 1) assert(0);
    snprintf(test_path, sizeof(test_path), "/tmp/xz-ram-fixture-%ld", (long)getpid());
    for (size_t i = 0; i < sizeof(fixture); ++i) fixture[i] = (unsigned char)(i * 73 + i / 256);
    struct fault fault = {0}; unsigned char copied[512];
    struct xz_ram_copy copy = {&fault, fault_read, fault_cancelled, fault_progress};
    assert(!xz_ram_copy_all(copied, sizeof(copied), &copy));
    assert(!memcmp(copied, fixture, sizeof(copied)) && fault.copied == sizeof(copied));
    fault = (struct fault){1, 0}; assert(xz_ram_copy_all(copied, sizeof(copied), &copy) == -1);
    fault = (struct fault){2, 0}; assert(xz_ram_copy_all(copied, sizeof(copied), &copy) == -2);
    assert(xz_ram_budget(1, 0, XZ_RAM_RESERVE + 1));
    assert(!xz_ram_budget(0, 0, UINT64_MAX));
    assert(!xz_ram_budget(XZ_RAM_TRACK_LIMIT + 1ull, 0, UINT64_MAX));
    assert(!xz_ram_budget(1, XZ_RAM_TOTAL_LIMIT, UINT64_MAX));
    assert(!xz_ram_budget(2, 0, XZ_RAM_RESERVE + 1));
    assert(!xz_ram_start()); manager_complete = fake_complete;
    fixture_write(65536); bind_deck(0);
    pthread_t blocked = start_blocked_read();
    assert(!xz_ram_request(0)); wait_full_copy();
    __atomic_store_n(&block_stock, 0, __ATOMIC_RELEASE); assert(!pthread_join(blocked, NULL));
    wait_state(0, XZ_RAM_READY); stock_reads = 0;
    struct xz126_deck_source repeat = {.player = (uint32_t)(uintptr_t)player[0], .reader = 0x20000, .reader_impl = 0x30000};
    strcpy(repeat.path, "/media/usb1/fixture.mp3");
    xz_ram_bind(0, &repeat); wait_state(0, XZ_RAM_READY);
    struct stat source_identity, backing;
    assert(!stat(test_path, &source_identity) && !fstat(fileno(files[0]), &backing));
    assert(source_identity.st_dev != backing.st_dev || source_identity.st_ino != backing.st_ino);
    assert(fcntl(fileno(files[0]), F_GETFD) & FD_CLOEXEC);
    unsigned char output[4096];
    seek_stream(0, 12345); assert(read_hook(stream[0], output, sizeof(output)) == sizeof(output));
    assert(!memcmp(output, fixture + 12345, sizeof(output)) && stock_reads == 0);
    /* Returning to USB must seek to the virtual position, not the old FILE cursor. */
    assert(!xz_ram_release(0));
    for (unsigned i = 0; i < 10000; ++i) {
        pthread_mutex_lock(&mutex); int done = decks[0].data == NULL; pthread_mutex_unlock(&mutex);
        if (done) break; usleep(1000);
    }
    assert(read_hook(stream[0], output, 100) == 100);
    assert(!memcmp(output, fixture + 12345 + 4096, 100) && stock_reads == 1);
    assert(!fstat(fileno(files[0]), &backing) && same_file(&backing, &source_identity));
    assert(!(fcntl(fileno(files[0]), F_GETFD) & FD_CLOEXEC));
    /* Cancel while a previous USB read is still draining; the worker must wake
     * and retire its unpublished RAM file, then accept another request. */
    blocked = start_blocked_read(); assert(!xz_ram_request(0)); wait_full_copy();
    assert(!xz_ram_cancel(0)); wait_state(0, XZ_RAM_EMPTY);
    __atomic_store_n(&block_stock, 0, __ATOMIC_RELEASE); assert(!pthread_join(blocked, NULL));
    assert(!xz_ram_request(0)); wait_state(0, XZ_RAM_READY);
    bind_deck(1); assert(!xz_ram_request(1)); wait_state(1, XZ_RAM_READY);
    put(manager, 0x3b0, 2); put(manager, 0x3e0, 0x11000); put(manager, 0x3e4, 0x12000);
    unsigned before = stock_unloads;
    unload_hook((void *)(uintptr_t)0x11000, 2, 0); assert(stock_unloads == before + 1);
    remove_hook(manager, NULL); assert(acknowledgements == 2 && pending_acks_seen == 1);
    assert(manager[0x95] == 0 && manager[0x96] == 0);
    wait_state(0, XZ_RAM_DETACHED); wait_state(1, XZ_RAM_DETACHED);
    stop_hook(manager, NULL); stop_no_args_hook(manager); rec_complete_hook(manager, 0, NULL);
    assert(acknowledgements == 8 && pending_acks_seen == 4);
    struct removal_scope guarded = {manager, 0, NULL, 0}; scope = &guarded;
    put(manager, 0x88, 0); assert(!suppress((void *)(uintptr_t)0x11000, 2, 0));
    put(manager, 0x88, 4); manager[0x95] = 2;
    assert(!suppress((void *)(uintptr_t)0x11000, 3, 0));
    assert(!suppress((void *)(uintptr_t)0x11000, 2, 1));
    assert(!suppress((void *)(uintptr_t)0x13000, 2, 0));
    manager[0x95] = 1; assert(!suppress((void *)(uintptr_t)0x11000, 2, 0));
    scope = NULL;
    assert(!unlink(test_path)); removed = 1;
    before = stock_reads;
    for (unsigned i = 0; i < 100; ++i) {
        int64_t at = (int64_t)i * 509; seek_stream(0, at);
        assert(read_hook(stream[0], output, sizeof(output)) == sizeof(output));
        assert(!memcmp(output, fixture + at, sizeof(output)));
    }
    assert(stock_reads == before && xz_ram_release(0) == -1);
    seek_stream(0, 65500); assert(read_hook(stream[0], output, sizeof(output)) == 36);
    assert(read_hook(stream[0], output, sizeof(output)) == 0);
    seek_stream(0, -1); assert(read_hook(stream[0], output, 4) == 0);
    /* A new load immediately invalidates old bytes before the worker retires them. */
    xz_ram_invalidate(0); assert(read_hook(stream[0], output, sizeof(output)) == 0);
    assert(stock_reads == before + 1);
    fixture_write(sizeof(fixture)); bind_deck(0); assert(!xz_ram_request(0));
    struct xz_ram_status status;
    for (unsigned i = 0; i < 10000; ++i) { xz_ram_status(0, &status); if (status.copied) break; usleep(1000); }
    assert(status.state == XZ_RAM_LOADING && status.copied < sizeof(fixture));
    assert(!xz_ram_cancel(0)); wait_state(0, XZ_RAM_EMPTY); assert(!xz_ram_preserve_usb(0, 2));
    assert(!unlink(test_path)); assert(!xz_ram_request(0)); wait_state(0, XZ_RAM_ERROR);
    xz_ram_stop(); struct xz_ram_usage usage; xz_ram_usage(&usage);
    assert(!usage.original_pending && !usage.original_held);
    for (unsigned i = 0; i < 2; ++i) fclose(files[i]);
    puts("PASS actual RAM worker, compressed-byte identity, source deletion, seeks/EOF, two-deck deferred eject, explicit unload, replacement, cancel, failure and retirement");
    return 0;
}
