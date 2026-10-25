#define _GNU_SOURCE
#include "../support/native_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef NDEBUG
#error Assertions required
#endif
int main(int argc,char **argv) {
    assert(argc==3);
    assert(!setenv("XZ_SUPPORT_BIN",argv[1],1));
    assert(!setenv("XZ_SUPPORT_STATE_DIR",argv[2],1));
    struct xz_support_request_args request={.op=XZ_SUPPORT_OP_CHECK};
    struct xz_support_model model={0};
    assert(!xz_support_request(&request));
    usleep(600000);xz_support_read(&model,0);
    assert(model.phase==XZ_SUPPORT_OUTCOME_UNKNOWN);
    assert(!xz_support_request(&request));
    usleep(600000);xz_support_read(&model,0);
    assert(model.phase==XZ_SUPPORT_OUTCOME_UNKNOWN);
    puts("PASS helper EOF deadline, unknown outcome and retry after reap");
    return 0;
}
