/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* publisher.h - bounded event queue + worker thread + backend vtable.
 *
 * The probe calls rtvision_publisher_enqueue() on the streaming thread: it never blocks beyond
 * a short mutex section and never does I/O. When the queue is full the configured policy applies
 * (drop_oldest by default) and the dropped counter is incremented. The worker thread serializes
 * events (event_serialize.c) and hands the bytes to the backend (kafka | stdout | file).
 */
#ifndef RTVISION_PUBLISHER_H
#define RTVISION_PUBLISHER_H

#include <glib.h>

#include "event_model.h"

G_BEGIN_DECLS

typedef struct RTVisionPublisher RTVisionPublisher;
typedef struct RTVisionBackend RTVisionBackend;

typedef struct {
    const char *name;
    /* TRUE: send() only queues; the backend later calls rtvision_publisher_report_delivery(). */
    gboolean async_delivery;
    /* Hands one serialized event to the backend (worker thread). key may be NULL. */
    gboolean (*send)(RTVisionBackend *b, const char *key, const char *data, gsize len,
                     GError **error);
    /* Optional: called by the worker regularly (callbacks, health checks). */
    void (*poll)(RTVisionBackend *b);
    /* Waits until everything sent has been delivered or timeout_ms elapsed. */
    gboolean (*flush)(RTVisionBackend *b, gint timeout_ms);
    gboolean (*healthy)(RTVisionBackend *b);
    void (*destroy)(RTVisionBackend *b);
} RTVisionBackendOps;

struct RTVisionBackend {
    const RTVisionBackendOps *ops;
    RTVisionPublisher *publisher; /* set by rtvision_publisher_new() */
};

typedef enum { RTVISION_DROP_OLDEST, RTVISION_DROP_NEWEST } RTVisionDropPolicy;

typedef struct {
    guint64 enqueued;  /* accepted into the queue */
    guint64 published; /* delivered by the backend */
    guint64 dropped;   /* dropped by the queue policy or at shutdown */
    guint64 failed;    /* backend errors */
    guint depth;       /* current queue length */
    gboolean healthy;  /* backend health (Kafka connected) */
} RTVisionPublisherStats;

/* Takes ownership of `backend`. key_by_sensor: use sensor.id as the message key. */
RTVisionPublisher *rtvision_publisher_new(RTVisionBackend *backend, const RTVisionEventContext *ctx,
                                          guint max_events, RTVisionDropPolicy policy,
                                          gboolean key_by_sensor);

/* Takes ownership of `ev` (freed on drop). Returns FALSE if the event was dropped. */
gboolean rtvision_publisher_enqueue(RTVisionPublisher *pub, RTVisionFrameEvent *ev);

/* For async backends: one delivery result. Thread-safe. */
void rtvision_publisher_report_delivery(RTVisionPublisher *pub, gboolean ok);

/* Stops accepting events, drains the queue and flushes the backend within timeout_ms, joins the
 * worker. Events still queued at the deadline are counted as dropped. Returns TRUE if everything
 * was delivered. Idempotent. */
gboolean rtvision_publisher_shutdown(RTVisionPublisher *pub, guint timeout_ms);

void rtvision_publisher_free(RTVisionPublisher *pub); /* calls shutdown(0) if needed */

void rtvision_publisher_get_stats(RTVisionPublisher *pub, RTVisionPublisherStats *out);

/* {"events":[{"age_ms":N,"event":{...}}]}: the last event of every camera, whether or not the
 * backend delivered it. Thread-safe; free with g_free(). */
gchar *rtvision_publisher_latest_json(RTVisionPublisher *pub);

/* Backends (publisher_stream.c, publisher_kafka.c). */
/* JSON Lines on a file descriptor that the backend takes ownership of (e.g. a dup of stdout). */
RTVisionBackend *rtvision_backend_stream_new_fd(int fd, GError **error);
/* JSON Lines appended to `path` (parent directories created). */
RTVisionBackend *rtvision_backend_file_new(const char *path, GError **error);
/* librdkafka producer. properties_file: librdkafka key=value lines (may be NULL/empty). */
RTVisionBackend *rtvision_backend_kafka_new(const char *brokers, const char *topic,
                                            const char *properties_file, const char *client_id,
                                            GError **error);

G_END_DECLS

#endif /* RTVISION_PUBLISHER_H */
