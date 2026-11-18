/* SPDX-License-Identifier: MIT */
/* Which computer made this loader: VJ.Tools/loader-identity.json on the loader stick. The contract is the bug
   desk's spec/generated/loader-identity.schema.json (copied to spec/ here); the XZ Mods app writes it whenever it
   writes or updates a loader:

     {"version":1,"loader_id":"L-4F7K-2M9Q","loader_key":"<64 lower-case hex>","installation_id":"I-4F7K-2M9Q",
      "created_at":"2026-10-27T00:00:00.000Z","app_version":"0.5.0-preview.22.6"}

   loader_id and installation_id are not secret; ABOUT shows the loader ID and reports carry both. loader_key
   (256 bits) only lets this XZ add pending reports to its own installation's queue. It stays in memory: it is
   never logged, shown, written to the XZ or copied into a report. An older loader has no file and still reports
   through the stick. */
#ifndef XZ_LOADER_IDENTITY_H
#define XZ_LOADER_IDENTITY_H
#include <stddef.h>

enum { XZ_LOADER_IDENTITY_MAX_BYTES = 2048, XZ_LOADER_KEY_BYTES = 32 };
enum xz_loader_identity_state {
    XZ_LOADER_NONE = 0,     /* no file: an older loader, or no loader stick */
    XZ_LOADER_OK,
    XZ_LOADER_INVALID,      /* the file is not exactly the schema */
    XZ_LOADER_OVERSIZED,    /* larger than XZ_LOADER_IDENTITY_MAX_BYTES */
    XZ_LOADER_UNREADABLE    /* not a regular file, or a read error */
};
struct xz_loader_identity {
    int state;                                 /* enum xz_loader_identity_state */
    char id[12];                               /* "L-XXXX-XXXX" when OK, else "" */
    char installation[12];                     /* "I-XXXX-XXXX" when OK, else "" */
    unsigned char key[XZ_LOADER_KEY_BYTES];    /* when OK; zero otherwise */
};

/* Strict, as the schema: one object with exactly version 1, loader_id, loader_key, installation_id, created_at and
   app_version, each once, IDs canonical, nothing after it. Returns the state it sets in out. */
int xz_loader_identity_parse(const char *json, size_t len, struct xz_loader_identity *out);
/* Reads <usb_root>/VJ.Tools/loader-identity.json (bounded, no symlinks). Returns the state it sets in out. */
int xz_loader_identity_read(const char *usb_root, struct xz_loader_identity *out);
/* Zeroes the key and the rest of the record. */
void xz_loader_identity_clear(struct xz_loader_identity *out);
#endif
