/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* publisher.c - bounded queue, worker thread, drop policy, shutdown flush (see publisher.h). */
#include "publisher.h"

#include "event_serialize.h"
#include "log.h"

/* Cameras remembered for rtvision_publisher_latest_json(); the stalest is evicted beyond that. */
#define LATEST_MAX 256

typedef struct {
    gchar *json;
    gint64 at_us; /* monotonic */
} LatestEvent;

#define WORKER_IDLE_WAIT_US (100 * G_TIME_SPAN_MILLISECOND)

struct RTVisionPublisher {
    RTVisionBackend *backend;
    RTVisionEventContext ctx;
    guint max_events;
    RTVisionDropPolicy policy;
    gboolean key_by_sensor;

    GMutex lock;
    GCond cond;
    GQueue queue;      /* RTVisionFrameEvent* */
    gboolean stopping; /* no new events; worker drains until deadline */
    gint64 deadline_us;
    gboolean shut_down;
    GThread *worker;

    GMutex latest_lock;
    GHashTable *latest; /* sensor id -> LatestEvent */

    guint64 enqueued, published, dropped, failed; /* atomic (__atomic builtins) */
};

static void add(guint64 *counter, guint64 n) {
    __atomic_add_fetch(counter, n, __ATOMIC_RELAXED);
}

static guint64 get(const guint64 *counter) {
    return __atomic_load_n(counter, __ATOMIC_RELAXED);
}

void rtvision_publisher_report_delivery(RTVisionPublisher *pub, gboolean ok) {
    add(ok ? &pub->published : &pub->failed, 1);
}

static void latest_free(gpointer data) {
    LatestEvent *l = data;
    g_free(l->json);
    g_free(l);
}

/* Worker thread: remember the last event of each camera (for GET /events/latest). */
static void remember_latest(RTVisionPublisher *pub, const char *sensor_id, const GString *json) {
    LatestEvent *l = g_new(LatestEvent, 1);
    l->json = g_strndup(json->str, json->len);
    l->at_us = g_get_monotonic_time();

    g_mutex_lock(&pub->latest_lock);
    if (g_hash_table_size(pub->latest) >= LATEST_MAX &&
        !g_hash_table_contains(pub->latest, sensor_id)) {
        GHashTableIter it;
        gpointer key, value, oldest = NULL;
        gint64 oldest_us = G_MAXINT64;
        g_hash_table_iter_init(&it, pub->latest);
        while (g_hash_table_iter_next(&it, &key, &value)) {
            if (((LatestEvent *)value)->at_us < oldest_us) {
                oldest_us = ((LatestEvent *)value)->at_us;
                oldest = key;
            }
        }
        g_hash_table_remove(pub->latest, oldest);
    }
    g_hash_table_replace(pub->latest, g_strdup(sensor_id), l);
    g_mutex_unlock(&pub->latest_lock);
}

gchar *rtvision_publisher_latest_json(RTVisionPublisher *pub) {
    GString *out = g_string_new("{\"events\":[");
    GHashTableIter it;
    gpointer value;
    gboolean first = TRUE;

    g_mutex_lock(&pub->latest_lock);
    gint64 now = g_get_monotonic_time();
    g_hash_table_iter_init(&it, pub->latest);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        const LatestEvent *l = value;
        g_string_append_printf(out,
                               "%s{\"age_ms\":%" G_GINT64_FORMAT ",\"event\":", first ? "" : ",",
                               MAX(now - l->at_us, 0) / 1000);
        g_string_append(out, l->json);
        g_string_append_c(out, '}');
        first = FALSE;
    }
    g_mutex_unlock(&pub->latest_lock);
    g_string_append(out, "]}");
    return g_string_free(out, FALSE);
}

static gpointer worker_main(gpointer data) {
    RTVisionPublisher *pub = data;
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
            RTVisionFrameEvent *ev;
            while ((ev = g_queue_pop_head(&pub->queue)) != NULL)
                rtvision_event_free(ev);
            g_mutex_unlock(&pub->lock);
            if (left > 0) {
                add(&pub->dropped, left);
                PLOG_WARN("publisher", "event=shutdown_drop events=%u", left);
            }
            break;
        }
        RTVisionFrameEvent *ev = g_queue_pop_head(&pub->queue);
        g_mutex_unlock(&pub->lock);

        rtvision_event_to_json(ev, &pub->ctx, json);
        remember_latest(pub, ev->sensor_id ? ev->sensor_id : "", json);
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
        rtvision_event_free(ev);
        if (pub->backend->ops->poll)
            pub->backend->ops->poll(pub->backend);
    }
    g_string_free(json, TRUE);
    return NULL;
}

RTVisionPublisher *rtvision_publisher_new(RTVisionBackend *backend, const RTVisionEventContext *ctx,
                                          guint max_events, RTVisionDropPolicy policy,
                                          gboolean key_by_sensor) {
    RTVisionPublisher *pub = g_new0(RTVisionPublisher, 1);
    pub->backend = backend;
    backend->publisher = pub;
    pub->ctx = *ctx;
    pub->max_events = MAX(max_events, 1u);
    pub->policy = policy;
    pub->key_by_sensor = key_by_sensor;
    g_mutex_init(&pub->lock);
    g_cond_init(&pub->cond);
    g_queue_init(&pub->queue);
    g_mutex_init(&pub->latest_lock);
    pub->latest = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, latest_free);
    pub->worker = g_thread_new("rtvision-publisher", worker_main, pub);
    PLOG_INFO("publisher", "event=started backend=%s max_events=%u on_full=%s key=%s",
              backend->ops->name, pub->max_events,
              policy == RTVISION_DROP_OLDEST ? "drop_oldest" : "drop_newest",
              key_by_sensor ? "sensor_id" : "none");
    return pub;
}

gboolean rtvision_publisher_enqueue(RTVisionPublisher *pub, RTVisionFrameEvent *ev) {
    RTVisionFrameEvent *victim = NULL;
    gboolean accepted = TRUE;

    g_mutex_lock(&pub->lock);
    if (pub->stopping) {
        accepted = FALSE;
        victim = ev;
    } else if (g_queue_get_length(&pub->queue) >= pub->max_events) {
        if (pub->policy == RTVISION_DROP_OLDEST) {
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
        rtvision_event_free(victim); /* outside the lock */
    }
    return accepted;
}

gboolean rtvision_publisher_shutdown(RTVisionPublisher *pub, guint timeout_ms) {
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

    RTVisionPublisherStats s;
    rtvision_publisher_get_stats(pub, &s);
    PLOG_INFO("publisher",
              "event=stopped flushed=%d enqueued=%" G_GUINT64_FORMAT " published=%" G_GUINT64_FORMAT
              " dropped=%" G_GUINT64_FORMAT " failed=%" G_GUINT64_FORMAT
              " took_ms=%" G_GINT64_FORMAT,
              flushed, s.enqueued, s.published, s.dropped, s.failed,
              (g_get_monotonic_time() - start) / G_TIME_SPAN_MILLISECOND);
    return flushed && s.enqueued == s.published + s.dropped + s.failed && s.dropped == 0 &&
           s.failed == 0;
}

void rtvision_publisher_free(RTVisionPublisher *pub) {
    if (pub == NULL)
        return;
    if (!pub->shut_down)
        rtvision_publisher_shutdown(pub, 0);
    pub->backend->ops->destroy(pub->backend);
    g_queue_clear(&pub->queue);
    g_hash_table_destroy(pub->latest);
    g_mutex_clear(&pub->latest_lock);
    g_mutex_clear(&pub->lock);
    g_cond_clear(&pub->cond);
    g_free(pub);
}

void rtvision_publisher_get_stats(RTVisionPublisher *pub, RTVisionPublisherStats *out) {
    out->enqueued = get(&pub->enqueued);
    out->published = get(&pub->published);
    out->dropped = get(&pub->dropped);
    out->failed = get(&pub->failed);
    g_mutex_lock(&pub->lock);
    out->depth = g_queue_get_length(&pub->queue);
    g_mutex_unlock(&pub->lock);
    out->healthy = pub->backend->ops->healthy ? pub->backend->ops->healthy(pub->backend) : TRUE;
}
