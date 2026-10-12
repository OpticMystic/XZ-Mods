/* SPDX-License-Identifier: MIT */
#ifndef XZ_METRICS_RUNTIME_H
#define XZ_METRICS_RUNTIME_H
#include "ui/metrics.h"
int xz_metrics_start(void);
void xz_metrics_stop(void);
void xz_metrics_enable(int enabled);
int xz_metrics_snapshot(struct xz_metrics_sample *);
#endif
