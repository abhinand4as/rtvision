/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* publisher.h - bounded event queue + worker thread + backend vtable.
 *
 * The probe calls perceptra_publisher_enqueue() on the streaming thread: it never blocks beyond
 * a short mutex section and never does I/O. When the queue is full the configured policy applies
 * (drop_oldest by default) and the dropped counter is incremented. The worker thread serializes
 * events (event_serialize.c) and hands the bytes to the backend (kafka | stdout | file).
 */
#ifndef PERCEPTRA_PUBLISHER_H
#define PERCEPTRA_PUBLISHER_H

#include <glib.h>

#include "event_model.h"

G_BEGIN_DECLS

typedef struct PerceptraPublisher PerceptraPublisher;
typedef struct PerceptraBackend PerceptraBackend;

typedef struct {
    const char *name;
    /* TRUE: send() only queues; the backend later calls perceptra_publisher_report_delivery(). */
    gboolean async_delivery;
    /* Hands one serialized event to the backend (worker thread). key may be NULL. */
    gboolean (*send)(PerceptraBackend *b, const char *key, const char *data, gsize len,
                     GError **error);
    /* Optional: called by the worker regularly (callbacks, health checks). */
    void (*poll)(PerceptraBackend *b);
    /* Waits until everything sent has been delivered or timeout_ms elapsed. */
    gboolean (*flush)(PerceptraBackend *b, gint timeout_ms);
    gboolean (*healthy)(PerceptraBackend *b);
    void (*destroy)(PerceptraBackend *b);
} PerceptraBackendOps;

struct PerceptraBackend {
    const PerceptraBackendOps *ops;
    PerceptraPublisher *publisher; /* set by perceptra_publisher_new() */
};

typedef enum { PERCEPTRA_DROP_OLDEST, PERCEPTRA_DROP_NEWEST } PerceptraDropPolicy;

typedef struct {
    guint64 enqueued;  /* accepted into the queue */
    guint64 published; /* delivered by the backend */
    guint64 dropped;   /* dropped by the queue policy or at shutdown */
    guint64 failed;    /* backend errors */
    guint depth;       /* current queue length */
    gboolean healthy;  /* backend health (Kafka connected) */
} PerceptraPublisherStats;

/* Takes ownership of `backend`. key_by_sensor: use sensor.id as the message key. */
PerceptraPublisher *perceptra_publisher_new(PerceptraBackend *backend,
                                            const PerceptraEventContext *ctx, guint max_events,
                                            PerceptraDropPolicy policy, gboolean key_by_sensor);

/* Takes ownership of `ev` (freed on drop). Returns FALSE if the event was dropped. */
gboolean perceptra_publisher_enqueue(PerceptraPublisher *pub, PerceptraFrameEvent *ev);

/* For async backends: one delivery result. Thread-safe. */
void perceptra_publisher_report_delivery(PerceptraPublisher *pub, gboolean ok);

/* Stops accepting events, drains the queue and flushes the backend within timeout_ms, joins the
 * worker. Events still queued at the deadline are counted as dropped. Returns TRUE if everything
 * was delivered. Idempotent. */
gboolean perceptra_publisher_shutdown(PerceptraPublisher *pub, guint timeout_ms);

void perceptra_publisher_free(PerceptraPublisher *pub); /* calls shutdown(0) if needed */

void perceptra_publisher_get_stats(PerceptraPublisher *pub, PerceptraPublisherStats *out);

/* Backends (publisher_stream.c, publisher_kafka.c). */
/* JSON Lines on a file descriptor that the backend takes ownership of (e.g. a dup of stdout). */
PerceptraBackend *perceptra_backend_stream_new_fd(int fd, GError **error);
/* JSON Lines appended to `path` (parent directories created). */
PerceptraBackend *perceptra_backend_file_new(const char *path, GError **error);
/* librdkafka producer. properties_file: librdkafka key=value lines (may be NULL/empty). */
PerceptraBackend *perceptra_backend_kafka_new(const char *brokers, const char *topic,
                                              const char *properties_file, const char *client_id,
                                              GError **error);

G_END_DECLS

#endif /* PERCEPTRA_PUBLISHER_H */
