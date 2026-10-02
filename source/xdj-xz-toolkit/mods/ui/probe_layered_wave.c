/* On-device read-only probe: load each track's 3-band and stem waveform with the
 * production loader and report sizes and timing. Never touches rbp. */
#include "layered_wave.h"
#include "layered_wave_source.h"
#include <stdio.h>
#include <sys/time.h>
static long ms(void){struct timeval t;gettimeofday(&t,NULL);return t.tv_sec*1000L+t.tv_usec/1000;}
int main(int argc,char **argv){
 int failures=0;
 for(int i=1;i<argc;i++){
  struct xz_wave_load w;long t=ms();
  int a=xz_wave_load_three_band(argv[i],&w);long t3=ms()-t;
  printf("3band rc=%d samples=%u norm=%u ms=%ld %s\n",a,w.count,w.normalization,t3,argv[i]);
  uint32_t three=w.count;xz_wave_load_free(&w);
  t=ms();int b=xz_wave_load_stems(argv[i],&w);long ts=ms()-t;
  printf("stems rc=%d samples=%u norm=%u ms=%ld (3band samples %u)\n",b,w.count,w.normalization,ts,three);
  xz_wave_load_free(&w);failures+=a!=0;
 }
 return failures;
}
