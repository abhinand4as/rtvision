/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* publisher.c - bounded queue, worker thread, drop policy, shutdown flush (see publisher.h). */
#include "publisher.h"

#include "event_serialize.h"
#include "log.h"

#define WORKER_IDLE_WAIT_US (100 * G_TIME_SPAN_MILLISECOND)

struct PerceptraPublisher {
    PerceptraBackend *backend;
    PerceptraEventContext ctx;
    guint max_events;
    PerceptraDropPolicy policy;
    gboolean key_by_sensor;

    GMutex lock;
    GCond cond;
    GQueue queue;      /* PerceptraFrameEvent* */
    gboolean stopping; /* no new events; worker drains until deadline */
    gint64 deadline_us;
    gboolean shut_down;
    GThread *worker;

    guint64 enqueued, published, dropped, failed; /* atomic (__atomic builtins) */
};

static void add(guint64 *counter, guint64 n) {
    __atomic_add_fetch(counter, n, __ATOMIC_RELAXED);
}

static guint64 get(const guint64 *counter) {
    return __atomic_load_n(counter, __ATOMIC_RELAXED);
}

void perceptra_publisher_report_delivery(PerceptraPublisher *pub, gboolean ok) {
    add(ok ? &pub->published : &pub->failed, 1);
}

static gpointer worker_main(gpointer data) {
    PerceptraPublisher *pub = data;
    GString *json = g_string_sized_new(4096);
    GError *error = NULL;
    guint64 failures_logged = 0;

    for (;;) {
        g_mutex_lock(&pub->lock);
        while (g_queue_is_empty(&pub->queue) && !pub->stopping) {
            gint64 until = g_get_monotonic_time() + WORKER_IDLE_WAIT_US;
            if (!g_cond_wait_until(&pub->cond, &pub->lock, until)) {
                g_mutex_unlock(&pub->lock);
                if (pub->backend->ops->poll)
                    pub->backend->ops->poll(pub->backend);
                g_mutex_lock(&pub->lock);
            }
        }
        if (pub->stopping &&
            (g_queue_is_empty(&pub->queue) || g_get_monotonic_time() >= pub->deadline_us)) {
            /* Deadline reached: whatever is left is dropped. */
            guint left = g_queue_get_length(&pub->queue);
            PerceptraFrameEvent *ev;
            while ((ev = g_queue_pop_head(&pub->queue)) != NULL)
                perceptra_event_free(ev);
            g_mutex_unlock(&pub->lock);
            if (left > 0) {
                add(&pub->dropped, left);
                PLOG_WARN("publisher", "event=shutdown_drop events=%u", left);
            }
            break;
        }
        PerceptraFrameEvent *ev = g_queue_pop_head(&pub->queue);
        g_mutex_unlock(&pub->lock);

        perceptra_event_to_json(ev, &pub->ctx, json);
        const char *key = pub->key_by_sensor ? ev->sensor_id : NULL;
        gboolean ok = pub->backend->ops->send(pub->backend, key, json->str, json->len, &error);
        if (!ok) {
            add(&pub->failed, 1);
            /* Rate-limit: first failure, then every 1000th. */
            if (failures_logged++ % 1000 == 0)
                PLOG_WARN("publisher",
                          "event=send_failed backend=%s error=\"%s\" failures=%" G_GUINT64_FORMAT,
                          pub->backend->ops->name, error ? error->message : "?", failures_logged);
            g_clear_error(&error);
        } else if (!pub->backend->ops->async_delivery) {
            add(&pub->published, 1);
        }
        perceptra_event_free(ev);
        if (pub->backend->ops->poll)
            pub->backend->ops->poll(pub->backend);
    }
    g_string_free(json, TRUE);
    return NULL;
}

PerceptraPublisher *perceptra_publisher_new(PerceptraBackend *backend,
                                            const PerceptraEventContext *ctx, guint max_events,
                                            PerceptraDropPolicy policy, gboolean key_by_sensor) {
    PerceptraPublisher *pub = g_new0(PerceptraPublisher, 1);
    pub->backend = backend;
    backend->publisher = pub;
    pub->ctx = *ctx;
    pub->max_events = MAX(max_events, 1u);
    pub->policy = policy;
    pub->key_by_sensor = key_by_sensor;
    g_mutex_init(&pub->lock);
    g_cond_init(&pub->cond);
    g_queue_init(&pub->queue);
    pub->worker = g_thread_new("perceptra-publisher", worker_main, pub);
    PLOG_INFO("publisher", "event=started backend=%s max_events=%u on_full=%s key=%s",
              backend->ops->name, pub->max_events,
              policy == PERCEPTRA_DROP_OLDEST ? "drop_oldest" : "drop_newest",
              key_by_sensor ? "sensor_id" : "none");
    return pub;
}

gboolean perceptra_publisher_enqueue(PerceptraPublisher *pub, PerceptraFrameEvent *ev) {
    PerceptraFrameEvent *victim = NULL;
    gboolean accepted = TRUE;

    g_mutex_lock(&pub->lock);
    if (pub->stopping) {
        accepted = FALSE;
        victim = ev;
    } else if (g_queue_get_length(&pub->queue) >= pub->max_events) {
        if (pub->policy == PERCEPTRA_DROP_OLDEST) {
            victim = g_queue_pop_head(&pub->queue);
            g_queue_push_tail(&pub->queue, ev);
        } else {
            victim = ev;
            accepted = FALSE;
        }
    } else {
        g_queue_push_tail(&pub->queue, ev);
    }
    if (accepted)
        g_cond_signal(&pub->cond);
    g_mutex_unlock(&pub->lock);

    if (accepted)
        add(&pub->enqueued, 1);
    if (victim) {
        add(&pub->dropped, 1);
        perceptra_event_free(victim); /* outside the lock */
    }
    return accepted;
}

gboolean perceptra_publisher_shutdown(PerceptraPublisher *pub, guint timeout_ms) {
    if (pub->shut_down)
        return TRUE;
    gint64 start = g_get_monotonic_time();
    gint64 deadline = start + (gint64)timeout_ms * G_TIME_SPAN_MILLISECOND;

    g_mutex_lock(&pub->lock);
    pub->stopping = TRUE;
    pub->deadline_us = deadline;
    guint queued = g_queue_get_length(&pub->queue);
    g_cond_broadcast(&pub->cond);
    g_mutex_unlock(&pub->lock);
    PLOG_INFO("publisher", "event=shutdown queued=%u timeout_ms=%u", queued, timeout_ms);

    g_thread_join(pub->worker);
    pub->worker = NULL;

    gint64 left_ms = (deadline - g_get_monotonic_time()) / G_TIME_SPAN_MILLISECOND;
    gboolean flushed = pub->backend->ops->flush(pub->backend, (gint)MAX(left_ms, 0));
    pub->shut_down = TRUE;

    PerceptraPublisherStats s;
    perceptra_publisher_get_stats(pub, &s);
    PLOG_INFO("publisher",
              "event=stopped flushed=%d enqueued=%" G_GUINT64_FORMAT " published=%" G_GUINT64_FORMAT
              " dropped=%" G_GUINT64_FORMAT " failed=%" G_GUINT64_FORMAT
              " took_ms=%" G_GINT64_FORMAT,
              flushed, s.enqueued, s.published, s.dropped, s.failed,
              (g_get_monotonic_time() - start) / G_TIME_SPAN_MILLISECOND);
    return flushed && s.enqueued == s.published + s.dropped + s.failed && s.dropped == 0 &&
           s.failed == 0;
}

void perceptra_publisher_free(PerceptraPublisher *pub) {
    if (pub == NULL)
        return;
    if (!pub->shut_down)
        perceptra_publisher_shutdown(pub, 0);
    pub->backend->ops->destroy(pub->backend);
    g_queue_clear(&pub->queue);
    g_mutex_clear(&pub->lock);
    g_cond_clear(&pub->cond);
    g_free(pub);
}

void perceptra_publisher_get_stats(PerceptraPublisher *pub, PerceptraPublisherStats *out) {
    out->enqueued = get(&pub->enqueued);
    out->published = get(&pub->published);
    out->dropped = get(&pub->dropped);
    out->failed = get(&pub->failed);
    g_mutex_lock(&pub->lock);
    out->depth = g_queue_get_length(&pub->queue);
    g_mutex_unlock(&pub->lock);
    out->healthy = pub->backend->ops->healthy ? pub->backend->ops->healthy(pub->backend) : TRUE;
}
