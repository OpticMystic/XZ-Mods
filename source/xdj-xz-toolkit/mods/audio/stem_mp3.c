/* SPDX-License-Identifier: MIT */
/* LLVM 21 cannot lower dr_mp3 with ARM softfp. Keep this decoder in a
 * software-float object, like stem_decode.c; Vorbis and mixing retain VFP. */
#define DR_MP3_IMPLEMENTATION
#include "vendor/dr_libs/dr_mp3.h"
