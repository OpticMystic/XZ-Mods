/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Key music acceptance requires active assertions
#endif
#include "music.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void){
    for(int root=0;root<12;root++)for(int minor=0;minor<2;minor++)for(int format=0;format<2;format++){
        struct xz_music_key original={root,minor},parsed;char text[8];xz_music_key_format(original,format,text);
        assert(xz_music_key_parse(text,&parsed)&&parsed.root==root&&parsed.minor==minor);
    }
    struct xz_music_key key;assert(xz_music_key_parse("C#m",&key)&&key.root==1&&key.minor);
    assert(xz_music_key_parse("Cb",&key)&&key.root==11);assert(xz_music_key_parse("B#",&key)&&key.root==0);
    const char *invalid[]={"","--","13A","0B","8AB","Amx","H","C major","1","99B","A#minor"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(!xz_music_key_parse(invalid[i],&key));
    struct xz_key_snapshot source={.original={0,0},.tempo_ratio=exp2(1.23/12),.loaded=1};
    struct xz_playing_key playing,target={.key={2,0}};int desired;char label[48];
    assert(xz_key_playing(&source,0,&playing)&&playing.key.root==1&&playing.cents==23);
    assert(xz_key_sync_cents(&source,&target,&desired)&&desired==77);
    assert(xz_key_playing(&source,desired,&playing)&&playing.key.root==2&&!playing.cents);
    xz_key_readout(&source,desired,label,sizeof(label));assert(!strcmp(label,"C > D"));
    source.master_tempo=1;assert(xz_key_playing(&source,0,&playing)&&playing.key.root==0&&!playing.cents);
    assert(xz_key_sync_cents(&source,&target,&desired)&&desired==200);
    source.original.minor=1;assert(xz_key_sync_cents(&source,&target,&desired)&&desired==-100);
    assert(xz_key_playing(&source,desired,&playing)&&playing.key.root==11&&playing.key.minor);
    assert(xz_key_match(playing,target)==XZ_KEY_COMPATIBLE);
    playing.key=(struct xz_music_key){2,1};assert(xz_key_match(playing,target)==XZ_KEY_OTHER);
    for(int root=0;root<12;root++)for(int minor=0;minor<2;minor++)for(int other=0;other<12;other++)for(int other_minor=0;other_minor<2;other_minor++){
        source.original=(struct xz_music_key){root,minor};target.key=(struct xz_music_key){other,other_minor};target.cents=17;
        assert(xz_key_sync_cents(&source,&target,&desired)&&desired>=-600&&desired<=600);
        assert(xz_key_playing(&source,desired,&playing));assert(xz_key_match(playing,target)==XZ_KEY_COMPATIBLE);
        playing.cents+=26;assert(xz_key_match(playing,target)==XZ_KEY_DETUNED);
    }
    source.loaded=0;assert(!xz_key_playing(&source,0,&playing)&&!xz_key_sync_cents(&source,&target,&desired));
    source.loaded=1;source.tempo_ratio=NAN;assert(!xz_key_playing(&source,0,&playing));
    source.tempo_ratio=1;source.original.root=-1;assert(!xz_key_playing(&source,0,&playing));
    uint16_t title[]={0x65e5,0x672c,0xd83c,0xdfb5,0},out[12];
    assert(xz_key_prefix_utf16(out,12,title,4,(struct xz_music_key){9,1},XZ_KEY_CAMELOT)==7);
    assert(out[0]=='8'&&out[1]=='A'&&out[3]==0x65e5&&out[6]==0xdfb5&&!out[7]);
    assert(xz_key_prefix_utf16(out,7,title,4,(struct xz_music_key){9,1},XZ_KEY_CAMELOT)==5);
    assert(!out[5]);puts("PASS all 24 keys, both formats, 576 sync pairs, fractional tempo, Master Tempo, tuning and Unicode bounds");return 0;
}
