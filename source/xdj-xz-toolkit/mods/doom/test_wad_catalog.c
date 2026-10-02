/* SPDX-License-Identifier: MIT */
#include "wad_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==2);char path[1200],reason[96];struct xz_wad_file base,map,bad;
    snprintf(path,sizeof(path),"%s/full.wad",argv[1]);assert(xz_wad_inspect(path,&base)==0&&base.kind==XZ_WAD_BASE&&!base.shareware);
    snprintf(path,sizeof(path),"%s/maps/map02.wad",argv[1]);assert(xz_wad_inspect(path,&map)==0&&map.kind==XZ_WAD_MAP&&map.first_map==2);
    assert(xz_wad_compatible(&base,&map,reason)==0);
    snprintf(path,sizeof(path),"%s/shareware.wad",argv[1]);assert(xz_wad_inspect(path,&bad)==0&&bad.shareware);assert(xz_wad_compatible(&bad,&map,reason)!=0);
    snprintf(path,sizeof(path),"%s/myhouse.wad",argv[1]);assert(xz_wad_inspect(path,&bad)==0&&bad.kind==XZ_WAD_ADVANCED);
    snprintf(path,sizeof(path),"%s/myhouse.pk3",argv[1]);assert(xz_wad_inspect(path,&bad)==0&&bad.kind==XZ_WAD_ADVANCED);
    snprintf(path,sizeof(path),"%s/broken.wad",argv[1]);assert(xz_wad_inspect(path,&bad)!=0);
    static struct xz_wad_catalog catalog;assert(xz_wad_scan(&catalog,argv[1])==0&&catalog.count==6);
    assert(xz_wad_scan(&catalog,"/this-path-does-not-exist")!=0);
    puts("PASS base/PWAD matching, shareware restriction, actual map02 entrypoint, GZDoom detection, corrupt directory and recursive USB catalog");
}
