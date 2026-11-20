/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* probe.h - rtvision_probe: runs on the streaming thread after detection/tracking.
 *
 * Per frame: counters; on frames selected by publish.interval_frames it builds one
 * RTVisionFrameEvent via the task handler and enqueues it on the publisher (non-blocking).
 * Rules (CLAUDE.md rule 5): no network I/O, no unbounded allocation (objects per event are
 * capped at RTVISION_MAX_OBJECTS_PER_EVENT), no per-object logging at info level.
 */
#ifndef RTVISION_PROBE_H
#define RTVISION_PROBE_H

#include <glib.h>
#include <gst/gst.h>

#include "deepstream_app.h"
#include "event_model.h"
#include "publisher.h"
#include "task_registry.h"

G_BEGIN_DECLS

#define RTVISION_MAX_OBJECTS_PER_EVENT 1024

typedef struct {
    guint log_every_n_frames;           /* per-stream debug log interval (0 = off) */
    RTVisionPublisher *publisher;       /* NULL: no events */
    const RTVisionTaskHandler *handler; /* required when publisher != NULL */
    guint interval_frames;              /* emit when frame_num % interval_frames == 0 */
    gboolean include_empty_frames;
} RTVisionProbeConfig;

/* Copies cfg. Call before the pipeline starts. */
void rtvision_probe_init(const RTVisionProbeConfig *cfg);

/* After this, no more events are enqueued (shutdown step 1). Thread-safe. */
void rtvision_probe_stop_events(void);

/* bbox_generated_callback for create_pipeline() (post analytics: PGIE/tracker). */
void rtvision_probe(AppCtx *app_ctx, GstBuffer *buf, NvDsBatchMeta *batch_meta, guint index);

/* Totals across all streams (thread-safe reads). */
guint64 rtvision_probe_frames_total(void);
guint64 rtvision_probe_objects_total(void);
guint64 rtvision_probe_events_built(void);

/* Metrics views (thread-safe, lock-free reads). Class ids >= RTVISION_PROBE_MAX_CLASSES are
 * counted under the last slot. */
#define RTVISION_PROBE_MAX_CLASSES 256

typedef struct {
    guint source_id;
    const gchar *sensor_id; /* interned */
    guint64 frames;
} RTVisionProbeSourceStat;

/* Fills up to max sources that have processed frames; returns the count. */
guint rtvision_probe_sources(RTVisionProbeSourceStat *out, guint max);

/* Calls fn for every (source, label) pair with a non-zero object count. */
typedef void (*RTVisionProbeObjectFn)(const gchar *sensor_id, const gchar *label, guint64 count,
                                      gpointer user_data);
void rtvision_probe_foreach_object_count(RTVisionProbeObjectFn fn, gpointer user_data);

G_END_DECLS

#endif /* RTVISION_PROBE_H */
