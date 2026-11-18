/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdlib.h>
#include "../vendor/dr_libs/dr_mp3.h"
void *__real_calloc(size_t,size_t);void *__real_malloc(size_t);
unsigned xz_test_failed_index_allocations,xz_test_failed_cache_allocations;
void *__wrap_calloc(size_t n,size_t s){if(n>=1&&n<=8192&&s==sizeof(drmp3_seek_point)){++xz_test_failed_index_allocations;return NULL;}return __real_calloc(n,s);}
void *__wrap_malloc(size_t n){if(n==524288){++xz_test_failed_cache_allocations;return NULL;}return __real_malloc(n);}
