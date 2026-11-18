#include "l10n.h"
/* SPDX-License-Identifier: MIT */
/* REPORT AN ISSUE on the XZ: one question per screen, then SEND. Everything the player can tell about itself is
   collected automatically by the support helper; the DJ only says what it is about and, if they like, what
   happened. Typing and steps stay in the UI; SEND and TRY AGAIN hand the runtime one request. */
#include "ui_support.h"
#include <stdio.h>
#include <string.h>

enum { X0 = XZ_UI_CX, CW = XZ_UI_CW, COL = 332, TILE_W = 324, TILE_H = 96, TILE_Y = 92, TILE_PITCH = 104,
       KEY_Y = 140, KEY_PITCH = 65, KEY_W = 60, KEY_H = 40, ROW = 44, BUTTON_Y = 360, BUTTON_H = 40,
       SEND_Y = 344, SEND_H = 52, CHANGE_W = 104 };

static const struct { const char *name, *line; } topic_text[XZ_SUPPORT_TOPICS] = {
    {"SOMETHING GOT WORSE", "It used to work, or worked better."},
    {"SOMETHING ISN'T WORKING", "Broken, stops, sounds or looks wrong."},
    {"IT SHOULD WORK DIFFERENTLY", "It works, but not the way you expect."},
    {"I HAVE A NEW IDEA", "Something XZ Mods does not do yet."},
    {"HOW DO I...?", "A question about using XZ Mods."},
};

int xz_ui_support_page(int page) { return page == XZ_UI_SUPPORT; }
int xz_ui_support_kind(int kind) { return kind >= XZ_UI_SUPPORT_STEP && kind <= XZ_UI_SUPPORT_ALERT; }
const char *xz_ui_support_title(int page) { (void)page; return "REPORT AN ISSUE"; }
const char *xz_ui_support_topic_name(int topic) {
    return topic >= 0 && topic < XZ_SUPPORT_TOPICS ? topic_text[topic].name : "NOT SURE";
}

static int step(const struct xz_ui *u) {
    int s = u->support.step;
    return s >= 0 && s < XZ_UI_REPORT_STEPS ? s : XZ_UI_REPORT_TOPIC;
}
/* enum xz_support_topic, -1 NOT SURE, -2 not chosen yet. */
static int topic(const struct xz_ui *u) {
    int t = u->support.topic;
    return t <= 0 ? -2 : t <= XZ_SUPPORT_TOPICS ? t - 1 : -1;
}
static int chars(const char *s) { return (int)strlen(s); }
static int blank(const char *s) { for (; s && *s; s++) if (*s != ' ') return 0; return 1; }
static int text_sent(const struct xz_ui *u) { return !u->support.skipped && !blank(u->support.text); }
static int phase(const struct xz_ui_model *m) { return (int)m->support.phase; }
static int finished(const struct xz_ui_model *m) { return phase(m) == XZ_SUPPORT_SENT || phase(m) == XZ_SUPPORT_SAVED; }

static void add(struct xz_ui_widget *w, size_t *n, int kind, int index, const char *label) {
    if (*n < XZ_UI_WIDGETS) w[(*n)++] = (struct xz_ui_widget){0, 0, 0, 0, (enum xz_ui_action_kind)kind, index, 0, label, -1};
}

size_t xz_ui_support_controls(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n) {
    switch (step(u)) {
    case XZ_UI_REPORT_TOPIC:
        for (int i = 0; i < XZ_SUPPORT_TOPICS; i++) add(w, &n, XZ_UI_SUPPORT_TOPIC, i, topic_text[i].name);
        add(w, &n, XZ_UI_SUPPORT_TOPIC, -1, xz_l10n_authored("NOT SURE"));
        break;
    case XZ_UI_REPORT_TEXT:
        for (int r = 0; r < 4; r++) {
            const char *row = xz_ui_kb_row(u, r);
            for (int k = 0; k < 10 && row[k]; k++) add(w, &n, XZ_UI_SUPPORT_KEY, xz_ui_kb_byte(u, row[k]), "");
        }
        add(w, &n, XZ_UI_SUPPORT_KEY, XZ_UI_KB_SHIFT, u->kb_shift ? xz_l10n_authored("SHIFT ON") : "SHIFT");
        add(w, &n, XZ_UI_SUPPORT_KEY, XZ_UI_KB_SYMBOLS, u->kb_symbols ? xz_l10n_authored("ABC") : "#+=");
        add(w, &n, XZ_UI_SUPPORT_KEY, ' ', xz_l10n_authored("SPACE"));
        add(w, &n, XZ_UI_SUPPORT_KEY, XZ_UI_KB_DELETE, xz_l10n_authored("DELETE"));
        add(w, &n, XZ_UI_SUPPORT_SKIP, 0, xz_l10n_authored("SKIP, I'LL ADD IT ON MY COMPUTER"));
        add(w, &n, XZ_UI_SUPPORT_NEXT, 0, xz_l10n_authored("NEXT"));
        break;
    case XZ_UI_REPORT_SEND:
        add(w, &n, XZ_UI_SUPPORT_STEP, XZ_UI_REPORT_TOPIC, xz_l10n_authored("CHANGE"));
        add(w, &n, XZ_UI_SUPPORT_STEP, XZ_UI_REPORT_TEXT, u->support.skipped || blank(u->support.text) ? xz_l10n_authored("ADD TEXT") : xz_l10n_authored("CHANGE"));
        add(w, &n, XZ_UI_SUPPORT_SEND, 0, xz_l10n_authored("SEND REPORT"));
        break;
    default:
        if (phase(m) == XZ_SUPPORT_UNSAVED || phase(m) == XZ_SUPPORT_ERROR) {
            add(w, &n, XZ_UI_SUPPORT_RETRY, 0, xz_l10n_authored("TRY AGAIN"));
            add(w, &n, XZ_UI_SUPPORT_EDIT, 0, xz_l10n_authored("EDIT REPORT"));
        } else add(w, &n, XZ_UI_SUPPORT_DONE, 0, xz_l10n_authored("DONE"));
        break;
    }
    return n;
}

static void put(struct xz_ui_widget *w, int x, int y, int width, int height) { w->x = x; w->y = y; w->w = width; w->h = height; }

/* Rows of the SEND step's summary, so the CHANGE buttons sit on the row they change. */
enum { ROW_ABOUT_Y = 90, ROW_TEXT_Y = 142 };

void xz_ui_support_place(const struct xz_ui *u, const struct xz_ui_model *m, struct xz_ui_widget *w, size_t n) {
    (void)m;
    if (u->page != XZ_UI_SUPPORT) return;
    for (size_t i = 0; i < n; i++) {
        struct xz_ui_widget *a = &w[i];
        int k = a->index;
        switch (a->kind) {
        case XZ_UI_SUPPORT_TOPIC: {
            int slot = k < 0 ? XZ_SUPPORT_TOPICS : k;
            put(a, X0 + (slot % 2) * COL, TILE_Y + (slot / 2) * TILE_PITCH, TILE_W, TILE_H);
            break;
        }
        case XZ_UI_SUPPORT_KEY:
            if (k == XZ_UI_KB_SHIFT) put(a, X0, KEY_Y + 4 * ROW, 125, KEY_H);
            else if (k == XZ_UI_KB_SYMBOLS) put(a, X0 + 130, KEY_Y + 4 * ROW, 125, KEY_H);
            else if (k == ' ') put(a, X0 + 260, KEY_Y + 4 * ROW, 255, KEY_H);
            else if (k == XZ_UI_KB_DELETE) put(a, X0 + 520, KEY_Y + 4 * ROW, 136, KEY_H);
            else {
                int slot = xz_ui_kb_slot(u, k);
                if (slot < 0) slot = 0;
                put(a, X0 + (slot % 10) * KEY_PITCH, KEY_Y + (slot / 10) * ROW, KEY_W, KEY_H);
            }
            break;
        case XZ_UI_SUPPORT_SKIP: put(a, X0, BUTTON_Y, TILE_W, BUTTON_H); break;
        case XZ_UI_SUPPORT_NEXT: put(a, X0 + COL, BUTTON_Y, TILE_W, BUTTON_H); break;
        case XZ_UI_SUPPORT_STEP: put(a, X0 + CW - CHANGE_W, k == XZ_UI_REPORT_TOPIC ? ROW_ABOUT_Y : ROW_TEXT_Y, CHANGE_W, 36); break;
        case XZ_UI_SUPPORT_SEND: put(a, X0, SEND_Y, CW, SEND_H); break;
        case XZ_UI_SUPPORT_RETRY: case XZ_UI_SUPPORT_DONE: put(a, X0, SEND_Y, TILE_W, SEND_H); break;
        case XZ_UI_SUPPORT_EDIT: put(a, X0 + COL, SEND_Y, TILE_W, SEND_H); break;
        default: break;
        }
    }
}

int xz_ui_support_selected(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w) {
    (void)m;
    if (w->kind == XZ_UI_SUPPORT_TOPIC) return topic(u) == w->index;
    if (w->kind == XZ_UI_SUPPORT_KEY) return (w->index == XZ_UI_KB_SHIFT && u->kb_shift) || (w->index == XZ_UI_KB_SYMBOLS && u->kb_symbols);
    return 0;
}

int xz_ui_support_ready(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w) {
    if (w->kind == XZ_UI_SUPPORT_KEY && w->index >= 32) return chars(u->support.text) < XZ_SUPPORT_TEXT_CHARS;
    if (w->kind == XZ_UI_SUPPORT_SEND || w->kind == XZ_UI_SUPPORT_RETRY) return phase(m) != XZ_SUPPORT_WORKING;
    return 1;
}

/* Which button the DJ most likely wants next is drawn filled. */
static int primary(const struct xz_ui *u, const struct xz_ui_widget *w) {
    switch (w->kind) {
    case XZ_UI_SUPPORT_SKIP: return blank(u->support.text);
    case XZ_UI_SUPPORT_NEXT: return !blank(u->support.text);
    case XZ_UI_SUPPORT_SEND: case XZ_UI_SUPPORT_RETRY: case XZ_UI_SUPPORT_DONE: return 1;
    default: return 0;
    }
}

static void focus(struct xz_ui *u, int kind, int index) {
    u->focus_kind = (enum xz_ui_action_kind)kind;
    u->focus_index = index;
    u->focus_deck = -1;
    u->adjusting = 0;
}
static void go(struct xz_ui *u, int to) {
    u->support.step = to;
    u->notice[0] = 0;
    u->capture = -1;
    if (to == XZ_UI_REPORT_TOPIC) focus(u, XZ_UI_SUPPORT_TOPIC, topic(u) >= -1 ? topic(u) : 0);
    else if (to == XZ_UI_REPORT_TEXT) {
        u->kb_symbols = 0;
        if (!u->support.text[0]) u->kb_shift = 1;
        focus(u, blank(u->support.text) ? XZ_UI_SUPPORT_SKIP : XZ_UI_SUPPORT_NEXT, 0);
    } else if (to == XZ_UI_REPORT_SEND) focus(u, XZ_UI_SUPPORT_SEND, 0);
    else focus(u, XZ_UI_NONE, 0);
}
static void fresh(struct xz_ui *u, int entry) {
    memset(&u->support, 0, sizeof u->support);
    u->support.entry = entry;
    u->support.from = entry == XZ_SUPPORT_FROM_UPDATES ? XZ_UI_UPDATES : XZ_UI_EXTRAS;
}

void xz_ui_support_opened(struct xz_ui *u, const struct xz_ui_model *m, enum xz_ui_page from) {
    if (!u || u->page != XZ_UI_SUPPORT) return;
    if (step(u) == XZ_UI_REPORT_RESULT && finished(m)) fresh(u, XZ_SUPPORT_FROM_EXTRAS);
    if (step(u) == XZ_UI_REPORT_TOPIC && topic(u) == -2 && !u->support.text[0]) {
        u->support.entry = from == XZ_UI_UPDATES ? XZ_SUPPORT_FROM_UPDATES : XZ_SUPPORT_FROM_EXTRAS;
        u->support.from = (int)from;
    }
    go(u, step(u));
}

int xz_ui_support_back(struct xz_ui *u) {
    if (!u || u->page != XZ_UI_SUPPORT) return 0;
    switch (step(u)) {
    case XZ_UI_REPORT_TEXT: go(u, XZ_UI_REPORT_TOPIC); return 1;
    case XZ_UI_REPORT_SEND: go(u, XZ_UI_REPORT_TEXT); return 1;
    default: return 0;
    }
}

static struct xz_ui_action press(int kind, int index, float value) {
    return (struct xz_ui_action){.kind = (enum xz_ui_action_kind)kind, .phase = XZ_UI_PRESS, .deck = -1, .index = index,
                                 .value = value, .pc_target = -1};
}

size_t xz_ui_support_action(struct xz_ui *u, const struct xz_ui_model *m, int kind, int index, struct xz_ui_action *out) {
    out[0] = press(kind, index, 0.0f);
    u->capture = -1;
    switch (kind) {
    case XZ_UI_SUPPORT_TOPIC:
        u->support.topic = index >= 0 && index < XZ_SUPPORT_TOPICS ? index + 1 : XZ_SUPPORT_TOPICS + 1;
        go(u, XZ_UI_REPORT_TEXT);
        return 1;
    case XZ_UI_SUPPORT_KEY: {
        char *text = u->support.text;
        size_t len = strlen(text);
        if (index == XZ_UI_KB_SHIFT) u->kb_shift = !u->kb_shift;
        else if (index == XZ_UI_KB_SYMBOLS) u->kb_symbols = !u->kb_symbols;
        else if (index == XZ_UI_KB_DELETE) { if (len) text[len - 1] = 0; }
        else if (index >= 32 && index < 127) {
            if (len >= XZ_SUPPORT_TEXT_CHARS) { snprintf(u->notice, sizeof u->notice, "280 CHARACTERS IS THE MOST. ADD MORE ON YOUR COMPUTER."); return 1; }
            text[len] = (char)index;
            text[len + 1] = 0;
            /* A capital to start, then lower case, as on a phone. */
            if (u->kb_shift && index >= 'A' && index <= 'Z') u->kb_shift = 0;
        }
        u->support.skipped = 0;
        return 1;
    }
    case XZ_UI_SUPPORT_SKIP:
        u->support.skipped = 1;
        go(u, XZ_UI_REPORT_SEND);
        return 1;
    case XZ_UI_SUPPORT_NEXT:
        u->support.skipped = blank(u->support.text);
        go(u, XZ_UI_REPORT_SEND);
        return 1;
    case XZ_UI_SUPPORT_STEP:
        if (index >= XZ_UI_REPORT_TOPIC && index <= XZ_UI_REPORT_SEND) go(u, index);
        if (index == XZ_UI_REPORT_TEXT) u->support.skipped = 0;
        return 1;
    case XZ_UI_SUPPORT_EDIT:
        go(u, XZ_UI_REPORT_SEND);
        return 1;
    case XZ_UI_SUPPORT_SEND:
    case XZ_UI_SUPPORT_RETRY:
        if (phase(m) == XZ_SUPPORT_WORKING) { snprintf(u->notice, sizeof u->notice, "STILL SENDING THE LAST REPORT"); return 1; }
        go(u, XZ_UI_REPORT_RESULT);
        out[0].value = 1.0f;
        return 1;
    case XZ_UI_SUPPORT_DONE:
        /* While it is still sending the page keeps its place, so coming back shows how it went. */
        if (phase(m) != XZ_SUPPORT_WORKING) fresh(u, XZ_SUPPORT_FROM_EXTRAS);
        return xz_ui_back(u, out);
    case XZ_UI_SUPPORT_ALERT: {
        /* The error on the strip is what it is about: straight to WHAT HAPPENED. A report still on its way keeps
           the page; text already typed is kept. */
        int busy = step(u) == XZ_UI_REPORT_RESULT && (phase(m) == XZ_SUPPORT_WORKING || phase(m) == XZ_SUPPORT_UNSAVED);
        int from = (int)u->page;
        if (u->page != XZ_UI_SUPPORT) xz_ui_navigate(u, XZ_UI_SUPPORT);
        if (busy) { go(u, XZ_UI_REPORT_RESULT); return 1; }
        if (step(u) == XZ_UI_REPORT_RESULT || !u->support.text[0]) fresh(u, XZ_SUPPORT_FROM_ALERT);
        u->support.entry = XZ_SUPPORT_FROM_ALERT;
        u->support.from = from;
        u->support.topic = XZ_SUPPORT_NOT_WORKING + 1;
        go(u, XZ_UI_REPORT_TEXT);
        return 1;
    }
    default:
        return 1;
    }
}

int xz_ui_support_take_request(struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_action *a, struct xz_support_request_args *out) {
    (void)m;
    if (!u || !a || !out || a->phase != XZ_UI_PRESS || a->value <= 0.0f) return 0;
    if (a->kind != XZ_UI_SUPPORT_SEND && a->kind != XZ_UI_SUPPORT_RETRY) return 0;
    memset(out, 0, sizeof *out);
    out->op = XZ_SUPPORT_OP_REPORT;
    out->report.topic = topic(u) >= 0 ? topic(u) : -1;
    out->report.entry = u->support.entry;
    out->report.retry = a->kind == XZ_UI_SUPPORT_RETRY;
    out->report.mods_page = u->support.from >= 0 && u->support.from < XZ_UI_PAGE_COUNT ? u->support.from : -1;
    if (text_sent(u)) snprintf(out->report.text, sizeof out->report.text, "%s", u->support.text);
    return 1;
}

const char *xz_ui_support_describe(const struct xz_ui *u, const struct xz_ui_model *m, const struct xz_ui_widget *w, const char **title) {
    (void)u;
    int k = w->index;
    switch (w->kind) {
    case XZ_UI_SUPPORT_TOPIC:
        if (k < 0) return "Skips the question. Your report still goes, and the details tell the rest.";
        return topic_text[k >= 0 && k < XZ_SUPPORT_TOPICS ? k : 0].line;
    case XZ_UI_SUPPORT_KEY:
        *title = k == XZ_UI_KB_SHIFT ? "SHIFT" : k == XZ_UI_KB_SYMBOLS ? "SYMBOLS" : k == XZ_UI_KB_DELETE ? "DELETE" : k == ' ' ? "SPACE" : "KEY";
        return k == XZ_UI_KB_SHIFT ? "Capital letters on or off." : k == XZ_UI_KB_SYMBOLS ? "Switches between letters and symbols." :
               k == XZ_UI_KB_DELETE ? "Deletes the last character." : "Types this character. Up to 280 in all.";
    case XZ_UI_SUPPORT_SKIP: return "Sends it without words. Your computer asks what happened when you plug in the stick or open XZ Mods.";
    case XZ_UI_SUPPORT_NEXT: return "Shows what will be sent, with SEND.";
    case XZ_UI_SUPPORT_STEP: return k == XZ_UI_REPORT_TOPIC ? "Choose another topic." : "Type what happened, or change it.";
    case XZ_UI_SUPPORT_SEND: return "Sends it to the XZ Mods inbox when the XZ is online, or saves it on this stick for your computer.";
    case XZ_UI_SUPPORT_RETRY: return "Sends the same report again. Nothing needs typing again.";
    case XZ_UI_SUPPORT_EDIT: return "Back to the report, with your text kept.";
    case XZ_UI_SUPPORT_DONE: return m->support.phase == XZ_SUPPORT_WORKING ? "Leaves this page. The report keeps sending; come back to see how it went." :
        "Closes the report. Your computer reminds you to finish it.";
    case XZ_UI_SUPPORT_ALERT: return "Reports the error shown here.";
    default: return "Report a problem or an idea from the player.";
    }
}

static const char *saved_reason(const char *why) {
    if (!strcmp(why, "NO_IDENTITY")) return "This loader is from an older XZ Mods app, so the report went to the stick.";
    if (!strcmp(why, "NEEDS_CLOCK")) return "The XZ's clock is not set, so it could not connect safely.";
    if (!strcmp(why, "REJECTED")) return "The XZ Mods inbox did not take it from this stick. Your computer will send it.";
    if (!strcmp(why, "UNKNOWN")) return "The connection dropped while sending. If it arrived, your computer counts it once.";
    return "The XZ is not connected to the internet.";
}
static const char *unsaved_reason(const char *why) {
    if (!strcmp(why, "NO_STICK")) return "No XZ Mods stick found. Put it back in, then push TRY AGAIN.";
    if (!strcmp(why, "READ_ONLY")) return "This stick is read-only. Use a writable XZ Mods stick, then push TRY AGAIN.";
    if (!strcmp(why, "FULL")) return "This stick is full. Make room on it with your computer, then push TRY AGAIN.";
    if (!strcmp(why, "LIMIT")) return "This stick holds 32 unsent reports. Plug it into your computer first.";
    return "It could not be sent or saved. Push TRY AGAIN.";
}

const char *xz_ui_support_footer(const struct xz_ui *u, const struct xz_ui_model *m) {
    if (u->notice[0]) return u->notice;
    switch (step(u)) {
    case XZ_UI_REPORT_TOPIC: return "TAP A TOPIC, OR TURN AND PUSH";
    case XZ_UI_REPORT_TEXT: return "OPTIONAL / UP TO 280 CHARACTERS";
    case XZ_UI_REPORT_SEND: return "NOTHING IS SENT UNTIL YOU PUSH SEND";
    default: return phase(m) == XZ_SUPPORT_WORKING ? "SENDING / YOU CAN KEEP PLAYING" : m->support.status[0] ? m->support.status : "REPORT AN ISSUE";
    }
}

/* ---- drawing ---- */
struct look { uint32_t bg, ink, dim, accent, alarm, panel, edge; };
static struct look colors(const struct xz_ui_scene *s) {
    struct look l;
    xz_ui_chrome_colors(s->m, s->p, &l.bg, &l.ink, &l.accent);
    l.dim = xz_blend(l.bg, l.ink, 160);
    l.alarm = s->p->alarm;
    l.panel = xz_blend(l.bg, l.ink, 26);
    l.edge = xz_blend(l.bg, l.ink, 70);
    return l;
}
static void label(struct xz_canvas c, int x, int y, int w, const char *text, int scale, uint32_t color) {
    if (scale > 1 && xz_text_width(c, text, scale, 200) > w) scale = 1;
    xz_ui_chrome_fit(c, x, y, w, text, scale, color);
}
/* Word wrap into at most `lines` rows; returns rows used. */
static int wrap(struct xz_canvas c, int x, int y, int width, int lines, int pitch, int scale, const char *text, uint32_t color) {
    char row[320];
    int used = 0;
    while (text && *text && used < lines) {
        while (*text == ' ') text++;
        size_t fit = 0, space = 0, n = strlen(text);
        for (size_t k = 1; k <= n && k < sizeof row; k++) {
            memcpy(row, text, k);
            row[k] = 0;
            if (xz_text_width(c, row, scale, 300) > width) break;
            fit = k;
            if (text[k] == ' ' || !text[k]) space = k;
        }
        if (!fit) break;
        size_t take = fit == n ? fit : space ? space : fit;
        if (used == lines - 1 && take < n) {
            /* Last row: end with an ellipsis. */
            for (size_t cut = take;; cut--) {
                snprintf(row, sizeof row, "%.*s...", (int)cut, text);
                if (!cut || xz_text_width(c, row, scale, 300) <= width) break;
            }
            xz_text(c, x, y + used * pitch, row, scale, 300, color);
            return used + 1;
        }
        memcpy(row, text, take);
        row[take] = 0;
        xz_text(c, x, y + used * pitch, row, scale, 300, color);
        text += take;
        used++;
    }
    return used;
}
static void heading(struct xz_canvas c, const struct look *l, const char *text, int current) {
    label(c, X0, 56, 440, text, 2, l->ink);
    if (current < 0) return;
    char where[16];
    snprintf(where, sizeof where, "STEP %d OF 3", current + 1);
    int x = X0 + CW - 3 * 30 + 4;
    xz_text(c, x - 12 - xz_text_width(c, where, 1, 16), 59, where, 1, 16, l->dim);
    for (int i = 0; i < 3; i++)
        xz_round_rect(c, x + i * 30, 62, 26, 6, 3, 15, i == current ? l->accent : i < current ? xz_blend(l->bg, l->accent, 130) : l->edge);
}
static void button(struct xz_canvas c, const struct xz_ui_widget *a, const struct look *l, int filled, int selected, int ready, const char *text) {
    uint32_t fill = !ready ? xz_blend(l->bg, l->ink, 12) : filled ? l->accent : selected ? xz_blend(l->bg, l->accent, 70) : l->panel;
    xz_round_rect(c, a->x, a->y, a->w, a->h, 8, 15, fill);
    if (selected && !filled) xz_border(c, a->x + 2, a->y + 2, a->w - 4, a->h - 4, l->accent);
    int scale = a->h >= 40 && xz_text_width(c, text, 2, 60) <= a->w - 20 ? 2 : 1, tw = xz_text_width(c, text, scale, 60);
    uint32_t ink = !ready ? l->dim : filled ? (xz_theme_luma(l->accent) > 150 ? 0x101010 : 0xffffff) : l->ink;
    xz_text(c, a->x + (a->w - tw) / 2, a->y + (a->h - 7 * scale) / 2 - (scale > 1 ? 2 : 0), text, scale, 60, ink);
}

static void topic_step(const struct xz_ui_scene *s, const struct look *l) {
    struct xz_canvas c = s->c;
    heading(c, l, "WHAT'S THIS ABOUT?", 0);
    for (size_t i = 0; i < s->n; i++) {
        const struct xz_ui_widget *a = &s->w[i];
        if (a->kind != XZ_UI_SUPPORT_TOPIC) continue;
        int chosen = topic(s->u) == a->index;
        xz_round_rect(c, a->x, a->y, a->w, a->h, 10, 15, chosen ? xz_blend(l->bg, l->accent, 70) : l->panel);
        xz_round_rect(c, a->x, a->y + 14, 4, a->h - 28, 2, 15, a->index < 0 ? l->edge : l->accent);
        label(c, a->x + 18, a->y + 24, a->w - 30, a->label, 2, l->ink);
        label(c, a->x + 18, a->y + 60, a->w - 30, a->index < 0 ? "Skip the question. The report still goes." :
              topic_text[a->index].line, 1, l->dim);
    }
}

static void text_step(const struct xz_ui_scene *s, const struct look *l) {
    struct xz_canvas c = s->c;
    const struct xz_ui *u = s->u;
    heading(c, l, "WHAT HAPPENED?", 1);
    int hw = xz_text_width(c, "WHAT HAPPENED?", 2, 40);
    if (hw > 440) hw = 440;
    xz_round_rect(c, X0 + hw + 12, 58, xz_text_width(c, "OPTIONAL", 1, 20) + 16, 18, 9, 15, xz_blend(l->bg, l->ink, 40));
    xz_text(c, X0 + hw + 20, 62, xz_l10n_authored("OPTIONAL"), 1, 20, l->ink);
    /* The box: the end of what is typed, with a caret, and the count. */
    xz_round_rect(c, X0, 84, CW, 50, 8, 15, xz_blend(l->bg, l->accent, 120));
    xz_round_rect(c, X0 + 2, 86, CW - 4, 46, 7, 15, xz_blend(l->bg, l->ink, 10));
    char count[16];
    snprintf(count, sizeof count, "%d/280", chars(u->support.text));
    int cw = xz_text_width(c, count, 1, 16);
    xz_text(c, X0 + CW - 12 - cw, 119, count, 1, 16, chars(u->support.text) >= XZ_SUPPORT_TEXT_CHARS ? l->alarm : l->dim);
    if (!u->support.text[0]) {
        label(c, X0 + 12, 92, CW - 24, "Type what you saw or heard, if you like.", 1, l->dim);
        label(c, X0 + 12, 106, CW - 36 - cw, "Or skip it and add it on your computer.", 1, l->dim);
        return;
    }
    /* Wrap everything, show the last three rows. */
    char shown[XZ_SUPPORT_TEXT_CHARS + 2];
    snprintf(shown, sizeof shown, "%s_", u->support.text);
    const char *rows[64];
    int lens[64], n = 0, width = CW - 36 - cw;
    const char *p = shown;
    while (*p && n < 64) {
        char row[XZ_SUPPORT_TEXT_CHARS + 2];
        size_t fit = 0, space = 0, len = strlen(p);
        for (size_t k = 1; k <= len; k++) {
            memcpy(row, p, k);
            row[k] = 0;
            if (xz_text_width(c, row, 1, 300) > width) break;
            fit = k;
            if (p[k] == ' ') space = k + 1;
        }
        if (!fit) break;
        size_t take = fit == len ? fit : space ? space : fit;
        rows[n] = p;
        lens[n++] = (int)take;
        p += take;
    }
    int first = n > 3 ? n - 3 : 0;
    for (int i = first; i < n; i++) {
        char row[XZ_SUPPORT_TEXT_CHARS + 2];
        snprintf(row, sizeof row, "%.*s", lens[i], rows[i]);
        xz_text(c, X0 + 12, 91 + (i - first) * 14, row, 1, 300, l->ink);
    }
}

/* One row of the summary: a dim name on the left, the value beside it. Returns the row's height. */
static int summary_row(struct xz_canvas c, const struct look *l, int y, const char *name, const char *value, int lines, uint32_t color, int room) {
    label(c, X0, y + 3, 150, name, 1, l->dim);
    return 14 * wrap(c, X0 + 160, y + 3, room, lines, 14, 1, value, color);
}

static void send_step(const struct xz_ui_scene *s, const struct look *l) {
    struct xz_canvas c = s->c;
    const struct xz_ui *u = s->u;
    const struct xz_ui_model *m = s->m;
    heading(c, l, "READY TO SEND", 2);
    int room = CW - 160 - CHANGE_W - 12;
    xz_rect(c, X0, ROW_ABOUT_Y - 6, CW, 1, l->edge);
    label(c, X0, ROW_ABOUT_Y + 3, 150, "ABOUT", 1, l->dim);
    label(c, X0 + 160, ROW_ABOUT_Y + 2, room, xz_ui_support_topic_name(topic(u) >= 0 ? topic(u) : -1), 2, l->ink);
    xz_rect(c, X0, ROW_TEXT_Y - 8, CW, 1, l->edge);
    if (text_sent(u)) summary_row(c, l, ROW_TEXT_Y, "WHAT HAPPENED", u->support.text, 3, l->ink, room);
    else summary_row(c, l, ROW_TEXT_Y, "WHAT HAPPENED", "You add it on your computer.", 1, l->dim, room);
    int y = ROW_TEXT_Y + 52;
    xz_rect(c, X0, y - 8, CW, 1, l->edge);
    /* The errors on screen go along as codes. Their wording is the player's own, shown here only. */
    char alert[160] = "";
    for (int d = 0; d < 2; d++) if (m->counter_error[d].code && xz_ui_counter_errors(m) & (1u << d)) {
        size_t used = strlen(alert);
        snprintf(alert + used, sizeof alert - used, "%s%s on deck %d", used ? ", " : "", m->counter_error[d].code, d + 1);
    }
    if (xz_ui_stock_window_notice(m)) {
        size_t used = strlen(alert);
        snprintf(alert + used, sizeof alert - used, "%s%s", used ? ", " : "", m->stock_window);
    }
    if (alert[0]) { y += 6 + summary_row(c, l, y, "ERROR ON SCREEN", alert, 1, l->alarm, CW - 160); xz_rect(c, X0, y + 2, CW, 1, l->edge); y += 10; }
    char also[240];
    snprintf(also, sizeof also, "The XZ Mods version, your settings, %s%s, the recent USB diary and the screen you were on. "
             "No music, track names or file names.", m->support.loader_id[0] ? "loader " : "", m->support.loader_id[0] ? m->support.loader_id : "this stick's loader");
    y += 6 + summary_row(c, l, y, "ALSO INCLUDED", also, 3, l->ink, CW - 160);
    xz_rect(c, X0, y + 2, CW, 1, l->edge);
    y += 10;
    summary_row(c, l, y, "IT GOES TO", "The XZ Mods inbox when the XZ is online, otherwise this stick for your computer. Only the maintainer reads it.",
                2, l->dim, CW - 160);
}

/* Text twice the size of the large face: drawn small off screen, then each pixel doubled. */
static void big_text(struct xz_canvas c, int x, int y, const char *text, uint32_t color, uint32_t bg) {
    enum { W = 320, H = 28 };
    static uint16_t buffer[W * H];
    struct xz_canvas off = {buffer, W, W, H, c.theme};
    xz_rect(off, 0, 0, W, H, bg);
    xz_text(off, 0, 4, text, 2, 40, color);
    int width = xz_text_width(off, text, 2, 40);
    for (int py = 0; py < H; py++) for (int px = 0; px < width && px < W; px++) {
        uint16_t v = buffer[py * W + px];
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
            int tx = x + px * 2 + dx, ty = y + py * 2 + dy;
            if (tx >= 0 && ty >= 0 && tx < c.width && ty < c.height) c.p[(size_t)ty * c.stride + (size_t)tx] = v;
        }
    }
}

static void result_step(const struct xz_ui_scene *s, const struct look *l) {
    struct xz_canvas c = s->c;
    const struct xz_ui_model *m = s->m;
    switch (phase(m)) {
    case XZ_SUPPORT_SENT:
        /* No code to scan: a loader has no tracking credential. The reference is what the DJ will see again. */
        heading(c, l, "SENT", -1);
        label(c, X0, 100, CW, "YOUR REFERENCE", 1, l->dim);
        big_text(c, X0 - 2, 112, m->support.reference[0] ? m->support.reference : "R-....-....", l->ink, l->bg);
        label(c, X0, 186, CW, "Finish this on your computer: XZ Mods will remind you.", 2, l->ink);
        wrap(c, X0, 222, CW, 2, 14, 1, "Open XZ Mods or plug in this stick to add what happened. The reference above is the same there.", l->dim);
        break;
    case XZ_SUPPORT_SAVED:
        heading(c, l, "SAVED ON THIS STICK", -1);
        wrap(c, X0, 100, CW, 2, 24, 2, "Plug it into your computer and XZ Mods will open it.", l->ink);
        wrap(c, X0, 162, CW, 2, 14, 1, saved_reason(m->support.result), l->dim);
        wrap(c, X0, 200, CW, 2, 14, 1, "Nothing needs typing again: the report is kept until your computer sends it.", l->dim);
        break;
    case XZ_SUPPORT_UNSAVED:
    case XZ_SUPPORT_ERROR:
        heading(c, l, "NOT SENT YET", -1);
        wrap(c, X0, 100, CW, 2, 24, 2, phase(m) == XZ_SUPPORT_ERROR ? "The report could not start. Push TRY AGAIN." :
             unsaved_reason(m->support.result), l->alarm);
        wrap(c, X0, 162, CW, 2, 14, 1, "Your report and your text are kept here until it goes.", l->dim);
        break;
    default: {
        heading(c, l, "SENDING", -1);
        unsigned dots = (m->clock_ms / 400u) % 4u;
        char line[32];
        snprintf(line, sizeof line, "Sending your report%.*s", (int)dots, "...");
        label(c, X0, 108, CW, line, 2, l->ink);
        wrap(c, X0, 146, CW, 2, 14, 1, "This takes up to half a minute. Keep playing; you can close MODS.", l->dim);
        break;
    }
    }
}

void xz_ui_support_render(const struct xz_ui_scene *s) {
    const struct xz_ui *u = s->u;
    struct look l = colors(s);
    struct xz_canvas c = s->c;
    xz_rect(c, X0, XZ_UI_CY, CW, XZ_UI_CB - XZ_UI_CY, l.bg);
    switch (step(u)) {
    case XZ_UI_REPORT_TOPIC: topic_step(s, &l); break;
    case XZ_UI_REPORT_TEXT: text_step(s, &l); break;
    case XZ_UI_REPORT_SEND: send_step(s, &l); break;
    default: result_step(s, &l); break;
    }
    for (size_t i = 0; i < s->n; i++) {
        const struct xz_ui_widget *a = &s->w[i];
        if (!xz_ui_support_kind(a->kind) || a->kind == XZ_UI_SUPPORT_TOPIC || a->kind == XZ_UI_SUPPORT_ALERT) continue;
        int ready = xz_ui_support_ready(u, s->m, a);
        if (a->kind == XZ_UI_SUPPORT_KEY) {
            int k = a->index;
            char glyph[2] = {(char)k, 0};
            button(c, a, &l, 0, xz_ui_support_selected(u, s->m, a), ready, k >= 33 && k < 127 ? glyph : a->label);
            continue;
        }
        button(c, a, &l, primary(u, a), 0, ready, a->label);
    }
}
