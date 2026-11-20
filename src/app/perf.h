/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* perf.h - perf callback for create_pipeline(): per-stream FPS logs and the FPS gauges. */
#ifndef RTVISION_PERF_H
#define RTVISION_PERF_H

#include <glib.h>

#include "deepstream_perf.h"

G_BEGIN_DECLS

/* perf_callback; context is the AppCtx. Called every perf-measurement-interval-sec. */
void rtvision_perf_cb(gpointer context, NvDsAppPerfStruct *perf);

/* Streams present in the last report (nvmultiurisrcbin: streams currently added). */
guint rtvision_perf_active_sources(void);

typedef struct {
    guint source_id;
    gchar sensor_id[128];
    gdouble fps;
} RTVisionPerfStream;

/* Copies the last report (up to max streams). Thread-safe. Returns the number copied. */
guint rtvision_perf_snapshot(RTVisionPerfStream *out, guint max);

G_END_DECLS

#endif /* RTVISION_PERF_H */
