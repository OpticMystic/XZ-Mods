/* SPDX-License-Identifier: MIT */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#ifndef XZ_INDEX_COST_INSTRUMENTED
#error Run through verify_index_parser.py to count parser token inspections.
#endif
static uint64_t token_inspections;
#include "../overcue_file.c"

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    struct document d = {0};
    char bundle[17] = "";
    int parsed = document_open(argv[1], &d);
    int found = parsed ? -1 : find_entry(&d, argv[2], NULL, bundle);
    uint64_t budget = d.count > 0 ? (uint64_t)d.count * 8 : 0;
    int correct = !parsed && found >= 0 && !strcmp(bundle, argv[3]);
    int bounded = token_inspections <= budget;
    printf("{\"tokens\":%d,\"token_inspections\":%llu,\"budget\":%llu,"
           "\"last_entry_correct\":%s,\"linear_work_budget_met\":%s}\n",
           d.count, (unsigned long long)token_inspections, (unsigned long long)budget,
           correct ? "true" : "false", bounded ? "true" : "false");
    document_close(&d);
    return correct && bounded ? 0 : 1;
}
