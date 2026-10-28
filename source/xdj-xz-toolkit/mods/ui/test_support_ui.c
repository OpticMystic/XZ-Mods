/* SPDX-License-Identifier: MIT */
/* REPORT AN ISSUE on the XZ: the three-press paths (touch, encoder, the error strip), typing, BACK, every result,
   and every step's geometry and drawing in every theme. */
#include "ui_support.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#error Assertions required
#endif

_Static_assert(XZ_UI_SUPPORT == 34 && XZ_UI_HOME == 35 && XZ_UI_SCREEN_LOOK == 42 && XZ_UI_BAND_COLORS == 43 && XZ_UI_PAGE_COUNT == 44, "append-only page IDs: support, the MODS sections, LAMPS AND TITLES, then 3-BAND COLOURS");
_Static_assert(XZ_UI_SUPPORT_ALERT - XZ_UI_SUPPORT_STEP == 9, "REPORT AN ISSUE keeps its ten action slots");

static uint16_t pixels[800 * 480];

static int find(struct xz_ui *u, struct xz_ui_model *m, int kind, int index, struct xz_ui_widget *out) {
    struct xz_ui_widget w[XZ_UI_WIDGETS];
    size_t n = xz_ui_layout(u, m, w);
    assert(n <= XZ_UI_WIDGETS);
    for (size_t i = 0; i < n; i++) if ((int)w[i].kind == kind && w[i].index == index) { if (out) *out = w[i]; return 1; }
    return 0;
}

static size_t tap(struct xz_ui *u, struct xz_ui_model *m, int kind, int index, struct xz_ui_action *out) {
    struct xz_ui_widget w;
    if (!find(u, m, kind, index, &w)) { fprintf(stderr, "missing kind %d index %d on step %d\n", kind, index, u->support.step); assert(0); }
    struct xz_ui_action rel[XZ_UI_ACTIONS];
    size_t n = xz_ui_touch(u, m, w.x + w.w / 2, w.y + w.h / 2, 1, out);
    xz_ui_touch(u, m, 0, 0, 0, rel);
    return n;
}

static int sends(struct xz_ui *u, struct xz_ui_model *m, const struct xz_ui_action *a, size_t n, struct xz_support_request_args *req) {
    for (size_t i = 0; i < n; i++) if (xz_ui_support_take_request(u, m, &a[i], req)) return 1;
    return 0;
}

/* Every step: controls inside the content area, apart, explained; drawn in every theme. */
static void check_page(struct xz_ui *u, struct xz_ui_model *m) {
    int theme = m->theme;
    for (int t = 0; t < XZ_THEME_COUNT; t++) {
        m->theme = t;
        struct xz_ui_widget w[XZ_UI_WIDGETS];
        size_t n = xz_ui_layout(u, m, w);
        for (size_t i = 0; i < n; i++) {
            if (w[i].kind == XZ_UI_CLOSE && w[i].index == XZ_UI_CLOSE_COUNTER) continue;
            assert(w[i].x >= 0 && w[i].y >= 0 && w[i].x + w[i].w <= 800);
            if (xz_ui_support_kind(w[i].kind)) {
                assert(w[i].x >= XZ_UI_CX && w[i].y >= XZ_UI_CY && w[i].x + w[i].w <= XZ_UI_CX + XZ_UI_CW && w[i].y + w[i].h <= XZ_UI_CB);
                assert(w[i].w >= 56 && w[i].h >= 36);
            }
            struct xz_ui_info info = {0};
            assert(xz_ui_describe_widget(u, m, &w[i], &info) && info.title && info.title[0] && info.body && info.body[0]);
            for (size_t j = i + 1; j < n; j++)
                assert(w[i].x + w[i].w <= w[j].x || w[j].x + w[j].w <= w[i].x || w[i].y + w[i].h <= w[j].y || w[j].y + w[j].h <= w[i].y);
        }
        assert(xz_ui_render(u, m, pixels, 800 * 480, 800));
    }
    m->theme = theme;
    assert(!strcmp(xz_ui_page_title(u->page), "REPORT AN ISSUE"));
}

static void open_from_extras(struct xz_ui *u, struct xz_ui_model *m) {
    struct xz_ui_action a[XZ_UI_ACTIONS];
    xz_ui_init(u);
    tap(u, m, XZ_UI_PANEL, XZ_UI_EXTRAS, a);
    assert(u->page == XZ_UI_EXTRAS);
    tap(u, m, XZ_UI_PANEL, XZ_UI_SUPPORT, a);
    assert(u->page == XZ_UI_SUPPORT && u->support.step == XZ_UI_REPORT_TOPIC && u->support.entry == XZ_SUPPORT_FROM_EXTRAS);
}

int main(void) {
    struct xz_ui u;
    struct xz_ui_model m;
    struct xz_ui_action a[XZ_UI_ACTIONS];
    struct xz_support_request_args req;
    memset(&m, 0, sizeof m);
    snprintf(m.support.loader_id, sizeof m.support.loader_id, "L-4F7K-2M9Q");

    /* Touch: a topic, SKIP, SEND. Three presses. */
    open_from_extras(&u, &m);
    check_page(&u, &m);
    size_t n = tap(&u, &m, XZ_UI_SUPPORT_TOPIC, XZ_SUPPORT_NOT_WORKING, a);
    assert(!sends(&u, &m, a, n, &req) && u.support.step == XZ_UI_REPORT_TEXT);
    assert(u.focus_kind == XZ_UI_SUPPORT_SKIP && u.kb_shift);
    check_page(&u, &m);
    n = tap(&u, &m, XZ_UI_SUPPORT_SKIP, 0, a);
    assert(!sends(&u, &m, a, n, &req) && u.support.step == XZ_UI_REPORT_SEND && u.focus_kind == XZ_UI_SUPPORT_SEND);
    check_page(&u, &m);
    n = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0, a);
    assert(sends(&u, &m, a, n, &req) && req.op == XZ_SUPPORT_OP_REPORT && req.report.topic == XZ_SUPPORT_NOT_WORKING);
    assert(req.report.entry == XZ_SUPPORT_FROM_EXTRAS && !req.report.retry && !req.report.text[0]);
    assert(u.support.step == XZ_UI_REPORT_RESULT);

    /* The encoder: push, push, push, with no turn, lands on SEND. NOT SURE sends no topic. */
    open_from_extras(&u, &m);
    int presses = 0, sent = 0;
    while (!sent && presses < 3) {
        n = xz_ui_encoder_push(&u, &m, a);
        presses++;
        sent = sends(&u, &m, a, n, &req);
    }
    assert(sent && presses == 3 && req.report.topic == XZ_SUPPORT_GOT_WORSE);
    open_from_extras(&u, &m);
    tap(&u, &m, XZ_UI_SUPPORT_TOPIC, -1, a);
    tap(&u, &m, XZ_UI_SUPPORT_SKIP, 0, a);
    n = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0, a);
    assert(sends(&u, &m, a, n, &req) && req.report.topic == -1);

    /* Typing: a capital to start, then lower case; symbols; delete; the 280 limit. */
    open_from_extras(&u, &m);
    tap(&u, &m, XZ_UI_SUPPORT_TOPIC, XZ_SUPPORT_GOT_WORSE, a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, 'D', a);
    assert(!u.kb_shift);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, 'e', a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, 'c', a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, 'k', a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, ' ', a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, XZ_UI_KB_SYMBOLS, a);
    assert(u.kb_symbols);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, '!', a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, '?', a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, XZ_UI_KB_DELETE, a);
    assert(!strcmp(u.support.text, "Deck !"));
    check_page(&u, &m);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, XZ_UI_KB_SYMBOLS, a);
    while (strlen(u.support.text) < XZ_SUPPORT_TEXT_CHARS) tap(&u, &m, XZ_UI_SUPPORT_KEY, 'x', a);
    struct xz_ui_widget key;
    assert(find(&u, &m, XZ_UI_SUPPORT_KEY, 'x', &key) && !xz_ui_widget_ready(&u, &m, &key));
    tap(&u, &m, XZ_UI_SUPPORT_KEY, 'x', a);
    assert(strlen(u.support.text) == XZ_SUPPORT_TEXT_CHARS && strstr(u.notice, "280"));
    struct xz_ui_widget del;
    assert(find(&u, &m, XZ_UI_SUPPORT_KEY, XZ_UI_KB_DELETE, &del) && xz_ui_widget_ready(&u, &m, &del));
    check_page(&u, &m);
    n = tap(&u, &m, XZ_UI_SUPPORT_NEXT, 0, a);
    assert(u.support.step == XZ_UI_REPORT_SEND && !u.support.skipped);
    check_page(&u, &m);
    n = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0, a);
    assert(sends(&u, &m, a, n, &req) && strlen(req.report.text) == XZ_SUPPORT_TEXT_CHARS && !strncmp(req.report.text, "Deck !", 6));

    /* SKIP keeps the text here but does not send it; ADD TEXT brings it back. */
    open_from_extras(&u, &m);
    tap(&u, &m, XZ_UI_SUPPORT_TOPIC, XZ_SUPPORT_NEW_IDEA, a);
    tap(&u, &m, XZ_UI_SUPPORT_KEY, 'A', a);
    tap(&u, &m, XZ_UI_SUPPORT_SKIP, 0, a);
    assert(u.support.skipped && !strcmp(u.support.text, "A"));
    assert(find(&u, &m, XZ_UI_SUPPORT_STEP, XZ_UI_REPORT_TEXT, &key) && !strcmp(key.label, "ADD TEXT"));
    n = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0, a);
    assert(sends(&u, &m, a, n, &req) && !req.report.text[0] && req.report.topic == XZ_SUPPORT_NEW_IDEA);
    u.support.step = XZ_UI_REPORT_SEND;
    tap(&u, &m, XZ_UI_SUPPORT_STEP, XZ_UI_REPORT_TEXT, a);
    assert(u.support.step == XZ_UI_REPORT_TEXT && !u.support.skipped && !strcmp(u.support.text, "A") && u.focus_kind == XZ_UI_SUPPORT_NEXT);
    tap(&u, &m, XZ_UI_SUPPORT_NEXT, 0, a);
    tap(&u, &m, XZ_UI_SUPPORT_STEP, XZ_UI_REPORT_TOPIC, a);
    assert(u.support.step == XZ_UI_REPORT_TOPIC && u.focus_kind == XZ_UI_SUPPORT_TOPIC && u.focus_index == XZ_SUPPORT_NEW_IDEA);

    /* BACK walks the steps back, then leaves for EXTRAS. */
    u.support.step = XZ_UI_REPORT_SEND;
    xz_ui_back(&u, a);
    assert(u.page == XZ_UI_SUPPORT && u.support.step == XZ_UI_REPORT_TEXT);
    xz_ui_back(&u, a);
    assert(u.page == XZ_UI_SUPPORT && u.support.step == XZ_UI_REPORT_TOPIC);
    xz_ui_back(&u, a);
    assert(u.page == XZ_UI_EXTRAS && !strcmp(u.support.text, "A")); /* a draft survives leaving */

    /* Results. Sending: SEND is not ready, DONE leaves without losing the outcome. */
    tap(&u, &m, XZ_UI_PANEL, XZ_UI_SUPPORT, a);
    assert(u.page == XZ_UI_SUPPORT && !strcmp(u.support.text, "A") && u.support.step == XZ_UI_REPORT_TOPIC);
    tap(&u, &m, XZ_UI_SUPPORT_TOPIC, XZ_SUPPORT_HOW_DO_I, a);
    tap(&u, &m, XZ_UI_SUPPORT_NEXT, 0, a);
    n = tap(&u, &m, XZ_UI_SUPPORT_SEND, 0, a);
    assert(sends(&u, &m, a, n, &req) && !strcmp(req.report.text, "A"));
    m.support.phase = XZ_SUPPORT_WORKING;
    check_page(&u, &m);
    tap(&u, &m, XZ_UI_SUPPORT_DONE, 0, a);
    assert(u.page == XZ_UI_EXTRAS && u.support.step == XZ_UI_REPORT_RESULT);
    tap(&u, &m, XZ_UI_PANEL, XZ_UI_SUPPORT, a);
    assert(u.support.step == XZ_UI_REPORT_RESULT);

    /* Neither sent nor saved: TRY AGAIN sends the same report; EDIT REPORT keeps the text. */
    m.support.phase = XZ_SUPPORT_UNSAVED;
    snprintf(m.support.result, sizeof m.support.result, "READ_ONLY");
    check_page(&u, &m);
    n = tap(&u, &m, XZ_UI_SUPPORT_RETRY, 0, a);
    assert(sends(&u, &m, a, n, &req) && req.report.retry && !strcmp(req.report.text, "A") && req.report.topic == XZ_SUPPORT_HOW_DO_I);
    tap(&u, &m, XZ_UI_SUPPORT_EDIT, 0, a);
    assert(u.support.step == XZ_UI_REPORT_SEND && !strcmp(u.support.text, "A"));
    u.support.step = XZ_UI_REPORT_RESULT;
    m.support.phase = XZ_SUPPORT_SAVED;
    snprintf(m.support.result, sizeof m.support.result, "OFFLINE");
    check_page(&u, &m);

    /* Sent: the reference and its QR code, then DONE starts the next report afresh. */
    m.support.phase = XZ_SUPPORT_SENT;
    snprintf(m.support.reference, sizeof m.support.reference, "R-4F7K-2M9Q");
    snprintf(m.support.tracking_url, sizeof m.support.tracking_url, "https://xz-mods-support.will-026.workers.dev/r/R-4F7K-2M9Q");
    check_page(&u, &m);
    m.theme = 0;
    assert(xz_ui_render(&u, &m, pixels, 800 * 480, 800));
    int white = 0, black = 0;
    for (int y = 60; y < 248; y++) for (int x = 600; x < 788; x++) { white += pixels[y * 800 + x] == 0xffff; black += pixels[y * 800 + x] == 0; }
    assert(white > 3000 && black > 3000);
    tap(&u, &m, XZ_UI_SUPPORT_DONE, 0, a);
    assert(u.page == XZ_UI_EXTRAS && u.support.step == XZ_UI_REPORT_TOPIC && !u.support.text[0] && !u.support.topic);
    /* A finished report seen by leaving with BACK is also replaced when the page opens again. */
    u.support.step = XZ_UI_REPORT_RESULT;
    snprintf(u.support.text, sizeof u.support.text, "old");
    tap(&u, &m, XZ_UI_PANEL, XZ_UI_SUPPORT, a);
    assert(u.support.step == XZ_UI_REPORT_TOPIC && !u.support.text[0]);
    m.support.phase = XZ_SUPPORT_IDLE;

    /* The dock's error strip. MODERN, a counter error: REPORT THIS beside SHOW DECK, touch only, straight to WHAT
       HAPPENED with SOMETHING ISN'T WORKING; then SKIP and SEND. */
    for (int iface = 0; iface < 2; iface++) {
        struct xz_ui_model e = m;
        e.ui_interface = iface ? XZ_UI_INTERFACE_MODERN : XZ_UI_INTERFACE_CLASSIC;
        e.theme = iface ? XZ_THEME_FLAGSHIP : 0;
        e.modern_look.known = 1;
        if (iface) e.counter_error[1] = (struct xz_ui_counter_error){"E-8302", "CANNOT PLAY TRACK"};
        else e.usb_hub = "E-8307: HUBS ARE NOT SUPPORTED";
        xz_ui_init(&u);
        u.page = XZ_UI_PLAY;
        struct xz_ui_widget r, strip;
        assert(find(&u, &e, XZ_UI_SUPPORT_ALERT, 0, &r) && !xz_ui_focusable(&r));
        assert(r.y == XZ_UI_COUNTER_Y && r.h == XZ_UI_COUNTER_H && r.x + r.w <= XZ_UI_COUNTER_W);
        if (iface) assert(find(&u, &e, XZ_UI_CLOSE, XZ_UI_CLOSE_COUNTER, &strip) && r.x + r.w < strip.x + strip.w - 60);
        assert(xz_ui_render(&u, &e, pixels, 800 * 480, 800));
        int presses_alert = 0;
        xz_ui_touch(&u, &e, r.x + r.w / 2, r.y + r.h / 2, 1, a);xz_ui_touch(&u, &e, 0, 0, 0, a);presses_alert++;
        assert(u.page == XZ_UI_SUPPORT && u.support.step == XZ_UI_REPORT_TEXT && u.support.entry == XZ_SUPPORT_FROM_ALERT);
        assert(u.support.topic == XZ_SUPPORT_NOT_WORKING + 1 && u.focus_kind == XZ_UI_SUPPORT_SKIP);
        assert(!find(&u, &e, XZ_UI_SUPPORT_ALERT, 0, NULL)); /* not offered on the report itself */
        e.theme = iface ? XZ_THEME_FLAGSHIP : 0;
        assert(xz_ui_render(&u, &e, pixels, 800 * 480, 800));
        tap(&u, &e, XZ_UI_SUPPORT_SKIP, 0, a);presses_alert++;
        n = tap(&u, &e, XZ_UI_SUPPORT_SEND, 0, a);presses_alert++;
        assert(sends(&u, &e, a, n, &req) && presses_alert == 3 && req.report.entry == XZ_SUPPORT_FROM_ALERT && req.report.topic == XZ_SUPPORT_NOT_WORKING);
        /* BACK from the report returns to the page it came from. */
        u.support.step = XZ_UI_REPORT_TOPIC;
        xz_ui_back(&u, a);
        assert(u.page == XZ_UI_PLAY);
        /* No error, no button. */
        struct xz_ui_model calm = m;
        calm.ui_interface = e.ui_interface;
        assert(!find(&u, &calm, XZ_UI_SUPPORT_ALERT, 0, NULL));
    }

    puts("PASS support UI: topic+SKIP+SEND in three touches, three encoder pushes and three from the error strip; typing, 280 limit, SKIP keeps text, BACK steps, every result, all-theme geometry and drawing");
    return 0;
}
