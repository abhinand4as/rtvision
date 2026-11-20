/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* event_model.h - in-memory form of one `rtvision.detection.v1` event (docs/EVENT_SCHEMA.md).
 *
 * Plain C + GLib only (no DeepStream types) so the serializer is unit-testable without a GPU.
 * One event = one sampled frame of one stream. The probe fills it on the streaming thread and
 * hands ownership to the publisher queue; serialization happens on the publisher thread.
 */
#ifndef RTVISION_EVENT_MODEL_H
#define RTVISION_EVENT_MODEL_H

#include <glib.h>

G_BEGIN_DECLS

#define RTVISION_SCHEMA_ID "rtvision.detection.v1"
#define RTVISION_UUID_LEN 36

typedef enum {
    RTVISION_TS_NTP = 0,
    RTVISION_TS_URI_UTC = 1,
    RTVISION_TS_SYSTEM = 2,
} RTVisionTimestampSource;

const char *rtvision_timestamp_source_name(RTVisionTimestampSource s);
gboolean rtvision_timestamp_source_parse(const char *text, RTVisionTimestampSource *out);

/* Tracker ReID feature (M5). Serialized as {"dim", "encoding":"base64-f32le", "data"}. */
typedef struct {
    guint dim;
    gfloat *data; /* owned, dim floats */
} RTVisionEmbedding;

typedef struct {
    gboolean has_track_id; /* FALSE -> "track_id": null (untracked / tracker disabled) */
    guint64 track_id;
    gint class_id;
    const gchar *label;      /* interned string (g_intern_string), never freed */
    gboolean has_confidence; /* FALSE -> "confidence": null */
    gfloat confidence;
    gdouble left, top, width, height; /* source-resolution pixels */
    RTVisionEmbedding *embedding;     /* owned; NULL unless include_embeddings */
    gpointer mask;                    /* reserved for task=segment: NULL in v1, never serialized */
    gpointer attributes;              /* reserved for secondary classifiers: NULL in v1 */
} RTVisionObject;

typedef struct {
    gchar event_id[RTVISION_UUID_LEN + 1];
    gint64 timestamp_ms; /* Unix epoch milliseconds, UTC */
    RTVisionTimestampSource timestamp_source;
    gchar *sensor_id;   /* owned, non-empty */
    gchar *sensor_name; /* owned, NULL -> null */
    guint stream_index;
    guint64 frame_number;
    guint frame_width, frame_height; /* source resolution */
    guint num_objects;
    guint capacity;
    RTVisionObject objects[]; /* capacity entries, allocated with the event */
} RTVisionFrameEvent;

/* Per-process constants shared by all events (not copied per event). */
typedef struct {
    const gchar *service;
    const gchar *model_name;
    const gchar *model_version;
    const gchar *model_task;
} RTVisionEventContext;

/* One allocation for the event and `capacity` objects (zeroed). */
RTVisionFrameEvent *rtvision_event_new(guint capacity);
void rtvision_event_free(RTVisionFrameEvent *ev);

/* Sets ev->event_id to a random UUID v4. */
void rtvision_event_set_random_id(RTVisionFrameEvent *ev);

G_END_DECLS

#endif /* RTVISION_EVENT_MODEL_H */
