#ifndef XZ_UI_THEME_FLAGSHIP_H
#define XZ_UI_THEME_FLAGSHIP_H
/* Included by themes.h after the fill/edge/mix primitives.
   Flagship club-player look, measured from 1280x720 captures of the current
   flagship players (docs/research/2026-10-18-cdj3000-xdj-az-ui.md section 4):
   black page, #191919 chrome, #3c3c3c rails, #007de1 selection blue, #ff4619
   tempo/quantize red, white caps text with #a4a4a4 captions, three-band
   waveform (blue lows, amber mids, cream highs). */
#define XZ_THEME_FLAGSHIP_PALETTE {0x000000,0xffffff,0x007de1,0xff4619,{0x007de1,0xff4619,0x28e028}}
#define XZ_THEME_FLAGSHIP_NAME "FLAGSHIP"
#define XZ_FLAGSHIP_CHROME 0x191919
#define XZ_FLAGSHIP_RAIL 0x3c3c3c
#define XZ_FLAGSHIP_RAIL_SELECTED 0x646465
#define XZ_FLAGSHIP_ROW_ALT 0x282828
#define XZ_FLAGSHIP_TILE 0x313131
#define XZ_FLAGSHIP_DIM 0xa4a4a4
#define XZ_FLAGSHIP_BLUE 0x007de1
#define XZ_FLAGSHIP_DEEP_BLUE 0x095694
#define XZ_FLAGSHIP_RED 0xff4619
#define XZ_FLAGSHIP_GREEN 0x28e028
#define XZ_FLAGSHIP_LOOP_ORANGE 0xff8c1a
#define XZ_FLAGSHIP_WAVE_LOW 0x0055e1
#define XZ_FLAGSHIP_WAVE_LOW_DEEP 0x003fa8
#define XZ_FLAGSHIP_WAVE_MID 0xffa028
#define XZ_FLAGSHIP_WAVE_HIGH 0xf5ebd7
/* Flat frame: chrome fill, 1 px black separator, solid selection blue. */
static inline void xz_theme_flagship_frame(struct xz_theme_surface s,struct xz_theme_rect r,uint32_t fill,int selected,enum xz_theme_role role) {
 (void)fill;
 if(role==XZ_THEME_FOOTER){xz_theme_fill(s,r,XZ_FLAGSHIP_CHROME);xz_theme_fill(s,(struct xz_theme_rect){r.x,r.y,r.w,1},0);return;}
 if(role==XZ_THEME_LIST){xz_theme_fill(s,r,selected?XZ_FLAGSHIP_BLUE:XZ_FLAGSHIP_CHROME);return;}
 if(role==XZ_THEME_HEADER){xz_theme_fill(s,r,XZ_FLAGSHIP_CHROME);return;}
 xz_theme_fill(s,r,selected?XZ_FLAGSHIP_BLUE:XZ_FLAGSHIP_CHROME);
 xz_theme_edge(s,r,0,selected?XZ_FLAGSHIP_BLUE:0x000000);
}
/* Stock screen: greys become a neutral ramp to white; saturated hues snap to
   the flagship vocabulary (red for tempo and quantize marks, green for cue and
   key-match, blue for selection) so native status words read as that player draws them. */
static inline int xz_theme_flagship_tone(unsigned r,unsigned g,unsigned b,unsigned light,uint32_t *rgb) {
 unsigned mx=r>g?r:g,mn=r<g?r:g;if(b>mx)mx=b;if(b<mn)mn=b;
 if(mx>=64&&mx-mn>=48){
  uint32_t hue;
  if(r>=g&&r>=b)hue=g*2>=r&&g>b?XZ_FLAGSHIP_WAVE_MID:XZ_FLAGSHIP_RED;
  else if(g>=b)hue=b*4>=g*3?0x5ad8ff:XZ_FLAGSHIP_GREEN;
  else hue=r*4>=b*3?0xb48cff:XZ_FLAGSHIP_BLUE;
  *rgb=xz_theme_mix(0,hue,mx*256/255);return 1;
 }
 *rgb=light<8?0:xz_theme_mix(0,0xffffff,light*256/255);
 return 1;
}
#endif
