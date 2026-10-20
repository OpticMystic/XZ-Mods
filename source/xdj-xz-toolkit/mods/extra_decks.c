/* SPDX-License-Identifier: MIT */
#include "extra_decks.h"
#include <string.h>
/* No players yet: every interface keeps its two-deck screen. */
int xz_extra_decks_snapshot(struct xz_extra_decks *out){
 if(out)memset(out,0,sizeof(*out));
 return 0;
}
