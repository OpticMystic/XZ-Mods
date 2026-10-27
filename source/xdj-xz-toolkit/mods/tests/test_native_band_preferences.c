/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Native band preference acceptance requires active assertions
#endif
#define main original_skin_fixture_main
#include "../ui/test_native_skin_runtime.c"
#undef main
int main(void){
 init_fixture();
 uint32_t standard[3]={0},custom[3]={0xff3030,0xc040ff,0x30ffff};
 unsigned generation=request_generation;
 xz_native_skin_band_colors(standard);assert(request_generation==generation);
 xz_native_skin_request(XZ_THEME_FLAGSHIP);
 struct skin_snapshot *old=prepare(XZ_THEME_FLAGSHIP,request_generation);assert(old&&old->wave_lut);
 assert(old->wave_lut[0x001f]==xz_theme_rgb565(XZ_FLAGSHIP_WAVE_LOW));
 generation=request_generation;xz_native_skin_band_colors(custom);assert(request_generation==generation+1);
 xz_native_skin_band_colors(custom);assert(request_generation==generation+1);
 assert(!publish_snapshot(old));free_snapshot(old);
 struct skin_snapshot *next=prepare(XZ_THEME_FLAGSHIP,request_generation);assert(next&&next->wave_lut);
 assert(next->wave_lut[0x001f]==xz_theme_rgb565(custom[0]));
 assert(next->wave_lut[0xf800]==xz_theme_rgb565(custom[1]));
 assert(next->wave_lut[0x07e0]==xz_theme_rgb565(custom[2]));
 assert(publish_snapshot(next));next=NULL;
 xz_native_skin_band_colors(standard);
 struct skin_snapshot *restored=prepare(XZ_THEME_FLAGSHIP,request_generation);assert(restored);
 for(unsigned p=0;p<65536;p++)assert(restored->wave_lut[p]==xz_flagship_wave_pixel((uint16_t)p));
 free_snapshot(restored);free_snapshot(pending_snapshot);pending_snapshot=NULL;free(original_pack);original_pack=NULL;
 puts("PASS native skin worker preparation: custom map, unchanged request skip, obsolete snapshot refusal, STANDARD map restore");
 return 0;
}
