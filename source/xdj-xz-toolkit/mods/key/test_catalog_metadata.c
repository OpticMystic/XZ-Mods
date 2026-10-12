#include "catalog.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv) {
    assert(argc==2);struct xz_key_catalog cat={0};assert(!xz_key_catalog_load(argv[1],&cat));
    struct xz_key_entry track;uint16_t artist[64];const uint16_t title[]={'N','i','g','h','t',' ','D','r','i','v','e',0};
    assert(xz_key_catalog_track(&cat,title,11,&track)==1&&track.tempo==14225&&track.artist_id==123);
    assert(xz_key_catalog_artist(&cat,123,artist)&&artist[0]==0x65e5&&artist[1]==0x672c&&artist[2]==0);
    const uint16_t duplicate[]={'D','u','p','l','i','c','a','t','e',0};
    assert(xz_key_catalog_track(&cat,duplicate,9,&track)==-1);
    assert(!xz_key_catalog_artist(&cat,999,artist)&&!artist[0]);
    xz_key_catalog_free(&cat);puts("PASS PDB artist references, far Unicode artist names, hundredth BPM and ambiguous-title refusal");
}
