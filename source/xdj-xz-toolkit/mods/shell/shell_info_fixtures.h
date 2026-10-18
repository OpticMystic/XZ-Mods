#ifndef XZ_SHELL_INFO_FIXTURES_H
#define XZ_SHELL_INFO_FIXTURES_H
/* Example INFO and SOURCE models for the host tests and previews. Never device data. */
#include "fixtures.h"
#include "shell_info.h"
#include "shell_source.h"

#define FXI_ART_SIDE 64
static uint16_t fxi_rgb565(unsigned r, unsigned g, unsigned b) { return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)); }
/* An abstract sleeve: a dusk gradient, a sun disc and a horizon band. */
static void fxi_artwork(uint16_t art[FXI_ART_SIDE*FXI_ART_SIDE]) {
 for(int y=0;y<FXI_ART_SIDE;y++)for(int x=0;x<FXI_ART_SIDE;x++){
  unsigned r=40+y*3,g=20+y,b=120-y;
  int dx=x-40,dy=y-30;
  if(dx*dx+dy*dy<14*14){r=255;g=150+(unsigned)(30-y)*2;b=60;}
  if(y>44){r=10+(unsigned)(y-44);g=12;b=30+(unsigned)(x%8==0?20:0);}
  art[y*FXI_ART_SIDE+x]=fxi_rgb565(r>255?255:r,g>255?255:g,b>255?255:b);
 }
}
static uint64_t fxi_all(void) { return (XZ_SHELL_KNOWN(XZ_SHELL_INFO_FIELD_COUNT)-1); }
static uint64_t fxi_links(void) {
 return XZ_SHELL_KNOWN(XZ_SHELL_I_ARTIST)|XZ_SHELL_KNOWN(XZ_SHELL_I_ALBUM)|XZ_SHELL_KNOWN(XZ_SHELL_I_GENRE)|
  XZ_SHELL_KNOWN(XZ_SHELL_I_KEY)|XZ_SHELL_KNOWN(XZ_SHELL_I_BPM);
}
static void fxi_info_full(struct xz_shell_info_model *im, const uint16_t *art) {
 memset(im,0,sizeof *im);
 struct xz_shell_info_track *a=&im->deck[0],*b=&im->deck[1],*h=&im->highlight;
 a->known=fxi_all();a->links=fxi_links();a->loaded=1;a->track_id=1;
 a->title="Alone - Mixed";a->artist="Example Artist";a->album="Night Shift EP";a->genre="Melodic Techno";
 a->label="Example Records";a->key="Fm";a->comment="Peak time. Long intro, drop at 1:00.";a->date_added="2026-09-14";
 a->file_name="01 Alone - Mixed.mp3";a->artist_id=11;a->album_id=21;a->genre_id=31;a->key_id=41;
 a->bpm_x100=12800;a->duration_ms=FX_TOTAL_MS;a->bitrate_kbps=320;a->rating=4;a->color=XZ_SHELL_COLOR_AQUA;
 a->source=XZ_SHELL_SRC_USB1;a->artwork=art;a->artwork_side=FXI_ART_SIDE;

 b->known=fxi_all()&~XZ_SHELL_KNOWN(XZ_SHELL_I_ARTWORK)&~XZ_SHELL_KNOWN(XZ_SHELL_I_LABEL);b->links=fxi_links();
 b->loaded=1;b->track_id=12;
 b->title="Night Drive (feat. Example Vocalist) [Club Remix] - Extended Mix";b->artist="Another Artist";
 b->album="Night Drive (Remixes)";b->genre="Techno";b->key="Am";b->comment="";b->date_added="2026-10-02";
 b->file_name="Another Artist - Night Drive (feat. Example Vocalist) [Club Remix] - Extended Mix.wav";
 b->artist_id=12;b->album_id=22;b->genre_id=32;b->key_id=42;b->bpm_x100=12800;b->duration_ms=FX_TOTAL_MS;
 b->bitrate_kbps=1411;b->rating=0;b->color=XZ_SHELL_COLOR_NONE;b->source=XZ_SHELL_SRC_USB1;

 h->known=fxi_all()&~XZ_SHELL_KNOWN(XZ_SHELL_I_ARTWORK);h->links=fxi_links();h->loaded=1;h->track_id=77;
 h->title="Sunrise Over Example Bay";h->artist="Example Artist";h->album="Coastline";h->genre="Progressive House";
 h->label="Harbour Audio";h->key="8A";h->comment="Warm-up opener";h->date_added="2025-06-30";
 h->file_name="sunrise_over_example_bay.aiff";h->artist_id=11;h->album_id=23;h->genre_id=33;h->key_id=43;
 h->bpm_x100=12200;h->duration_ms=402000;h->bitrate_kbps=1411;h->rating=5;h->color=XZ_SHELL_COLOR_ORANGE;
 h->source=XZ_SHELL_SRC_USB1;
}
static void fxi_source_full(struct xz_shell_source_model *sm) {
 memset(sm,0,sizeof *sm);
 struct xz_shell_device *u1=&sm->device[0],*u2=&sm->device[1],*pc=&sm->device[3];
 u1->known=(1u<<XZ_SHELL_DEVICE_FIELD_COUNT)-1;u1->status=XZ_SHELL_DEVICE_READY;u1->name="XZ SET 2026";
 u1->color=0xff8c1a;u1->free_bytes=44345298944ull;u1->total_bytes=63999836160ull;u1->tracks=1284;
 u2->known=(1u<<XZ_SHELL_D_STATUS)|(1u<<XZ_SHELL_D_NAME)|(1u<<XZ_SHELL_D_FREE)|(1u<<XZ_SHELL_D_TOTAL);
 u2->status=XZ_SHELL_DEVICE_LOADING;u2->name="BACKUP";u2->free_bytes=3900000000ull;u2->total_bytes=31000000000ull;
 sm->device[2].known=1u<<XZ_SHELL_D_STATUS;sm->device[2].status=XZ_SHELL_DEVICE_NOT_CONNECTED;
 pc->known=(1u<<XZ_SHELL_D_STATUS)|(1u<<XZ_SHELL_D_NAME)|(1u<<XZ_SHELL_D_TRACKS)|(1u<<XZ_SHELL_D_COLOR);
 pc->status=XZ_SHELL_DEVICE_READY;pc->name="STUDIO-PC";pc->tracks=8421;pc->color=0x3c64ff;
 sm->current_known=1;sm->current=XZ_SHELL_SRC_USB1;
}
#endif
