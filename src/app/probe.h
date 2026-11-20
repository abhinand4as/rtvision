/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* probe.h - perceptra_probe: runs on the streaming thread after detection/tracking.
 *
 * M1: counts frames and objects per stream and logs every N frames at debug level.
 * Task dispatch and event building are added in M4. The probe must never block: no network
 * I/O, no unbounded allocation, no per-object info logging (CLAUDE.md rule 5).
 */
#ifndef PERCEPTRA_PROBE_H
#define PERCEPTRA_PROBE_H

#include <glib.h>
#include <gst/gst.h>

#include "deepstream_app.h"

G_BEGIN_DECLS

/* every_n_frames: per-stream debug log interval (0 disables the periodic log). */
void perceptra_probe_init(guint every_n_frames);

/* bbox_generated_callback for create_pipeline() (post analytics: PGIE/tracker). */
void perceptra_probe(AppCtx *app_ctx, GstBuffer *buf, NvDsBatchMeta *batch_meta, guint index);

/* Totals across all streams (thread-safe reads). */
guint64 perceptra_probe_frames_total(void);
guint64 perceptra_probe_objects_total(void);

G_END_DECLS

#endif /* PERCEPTRA_PROBE_H */
