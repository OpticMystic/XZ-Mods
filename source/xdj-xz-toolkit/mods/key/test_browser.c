/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Browser key acceptance requires active assertions
#endif
#include "browser.c"
#include <assert.h>
#include <stdlib.h>
static void ascii(const char *text,uint16_t out[256]){size_t i=0;for(;text[i];i++)out[i]=(unsigned char)text[i];out[i]=0;}
int main(int argc,char **argv){
    assert(argc==3);struct xz_key_catalog catalog={0},invalid={0};
    assert(!xz_key_catalog_load(argv[1],&catalog)&&catalog.count==6);
    assert(xz_key_catalog_load(argv[2],&invalid)==-1&&!invalid.entries);
    uint16_t title[256],out[264],prefix[8];uint32_t color;struct xz_music_key key;
    ascii("Night Drive",title);assert(xz_key_catalog_lookup(&catalog,title,11,&key)==1&&key.root==0&&!key.minor);
    ascii("Duplicate",title);assert(xz_key_catalog_lookup(&catalog,title,9,&key)==-1);
    ascii("Unknown",title);assert(xz_key_catalog_lookup(&catalog,title,7,&key)==-1);
    ascii("Deleted",title);assert(!xz_key_catalog_lookup(&catalog,title,7,&key));
    uint16_t unicode[]={0x65e5,0x672c,0xd83c,0xdfb5,0};assert(xz_key_catalog_lookup(&catalog,unicode,4,&key)==1&&key.root==9&&key.minor);
    catalogs[0]=catalog;memset(&catalog,0,sizeof(catalog));ascii("Night Drive",title);
    xz_key_browser_scope(0,0,0,-1);assert(!xz_key_browser_prefix(title,11,out,prefix,&color));
    xz_key_browser_scope(1,0,0,0);assert(!xz_key_browser_prefix(title,11,out,prefix,&color));
    xz_key_browser_scope(1,1,XZ_KEY_CAMELOT,0);
    struct xz_playing_key current={.key={0,0}};xz_key_browser_master(&current,XZ_KEY_CAMELOT);
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)==15&&out[0]=='8'&&out[1]=='B'&&out[2]==' '&&out[3]==' '&&out[4]=='N');
    assert(color==0x52d794&&prefix[0]=='8'&&prefix[1]=='B'&&!prefix[2]);
    current.cents=26;xz_key_browser_master(&current,XZ_KEY_CAMELOT);
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)&&color==0xffbd59);
    xz_key_browser_master(NULL,XZ_KEY_CAMELOT);assert(xz_key_browser_prefix(title,11,out,prefix,&color)&&color==0xa8b2bf);
    xz_key_browser_scope(1,1,XZ_KEY_CLASSIC,0);assert(xz_key_browser_prefix(title,11,out,prefix,&color)==14&&out[0]=='C'&&out[1]==' '&&out[3]=='N');
    assert(xz_key_browser_prefix(unicode,4,out,prefix,&color)==8&&out[0]=='A'&&out[1]=='m'&&out[4]==0x65e5);
    assert(xz_key_browser_repaint()&&!xz_key_browser_repaint());
    ascii("Duplicate",title);assert(!xz_key_browser_prefix(title,9,out,prefix,&color));
    ascii("Night Drive",title);xz_key_browser_scope(1,1,XZ_KEY_CAMELOT,1);
    assert(!xz_key_browser_prefix(title,11,out,prefix,&color));
    catalogs[1].entries=malloc(sizeof(*catalogs[1].entries));assert(catalogs[1].entries);catalogs[1].count=1;
    for(size_t i=0;i<catalogs[0].count;i++)if(catalogs[0].entries[i].id==1)catalogs[1].entries[0]=catalogs[0].entries[i];
    catalogs[1].entries[0].key=(struct xz_music_key){2,0};
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)&&out[0]=='1'&&out[1]=='0'&&out[2]=='B');
    xz_key_browser_scope(1,1,XZ_KEY_CAMELOT,0);assert(xz_key_browser_prefix(title,11,out,prefix,&color)&&out[0]=='8'&&out[1]=='B');
    /* The traffic matcher takes the deck's effective key: tempo transposes unless Master Tempo holds it. */
    struct xz_key_snapshot deck={.original={0,0},.tempo_ratio=1.0594630943592953,.loaded=1,.master=1};
    assert(xz_key_playing(&deck,0,&current)&&current.key.root==1);xz_key_browser_master(&current,XZ_KEY_CAMELOT);
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)&&color==0xa8b2bf);
    deck.master_tempo=1;assert(xz_key_playing(&deck,0,&current)&&current.key.root==0);xz_key_browser_master(&current,XZ_KEY_CAMELOT);
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)&&color==0x52d794);
    deck.master_tempo=0;assert(xz_key_playing(&deck,-100,&current)&&current.key.root==0&&!current.cents);xz_key_browser_master(&current,XZ_KEY_CAMELOT);
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)&&color==0x52d794);
    /* Browse columns: distinct values, duplicates swap. */
    int columns[3]={XZ_KEY_COLUMN_TITLE,XZ_KEY_COLUMN_KEY,XZ_KEY_COLUMN_BPM},bad[3]={0,0,3};
    assert(xz_key_browser_columns_valid(columns)&&!xz_key_browser_columns_valid(bad)&&!xz_key_browser_column_choose(bad,0,1));
    assert(xz_key_browser_column_choose(columns,0,XZ_KEY_COLUMN_KEY)&&columns[0]==XZ_KEY_COLUMN_KEY&&columns[1]==XZ_KEY_COLUMN_TITLE&&columns[2]==XZ_KEY_COLUMN_BPM);
    assert(xz_key_browser_column_choose(columns,2,XZ_KEY_COLUMN_ARTIST)&&columns[2]==XZ_KEY_COLUMN_ARTIST&&xz_key_browser_columns_valid(columns));
    assert(!xz_key_browser_column_choose(columns,3,0)&&!xz_key_browser_column_choose(columns,0,4));
    /* Three columns compose in slot order inside the title cell; a missing field is left out. */
    {
        uint16_t name[256],artist[256],row[264];size_t lead;char text[264];ascii("Strobe",name);ascii("Dead",artist);
        struct xz_key_browser_fields all={name,6,"8A",artist,12850};
        #define ROW(cols,first,f) do{size_t n_=xz_key_browser_compose(cols,first,f,row,&lead);for(size_t i_=0;i_<=n_;i_++)text[i_]=(char)row[i_];if(!n_)text[0]=0;}while(0)
        int tka[3]={XZ_KEY_COLUMN_TITLE,XZ_KEY_COLUMN_KEY,XZ_KEY_COLUMN_ARTIST};
        ROW(tka,0,&all);assert(!strcmp(text,"Strobe  8A  Dead")&&!lead);
        ROW(tka,1,&all);assert(!strcmp(text,"8A  Strobe  Dead")&&lead==2);
        int abt[3]={XZ_KEY_COLUMN_ARTIST,XZ_KEY_COLUMN_BPM,XZ_KEY_COLUMN_TITLE};
        ROW(abt,0,&all);assert(!strcmp(text,"Dead  128.5  Strobe")&&!lead);
        ROW(abt,1,&all);assert(!strcmp(text,"8A  Dead  128.5  Strobe")&&lead==2);
        int kbt[3]={XZ_KEY_COLUMN_KEY,XZ_KEY_COLUMN_BPM,XZ_KEY_COLUMN_TITLE};
        ROW(kbt,0,&all);assert(!strcmp(text,"8A  128.5  Strobe")&&lead==2);
        struct xz_key_browser_fields bare={name,6,NULL,NULL,0};
        ROW(tka,1,&bare);assert(!text[0]);
        struct xz_key_browser_fields partial={name,6,NULL,artist,0};
        ROW(abt,1,&partial);assert(!strcmp(text,"Dead  Strobe")&&!lead);
        assert(!xz_key_browser_compose(bad,1,&all,row,&lead));
        uint16_t surrogate[300];for(int i=0;i<299;i++)surrogate[i]='x';surrogate[260]=0xd83c;surrogate[261]=0xdfb5;
        struct xz_key_browser_fields big={surrogate,255,"10B",NULL,0};
        size_t n=xz_key_browser_compose(kbt,0,&big,row,&lead);assert(n<=263&&!row[n]&&!(row[n-1]>=0xd800&&row[n-1]<=0xdbff));
        #undef ROW
    }
    xz_key_browser_repaint();
    int plain[3]={XZ_KEY_COLUMN_TITLE,XZ_KEY_COLUMN_KEY,XZ_KEY_COLUMN_BPM};
    /* Key after the title: drawn, but uncoloured because its glyph origin is unknown. */
    xz_key_browser_configure(plain,0,1,0);assert(xz_key_browser_repaint());
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)==15&&out[0]=='N'&&out[13]=='8'&&out[14]=='B'&&!prefix[0]&&!color);
    xz_key_browser_configure(columns,0,1,0);assert(xz_key_browser_prefix(title,11,out,prefix,&color)==15&&out[0]=='8'&&prefix[0]=='8'&&color);
    xz_key_browser_configure(plain,1,0,1);assert(xz_key_browser_prefix(title,11,out,prefix,&color)==14&&out[0]=='C'&&!prefix[0]&&!color);
    xz_key_browser_configure(plain,1,1,2);xz_key_browser_scope(1,1,XZ_KEY_CLASSIC,0);
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)==15&&out[0]=='8'&&prefix[0]=='8'&&color==0x52d794);
    xz_key_browser_configure(bad,1,1,0);xz_key_browser_configure(NULL,1,1,0);
    assert(xz_key_browser_prefix(title,11,out,prefix,&color)==14&&out[0]=='C');
    /* A title-only row is the stock row: nothing to rewrite. */
    int title_only[3]={XZ_KEY_COLUMN_TITLE,XZ_KEY_COLUMN_ARTIST,XZ_KEY_COLUMN_BPM};
    xz_key_browser_configure(title_only,0,1,0);assert(!xz_key_browser_prefix(title,11,out,prefix,&color));
    xz_key_browser_configure(plain,1,1,0);
    /* Play key line: only text inside the stock key line records is extended. */
    assert(xz_key_line_deck(0x224e19c)==0&&xz_key_line_deck(0x224e19c+0x223)==0&&xz_key_line_deck(0x224e19c+0x224)==1);
    assert(xz_key_line_deck(0x224e19b)==-1&&xz_key_line_deck(0x224e19c+2*0x224)==-1&&xz_key_line_deck((uintptr_t)title)==-1);
    uint16_t line[96];ascii("Am",title);
    assert(!xz_key_line_adjacent(0,title,2,line));
    xz_key_line_publish(0,"> Bm +5c  KEY SYNC");
    assert(xz_key_line_adjacent(0,title,2,line)==21&&line[0]=='A'&&line[2]==' '&&line[3]=='>'&&line[20]=='C'&&!line[21]);
    assert(!xz_key_line_adjacent(1,title,2,line)&&!xz_key_line_adjacent(2,title,2,line)&&!xz_key_line_adjacent(0,title,0,line));
    xz_key_line_publish(0,"");assert(!xz_key_line_adjacent(0,title,2,line));
    assert(xz_mods_key_line_proof_v1.matched[0]==2&&xz_mods_key_line_proof_v1.matched[1]==1&&xz_mods_key_line_proof_v1.drawn[0]==1);
    xz_key_catalog_free(&catalogs[0]);xz_key_catalog_free(&catalogs[1]);
    puts("PASS real export.pdb pages, deleted rows, Unicode, unknown/duplicate keys, browser-only prefixes, harmonic tuning colors, effective master, three ordered browse columns with missing-field omission, leading-key colours and key line");return 0;
}
