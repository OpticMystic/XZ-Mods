/* Deferred DJ Profile online-page link and QR codec; excluded from the XZ runtime build.
   Offline codec checks do not establish online-page or XZ QR availability.
   See docs/architecture/dj-profile-standard.md section 6.
   Pure computation: no I/O, no allocation beyond malloc/free, thread-safe. */
#ifndef XZ_DJ_LINK_H
#define XZ_DJ_LINK_H

#include <stddef.h>
#include <stdint.h>

#define XZ_DJ_LINK_MAX 1200          /* longest link we will draw as a QR */
#define XZ_DJ_QR_MAX_SIDE 177        /* modules, version 40 */
#define XZ_DJ_QR_VERSION 12          /* the XZ draws at 3 px per module: the link must fit version 12, ECC M */

/* The link a phone should open for a profile: profile.json's page_url when it is a valid
   https URL, else https://vj.tools/dj#p1.<data> built from dj_id, dj_name, tagline, accent and
   links exactly like encodeProfileFragment (stored deflate). Takes profile.json bytes (validated
   leniently: only the fields used). When the full link would not fit the QR budget, drops links
   from the end, then the tagline, until it fits (`dropped` reports how many fields were dropped,
   may be NULL). Returns the link length, or -1 when the JSON lacks a usable dj_id/dj_name.
   Details:
   - Fields are checked with the online page's rules; a tagline, accent or link the page would
     reject is left out (not counted in `dropped`), and only the first 12 links are used.
   - page_url wins only when it also fits the QR budget; otherwise the fragment link is used.
   - When even the link without links and tagline does not fit the QR budget it is still
     returned (xz_dj_qr then returns 0).
   - Also -1 when `out` cannot hold the link and its NUL. */
int xz_dj_link_from_profile(const char *profile_json, size_t n, char *out, size_t cap, int *dropped);

/* Encodes text as a QR (byte mode, ECC M, versions 1..max_version, mask auto, boosting ECC when
   it still fits). modules gets side*side bytes, 1 = dark, row-major. Returns side, or 0 when it
   does not fit. */
int xz_dj_qr(const char *text, int max_version, uint8_t *modules, size_t cap);

#endif
