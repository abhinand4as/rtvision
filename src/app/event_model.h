/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* event_model.h - in-memory form of one `perceptra.detection.v1` event (docs/EVENT_SCHEMA.md).
 *
 * Plain C + GLib only (no DeepStream types) so the serializer is unit-testable without a GPU.
 * One event = one sampled frame of one stream. The probe fills it on the streaming thread and
 * hands ownership to the publisher queue; serialization happens on the publisher thread.
 */
#ifndef PERCEPTRA_EVENT_MODEL_H
#define PERCEPTRA_EVENT_MODEL_H

#include <glib.h>

G_BEGIN_DECLS

#define PERCEPTRA_SCHEMA_ID "perceptra.detection.v1"
#define PERCEPTRA_UUID_LEN 36

typedef enum {
    PERCEPTRA_TS_NTP = 0,
    PERCEPTRA_TS_URI_UTC = 1,
    PERCEPTRA_TS_SYSTEM = 2,
} PerceptraTimestampSource;

const char *perceptra_timestamp_source_name(PerceptraTimestampSource s);
gboolean perceptra_timestamp_source_parse(const char *text, PerceptraTimestampSource *out);

/* Tracker ReID feature (M5). Serialized as {"dim", "encoding":"base64-f32le", "data"}. */
typedef struct {
    guint dim;
    gfloat *data; /* owned, dim floats */
} PerceptraEmbedding;

typedef struct {
    gboolean has_track_id; /* FALSE -> "track_id": null (untracked / tracker disabled) */
    guint64 track_id;
    gint class_id;
    const gchar *label;      /* interned string (g_intern_string), never freed */
    gboolean has_confidence; /* FALSE -> "confidence": null */
    gfloat confidence;
    gdouble left, top, width, height; /* source-resolution pixels */
    PerceptraEmbedding *embedding;    /* owned; NULL unless include_embeddings */
    gpointer mask;                    /* reserved for task=segment: NULL in v1, never serialized */
    gpointer attributes;              /* reserved for secondary classifiers: NULL in v1 */
} PerceptraObject;

typedef struct {
    gchar event_id[PERCEPTRA_UUID_LEN + 1];
    gint64 timestamp_ms; /* Unix epoch milliseconds, UTC */
    PerceptraTimestampSource timestamp_source;
    gchar *sensor_id;   /* owned, non-empty */
    gchar *sensor_name; /* owned, NULL -> null */
    guint stream_index;
    guint64 frame_number;
    guint frame_width, frame_height; /* source resolution */
    guint num_objects;
    guint capacity;
    PerceptraObject objects[]; /* capacity entries, allocated with the event */
} PerceptraFrameEvent;

/* Per-process constants shared by all events (not copied per event). */
typedef struct {
    const gchar *service;
    const gchar *model_name;
    const gchar *model_version;
    const gchar *model_task;
} PerceptraEventContext;

/* One allocation for the event and `capacity` objects (zeroed). */
PerceptraFrameEvent *perceptra_event_new(guint capacity);
void perceptra_event_free(PerceptraFrameEvent *ev);

/* Sets ev->event_id to a random UUID v4. */
void perceptra_event_set_random_id(PerceptraFrameEvent *ev);

G_END_DECLS

#endif /* PERCEPTRA_EVENT_MODEL_H */
