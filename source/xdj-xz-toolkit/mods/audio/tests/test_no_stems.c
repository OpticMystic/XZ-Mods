/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define main existing_audio_runtime_test
#include "test_runtime.c"
#undef main
#include <unistd.h>

static const char *no_stem_path;
static int load_no_stems(void *reader,const void *track) {
    (void)track;
    assert((uintptr_t)reader==0x11200);
    assert(decks[0].generation==expect_generation);
    strcpy((char*)test_memory+0x1300,no_stem_path);
    return 0;
}

int main(void) {
    existing_audio_runtime_test();
    char root[]="/tmp/xz-no-stem-source-XXXXXX",path[1024];
    assert(mkdtemp(root));
    for(unsigned i=0;i<2;i++)for(int stems_on=0;stems_on<2;stems_on++) {
        assert(snprintf(path,sizeof(path),"%s/Contents/no-stems.%s",root,i?"wav":"mp3")<(int)sizeof(path));
        setup();memset(&decks[0],0,sizeof(decks[0]));running=1;enabled=stems_on;
        no_stem_path=path;original_load=load_no_stems;expect_generation=1;
        assert(!load_hook((void*)(uintptr_t)0x11200,NULL));
        assert(decks[0].history_valid&&!decks[0].published&&!strcmp(decks[0].pending.path,path));
        struct xz126_deck_source source=decks[0].pending;
        if(stems_on)prepare_deck(0,source,decks[0].generation);
        assert(!decks[0].published&&!decks[0].status.prepared);
        uint32_t rate=0,epoch=0;
        assert(xz_audio_output_deck((void*)(uintptr_t)source.manager,&rate,&epoch)==0);
        assert(rate==44100&&epoch==1);
        assert(decks[0].status.state==(stems_on?(i?XZ_AUDIO_CACHE_MISSING:XZ_AUDIO_SOURCE_UNSUPPORTED):XZ_AUDIO_DISABLED));
        printf("PASS %s without stems: stems=%d, no decoded PCM, key owner epoch=%u/rate=%u and source path retained\n",
               i?"WAV":"MP3",stems_on,epoch,rate);
    }
    running=enabled=0;assert(!rmdir(root));return 0;
}
