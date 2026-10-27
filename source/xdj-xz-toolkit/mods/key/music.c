/* SPDX-License-Identifier: MIT */
#include "music.h"
#include "notation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int minor_roots[12]={8,3,10,5,0,7,2,9,4,11,6,1};
static const int major_roots[12]={11,6,1,8,3,10,5,0,7,2,9,4};
static int wrap(int n){n%=12;return n<0?n+12:n;}
static int valid(struct xz_music_key k){return k.root>=0&&k.root<12&&(unsigned)k.minor<=1;}
static int wheel(struct xz_music_key k){
    const int *roots=k.minor?minor_roots:major_roots;
    for(int i=0;i<12;i++)if(roots[i]==k.root)return i;
    return -1;
}
int xz_music_key_parse(const char *text,struct xz_music_key *out){
    if(!text||!out)return 0;
    size_t n=strlen(text);if(!n||n>7)return 0;
    struct xz_music_key k={-1,0};
    if(text[0]>='1'&&text[0]<='9'){
        char *end;long number=strtol(text,&end,10);
        if(number<1||number>12||(end[0]!='A'&&end[0]!='B')||end[1])return 0;
        k.minor=end[0]=='A';
        if(k.minor)k.root=minor_roots[number-1];else k.root=major_roots[number-1];
    }else{
        static const int notes[7]={9,11,0,2,4,5,7};
        if(text[0]<'A'||text[0]>'G')return 0;
        k.root=notes[text[0]-'A'];size_t i=1;
        if(text[i]=='#'){k.root=wrap(k.root+1);i++;}
        else if(text[i]=='b'){k.root=wrap(k.root-1);i++;}
        if(text[i]=='m'){k.minor=1;i++;}
        if(text[i])return 0;
    }
    *out=k;return 1;
}
void xz_music_key_format(struct xz_music_key key,int format,char out[8]){
    xz_key_notation_format(key.root,key.minor,format==XZ_KEY_CAMELOT,out);
}
int xz_key_playing(const struct xz_key_snapshot *snapshot,int shift_cents,struct xz_playing_key *out){
    if(!snapshot||!out||!snapshot->loaded||!valid(snapshot->original)||
        !isfinite(snapshot->tempo_ratio)||snapshot->tempo_ratio<=0||
        shift_cents < -1200||shift_cents>1200)return 0;
    double total=shift_cents+(snapshot->master_tempo?0:1200*log2(snapshot->tempo_ratio));
    int nearest=(int)round(total/100);
    *out=(struct xz_playing_key){.key={wrap(snapshot->original.root+nearest),snapshot->original.minor},
        .cents=(int)round(total-nearest*100),.shift_cents=(int)round(total)};
    return 1;
}
int xz_key_sync_cents(const struct xz_key_snapshot *source,const struct xz_playing_key *master,int *out){
    struct xz_playing_key current;
    if(!master||!out||!valid(master->key)||!xz_key_playing(source,0,&current))return 0;
    int target=master->key.root;
    if(source->original.minor!=master->key.minor)target=wrap(target+(source->original.minor?9:3));
    double wanted=(target-source->original.root)*100+master->cents-current.shift_cents;
    wanted=fmod(wanted,1200);if(wanted>600)wanted-=1200;if(wanted < -600)wanted+=1200;
    *out=(int)round(wanted);return 1;
}
enum xz_key_match xz_key_match(struct xz_playing_key candidate,struct xz_playing_key master){
    if(!valid(candidate.key)||!valid(master.key))return XZ_KEY_UNKNOWN;
    int distance=wrap(wheel(candidate.key)-wheel(master.key));
    int compatible=candidate.key.minor==master.key.minor?(distance==0||distance==1||distance==11):distance==0;
    if(!compatible)return XZ_KEY_OTHER;
    return abs(candidate.cents-master.cents)<=25?XZ_KEY_COMPATIBLE:XZ_KEY_DETUNED;
}
void xz_key_readout(const struct xz_key_snapshot *snapshot,int shift_cents,char *out,size_t size){
    struct xz_playing_key playing;char original[8],now[8];
    if(!out||!size)return;
    if(!xz_key_playing(snapshot,shift_cents,&playing)){snprintf(out,size,"KEY --");return;}
    xz_music_key_format(snapshot->original,snapshot->format,original);
    xz_music_key_format(playing.key,snapshot->format,now);
    if(playing.cents)snprintf(out,size,"%s > %s %+dc",original,now,playing.cents);
    else snprintf(out,size,"%s > %s",original,now);
}
size_t xz_key_prefix_utf16(uint16_t *out,size_t capacity,const uint16_t *title,size_t length,
    struct xz_music_key key,int format){
    if(!out||!capacity||!title)return 0;
    char prefix[8];xz_music_key_format(key,format,prefix);
    size_t n=strlen(prefix),used=0;
    for(size_t i=0;i<n&&used+1<capacity;i++)out[used++]=(unsigned char)prefix[i];
    if(used+1<capacity)out[used++]=' ';
    for(size_t i=0;i<length&&title[i]&&used+1<capacity;i++)out[used++]=title[i];
    /* Do not leave a truncated high surrogate at the end. */
    if(used&&out[used-1]>=0xd800&&out[used-1]<=0xdbff)used--;
    out[used]=0;return used;
}
