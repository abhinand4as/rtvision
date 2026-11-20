/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* test_metrics.c - Prometheus exposition format (GLib only). */
#include <stdio.h>
#include <string.h>

#include "metrics.h"

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define HAS(text, needle) CHECK(strstr((text), (needle)) != NULL)

int main(void) {
    const PerceptraMetricsStream streams[] = {{"cam-1", 300}, {"cam \"2\"", 10}};
    const PerceptraMetricsFps fps[] = {{"cam-1", 29.97}};
    const PerceptraMetricsObjects objects[] = {{"cam-1", "person", 42}, {"cam-1", "car\\x", 7}};
    PerceptraMetricsSnapshot s = {
        .streams = streams,
        .n_streams = 2,
        .fps = fps,
        .n_fps = 1,
        .objects = objects,
        .n_objects = 2,
        .streams_active = 1,
        .has_publisher = TRUE,
        .events_enqueued = 11,
        .events_published = 10,
        .events_dropped = 1,
        .events_failed = 0,
        .queue_depth = 0,
        .model_name = "m",
        .model_version = "1",
        .model_task = "detect",
        .model_family = "yolo_e2e",
        .model_precision = "fp16",
        .model_license = "Apache-2.0",
        .swaps_ok = 2,
        .swaps_failed = 1,
    };
    GString *out = g_string_new(NULL);
    perceptra_metrics_render(&s, out);
    const char *t = out->str;
    HAS(t, "# TYPE perceptra_stream_fps gauge\n");
    HAS(t, "perceptra_stream_fps{sensor_id=\"cam-1\"} 29.97\n");
    HAS(t, "perceptra_streams_active 1\n");
    HAS(t, "perceptra_frames_processed_total{sensor_id=\"cam-1\"} 300\n");
    HAS(t, "perceptra_frames_processed_total{sensor_id=\"cam \\\"2\\\"\"} 10\n"); /* escaped */
    HAS(t, "perceptra_objects_detected_total{sensor_id=\"cam-1\",label=\"person\"} 42\n");
    HAS(t, "label=\"car\\\\x\"} 7\n");
    HAS(t, "perceptra_events_published_total 10\n");
    HAS(t, "perceptra_events_dropped_total 1\n");
    HAS(t, "perceptra_publish_queue_depth 0\n");
    HAS(t, "perceptra_model_info{name=\"m\",version=\"1\",task=\"detect\",family=\"yolo_e2e\","
           "precision=\"fp16\",license=\"Apache-2.0\"} 1\n");
    HAS(t, "perceptra_model_swaps_total{result=\"ok\"} 2\n");
    HAS(t, "perceptra_model_swaps_total{result=\"failed\"} 1\n");
    CHECK(g_str_has_suffix(t, "\n"));
    /* every non-comment line is "name[{labels}] value" */
    gchar **lines = g_strsplit(t, "\n", -1);
    for (gchar **l = lines; *l; l++) {
        if (**l == '\0' || **l == '#')
            continue;
        CHECK(g_str_has_prefix(*l, "perceptra_") && strrchr(*l, ' ') != NULL);
    }
    g_strfreev(lines);
    /* no publisher -> no event families */
    s.has_publisher = FALSE;
    perceptra_metrics_render(&s, out);
    CHECK(strstr(out->str, "perceptra_events_") == NULL);
    g_string_free(out, TRUE);
    printf("%s: metrics tests (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
