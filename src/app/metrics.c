/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* metrics.c - Prometheus text rendering (see metrics.h). */
#include "metrics.h"

void perceptra_metrics_escape_label(GString *out, const gchar *value) {
    for (const gchar *p = value ? value : ""; *p; p++) {
        if (*p == '\\')
            g_string_append(out, "\\\\");
        else if (*p == '"')
            g_string_append(out, "\\\"");
        else if (*p == '\n')
            g_string_append(out, "\\n");
        else
            g_string_append_c(out, *p);
    }
}

static void family(GString *out, const char *name, const char *type, const char *help) {
    g_string_append_printf(out, "# HELP %s %s\n# TYPE %s %s\n", name, help, name, type);
}

/* name{k1="v1",k2="v2"} — labels: NULL-terminated list of key, value pairs. */
static void sample_start(GString *out, const char *name, const char *const *labels) {
    g_string_append(out, name);
    if (labels && labels[0]) {
        g_string_append_c(out, '{');
        for (guint i = 0; labels[i]; i += 2) {
            if (i)
                g_string_append_c(out, ',');
            g_string_append_printf(out, "%s=\"", labels[i]);
            perceptra_metrics_escape_label(out, labels[i + 1]);
            g_string_append_c(out, '"');
        }
        g_string_append_c(out, '}');
    }
    g_string_append_c(out, ' ');
}

static void sample_u64(GString *out, const char *name, const char *const *labels, guint64 v) {
    sample_start(out, name, labels);
    g_string_append_printf(out, "%" G_GUINT64_FORMAT "\n", v);
}

static void sample_f(GString *out, const char *name, const char *const *labels, gdouble v) {
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    sample_start(out, name, labels);
    g_string_append(out, g_ascii_formatd(buf, sizeof(buf), "%.2f", v));
    g_string_append_c(out, '\n');
}

void perceptra_metrics_render(const PerceptraMetricsSnapshot *s, GString *out) {
    g_string_truncate(out, 0);

    family(out, "perceptra_stream_fps", "gauge",
           "Frames per second per stream (last perf interval).");
    for (guint i = 0; i < s->n_fps; i++) {
        const char *l[] = {"sensor_id", s->fps[i].sensor_id, NULL};
        sample_f(out, "perceptra_stream_fps", l, s->fps[i].fps);
    }

    family(out, "perceptra_streams_active", "gauge", "Streams currently in the pipeline.");
    sample_u64(out, "perceptra_streams_active", NULL, s->streams_active);

    family(out, "perceptra_frames_processed_total", "counter", "Frames seen by the probe.");
    for (guint i = 0; i < s->n_streams; i++) {
        const char *l[] = {"sensor_id", s->streams[i].sensor_id, NULL};
        sample_u64(out, "perceptra_frames_processed_total", l, s->streams[i].frames);
    }

    family(out, "perceptra_objects_detected_total", "counter", "Objects seen by the probe.");
    for (guint i = 0; i < s->n_objects; i++) {
        const char *l[] = {"sensor_id", s->objects[i].sensor_id, "label", s->objects[i].label,
                           NULL};
        sample_u64(out, "perceptra_objects_detected_total", l, s->objects[i].count);
    }

    if (s->has_publisher) {
        family(out, "perceptra_events_enqueued_total", "counter", "Events accepted by the queue.");
        sample_u64(out, "perceptra_events_enqueued_total", NULL, s->events_enqueued);
        family(out, "perceptra_events_published_total", "counter", "Events delivered.");
        sample_u64(out, "perceptra_events_published_total", NULL, s->events_published);
        family(out, "perceptra_events_dropped_total", "counter",
               "Events dropped (queue full or shutdown deadline).");
        sample_u64(out, "perceptra_events_dropped_total", NULL, s->events_dropped);
        family(out, "perceptra_events_failed_total", "counter",
               "Events the backend failed to deliver.");
        sample_u64(out, "perceptra_events_failed_total", NULL, s->events_failed);
        family(out, "perceptra_publish_queue_depth", "gauge", "Events waiting in the queue.");
        sample_u64(out, "perceptra_publish_queue_depth", NULL, s->queue_depth);
    }

    family(out, "perceptra_model_info", "gauge", "Loaded model (value is always 1).");
    {
        const char *l[] = {"name",      s->model_name,      "version", s->model_version,
                           "task",      s->model_task,      "family",  s->model_family,
                           "precision", s->model_precision, "license", s->model_license,
                           NULL};
        sample_u64(out, "perceptra_model_info", l, 1);
    }

    family(out, "perceptra_model_swaps_total", "counter", "Model hot swaps by result.");
    {
        const char *ok[] = {"result", "ok", NULL};
        const char *failed[] = {"result", "failed", NULL};
        sample_u64(out, "perceptra_model_swaps_total", ok, s->swaps_ok);
        sample_u64(out, "perceptra_model_swaps_total", failed, s->swaps_failed);
    }
}
