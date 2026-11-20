/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* perf.h - perf callback for create_pipeline(): per-stream FPS logging (metrics in M6). */
#ifndef PERCEPTRA_PERF_H
#define PERCEPTRA_PERF_H

#include <glib.h>

#include "deepstream_perf.h"

G_BEGIN_DECLS

/* perf_callback; context is the AppCtx. Called every perf-measurement-interval-sec. */
void perceptra_perf_cb(gpointer context, NvDsAppPerfStruct *perf);

/* Number of sources with non-zero FPS in the last report. */
guint perceptra_perf_active_sources(void);

G_END_DECLS

#endif /* PERCEPTRA_PERF_H */
