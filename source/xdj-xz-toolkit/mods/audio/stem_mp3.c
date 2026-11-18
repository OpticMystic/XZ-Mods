/* SPDX-License-Identifier: MIT */
/* LLVM 21 cannot lower this decoder with ARM softfp. build.py uses ARM GCC
 * with VFPv3 and the base calling convention; software float misses playback deadlines. */
#define DR_MP3_IMPLEMENTATION
#include "vendor/dr_libs/dr_mp3.h"
