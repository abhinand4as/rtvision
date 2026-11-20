/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modifications Copyright (c) 2026 <PROJECT_OWNER>
 * Changes: derived from the OTA model-update code of RT-CV src/metropolis_perception_app.c
 * (ota_handler_thread, apply_ota, infer_model_updated_cb); the override file is a small INI read
 * with GKeyFile instead of a full deepstream-app config; poll() with a timeout so the thread
 * stops cleanly; debounced events; fixed the "..data" ConfigMap check (strstr arguments were
 * swapped); the property is set on the main thread; results feed metrics.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* ota.c - model hot swap (see ota.h). */
#include "ota.h"

#include <errno.h>
#include <gio/gio.h>
#include <poll.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "log.h"

#define POLL_TIMEOUT_MS 500
#define DEBOUNCE_US (300 * G_TIME_SPAN_MILLISECOND)
#define EVENT_BUF_LEN (64 * (sizeof(struct inotify_event) + 256))

static GThread *thread;
static gint stop_requested;
static int inotify_fd = -1;
static gchar *watch_path;
static gchar *watch_name;
static GstElement *pgie;
static gulong signal_id;
static guint64 swaps_ok, swaps_failed;
static gint64 request_time_us; /* atomic */
static gchar *last_applied;    /* main thread only */

void perceptra_ota_counts(guint64 *ok, guint64 *failed) {
    *ok = __atomic_load_n(&swaps_ok, __ATOMIC_RELAXED);
    *failed = __atomic_load_n(&swaps_failed, __ATOMIC_RELAXED);
}

static void count(gboolean ok) {
    __atomic_add_fetch(ok ? &swaps_ok : &swaps_failed, 1, __ATOMIC_RELAXED);
}

/* nvinfer "model-updated": err == 0 on success. nvinfer also emits it for the initial model load
 * at startup; only signals answering a swap request (request_time_us set) are counted. */
static void on_model_updated(GstElement *gie, gint err, const gchar *config_file, gpointer data) {
    (void)gie;
    (void)data;
    gint64 started = __atomic_exchange_n(&request_time_us, 0, __ATOMIC_RELAXED);
    if (started == 0) {
        PLOG_DEBUG("ota", "event=model_loaded err=%d config=\"%s\" (not a swap)", err,
                   config_file ? config_file : "");
        return;
    }
    gint64 took_ms = (g_get_monotonic_time() - started) / 1000;
    count(err == 0);
    if (err == 0)
        PLOG_INFO("ota", "event=model_swapped result=ok config=\"%s\" took_ms=%" G_GINT64_FORMAT,
                  config_file ? config_file : "", took_ms);
    else
        PLOG_ERROR(
            "ota",
            "event=model_swapped result=failed error=%d config=\"%s\" took_ms=%" G_GINT64_FORMAT,
            err, config_file ? config_file : "", took_ms);
}

/* Main thread: read the override file and set the nvinfer property. */
static gboolean apply_override(gpointer data) {
    (void)data;
    GKeyFile *kf = g_key_file_new();
    GError *error = NULL;
    gchar *engine = NULL, *config = NULL;

    if (!g_key_file_load_from_file(kf, watch_path, G_KEY_FILE_NONE, &error)) {
        PLOG_ERROR("ota", "event=override_invalid file=%s error=\"%s\"", watch_path,
                   error->message);
        count(FALSE);
        goto out;
    }
    engine = g_key_file_get_string(kf, "primary-gie", "model-engine-file", NULL);
    config = g_key_file_get_string(kf, "primary-gie", "config-file", NULL);
    const char *prop = engine ? "model-engine-file" : "config-file-path";
    gchar *value = engine ? g_strstrip(engine) : (config ? g_strstrip(config) : NULL);
    if (value == NULL || *value == '\0') {
        PLOG_ERROR("ota",
                   "event=override_invalid file=%s error=\"[primary-gie] needs model-engine-file "
                   "or config-file\"",
                   watch_path);
        count(FALSE);
        goto out;
    }
    if (g_strcmp0(value, last_applied) == 0) {
        PLOG_DEBUG("ota", "event=override_unchanged value=%s", value);
        goto out;
    }
    if (!g_file_test(value, G_FILE_TEST_IS_REGULAR)) {
        PLOG_ERROR("ota", "event=override_invalid %s=%s error=\"file not found\"", prop, value);
        count(FALSE);
        goto out;
    }
    g_free(last_applied);
    last_applied = g_strdup(value);
    __atomic_store_n(&request_time_us, g_get_monotonic_time(), __ATOMIC_RELAXED);
    PLOG_INFO("ota", "event=model_swap_requested %s=%s", prop, value);
    g_object_set(G_OBJECT(pgie), prop, value, NULL); /* nvinfer loads it asynchronously */
out:
    g_clear_error(&error);
    g_free(engine);
    g_free(config);
    g_key_file_free(kf);
    return G_SOURCE_REMOVE;
}

static gboolean event_matches(const struct inotify_event *ev) {
    if (ev->len == 0 || (ev->mask & IN_ISDIR && strcmp(ev->name, "..data") != 0))
        return FALSE;
    if (!(ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)))
        return FALSE;
    /* the file itself, or a Kubernetes ConfigMap update (atomic swap of the "..data" link) */
    return strcmp(ev->name, watch_name) == 0 || strcmp(ev->name, "..data") == 0;
}

static gpointer watcher_main(gpointer data) {
    (void)data;
    char buf[EVENT_BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));
    gint64 pending_since = 0;
    struct pollfd pfd = {.fd = inotify_fd, .events = POLLIN};

    while (!g_atomic_int_get(&stop_requested)) {
        int r = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (r < 0 && errno != EINTR) {
            PLOG_ERROR("ota", "event=watch_error error=\"%s\"", g_strerror(errno));
            break;
        }
        if (r > 0 && (pfd.revents & POLLIN)) {
            ssize_t len = read(inotify_fd, buf, sizeof(buf));
            for (char *p = buf; len > 0 && p < buf + len;) {
                const struct inotify_event *ev = (const struct inotify_event *)p;
                if (event_matches(ev))
                    pending_since = g_get_monotonic_time();
                p += sizeof(struct inotify_event) + ev->len;
            }
        }
        /* Debounce: editors and ConfigMaps produce bursts of events. */
        if (pending_since && g_get_monotonic_time() - pending_since >= DEBOUNCE_US) {
            pending_since = 0;
            g_main_context_invoke(NULL, apply_override, NULL);
        }
    }
    return NULL;
}

gboolean perceptra_ota_start(AppCtx *app_ctx, const char *watch_file, GError **error) {
    pgie = app_ctx->pipeline.common_elements.primary_gie_bin.primary_gie;
    if (pgie == NULL) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "no primary nvinfer");
        return FALSE;
    }
    gchar *dir = g_path_get_dirname(watch_file);
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "hot-swap directory %s missing", dir);
        g_free(dir);
        return FALSE;
    }
    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0 ||
        inotify_add_watch(inotify_fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "inotify on %s: %s", dir,
                    g_strerror(errno));
        if (inotify_fd >= 0)
            close(inotify_fd);
        inotify_fd = -1;
        g_free(dir);
        return FALSE;
    }
    watch_path = g_strdup(watch_file);
    watch_name = g_path_get_basename(watch_file);
    signal_id =
        g_signal_connect(G_OBJECT(pgie), "model-updated", G_CALLBACK(on_model_updated), NULL);
    g_atomic_int_set(&stop_requested, 0);
    thread = g_thread_new("perceptra-ota", watcher_main, NULL);
    PLOG_INFO("ota", "event=watching file=%s dir=%s", watch_file, dir);
    g_free(dir);
    return TRUE;
}

void perceptra_ota_stop(void) {
    if (thread == NULL)
        return;
    g_atomic_int_set(&stop_requested, 1);
    g_thread_join(thread); /* returns within POLL_TIMEOUT_MS */
    thread = NULL;
    if (pgie && signal_id)
        g_signal_handler_disconnect(pgie, signal_id);
    signal_id = 0;
    close(inotify_fd);
    inotify_fd = -1;
    g_clear_pointer(&watch_path, g_free);
    g_clear_pointer(&watch_name, g_free);
    g_clear_pointer(&last_applied, g_free);
}
