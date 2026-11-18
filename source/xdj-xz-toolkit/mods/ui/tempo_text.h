#ifndef XZ_TEMPO_TEXT_H
#define XZ_TEMPO_TEXT_H
/* The tempo (playing speed change) as the unit shows it, for every screen that draws one (XZ manual p.42):
   two decimals in the 6, 10 and 16 % ranges (0.02 and 0.05 % steps), one decimal in WIDE (0.5 % steps, up to
   100 %). So the widest value is six glyphs, "+16.00" or "-100.0", and -100 % reads -100.0 instead of a
   clamped -99.99. A value at or past 100 % with no known range takes the WIDE form; past 100 % it clamps.
   Zero has no sign. No % sign: each screen sets it in its own face. */
#include <stdint.h>
#include <stdio.h>
static inline void xz_tempo_text(char *out, size_t cap, int32_t x100, int range_known, int range) {
 int32_t a=x100<0?(x100<-10000?10000:-x100):(x100>10000?10000:x100);
 const char *sign=x100>0?"+":x100<0?"-":"";
 if((range_known&&range>=100)||a>=10000){
  int32_t tenths=(a+5)/10;
  snprintf(out,cap,"%s%d.%d",tenths?sign:"",(int)(tenths/10),(int)(tenths%10));
 }else snprintf(out,cap,"%s%d.%02d",sign,(int)(a/100),(int)(a%100));
}
#endif
