/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* ops_server.c - libsoup-3.0 ops server (see ops_server.h). */
#include "ops_server.h"

#include <libsoup/soup.h>
#include <string.h>

#include "event_model.h"
#include "event_serialize.h"
#include "log.h"
#include "metrics.h"
#include "ota.h"
#include "perf.h"
#include "probe.h"

static GThread *thread;
static GMainContext *context;
static GMainLoop *loop;
static SoupServer *server;
static const PerceptraAppConfig *app_cfg;
static PerceptraPublisher *publisher;
static const char *version;
static gint started;        /* atomic */
static gint playing;        /* atomic */
static gint64 heartbeat_us; /* atomic, monotonic */

void perceptra_ops_set_started(void) {
    g_atomic_int_set(&started, 1);
}

void perceptra_ops_set_playing(gboolean v) {
    g_atomic_int_set(&playing, v ? 1 : 0);
}

void perceptra_ops_heartbeat(void) {
    __atomic_store_n(&heartbeat_us, g_get_monotonic_time(), __ATOMIC_RELAXED);
}

static void respond(SoupServerMessage *msg, guint status, const char *type, const char *body,
                    gsize len) {
    soup_server_message_set_status(msg, status, NULL);
    soup_message_headers_replace(soup_server_message_get_response_headers(msg), "Cache-Control",
                                 "no-store");
    soup_server_message_set_response(msg, type, SOUP_MEMORY_COPY, body, len);
}

static void respond_status(SoupServerMessage *msg, gboolean ok, const char *ok_status,
                           const char *reason, const char *extra_json) {
    GString *b = g_string_new("{\"status\":");
    perceptra_json_append_string(b, ok ? ok_status : reason);
    if (extra_json)
        g_string_append(b, extra_json);
    g_string_append_c(b, '}');
    respond(msg, ok ? SOUP_STATUS_OK : SOUP_STATUS_SERVICE_UNAVAILABLE, "application/json", b->str,
            b->len);
    g_string_free(b, TRUE);
}

typedef struct {
    GArray *objects; /* PerceptraMetricsObjects */
} ObjectsAcc;

static void collect_object(const gchar *sensor_id, const gchar *label, guint64 count,
                           gpointer user_data) {
    ObjectsAcc *acc = user_data;
    PerceptraMetricsObjects o = {sensor_id, label, count};
    g_array_append_val(acc->objects, o);
}

static void handle_metrics(SoupServerMessage *msg) {
    static PerceptraProbeSourceStat sources[MAX_SOURCE_BINS]; /* ops thread only */
    static PerceptraPerfStream perf[MAX_SOURCE_BINS];
    guint n_sources = perceptra_probe_sources(sources, MAX_SOURCE_BINS);
    guint n_perf = perceptra_perf_snapshot(perf, MAX_SOURCE_BINS);

    GArray *streams = g_array_sized_new(FALSE, FALSE, sizeof(PerceptraMetricsStream), n_sources);
    for (guint i = 0; i < n_sources; i++) {
        PerceptraMetricsStream s = {sources[i].sensor_id, sources[i].frames};
        g_array_append_val(streams, s);
    }
    GArray *fps = g_array_sized_new(FALSE, FALSE, sizeof(PerceptraMetricsFps), n_perf);
    for (guint i = 0; i < n_perf; i++) {
        PerceptraMetricsFps f = {perf[i].sensor_id, perf[i].fps};
        g_array_append_val(fps, f);
    }
    ObjectsAcc acc = {g_array_new(FALSE, FALSE, sizeof(PerceptraMetricsObjects))};
    perceptra_probe_foreach_object_count(collect_object, &acc);

    PerceptraMetricsSnapshot snap = {
        .streams = (PerceptraMetricsStream *)(void *)streams->data,
        .n_streams = streams->len,
        .fps = (PerceptraMetricsFps *)(void *)fps->data,
        .n_fps = fps->len,
        .objects = (PerceptraMetricsObjects *)(void *)acc.objects->data,
        .n_objects = acc.objects->len,
        .streams_active = perceptra_perf_active_sources(),
        .has_publisher = publisher != NULL,
        .model_name = app_cfg->model_name,
        .model_version = app_cfg->model_version,
        .model_task = app_cfg->model_task,
        .model_family = app_cfg->model_family,
        .model_precision = app_cfg->model_precision,
        .model_license = app_cfg->model_license,
    };
    if (publisher) {
        PerceptraPublisherStats ps;
        perceptra_publisher_get_stats(publisher, &ps);
        snap.events_enqueued = ps.enqueued;
        snap.events_published = ps.published;
        snap.events_dropped = ps.dropped;
        snap.events_failed = ps.failed;
        snap.queue_depth = ps.depth;
    }
    perceptra_ota_counts(&snap.swaps_ok, &snap.swaps_failed);

    GString *out = g_string_sized_new(8192);
    perceptra_metrics_render(&snap, out);
    respond(msg, SOUP_STATUS_OK, "text/plain; version=0.0.4; charset=utf-8", out->str, out->len);
    g_string_free(out, TRUE);
    g_array_free(streams, TRUE);
    g_array_free(fps, TRUE);
    g_array_free(acc.objects, TRUE);
}

static void handle_info(SoupServerMessage *msg) {
    GString *b = g_string_new("{\"service\":");
    perceptra_json_append_string(b, app_cfg->service_name);
    g_string_append(b, ",\"version\":");
    perceptra_json_append_string(b, version);
    g_string_append(b, ",\"model\":{\"name\":");
    perceptra_json_append_string(b, app_cfg->model_name);
    g_string_append(b, ",\"version\":");
    perceptra_json_append_string(b, app_cfg->model_version);
    g_string_append(b, ",\"task\":");
    perceptra_json_append_string(b, app_cfg->model_task);
    g_string_append(b, ",\"family\":");
    perceptra_json_append_string(b, app_cfg->model_family);
    g_string_append(b, ",\"precision\":");
    perceptra_json_append_string(b, app_cfg->model_precision);
    g_string_append(b, ",\"license\":");
    perceptra_json_append_string(b, app_cfg->model_license);
    g_string_append_printf(b, "},\"max_streams\":%u,\"schema\":", app_cfg->max_streams);
    perceptra_json_append_string(b, PERCEPTRA_SCHEMA_ID);
    g_string_append_c(b, '}');
    respond(msg, SOUP_STATUS_OK, "application/json", b->str, b->len);
    g_string_free(b, TRUE);
}

static void handler(SoupServer *srv, SoupServerMessage *msg, const char *path, GHashTable *query,
                    gpointer user_data) {
    (void)srv;
    (void)query;
    (void)user_data;
    const char *method = soup_server_message_get_method(msg);
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        static const char body[] = "{\"status\":\"method not allowed\"}";
        respond(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "application/json", body, sizeof(body) - 1);
        return;
    }
    if (strcmp(path, "/live") == 0) {
        /* Before startup completes the main loop is not running yet (pipeline creation and
         * engine builds can take minutes): the process counts as alive, /startup gates it. */
        gint64 age_us = g_get_monotonic_time() - __atomic_load_n(&heartbeat_us, __ATOMIC_RELAXED);
        respond_status(msg,
                       !g_atomic_int_get(&started) ||
                           age_us < PERCEPTRA_LIVE_MAX_AGE_S * G_TIME_SPAN_SECOND,
                       "alive", "main loop stalled", NULL);
    } else if (strcmp(path, "/startup") == 0) {
        respond_status(msg, g_atomic_int_get(&started), "started", "starting", NULL);
    } else if (strcmp(path, "/ready") == 0) {
        gboolean is_playing = g_atomic_int_get(&playing);
        gboolean pub_ok = TRUE;
        if (publisher) {
            PerceptraPublisherStats ps;
            perceptra_publisher_get_stats(publisher, &ps);
            pub_ok = ps.healthy;
        }
        gchar *extra = g_strdup_printf(",\"streams\":%u", perceptra_perf_active_sources());
        respond_status(msg, is_playing && pub_ok, "ready",
                       !is_playing ? "pipeline not playing" : "publisher not connected", extra);
        g_free(extra);
    } else if (strcmp(path, "/metrics") == 0) {
        handle_metrics(msg);
    } else if (strcmp(path, "/info") == 0) {
        handle_info(msg);
    } else {
        static const char body[] = "{\"status\":\"not found\"}";
        respond(msg, SOUP_STATUS_NOT_FOUND, "application/json", body, sizeof(body) - 1);
    }
}

typedef struct {
    guint port;
    GError *error;
    GMutex lock;
    GCond cond;
    gboolean done;
} StartSync;

static gpointer server_main(gpointer data) {
    StartSync *sync = data;
    g_main_context_push_thread_default(context);
    server = soup_server_new("server-header", "perceptra-ops", NULL);
    soup_server_add_handler(server, NULL, handler, NULL, NULL);
    GError *error = NULL;
    gboolean ok = soup_server_listen_all(server, sync->port, 0, &error);
    g_mutex_lock(&sync->lock);
    sync->error = error;
    sync->done = TRUE;
    g_cond_signal(&sync->cond);
    g_mutex_unlock(&sync->lock);
    if (ok)
        g_main_loop_run(loop);
    soup_server_disconnect(server);
    g_clear_object(&server);
    g_main_context_pop_thread_default(context);
    return NULL;
}

gboolean perceptra_ops_start(guint port, const PerceptraAppConfig *cfg, PerceptraPublisher *pub,
                             const char *ver, GError **error) {
    app_cfg = cfg;
    publisher = pub;
    version = ver;
    perceptra_ops_heartbeat();
    context = g_main_context_new();
    loop = g_main_loop_new(context, FALSE);
    StartSync sync = {.port = port};
    g_mutex_init(&sync.lock);
    g_cond_init(&sync.cond);
    thread = g_thread_new("perceptra-ops", server_main, &sync);
    g_mutex_lock(&sync.lock);
    while (!sync.done)
        g_cond_wait(&sync.cond, &sync.lock);
    g_mutex_unlock(&sync.lock);
    g_mutex_clear(&sync.lock);
    g_cond_clear(&sync.cond);
    if (sync.error) {
        g_propagate_error(error, sync.error);
        perceptra_ops_stop();
        return FALSE;
    }
    PLOG_INFO("ops", "event=listening port=%u paths=/live,/ready,/startup,/metrics,/info", port);
    return TRUE;
}

static gboolean quit_loop(gpointer data) {
    (void)data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

void perceptra_ops_stop(void) {
    if (thread == NULL)
        return;
    g_main_context_invoke(context, quit_loop, NULL);
    g_thread_join(thread);
    thread = NULL;
    g_clear_pointer(&loop, g_main_loop_unref);
    g_clear_pointer(&context, g_main_context_unref);
    publisher = NULL;
}
