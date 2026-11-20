/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & AFFILIATES. All rights
 * reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Modifications Copyright (c) 2026 <PROJECT_OWNER>
 * Changes: derived from perf_cb() in RT-CV src/metropolis_perception_app.c; console table
 * replaced by one key=value log line per active source plus a summary; state kept for metrics.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* perf.c - per-stream FPS from the deepstream-app perf measurement (see perf.h). */
#include "perf.h"

#include "log.h"

static GMutex fps_lock;
static gdouble fps[MAX_SOURCE_BINS];
static gdouble fps_avg[MAX_SOURCE_BINS];
static guint active_sources;

static const char *or_dash(const char *s) {
    return (s && *s) ? s : "-";
}

void perceptra_perf_cb(gpointer context, NvDsAppPerfStruct *perf) {
    (void)context;
    guint active = 0;

    g_mutex_lock(&fps_lock);
    if (!perf->use_nvmultiurisrcbin) {
        /* Static sources: indices 0..num_instances-1. */
        for (guint i = 0; i < perf->num_instances && i < MAX_SOURCE_BINS; i++) {
            fps[i] = perf->fps[i];
            fps_avg[i] = perf->fps_avg[i];
            if (fps[i] > 0)
                active++;
            PLOG_INFO("perf", "event=perf source_id=%u fps=%.2f fps_avg=%.2f", i, fps[i],
                      fps_avg[i]);
        }
    } else {
        /* nvmultiurisrcbin: only sources currently added over REST are reported. */
        for (guint j = 0; j < perf->active_source_size && j < MAX_SOURCE_BINS; j++) {
            const NvDsAppSourceDetail *d = &perf->source_detail[j];
            guint i = d->source_id;
            if (i >= MAX_SOURCE_BINS)
                continue;
            fps[i] = perf->fps[i];
            fps_avg[i] = perf->fps_avg[i];
            if (fps[i] > 0)
                active++;
            PLOG_INFO(
                "perf",
                "event=perf source_id=%u sensor_id=\"%s\" sensor_name=\"%s\" fps=%.2f fps_avg=%.2f",
                i, or_dash(d->sensor_id), or_dash(d->sensor_name), fps[i], fps_avg[i]);
        }
    }
    active_sources = active;
    g_mutex_unlock(&fps_lock);

    PLOG_INFO("perf", "event=perf_summary active_sources=%u", active);
}

guint perceptra_perf_active_sources(void) {
    g_mutex_lock(&fps_lock);
    guint n = active_sources;
    g_mutex_unlock(&fps_lock);
    return n;
}
