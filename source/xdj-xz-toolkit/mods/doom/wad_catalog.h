/* SPDX-License-Identifier: MIT */
#ifndef XZ_WAD_CATALOG_H
#define XZ_WAD_CATALOG_H
#include <stdint.h>
#define XZ_WAD_FILES 128
#define XZ_WAD_PATH 1024
enum xz_wad_kind { XZ_WAD_INVALID, XZ_WAD_BASE, XZ_WAD_MAP, XZ_WAD_ADVANCED };
enum xz_wad_family { XZ_WAD_ANY, XZ_WAD_DOOM, XZ_WAD_DOOM2 };
struct xz_wad_file {
    char path[XZ_WAD_PATH],name[96],reason[64];
    enum xz_wad_kind kind;
    enum xz_wad_family family;
    int shareware,first_episode,first_map,chex;
};
struct xz_wad_catalog { struct xz_wad_file files[XZ_WAD_FILES]; unsigned count,visited,truncated; };
int xz_wad_inspect(const char *,struct xz_wad_file *);
int xz_wad_scan(struct xz_wad_catalog *,const char *root);
int xz_wad_compatible(const struct xz_wad_file *base,const struct xz_wad_file *map,char reason[96]);
#endif
