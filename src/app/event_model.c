/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* event_model.c - event allocation helpers (see event_model.h). */
#include "event_model.h"

#include <string.h>

static const char *const ts_names[] = {"ntp", "uri_utc", "system"};

const char *rtvision_timestamp_source_name(RTVisionTimestampSource s) {
    return (guint)s < G_N_ELEMENTS(ts_names) ? ts_names[s] : "system";
}

gboolean rtvision_timestamp_source_parse(const char *text, RTVisionTimestampSource *out) {
    for (guint i = 0; text && i < G_N_ELEMENTS(ts_names); i++) {
        if (strcmp(text, ts_names[i]) == 0) {
            *out = (RTVisionTimestampSource)i;
            return TRUE;
        }
    }
    return FALSE;
}

RTVisionFrameEvent *rtvision_event_new(guint capacity) {
    RTVisionFrameEvent *ev =
        g_malloc0(sizeof(RTVisionFrameEvent) + (gsize)capacity * sizeof(RTVisionObject));
    ev->capacity = capacity;
    return ev;
}

void rtvision_event_free(RTVisionFrameEvent *ev) {
    if (ev == NULL)
        return;
    for (guint i = 0; i < ev->num_objects; i++) {
        RTVisionEmbedding *e = ev->objects[i].embedding;
        if (e) {
            g_free(e->data);
            g_free(e);
        }
    }
    g_free(ev->sensor_id);
    g_free(ev->sensor_name);
    g_free(ev);
}

void rtvision_event_set_random_id(RTVisionFrameEvent *ev) {
    gchar *uuid = g_uuid_string_random();
    g_strlcpy(ev->event_id, uuid, sizeof(ev->event_id));
    g_free(uuid);
}
