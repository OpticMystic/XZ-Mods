/* SPDX-License-Identifier: MIT */
#include "ui_support.h"
#include "../vendor/qrcodegen/qrcodegen.h"
#include <stdio.h>
#include <string.h>

static int month_days(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1] + (month == 2 && year % 4 == 0);
}
static uint64_t report_epoch(const int *v) {
    if (v[0] < 2026 || v[0] > 2037 || v[1] < 1 || v[1] > 12 ||
        v[2] < 1 || v[2] > month_days(v[0],v[1]) || v[3] < 0 || v[3] > 23 || v[4] < 0 || v[4] > 59) return 0;
    uint64_t days = 0;
    for (int y=1970;y<v[0];y++) days += 365 + (y%4 == 0);
    for (int m=1;m<v[1];m++) days += month_days(v[0],m);
    return ((days + v[2]-1)*24 + v[3])*3600 + v[4]*60;
}

static const char *const step_labels[XZ_SUPPORT_STEPS] = {
    "1 PREPARE", "2 PREVIEW", "3 RECEIPT"
};
static const char *const menu_steps[XZ_SUPPORT_STEPS] = {
    "PREPARE", "PREVIEW", "RECEIPT"
};
static const char *const category_labels[4] = {
    "FOUR-DECK BRIDGE", "USB MEDIA", "AUDIO / STEMS", "DISPLAY / MENU"
};
static const char *const summary_labels[4] = {
    "CH3/CH4 BRIDGE OR JOG SCRUB",
    "DECK UNRESPONSIVE ON SWITCH",
    "USB DRIVE / TRACK NOT READ",
    "DISPLAY / MENU / OTHER BUG"
};

int xz_ui_support_page(int page) {
    return page == XZ_UI_SUPPORT;
}

int xz_ui_support_kind(int kind) {
    return kind >= XZ_UI_SUPPORT_STEP && kind <= XZ_UI_SUPPORT_CHECK;
}

const char *xz_ui_support_title(int page) {
    (void)page;
    return "REPORT AN ISSUE";
}

static int current_step(const struct xz_ui *u) {
    int s = u->support.step;
    return (s >= 0 && s < XZ_SUPPORT_STEPS) ? s : XZ_SUPPORT_STEP_PREPARE;
}

static unsigned report_selection(const struct xz_ui *u) {
    return 16u | (unsigned)(u->support.category & 3) | (unsigned)((u->support.summary & 3) << 2);
}

int xz_ui_support_consented(const struct xz_ui *u, const struct xz_ui_model *m) {
    if (!u || !m || !u->support.consented || m->support.prepared_selection != report_selection(u)) return 0;
    if (!m->support.report_id[0] || !m->support.payload_sha256[0]) return 0;
    if (u->support.consented_revision != m->support.draft_revision) return 0;
    return strcmp(u->support.consented_sha256, m->support.payload_sha256) == 0;
}

static void add(struct xz_ui_widget *w, size_t *n, int kind, int index, const char *label) {
    if (*n < XZ_UI_WIDGETS) {
        w[(*n)++] = (struct xz_ui_widget){
            .kind = (enum xz_ui_action_kind)kind,
            .index = index,
            .deck = -1,
            .label = label
        };
    }
}

size_t xz_ui_support_controls(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n) {
    int step = current_step(u);
    for (int i = 0; i < XZ_SUPPORT_STEPS; i++) {
        add(w, &n, XZ_UI_SUPPORT_STEP, i, step_labels[i]);
    }
    if (u->support.clock_edit) {
        static const char *const labels[] = {"YEAR -", "YEAR +", "MONTH -", "MONTH +", "DAY -", "DAY +", "HOUR -", "HOUR +", "MINUTE -", "MINUTE +", "USE THIS UTC DATE / TIME", "CANCEL"};
        for (int i=0;i<12;i++) add(w,&n,XZ_UI_SUPPORT_CLOCK,i+1,labels[i]);
        return n;
    }
    if (step == XZ_SUPPORT_STEP_PREPARE) {
        for (int i = 0; i < 4; i++) add(w, &n, XZ_UI_SUPPORT_CATEGORY, i, category_labels[i]);
        for (int i = 0; i < 4; i++) add(w, &n, XZ_UI_SUPPORT_SUMMARY, i, summary_labels[i]);
        add(w, &n, XZ_UI_SUPPORT_PREPARE, 0,
            m->support.phase == XZ_SUPPORT_WORKING ? "PREPARING DRAFT..." : "PREPARE SANITIZED DRAFT");
    } else if (step == XZ_SUPPORT_STEP_PREVIEW) {
        add(w, &n, XZ_UI_SUPPORT_PAGE, -1, "PREV");
        add(w, &n, XZ_UI_SUPPORT_PAGE, 1, "NEXT");
        add(w, &n, XZ_UI_SUPPORT_CONSENT, 0,
            xz_ui_support_consented(u, m) ? "CONSENT: APPROVED" : "APPROVE EXACT SHA256");
        add(w, &n, XZ_UI_SUPPORT_CLOCK, 0,
            m->support.clock_ready ? "EDIT REPORT UTC DATE" : "SET CURRENT UTC DATE");
        add(w, &n, XZ_UI_SUPPORT_SEND, 0, "SEND PRIVATE LOGS");
        add(w, &n, XZ_UI_SUPPORT_EXPORT, 0, "EXPORT TO USB");
    } else {
        add(w, &n, XZ_UI_SUPPORT_CHECK, 0, "CHECK RECEIPT");
        add(w, &n, XZ_UI_SUPPORT_SEND, 0, "RETRY SEND");
        add(w, &n, XZ_UI_SUPPORT_EXPORT, 0, "EXPORT TO USB");
        add(w, &n, XZ_UI_SUPPORT_CLOCK, 0,
            m->support.clock_ready ? "EDIT REPORT UTC DATE" : "SET CURRENT UTC DATE");
    }
    return n;
}

static void put(struct xz_ui_widget *w, int x, int y, int width, int height) {
    w->x = x; w->y = y; w->w = width; w->h = height;
}

void xz_ui_support_place(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n) {
    (void)m;
    int step = current_step(u);
    for (size_t i = 0; i < n; i++) {
        struct xz_ui_widget *a = &w[i];
        if (u->page != XZ_UI_SUPPORT) continue;
        if (a->kind == XZ_UI_SUPPORT_STEP) {
            put(a, 136 + a->index * 221, 84, 214, 36);
            continue;
        }
        if (u->support.clock_edit && a->kind == XZ_UI_SUPPORT_CLOCK) {
            int idx=a->index-1;
            if (idx<10) put(a,136+(idx%2)*332,148+(idx/2)*42,324,36);
            else put(a,136+(idx-10)*332,362,324,40);
            continue;
        }
        if (step == XZ_SUPPORT_STEP_PREPARE) {
            if (a->kind == XZ_UI_SUPPORT_CATEGORY) {
                put(a, 136 + a->index * 166, 144, 158, 40);
            } else if (a->kind == XZ_UI_SUPPORT_SUMMARY) {
                put(a, 136 + (a->index % 2) * 332, 208 + (a->index / 2) * 46, 324, 40);
            } else if (a->kind == XZ_UI_SUPPORT_PREPARE) {
                put(a, 136, 316, 656, 44);
            }
        } else if (step == XZ_SUPPORT_STEP_PREVIEW) {
            if (a->kind == XZ_UI_SUPPORT_PAGE) {
                put(a, a->index < 0 ? 136 : 232, 316, 88, 40);
            } else if (a->kind == XZ_UI_SUPPORT_CONSENT) {
                put(a, 328, 316, 256, 40);
            } else if (a->kind == XZ_UI_SUPPORT_CLOCK) {
                put(a, 592, 316, 200, 40);
            } else if (a->kind == XZ_UI_SUPPORT_SEND) {
                put(a, 136, 362, 324, 40);
            } else if (a->kind == XZ_UI_SUPPORT_EXPORT) {
                put(a, 468, 362, 324, 40);
            }
        } else {
            if (a->kind == XZ_UI_SUPPORT_CHECK) {
                put(a, 136, 316, 212, 40);
            } else if (a->kind == XZ_UI_SUPPORT_SEND) {
                put(a, 356, 316, 212, 40);
            } else if (a->kind == XZ_UI_SUPPORT_EXPORT) {
                put(a, 576, 316, 216, 40);
            } else if (a->kind == XZ_UI_SUPPORT_CLOCK) {
                put(a, 136, 362, 656, 40);
            }
        }
    }
}

int xz_ui_support_selected(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w) {
    if (w->kind == XZ_UI_SUPPORT_STEP) return w->index == current_step(u);
    if (w->kind == XZ_UI_SUPPORT_CATEGORY) return w->index == (u->support.category & 3);
    if (w->kind == XZ_UI_SUPPORT_SUMMARY) return w->index == (u->support.summary & 3);
    if (w->kind == XZ_UI_SUPPORT_CONSENT) return xz_ui_support_consented(u, m);
    if (w->kind == XZ_UI_SUPPORT_CLOCK) return m->support.clock_ready != 0;
    return 0;
}

void xz_ui_support_render_qr_box(struct xz_canvas c, int x, int y, int box_side, const char *url) {
    if (!url || !url[0] || box_side < 64) return;
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    if (!qrcodegen_encodeText(url, temp, qrcode, qrcodegen_Ecc_LOW,
                              1, 10, qrcodegen_Mask_AUTO, true)) {
        return;
    }
    int qr_size = qrcodegen_getSize(qrcode);
    if (qr_size <= 0) return;
    int scale = box_side / (qr_size + 6);
    if (scale < 1) scale = 1;
    int drawn = (qr_size + 6) * scale;
    int left = x + (box_side - drawn) / 2;
    int top = y + (box_side - drawn) / 2;
    xz_rect(c, x, y, box_side, box_side, 0xffffff);
    for (int py = 0; py < qr_size; py++) {
        for (int px = 0; px < qr_size; px++) {
            if (qrcodegen_getModule(qrcode, px, py)) {
                xz_rect(c, left + (px + 3) * scale, top + (py + 3) * scale, scale, scale, 0x000000);
            }
        }
    }
}

static const char *private_log_state_text(enum xz_support_phase phase) {
    switch (phase) {
    case XZ_SUPPORT_STORED: return "STORED IN PRIVATE R2 INBOX";
    case XZ_SUPPORT_EXPORTED: return "EXPORTED TO USB (OFFLINE)";
    case XZ_SUPPORT_OUTCOME_UNKNOWN: return "OUTCOME UNKNOWN (CHECK RECEIPT)";
    case XZ_SUPPORT_NEEDS_CLOCK: return "NEEDS CURRENT UTC DATE FOR TLS";
    case XZ_SUPPORT_WORKING: return "WORKING IN BACKGROUND...";
    case XZ_SUPPORT_PREPARED: return "DRAFT PREPARED (NOT YET SENT)";
    case XZ_SUPPORT_ERROR: return "UPLOAD OR HELPER ERROR";
    default: return "NO DRAFT SENT YET";
    }
}

void xz_ui_support_render(const struct xz_ui_scene *s) {
    const struct xz_ui *u = s->u;
    const struct xz_ui_model *m = s->m;
    uint32_t bg, ink, accent;
    xz_ui_chrome_colors(m, s->p, &bg, &ink, &accent);
    uint32_t dim = xz_blend(bg, ink, 165);
    xz_rect(s->c, 136, 48, 656, 356, bg);
    xz_text(s->c, 136, 52, "REPORT AN ISSUE", 2, 40, ink);
    xz_ui_chrome_fit(s->c, 360, 56, 432,
                     "PUBLIC GITHUB ISSUE + PRIVATE SANITIZED LOGS", 1, accent);

    int step = current_step(u);
    char line[160];
    if (u->support.clock_edit) {
        const int *v=u->support.clock_field;
        snprintf(line,sizeof(line),"CURRENT UTC: %04d-%02d-%02d %02d:%02d / VERIFY BEFORE APPLY",v[0],v[1],v[2],v[3],v[4]);
        xz_ui_chrome_fit(s->c,136,126,656,line,1,accent);
    } else if (step == XZ_SUPPORT_STEP_PREPARE) {
        xz_ui_chrome_fit(s->c, 136, 128, 656,
                         "1 CHOOSE CATEGORY AND SUMMARY   2 PREPARE SANITIZED DRAFT TO PREVIEW", 1, dim);
        xz_ui_chrome_fit(s->c, 136, 192, 656,
                         "PRESET SUMMARY (FULL STEPS ADDED ON GITHUB AFTER UPLOAD):", 1, dim);
    } else if (step == XZ_SUPPORT_STEP_PREVIEW) {
        if (m->support.report_id[0]) {
            snprintf(line, sizeof(line), "ID: %s   BYTES: %u   REDACTED: %u   PAGE %u/%u",
                     m->support.report_id, m->support.payload_bytes, m->support.redactions,
                     m->support.page_index + 1, m->support.page_count ? m->support.page_count : 1);
            xz_ui_chrome_fit(s->c, 136, 126, 656, line, 1, accent);
            snprintf(line, sizeof(line), "SHA256: %s", m->support.payload_sha256);
            xz_ui_chrome_fit(s->c, 136, 144, 656, line, 1, ink);
            xz_rect(s->c, 136, 162, 656, 146, xz_blend(bg, ink, 22));
            for (int i = 0; i < XZ_SUPPORT_PREVIEW_LINES; i++) {
                xz_ui_chrome_fit(s->c, 144, 168 + i * 22, 640, m->support.preview[i], 1, ink);
            }
        } else {
            xz_ui_chrome_fit(s->c, 136, 152, 656,
                             "NO DRAFT PREPARED YET. GO TO STEP 1 AND TAP PREPARE SANITIZED DRAFT.", 1, ink);
            xz_ui_chrome_fit(s->c, 136, 178, 656,
                             "MACS, SERIALS, PATHS AND SECRETS ARE REDACTED BEFORE PREVIEW.", 1, dim);
        }
    } else {
        snprintf(line, sizeof(line), "PRIVATE LOGS: %s", private_log_state_text(m->support.phase));
        xz_ui_chrome_fit(s->c, 136, 132, 444, line, 1, accent);
        xz_ui_chrome_fit(s->c, 136, 154, 444,
                         "GITHUB ISSUE: PENDING USER SUBMISSION", 1, ink);
        snprintf(line, sizeof(line), "REPORT ID: %s",
                 m->support.report_id[0] ? m->support.report_id : "NONE");
        xz_ui_chrome_fit(s->c, 136, 178, 444, line, 1, ink);
        if (m->support.payload_sha256[0]) {
            snprintf(line, sizeof(line), "SHA256: %.32s", m->support.payload_sha256);
            xz_ui_chrome_fit(s->c, 136, 200, 444, line, 1, dim);
            snprintf(line, sizeof(line), "        %.32s", m->support.payload_sha256 + 32);
            xz_ui_chrome_fit(s->c, 136, 218, 444, line, 1, dim);
        }
        if (m->support.export_path[0] && m->support.phase == XZ_SUPPORT_EXPORTED) {
            snprintf(line, sizeof(line), "USB FILE: %.140s", m->support.export_path);
            xz_ui_chrome_fit(s->c, 136, 242, 444, line, 1, ink);
        } else {
            xz_ui_chrome_fit(s->c, 136, 242, 444,
                             "SCAN QR WITH PHONE TO OPEN PREFILLED GITHUB ISSUE.", 1, ink);
        }
        xz_ui_chrome_fit(s->c, 136, 264, 444,
                         "NO AUTO-SUBMISSION: FINAL SUBMIT HAPPENS ON GITHUB.", 1, dim);
        if (m->support.github_url[0]) {
            xz_ui_support_render_qr_box(s->c, 604, 128, 176, m->support.github_url);
        }
    }

    for (size_t i = 0; i < s->n; i++) {
        const struct xz_ui_widget *a = &s->w[i];
        if (!xz_ui_support_kind(a->kind)) continue;
        int selected = xz_ui_support_selected(u, m, a);
        uint32_t fill = xz_blend(bg, accent, selected ? 130 : 40);
        xz_round_rect(s->c, a->x, a->y, a->w, a->h, 6, 15, selected ? accent : xz_blend(bg, ink, 80));
        xz_round_rect(s->c, a->x + 1, a->y + 1, a->w - 2, a->h - 2, 5, 15, fill);
        xz_ui_chrome_fit(s->c, a->x + 8, a->y + (a->h < 38 ? 11 : 14), a->w - 16, a->label, 1, ink);
    }
}

size_t xz_ui_support_action(struct xz_ui *u, const struct xz_ui_model *m, int kind, int index, struct xz_ui_action *out) {
    out[0] = (struct xz_ui_action){
        .kind = (enum xz_ui_action_kind)kind,
        .phase = XZ_UI_PRESS,
        .deck = -1,
        .index = index,
        .pc_target = -1
    };
    if (kind == XZ_UI_SUPPORT_STEP) {
        if (index >= 0 && index < XZ_SUPPORT_STEPS) u->support.step = index;
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_CATEGORY) {
        if (u->support.category != (index & 3)) {
            u->support.consented = 0; u->support.consented_sha256[0] = 0;
        }
        u->support.category = index & 3;
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_SUMMARY) {
        if (u->support.summary != (index & 3)) {
            u->support.consented = 0; u->support.consented_sha256[0] = 0;
        }
        u->support.summary = index & 3;
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_PAGE) {
        int next = u->support.preview_page + (index < 0 ? -1 : 1);
        int max_p = m->support.page_count > 0 ? (int)m->support.page_count - 1 : 0;
        if (next < 0) next = 0;
        if (next > max_p) next = max_p;
        u->support.preview_page = next;
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_CONSENT) {
        if (!m->support.report_id[0] || !m->support.payload_sha256[0] ||
            m->support.prepared_selection != report_selection(u)) {
            u->support.consented = 0;
            snprintf(u->notice, sizeof(u->notice), "PREPARE AND REVIEW THE UPDATED SELECTION BEFORE APPROVING");
            return 1;
        }
        if (xz_ui_support_consented(u, m)) {
            u->support.consented = 0;
            u->support.consented_sha256[0] = 0;
            snprintf(u->notice, sizeof(u->notice), "UPLOAD CONSENT CLEARED");
        } else {
            u->support.consented = 1;
            u->support.consented_revision = m->support.draft_revision;
            snprintf(u->support.consented_sha256, sizeof(u->support.consented_sha256), "%s", m->support.payload_sha256);
            snprintf(u->notice, sizeof(u->notice), "APPROVED SHA256 %.16s... FOR SEND OR USB EXPORT", m->support.payload_sha256);
        }
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_PREPARE) {
        u->support.consented = 0;
        u->support.consented_sha256[0] = 0;
        u->support.preview_page = 0;
        u->support.step = XZ_SUPPORT_STEP_PREVIEW;
        out[0].value = 1.0f;
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_CLOCK) {
        out[0].value = 0.0f;
        int *v=u->support.clock_field;
        if (!u->support.clock_edit) {
            u->support.clock_edit=1;
            if (!report_epoch(v)) { v[0]=2026;v[1]=10;v[2]=25;v[3]=12;v[4]=0; }
        } else if (index>=1 && index<=10) {
            int field=(index-1)/2;
            const int lo[]={2026,1,1,0,0}, hi[]={2037,12,month_days(v[0],v[1]),23,59};
            v[field] += index%2 ? -1 : 1;
            if(v[field]<lo[field])v[field]=hi[field];
            if(v[field]>hi[field])v[field]=lo[field];
            if(v[2]>month_days(v[0],v[1]))v[2]=month_days(v[0],v[1]);
        } else if (index==11 && report_epoch(v)) {
            u->support.clock_edit=0; out[0].value=1.0f;
        } else if (index==12) u->support.clock_edit=0;
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_CHECK) {
        u->support.step = XZ_SUPPORT_STEP_RECEIPT;
        out[0].value = 1.0f;
        return 1;
    }
    if (kind == XZ_UI_SUPPORT_SEND || kind == XZ_UI_SUPPORT_EXPORT) {
        if (!xz_ui_support_consented(u, m)) {
            out[0].value = 0.0f;
            snprintf(u->notice, sizeof(u->notice), "APPROVE THE EXACT SHA256 IN PREVIEW BEFORE SENDING OR EXPORTING");
            return 1;
        }
        u->support.step = XZ_SUPPORT_STEP_RECEIPT;
        out[0].value = 1.0f;
        return 1;
    }
    return 1;
}

int xz_ui_support_take_request(struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_action *a, struct xz_support_request_args *out) {
    if (!u || !m || !a || !out || a->phase != XZ_UI_PRESS) return 0;
    memset(out, 0, sizeof(*out));
    out->category = u->support.category & 3;
    out->summary = u->support.summary & 3;
    if (a->kind == XZ_UI_SUPPORT_PREPARE && a->value > 0.0f) {
        out->op = XZ_SUPPORT_OP_PREPARE;
        return 1;
    }
    if (a->kind == XZ_UI_SUPPORT_CLOCK && a->value > 0.0f) {
        out->op = XZ_SUPPORT_OP_SET_CLOCK;
        out->report_epoch = report_epoch(u->support.clock_field);
        return out->report_epoch != 0;
    }
    if (a->kind == XZ_UI_SUPPORT_CHECK && a->value > 0.0f) {
        out->op = XZ_SUPPORT_OP_CHECK;
        return 1;
    }
    if ((a->kind == XZ_UI_SUPPORT_SEND || a->kind == XZ_UI_SUPPORT_EXPORT) && a->value > 0.0f) {
        if (!xz_ui_support_consented(u, m)) return 0;
        out->op = (a->kind == XZ_UI_SUPPORT_SEND) ? XZ_SUPPORT_OP_SEND : XZ_SUPPORT_OP_EXPORT;
        out->draft_revision = u->support.consented_revision;
        snprintf(out->expected_sha256, sizeof(out->expected_sha256), "%s", u->support.consented_sha256);
        return 1;
    }
    return 0;
}

const char *xz_ui_support_help(int kind) {
    switch (kind) {
    case XZ_UI_SUPPORT_PREPARE:
        return "Snapshots and redacts bounded diagnostics into /dev/shm/xz-support-state/draft.json for preview. Nothing is sent yet.";
    case XZ_UI_SUPPORT_CONSENT:
        return "Binds explicit upload/export consent to the exact 64-hex SHA-256 of the prepared draft. Any new draft clears consent.";
    case XZ_UI_SUPPORT_CLOCK:
        return "Enter the current UTC date and time for certificate checks. This affects reporting only; playback and system clocks stay unchanged.";
    case XZ_UI_SUPPORT_SEND:
        return "Uploads the exact approved draft over verified TLS 1.2 to the private XZ Mods support inbox and returns a receipt.";
    case XZ_UI_SUPPORT_EXPORT:
        return "Writes the exact approved draft to VJTOOLS/support-reports on the USB stick for offline submission from a computer.";
    case XZ_UI_SUPPORT_CHECK:
        return "Performs a read-only receipt check for the current report ID when a prior upload outcome was interrupted.";
    default:
        return "Prepare a sanitized diagnostic report, review the exact SHA-256 preview, and send private logs or export to USB.";
    }
}

int xz_ui_support_menu(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_shell_menu *out) {
    memset(out, 0, sizeof(*out));
    out->page = u->page;
    out->layout = XZ_SHELL_MENU_SUPPORT;
    if(current_step(u)==XZ_SUPPORT_STEP_RECEIPT && m->support.phase==XZ_SUPPORT_STORED)
        snprintf(out->report_qr_url,sizeof(out->report_qr_url),"%s",m->support.github_url);
    int depth = 0;
    out->root = xz_ui_menu_root(u, &depth);
    out->depth = (uint8_t)depth;
    out->focus_row = -1;
    snprintf(out->title, sizeof(out->title), "%s", xz_ui_support_title(u->page));
    snprintf(out->path, sizeof(out->path), "%s", out->root == XZ_UI_SETTINGS ? "ADVANCED" : "NETWORK / NETWORK UPDATES");
    out->section_count = 1;
    strcpy(out->sections[0], "ISSUE REPORT");
    out->step_count = XZ_SUPPORT_STEPS;
    out->step = (uint8_t)current_step(u);
    for (int i = 0; i < XZ_SUPPORT_STEPS; i++) strcpy(out->steps[i], menu_steps[i]);

    struct xz_ui_widget w[XZ_UI_WIDGETS];
    size_t n = xz_ui_layout(u, m, w);
    for (size_t i = 0; i < n && out->row_count < XZ_SHELL_MENU_ROWS; i++) {
        if (!xz_ui_support_kind(w[i].kind)) continue;
        struct xz_shell_menu_row *r = &out->row[out->row_count++];
        r->kind = XZ_SHELL_MENU_ACTION;
        r->available = 1;
        r->current = -1;
        r->key = ((int32_t)w[i].kind << 16) ^ (w[i].index & 0xffff);
        snprintf(r->label, sizeof(r->label), "%s", w[i].label);
        snprintf(r->help, sizeof(r->help), "%s", xz_ui_support_help(w[i].kind));
        if (xz_ui_support_selected(u, m, &w[i])) strcpy(r->value, "SELECTED");
        r->target = (struct xz_shell_menu_target){(int16_t)w[i].kind, (int16_t)w[i].index, -1, XZ_UI_MENU_TOUCH, 0};
        if (u->focus_kind == w[i].kind && u->focus_index == w[i].index) {
            out->focus_row = (int16_t)(out->row_count - 1);
        }
    }

    struct xz_shell_menu_line *ln = &out->line[out->line_count++];
    ln->tone = XZ_SHELL_TONE_HEAD;
    snprintf(ln->text, sizeof(ln->text), "PRIVATE LOGS: %s", private_log_state_text(m->support.phase));
    ln = &out->line[out->line_count++];
    ln->tone = XZ_SHELL_TONE_PLAIN;
    snprintf(ln->text, sizeof(ln->text), "GITHUB ISSUE: PENDING USER SUBMISSION");

    if (m->support.report_id[0] && out->line_count < XZ_SHELL_MENU_LINES) {
        ln = &out->line[out->line_count++];
        snprintf(ln->text, sizeof(ln->text), "ID %s (%u B, %u REDACTED)",
                 m->support.report_id, m->support.payload_bytes, m->support.redactions);
    }
    if (m->support.payload_sha256[0] && out->line_count < XZ_SHELL_MENU_LINES) {
        ln = &out->line[out->line_count++];
        snprintf(ln->text, sizeof(ln->text), "SHA256 %.32s", m->support.payload_sha256);
        if (out->line_count < XZ_SHELL_MENU_LINES) {
            ln = &out->line[out->line_count++];
            snprintf(ln->text, sizeof(ln->text), "       %.32s", m->support.payload_sha256 + 32);
        }
    }
    if (u->support.clock_edit && out->line_count < XZ_SHELL_MENU_LINES) {
        const int *v=u->support.clock_field;
        ln = &out->line[out->line_count++];
        snprintf(ln->text,sizeof(ln->text),"UTC %04d-%02d-%02d %02d:%02d",v[0],v[1],v[2],v[3],v[4]);
    } else if (current_step(u) == XZ_SUPPORT_STEP_PREVIEW && m->support.report_id[0]) {
        for (int i = 0; i < XZ_SUPPORT_PREVIEW_LINES && out->line_count < XZ_SHELL_MENU_LINES; i++) {
            if (!m->support.preview[i][0]) continue;
            ln = &out->line[out->line_count++];
            ln->tone = XZ_SHELL_TONE_DIM;
            snprintf(ln->text, sizeof(ln->text), "%.72s", m->support.preview[i]);
        }
    } else if (current_step(u) == XZ_SUPPORT_STEP_RECEIPT && m->support.github_url[0] && out->line_count < XZ_SHELL_MENU_LINES) {
        ln = &out->line[out->line_count++];
        ln->tone = XZ_SHELL_TONE_GOOD;
        snprintf(ln->text, sizeof(ln->text), "GITHUB: %.68s", m->support.github_url);
    }
    snprintf(out->status, sizeof(out->status), "%.99s",
             u->notice[0] ? u->notice : m->support.status[0] ? m->support.status : "PUBLIC GITHUB ISSUE + PRIVATE LOGS");
    return 1;
}
