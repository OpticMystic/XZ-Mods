/* SPDX-License-Identifier: MIT */
#include "ui_support.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error Assertions required
#endif

_Static_assert(XZ_UI_SUPPORT == 34 && XZ_UI_PAGE_COUNT == 35, "append-only support page ID");

static uint16_t pixels[800 * 480];

static struct xz_ui_widget find(struct xz_ui *u, struct xz_ui_model *m, int kind, int index) {
    struct xz_ui_widget w[XZ_UI_WIDGETS];
    size_t n = xz_ui_layout(u, m, w);
    assert(n <= XZ_UI_WIDGETS);
    for (size_t i = 0; i < n; i++) {
        assert(w[i].x >= 0 && w[i].y >= 0 && w[i].w >= 56 && w[i].h >= 36);
        assert(w[i].x + w[i].w <= 800 && w[i].y + w[i].h <= 404);
        if ((int)w[i].kind == kind && w[i].index == index) return w[i];
    }
    assert(!"missing support widget");
    return w[0];
}

static struct xz_ui_action tap(struct xz_ui *u, struct xz_ui_model *m, int kind, int index) {
    struct xz_ui_widget w = find(u, m, kind, index);
    struct xz_ui_action out[XZ_UI_ACTIONS] = {0}, rel[XZ_UI_ACTIONS] = {0};
    size_t n = xz_ui_touch(u, m, w.x + w.w / 2, w.y + w.h / 2, 1, out);
    assert(n == 1);
    assert(xz_ui_touch(u, m, 0, 0, 0, rel) == 0);
    return out[0];
}

static void check_step_geometry_and_menu(struct xz_ui *u, struct xz_ui_model *m) {
    for (int theme = 0; theme < XZ_THEME_COUNT; theme++) {
        m->theme = theme;
        struct xz_ui_widget w[XZ_UI_WIDGETS];
        size_t n = xz_ui_layout(u, m, w);
        int support_widgets = 0;
        for (size_t i = 0; i < n; i++) {
            assert(w[i].x >= 0 && w[i].y >= 0 && w[i].w >= 56 && w[i].h >= 36);
            assert(w[i].x + w[i].w <= 800 && w[i].y + w[i].h <= 404);
            struct xz_ui_info info = {0};
            assert(xz_ui_describe_widget(u, m, &w[i], &info) && info.title[0] && info.body[0]);
            if (xz_ui_support_kind(w[i].kind)) {
                assert(w[i].x >= 136 && w[i].y >= 48);
                support_widgets++;
            }
            for (size_t j = i + 1; j < n; j++) {
                assert(w[i].x + w[i].w <= w[j].x || w[j].x + w[j].w <= w[i].x ||
                       w[i].y + w[i].h <= w[j].y || w[j].y + w[j].h <= w[i].y);
            }
        }
        assert(support_widgets >= 5);
        assert(xz_ui_render(u, m, pixels, 800 * 480, 800));
    }
    struct xz_shell_menu menu;
    assert(xz_ui_menu_build(u, m, &menu) && menu.row_count > 0);
    assert(!strcmp(menu.title, "REPORT AN ISSUE") && menu.step_count == 3);
    int found_pending = 0;
    for (int i = 0; i < menu.line_count; i++) {
        if (strstr(menu.line[i].text, "GITHUB ISSUE: PENDING USER SUBMISSION")) found_pending = 1;
    }
    assert(found_pending);
}

int main(void) {
    struct xz_ui u;
    struct xz_ui_model m;
    memset(&m, 0, sizeof(m));
    xz_ui_init(&u);

    /* Reachable from ADVANCED (XZ_UI_SETTINGS) and NETWORK UPDATES (XZ_UI_UPDATES). */
    tap(&u, &m, XZ_UI_PANEL, XZ_UI_SETTINGS);
    assert(u.page == XZ_UI_SETTINGS);
    tap(&u, &m, XZ_UI_PANEL, XZ_UI_SUPPORT);
    assert(u.page == XZ_UI_SUPPORT && u.support.step == XZ_SUPPORT_STEP_PREPARE);
    check_step_geometry_and_menu(&u, &m);

    /* Select category & summary, then prepare draft. */
    tap(&u, &m, XZ_UI_SUPPORT_CATEGORY, 1);
    assert(u.support.category == 1);
    tap(&u, &m, XZ_UI_SUPPORT_SUMMARY, 2);
    assert(u.support.summary == 2);

    struct xz_ui_action act = tap(&u, &m, XZ_UI_SUPPORT_PREPARE, 0);
    struct xz_support_request_args req;
    assert(xz_ui_support_take_request(&u, &m, &act, &req));
    assert(req.op == XZ_SUPPORT_OP_PREPARE && req.category == 1 && req.summary == 2);
    assert(u.support.step == XZ_SUPPORT_STEP_PREVIEW);

    /* Populate synthetic prepared draft in model. */
    m.support.phase = XZ_SUPPORT_PREPARED;
    m.support.ready = 1;
    m.support.usb_writable = 1;
    m.support.clock_ready = 0;
    m.support.draft_revision = 1;
    m.support.payload_bytes = 2048;
    m.support.redactions = 7;
    m.support.page_index = 0;
    m.support.page_count = 3;
    strcpy(m.support.report_id, "r1234567890abcdef");
    strcpy(m.support.payload_sha256, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    strcpy(m.support.github_url, "https://xz-mods-support.will-026.workers.dev/r/r1234567890abcdef/github");
    strcpy(m.support.preview[0], "{\"schema\": \"xz-support-report/1\", \"mac\": \"<redacted-mac>\"}");
    check_step_geometry_and_menu(&u, &m);

    /* Paging preview works and clamps. */
    tap(&u, &m, XZ_UI_SUPPORT_PAGE, 1);
    assert(u.support.preview_page == 1);
    tap(&u, &m, XZ_UI_SUPPORT_PAGE, -1);
    assert(u.support.preview_page == 0);

    /* Sending or exporting without explicit SHA-256 consent is rejected! */
    act = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0);
    assert(!xz_ui_support_take_request(&u, &m, &act, &req));
    assert(u.support.step == XZ_SUPPORT_STEP_PREVIEW);
    assert(strstr(u.notice, "APPROVE"));

    /* Approve consent bound to exact SHA-256 and revision. */
    tap(&u, &m, XZ_UI_SUPPORT_CONSENT, 0);
    assert(xz_ui_support_consented(&u, &m));

    /* Mutating payload_sha256 or draft_revision invalidates consent automatically! */
    m.support.payload_sha256[0] = 'f';
    assert(!xz_ui_support_consented(&u, &m));
    act = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0);
    assert(!xz_ui_support_take_request(&u, &m, &act, &req));
    m.support.payload_sha256[0] = '0';
    assert(xz_ui_support_consented(&u, &m));

    /* Set reporting clock (for 2019 boot clock) without touching playback clock. */
    act = tap(&u, &m, XZ_UI_SUPPORT_CLOCK, 0);
    assert(!xz_ui_support_take_request(&u, &m, &act, &req));
    assert(u.support.clock_edit);
    check_step_geometry_and_menu(&u,&m);
    tap(&u,&m,XZ_UI_SUPPORT_CLOCK,2);
    assert(u.support.clock_field[0]==2027);
    act=tap(&u,&m,XZ_UI_SUPPORT_CLOCK,11);
    assert(xz_ui_support_take_request(&u,&m,&act,&req) && req.op==XZ_SUPPORT_OP_SET_CLOCK);
    assert(req.report_epoch==1824465600ULL && !u.support.clock_edit);
    m.support.clock_ready = 1;

    /* Send with valid consent emits exact expected_sha256 and advances to RECEIPT. */
    act = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0);
    assert(xz_ui_support_take_request(&u, &m, &act, &req));
    assert(req.op == XZ_SUPPORT_OP_SEND);
    assert(!strcmp(req.expected_sha256, m.support.payload_sha256));
    assert(u.support.step == XZ_SUPPORT_STEP_RECEIPT);

    m.support.phase = XZ_SUPPORT_STORED;
    memset(pixels, 0, sizeof(pixels));
    check_step_geometry_and_menu(&u, &m);

    /* Verify QR box rendered black & white modules in 604..780, 128..304. */
    int white = 0, black = 0;
    for (int y = 132; y < 300; y++) {
        for (int x = 608; x < 776; x++) {
            uint16_t p = pixels[y * 800 + x];
            if (p == 0xffff) white++;
            else if (p == 0x0000) black++;
        }
    }
    assert(white > 500 && black > 500);

    /* Verify CHECK RECEIPT action on OutcomeUnknown. */
    m.support.phase = XZ_SUPPORT_OUTCOME_UNKNOWN;
    act = tap(&u, &m, XZ_UI_SUPPORT_CHECK, 0);
    assert(xz_ui_support_take_request(&u, &m, &act, &req) && req.op == XZ_SUPPORT_OP_CHECK);

    /* Verify offline USB export uses exact approved SHA-256. */
    act = tap(&u, &m, XZ_UI_SUPPORT_EXPORT, 0);
    assert(xz_ui_support_take_request(&u, &m, &act, &req) && req.op == XZ_SUPPORT_OP_EXPORT);
    assert(!strcmp(req.expected_sha256, m.support.payload_sha256));

    puts("PASS support UI: 3-step wizard, all-theme geometry, exact SHA-256 consent guard, 2026 TLS clock action, QR render, receipt & offline export");
    return 0;
}
