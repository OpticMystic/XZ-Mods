/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#error Browser metadata acceptance requires active assertions
#endif
#include "browser.c"
#include <assert.h>
/* The native row hook draws artist and BPM only from the matching export.pdb track. */
int main(int argc,char **argv){
    assert(argc==2&&!xz_key_catalog_load(argv[1],&catalogs[0]));
    const uint16_t title[]={'N','i','g','h','t',' ','D','r','i','v','e',0};
    uint16_t out[264],prefix[8];uint32_t color;
    int columns[3]={XZ_KEY_COLUMN_ARTIST,XZ_KEY_COLUMN_BPM,XZ_KEY_COLUMN_TITLE};
    xz_key_browser_configure(columns,1,1,1);xz_key_browser_scope(1,1,XZ_KEY_CAMELOT,0);
    size_t n=xz_key_browser_prefix(title,11,out,prefix,&color);
    const uint16_t want[]={'C',' ',' ',0x65e5,0x672c,' ',' ','1','4','2','.','2',' ',' ','N','i','g','h','t',' ','D','r','i','v','e',0};
    assert(n==25&&!memcmp(out,want,sizeof(want))&&prefix[0]=='C'&&!prefix[1]&&color==0xa8b2bf);
    xz_key_browser_configure(columns,0,1,1);n=xz_key_browser_prefix(title,11,out,prefix,&color);
    assert(n==22&&!memcmp(out,want+3,(n+1)*2)&&!prefix[0]&&!color);
    const uint16_t other[]={'O','t','h','e','r',0};
    n=xz_key_browser_prefix(other,5,out,prefix,&color);
    const uint16_t bare[]={'O','t','h','e','r',0};
    assert(n==0||!memcmp(out+n-5,bare,12));
    xz_key_catalog_free(&catalogs[0]);
    puts("PASS native row hook draws exported artist + hundredth BPM in column order; absent fields omitted");return 0;
}
