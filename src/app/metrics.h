/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* metrics.h - Prometheus text exposition (format 0.0.4) of the metrics in ARCHITECTURE §8.
 *
 * Rendering works on a plain snapshot struct filled by the ops server from the probe, perf,
 * publisher and hot-swap counters, so this file depends on GLib only (unit-tested in CI).
 */
#ifndef RTVISION_METRICS_H
#define RTVISION_METRICS_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
    const gchar *sensor_id;
    guint64 frames; /* rtvision_frames_processed_total */
} RTVisionMetricsStream;

typedef struct {
    const gchar *sensor_id;
    gdouble fps; /* rtvision_stream_fps */
} RTVisionMetricsFps;

typedef struct {
    const gchar *sensor_id;
    const gchar *label;
    guint64 count; /* rtvision_objects_detected_total */
} RTVisionMetricsObjects;

typedef struct {
    const RTVisionMetricsStream *streams;
    guint n_streams;
    const RTVisionMetricsFps *fps;
    guint n_fps;
    const RTVisionMetricsObjects *objects;
    guint n_objects;
    guint streams_active;
    gboolean has_publisher;
    guint64 events_enqueued, events_published, events_dropped, events_failed;
    guint queue_depth;
    const gchar *model_name, *model_version, *model_task, *model_family, *model_precision,
        *model_license;
    guint64 swaps_ok, swaps_failed;
} RTVisionMetricsSnapshot;

/* Truncates `out` and writes the full exposition. */
void rtvision_metrics_render(const RTVisionMetricsSnapshot *snap, GString *out);

/* Appends a label value escaped per the exposition format (\\, \", \n). */
void rtvision_metrics_escape_label(GString *out, const gchar *value);

G_END_DECLS

#endif /* RTVISION_METRICS_H */
