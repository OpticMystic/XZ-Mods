#ifndef XZ_SHELL_BROWSE_FIXTURES_H
#define XZ_SHELL_BROWSE_FIXTURES_H
/* Example library for the browse host test and previews: a title-sorted
   track list with generated previews, and a playlist folder level. Never
   device data. */
#include "fixtures.h"
#include "shell_browse.h"
#include <stdio.h>
#include <stdlib.h>

#define BX_TRACKS 148
#define BX_PREVIEW 64
#define BX_FOLDERS 9

struct bx_library {
 char title[BX_TRACKS][72], artist[BX_TRACKS][40];
 struct xz_shell_column preview[BX_TRACKS][BX_PREVIEW];
 struct xz_shell_browse_row tracks[BX_TRACKS];
 struct xz_shell_browse_row folders[BX_FOLDERS];
 struct xz_shell_browse_letter letters[26];
 int letter_count;
};

static const char *const bx_first[]={"Alone","Bassline","Cascade","Daylight","Echoes","Firewall","Gravity","Horizon","Inside",
 "Jupiter","Kinetic","Lights","Midnight","Neon","Orbit","Pressure","Quartz","Rhythm","Signal","Tides","Undertow","Velvet",
 "Wavelength","Xenon","Yearning","Zero"};
static const char *const bx_second[]={"Drive","Theory","Point","Machine","Garden","State","Dreams","System","Fever","Motion","Ritual","Rise"};
static const char *const bx_suffix[]={""," (Original Mix)"," (Extended Mix)"," (Club Dub)"," - Edit"," (feat. Example Vocalist) [Club Remix]"};
static const char *const bx_artists[]={"Example Artist","Another Artist","Sample Collective","Placeholder Duo","Test Pattern",
 "Fixture & Friends","Mock Unit","Demo Signal"};
static const char *const bx_keys[]={"Am","Fm","Gm","Cm","Dm","Em","Bbm","Abm","G","C","F#m","Ebm"};
static const uint32_t bx_colors[]={0,0xff3c78,0,0xff8c1a,0,0x28e028,0x20c8ff,0,0x9b3cff,0};
static const char *const bx_playlists[BX_FOLDERS]={"Peak Time","Warm Up","Closing","Tools & Loops","Edits","Vocals","Acid",
 "Halloween Party 2026","Back To Back With A Very Long Playlist Name For Truncation"};

static int bx_compare(const void *a, const void *b) { return strcmp((const char *)a,(const char *)b); }
static void bx_preview(struct xz_shell_column *p, uint32_t seed) {
 uint32_t state=seed*2654435761u+17u;
 int drop=20+(int)(fx_noise(&state)%16),brk=40+(int)(fx_noise(&state)%10);
 for(int i=0;i<BX_PREVIEW;i++){
  int lows=i<drop?40+i*4:i<brk?230:i<brk+6?20:220,mids=i<drop?90+i*3:i<brk?200:150,highs=i<drop?120:i<brk?210:170;
  if(i>=BX_PREVIEW-6){lows=lows*(BX_PREVIEW-i)/7;mids=mids*(BX_PREVIEW-i)/7;}
  int jitter=200+(int)(fx_noise(&state)%56);
  p[i].low=(uint8_t)(lows*jitter/256>255?255:lows*jitter/256);
  p[i].mid=(uint8_t)(mids*jitter/256>255?255:mids*jitter/256);
  p[i].high=(uint8_t)(highs*jitter/256>255?255:highs*jitter/256);
 }
}
static void bx_build(struct bx_library *L) {
 memset(L,0,sizeof *L);
 for(int i=0;i<BX_TRACKS;i++)
  snprintf(L->title[i],sizeof L->title[i],"%s %s%s",bx_first[i%26],bx_second[(i*7)%12],bx_suffix[(i*5)%6]);
 qsort(L->title,BX_TRACKS,sizeof L->title[0],bx_compare);
 char last=0;
 for(int i=0;i<BX_TRACKS;i++){
  struct xz_shell_browse_row *r=&L->tracks[i];
  snprintf(L->artist[i],sizeof L->artist[i],"%s",bx_artists[(i*3)%8]);
  bx_preview(L->preview[i],(uint32_t)i);
  const char *key=bx_keys[(i*5)%12];
  r->known=(1u<<XZ_SHELL_RF_COUNT)-1;
  r->kind=XZ_SHELL_ROW_TRACK;r->id=1000u+(uint32_t)i;
  r->title=L->title[i];r->artist=L->artist[i];r->key=key;
  r->key_matches_master=!strcmp(key,"Fm")||!strcmp(key,"Cm")||!strcmp(key,"Bbm")||!strcmp(key,"Abm");
  r->bpm_x100=12000+(i*37)%1450;r->rating=i%6;r->color=bx_colors[i%10];
  r->duration_ms=300000+(i*7919)%150000;r->played=i%5==1;
  r->preview=L->preview[i];r->preview_count=BX_PREVIEW;
  r->on_deck=(uint8_t)(i==3?1:i==7?2:0);
  if(L->title[i][0]!=last){last=L->title[i][0];L->letters[L->letter_count++]=(struct xz_shell_browse_letter){last,i};}
 }
 for(int i=0;i<BX_FOLDERS;i++){
  struct xz_shell_browse_row *r=&L->folders[i];
  r->known=1u<<XZ_SHELL_RF_TITLE;r->kind=XZ_SHELL_ROW_FOLDER;r->id=(uint32_t)(i+1);r->title=bx_playlists[i];
 }
}
static void bx_window(struct xz_shell_browse_model *b, const struct bx_library *L, int32_t start) {
 if(start<0)start=0;
 if(start>BX_TRACKS)start=BX_TRACKS;
 b->window_start=start;
 b->window_count=BX_TRACKS-start<XZ_SHELL_BROWSE_WINDOW?BX_TRACKS-start:XZ_SHELL_BROWSE_WINDOW;
 b->rows=&L->tracks[start];
}
static uint32_t bx_all_categories(void) { return (1u<<XZ_SHELL_CAT_COUNT)-1; }
/* A track list in the given screen: the TRACK category on BROWSE, a playlist elsewhere. */
static void bx_tracks(struct xz_shell_browse_model *b, const struct bx_library *L, enum xz_shell_screen s) {
 memset(b,0,sizeof *b);
 b->known=(1u<<XZ_SHELL_BF_COUNT)-1;
 b->screen=s;
 b->source_name="USB1";b->source_color=0x8c7a00;
 b->category_known=bx_all_categories();
 b->category_available=bx_all_categories()&~(1u<<XZ_SHELL_CAT_HISTORY)&~(1u<<XZ_SHELL_CAT_MATCHING);
 b->category=s==XZ_SHELL_BROWSE?XZ_SHELL_CAT_TRACK:XZ_SHELL_CAT_PLAYLIST;
 b->level_name=s==XZ_SHELL_BROWSE?"TRACK":s==XZ_SHELL_TAG_LIST?"TAG LIST":"Peak Time";
 b->depth=s==XZ_SHELL_PLAYLIST?1:0;
 b->parent_name=s==XZ_SHELL_PLAYLIST?"PLAYLIST":NULL;
 if(!b->parent_name)b->known&=~(1u<<XZ_SHELL_BF_PARENT);
 b->list_id=(uint32_t)s*100u+7u;
 b->total=BX_TRACKS;
 bx_window(b,L,0);
 b->columns[0]=XZ_SHELL_COL_NUMBER;b->columns[1]=XZ_SHELL_COL_TITLE;b->columns[2]=XZ_SHELL_COL_BPM;b->columns[3]=XZ_SHELL_COL_KEY;
 b->column_count=4;
 b->sort_column=XZ_SHELL_COL_TITLE;b->sort_descending=0;
 b->letters=L->letters;b->letter_count=L->letter_count;
}
/* The playlist tree's top level: folders only. */
static void bx_folders(struct xz_shell_browse_model *b, const struct bx_library *L, enum xz_shell_screen s) {
 bx_tracks(b,L,s);
 b->category=XZ_SHELL_CAT_PLAYLIST;b->level_name="PLAYLIST";b->depth=0;b->parent_name=NULL;
 b->known&=~(1u<<XZ_SHELL_BF_PARENT);
 b->list_id=(uint32_t)s*100u+1u;
 b->total=BX_FOLDERS;b->window_start=0;b->window_count=BX_FOLDERS;b->rows=L->folders;
 b->columns[0]=XZ_SHELL_COL_TITLE;b->column_count=1;
 b->sort_column=XZ_SHELL_COL_TITLE;
 b->letters=NULL;b->letter_count=0;
}
#endif
