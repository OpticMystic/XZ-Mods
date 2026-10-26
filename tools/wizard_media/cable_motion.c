/* The loopback-cable explainer for the desktop guide, drawn by the deck's own renderer
   (source/xdj-xz-toolkit/mods/ui/cable_anim.c) in the app's colours: every 60 fps frame of the loop as a PPM.
   Built and run by tools/build_cable_media.py.   cable_motion OUTDIR WIDTH HEIGHT */
#include "cable_anim.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
 if (argc != 4) { fprintf(stderr, "usage: cable_motion OUTDIR WIDTH HEIGHT\n"); return 2; }
 int w = atoi(argv[2]), h = atoi(argv[3]);
 if (w <= 0 || h <= 0) return 2;
 /* Greys on the RGB565 grid (multiples of 8) so the page's background matches the video exactly; the app's
    yellow as the accent; USB 2 softer, as on the deck. */
 struct xz_cable_palette pal = {0x080808, 0x181818, 0x000000, 0xa0a0a0, 0xf3e900, 0xf3e900, 0xf3e900, 0xf3e900, 0xf3e900, 0x585858, 0xc8c8c8, 0xffffff, 0};
 struct xz_cable_look look; xz_cable_look_of(&look, &pal, 1, 0);
 uint16_t *px = calloc((size_t)w * h, 2); uint8_t *cov = calloc((size_t)w * h, 1); unsigned char *rgb = malloc((size_t)w * h * 3);
 if (!px || !cov || !rgb) return 1;
 struct xz_cable_picture pic = {px, cov, w, h};
 int frames = (int)(xz_cable_loop_ms() * 60u / 1000u);
 for (int f = 0; f < frames; f++) {
  xz_cable_draw(&pic, (uint32_t)((uint64_t)f * 1000u / 60u), &look, XZ_CABLE_SIGNAL_LOOP, 0);
  for (int i = 0; i < w * h; i++) {
   uint16_t v = px[i];
   rgb[i * 3] = (unsigned char)(((v >> 11) & 31) * 255 / 31);
   rgb[i * 3 + 1] = (unsigned char)(((v >> 5) & 63) * 255 / 63);
   rgb[i * 3 + 2] = (unsigned char)((v & 31) * 255 / 31);
  }
  char path[1024]; snprintf(path, sizeof path, "%s/f%04d.ppm", argv[1], f);
  FILE *out = fopen(path, "wb"); if (!out) { perror(path); return 1; }
  fprintf(out, "P6\n%d %d\n255\n", w, h); fwrite(rgb, 1, (size_t)w * h * 3, out); fclose(out);
 }
 printf("%d frames %dx%d\n", frames, w, h);
 return 0;
}
