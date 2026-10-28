#define _GNU_SOURCE
#include "../support/native_support.h"
#include "../support/loader_identity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef NDEBUG
#error Assertions required
#endif
static char scratch[512];
static void mode(const char *m) {
    char path[600];
    snprintf(path, sizeof path, "%s/report-mode", scratch);
    FILE *f = fopen(path, "w");
    assert(f && fputs(m, f) >= 0 && !fclose(f));
}
static void last_request(char *out, size_t cap) {
    char path[600];
    snprintf(path, sizeof path, "%s/last-request", scratch);
    FILE *f = fopen(path, "r");
    assert(f);
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = 0;
    fclose(f);
}
static void field(const char *request, const char *key, char *out, size_t cap) {
    const char *p = strstr(request, key);
    assert(p);
    p += strlen(key);
    size_t n = strcspn(p, "\n");
    assert(n < cap);
    memcpy(out, p, n);
    out[n] = 0;
}
static struct xz_support_model report(int retry, const char *text) {
    struct xz_support_request_args request = {.op = XZ_SUPPORT_OP_REPORT};
    request.report = (struct xz_support_report){.topic = XZ_SUPPORT_NOT_WORKING, .entry = XZ_SUPPORT_FROM_ALERT, .retry = retry,
        .ui_interface = 1, .before = XZ_SUPPORT_AT_BROWSE, .deck_view = 2, .alert = {8302, 1234, 8307}, .have_settings = 1,
        .settings = {1, 0, 1, 3, 2, 0}};
    snprintf(request.report.text, sizeof request.report.text, "%s", text);
    struct xz_support_model model = {0};
    assert(!xz_support_request(&request));
    xz_support_read(&model, 0);
    for (int i = 0; i < 400 && model.phase == XZ_SUPPORT_WORKING; i++) { usleep(20000); xz_support_read(&model, 0); }
    return model;
}
int main(int argc,char **argv) {
    assert(argc==5);
    snprintf(scratch, sizeof scratch, "%s", argv[4]);
    assert(!setenv("XZ_SUPPORT_BIN",argv[1],1));
    assert(!setenv("XZ_SUPPORT_STATE_DIR",argv[2],1));
    struct xz_support_request_args request={.op=XZ_SUPPORT_OP_CHECK};
    struct xz_support_model model={0};
    for(int pass=0;pass<2;pass++){
        assert(!xz_support_request(&request));
        do{usleep(20000);xz_support_read(&model,0);}while(model.phase==XZ_SUPPORT_WORKING);
        assert(model.phase==XZ_SUPPORT_OUTCOME_UNKNOWN);
        usleep(100000); /* the killed helper becomes reapable */
    }
    puts("PASS helper EOF deadline, unknown outcome and retry after reap");

    /* REPORT AN ISSUE: the identity read once, the request on stdin, every answer the helper can give. */
    char usb[600], request_text[4096], id[64], id2[64];
    snprintf(usb, sizeof usb, "%s/usb", scratch);
    assert(!setenv("XZ_SUPPORT_BIN", argv[3], 1));
    assert(!setenv("XZ_MODS_USB", usb, 1));
    xz_support_identity_load(usb);
    xz_support_read(&model, 0);
    assert(!strcmp(model.loader_id, "L-4F7K-2M9Q") && model.loader_state == XZ_LOADER_OK);

    mode("sent");
    model = report(0, "Deck 1\x01 stopped\n");
    assert(model.phase == XZ_SUPPORT_SENT && !strcmp(model.reference, "R-4F7K-2M9Q"));
    assert(!strcmp(model.tracking_url, "https://xz-mods-support.will-026.workers.dev/r/R-4F7K-2M9Q"));
    last_request(request_text, sizeof request_text);
    assert(!strncmp(request_text, "xz-pending-request/1\n", 21));
    assert(strstr(request_text, "\ntopic=NOT_WORKING\n") && strstr(request_text, "\ntext=Deck 1  stopped \n"));
    assert(strstr(request_text, "\ninterface=MODERN\n") && strstr(request_text, "\nbefore=BROWSE\n") && strstr(request_text, "\nentry=ALERT\n"));
    assert(strstr(request_text, "\ndeck_view=2\n") && strstr(request_text, "\nalert=8302,0,8307\n") && strstr(request_text, "\nsettings=1,0,1,3,2,0\n"));
    assert(strstr(request_text, "\nloader_id=L-4F7K-2M9Q\n") && strstr(request_text, "\nloader_key=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n"));
    char usb_seen[600];
    field(request_text, "\nusb=", usb_seen, sizeof usb_seen);
    assert(!strcmp(usb_seen, usb));
    field(request_text, "\nreport_id=", id, sizeof id);
    assert(strlen(id) == 32 && strspn(id, "0123456789abcdef") == 32);

    /* Neither sent nor saved: the reason words reach the model; TRY AGAIN keeps the report ID, a new report does not. */
    mode("unsaved");
    model = report(0, "");
    assert(model.phase == XZ_SUPPORT_UNSAVED && !strcmp(model.result, "READ_ONLY") && !strcmp(model.detail, "OFFLINE"));
    last_request(request_text, sizeof request_text);
    field(request_text, "\nreport_id=", id2, sizeof id2);
    assert(strcmp(id, id2) && strstr(request_text, "\ntext=\n"));
    model = report(1, "");
    last_request(request_text, sizeof request_text);
    field(request_text, "\nreport_id=", id, sizeof id);
    assert(!strcmp(id, id2));
    mode("saved");
    model = report(1, "");
    assert(model.phase == XZ_SUPPORT_SAVED && !strcmp(model.result, "OFFLINE") && !model.reference[0]);
    model = report(1, "");
    last_request(request_text, sizeof request_text);
    field(request_text, "\nreport_id=", id, sizeof id);
    assert(strcmp(id, id2));

    /* An older loader: no identity, no key in the request. */
    char bare[600];
    snprintf(bare, sizeof bare, "%s/bare", scratch);
    xz_support_identity_load(bare);
    xz_support_read(&model, 0);
    assert(!model.loader_id[0] && model.loader_state == XZ_LOADER_NONE);
    model = report(0, "");
    last_request(request_text, sizeof request_text);
    assert(!strstr(request_text, "loader_key") && !strstr(request_text, "loader_id"));

    /* Killed at the deadline after the stick copy was written: SAVED, outcome unknown. */
    mode("hang");
    model = report(0, "");
    assert(model.phase == XZ_SUPPORT_SAVED && !strcmp(model.result, "UNKNOWN"));
    puts("PASS report worker: identity, stdin request, SENT/UNSAVED/SAVED, retry keeps the report ID, no key without identity, deadline after stick copy");
    return 0;
}
