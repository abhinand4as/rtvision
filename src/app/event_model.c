/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* event_model.c - event allocation helpers (see event_model.h). */
#include "event_model.h"

#include <string.h>

static const char *const ts_names[] = {"ntp", "uri_utc", "system"};

const char *perceptra_timestamp_source_name(PerceptraTimestampSource s) {
    return (guint)s < G_N_ELEMENTS(ts_names) ? ts_names[s] : "system";
}

gboolean perceptra_timestamp_source_parse(const char *text, PerceptraTimestampSource *out) {
    for (guint i = 0; text && i < G_N_ELEMENTS(ts_names); i++) {
        if (strcmp(text, ts_names[i]) == 0) {
            *out = (PerceptraTimestampSource)i;
            return TRUE;
        }
    }
    return FALSE;
}

PerceptraFrameEvent *perceptra_event_new(guint capacity) {
    PerceptraFrameEvent *ev =
        g_malloc0(sizeof(PerceptraFrameEvent) + (gsize)capacity * sizeof(PerceptraObject));
    ev->capacity = capacity;
    return ev;
}

void perceptra_event_free(PerceptraFrameEvent *ev) {
    if (ev == NULL)
        return;
    for (guint i = 0; i < ev->num_objects; i++) {
        PerceptraEmbedding *e = ev->objects[i].embedding;
        if (e) {
            g_free(e->data);
            g_free(e);
        }
    }
    g_free(ev->sensor_id);
    g_free(ev->sensor_name);
    g_free(ev);
}

void perceptra_event_set_random_id(PerceptraFrameEvent *ev) {
    gchar *uuid = g_uuid_string_random();
    g_strlcpy(ev->event_id, uuid, sizeof(ev->event_id));
    g_free(uuid);
}
