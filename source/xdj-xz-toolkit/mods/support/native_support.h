/* SPDX-License-Identifier: MIT */
#ifndef XZ_NATIVE_SUPPORT_H
#define XZ_NATIVE_SUPPORT_H
#include <stdint.h>

enum xz_support_phase {
    XZ_SUPPORT_IDLE = 0,
    XZ_SUPPORT_WORKING,
    XZ_SUPPORT_PREPARED,
    XZ_SUPPORT_STORED,
    XZ_SUPPORT_EXPORTED,
    XZ_SUPPORT_OUTCOME_UNKNOWN,
    XZ_SUPPORT_NEEDS_CLOCK,
    XZ_SUPPORT_ERROR,
    /* REPORT AN ISSUE (XZ_SUPPORT_OP_REPORT): the Worker took it (reference); it is on the loader
       stick (result: why it was not sent); or neither (result: the stick's reason, detail: the upload's). */
    XZ_SUPPORT_SENT,
    XZ_SUPPORT_SAVED,
    XZ_SUPPORT_UNSAVED
};
/* The five topics (FEEDBACK-TAXONOMY.md section 1), in the order the XZ lists them; -1 is NOT SURE. */
enum xz_support_topic { XZ_SUPPORT_GOT_WORSE, XZ_SUPPORT_NOT_WORKING, XZ_SUPPORT_WORK_DIFFERENTLY, XZ_SUPPORT_NEW_IDEA,
    XZ_SUPPORT_HOW_DO_I, XZ_SUPPORT_TOPICS };
/* Where the report started. */
enum xz_support_entry { XZ_SUPPORT_FROM_EXTRAS, XZ_SUPPORT_FROM_ALERT, XZ_SUPPORT_FROM_UPDATES, XZ_SUPPORT_ENTRIES };
/* The screen under MODS when it opened: MODERN's own screens, or CLASSIC's stock ones as ui/menu_key.h names
   them. An enum only; nothing on the screen is read. */
enum xz_support_place { XZ_SUPPORT_AT_UNKNOWN, XZ_SUPPORT_AT_PLAY, XZ_SUPPORT_AT_BROWSE, XZ_SUPPORT_AT_PLAYLIST,
    XZ_SUPPORT_AT_TAG_LIST, XZ_SUPPORT_AT_SEARCH, XZ_SUPPORT_AT_INFO, XZ_SUPPORT_AT_SHORTCUT, XZ_SUPPORT_AT_UTILITY,
    XZ_SUPPORT_AT_SOURCE, XZ_SUPPORT_AT_STOCK_MENU, XZ_SUPPORT_AT_TRACK_LIST, XZ_SUPPORT_AT_GRID_ADJUST,
    XZ_SUPPORT_AT_TAKEOVER, XZ_SUPPORT_AT_GAME, XZ_SUPPORT_AT_OTHER, XZ_SUPPORT_PLACES };
#define XZ_SUPPORT_TEXT_CHARS 280

#define XZ_SUPPORT_PREVIEW_LINES 4
#define XZ_SUPPORT_LINE_CHARS 32

struct xz_support_model {
    enum xz_support_phase phase;
    int ready;
    int usb_writable;
    int clock_ready;
    uint32_t draft_revision;
    uint32_t prepared_selection; /* bit4 valid; category bits0..1, summary bits2..3 */
    uint32_t payload_bytes;
    uint32_t redactions;
    uint32_t page_index;
    uint32_t page_count;
    char report_id[33];
    char payload_sha256[65];
    char github_url[160];
    char export_path[160];
    char status[128];
    char preview[XZ_SUPPORT_PREVIEW_LINES][XZ_SUPPORT_LINE_CHARS];
    /* REPORT AN ISSUE. reference "R-XXXX-XXXX" once SENT (a loader gets no tracking link: the DJ follows the report
       in the XZ Mods app); result and detail are the helper's reason words (SAVED: NO_IDENTITY NEEDS_CLOCK OFFLINE
       REJECTED UNKNOWN; UNSAVED: NO_STICK READ_ONLY FULL LIMIT FAILED TIMEOUT, then the upload's). loader_id
       "L-XXXX-XXXX", or "" with loader_state saying why (enum xz_loader_identity_state, support/loader_identity.h). */
    char reference[12];
    char result[16];
    char detail[16];
    char loader_id[12];
    int loader_state;
};

enum xz_support_op {
    XZ_SUPPORT_OP_PREPARE = 0,
    XZ_SUPPORT_OP_SET_CLOCK = 1,
    XZ_SUPPORT_OP_SEND = 2,
    XZ_SUPPORT_OP_CHECK = 3,
    XZ_SUPPORT_OP_EXPORT = 4,
    XZ_SUPPORT_OP_REPORT = 5
};

/* One REPORT AN ISSUE send. The UI fills topic, entry, retry and text; the runtime adds what the DJ was looking at
   (enums and the alert codes on screen only) and the settings. */
struct xz_support_report {
    int topic;            /* enum xz_support_topic, or -1 NOT SURE */
    int entry;            /* enum xz_support_entry */
    int retry;            /* 1: the same report again, keeping its report ID */
    int ui_interface;     /* 0 CLASSIC, 1 MODERN */
    int before;           /* enum xz_support_place */
    int deck_view;        /* settings.h enum xz_deck_view: 0 two decks, 1 four 1 2 3 4, 2 four 3 1 2 4 */
    unsigned alert[3];    /* E- codes on screen: deck 1, deck 2, the USB port (8307); 0 none */
    int mods_page;        /* enum xz_ui_page the report was opened from, or -1 */
    int have_settings;
    int settings[6];      /* stems, gate, interface, classic theme, deck view, browse preview */
    char settings_sha256[65];
    char text[XZ_SUPPORT_TEXT_CHARS + 1]; /* printable ASCII as typed; "" when skipped */
};

struct xz_support_request_args {
    enum xz_support_op op;
    int category;
    int summary;
    uint32_t draft_revision;
    uint64_t report_epoch;
    char expected_sha256[65];
    struct xz_support_report report;
};

void xz_support_read(struct xz_support_model *out, int requested_page) __attribute__((weak));
int xz_support_request(const struct xz_support_request_args *req) __attribute__((weak));
/* Reads VJ.Tools/loader-identity.json from the loader stick once, at runtime start. The key stays in this module. */
void xz_support_identity_load(const char *usb_root) __attribute__((weak));
#endif
