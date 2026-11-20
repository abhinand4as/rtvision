/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* app_config.c - app.ini reader (see app_config.h). */
#include "app_config.h"

#include <string.h>

#define DEFAULT_PROBE_LOG_EVERY_N_FRAMES 300

static const char *const backend_names[] = {"none", "kafka", "stdout", "file"};

const char *perceptra_backend_kind_name(PerceptraBackendKind kind) {
    return (guint)kind < G_N_ELEMENTS(backend_names) ? backend_names[kind] : "?";
}

void perceptra_app_config_init(PerceptraAppConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->service_name = g_strdup("perceptra");
    cfg->model_name = g_strdup("unknown");
    cfg->model_version = g_strdup("unknown");
    cfg->model_task = g_strdup("detect");
    cfg->model_family = g_strdup("unknown");
    cfg->model_license = g_strdup("unknown");
    cfg->publish.backend = PERCEPTRA_BACKEND_NONE;
    cfg->publish.interval_frames = 30;
    cfg->publish.queue_max_events = 10000;
    cfg->publish.on_full = PERCEPTRA_DROP_OLDEST;
    cfg->publish.shutdown_flush_timeout_ms = 5000;
    cfg->publish.kafka_key_sensor_id = TRUE;
    cfg->timestamp_source = PERCEPTRA_TS_SYSTEM;
    cfg->log_level = PERCEPTRA_LOG_INFO;
    cfg->probe_log_every_n_frames = DEFAULT_PROBE_LOG_EVERY_N_FRAMES;
}

void perceptra_app_config_clear(PerceptraAppConfig *cfg) {
    g_clear_pointer(&cfg->service_name, g_free);
    g_clear_pointer(&cfg->model_name, g_free);
    g_clear_pointer(&cfg->model_version, g_free);
    g_clear_pointer(&cfg->model_task, g_free);
    g_clear_pointer(&cfg->model_family, g_free);
    g_clear_pointer(&cfg->model_license, g_free);
    g_clear_pointer(&cfg->publish.kafka_brokers, g_free);
    g_clear_pointer(&cfg->publish.kafka_topic, g_free);
    g_clear_pointer(&cfg->publish.kafka_properties_file, g_free);
    g_clear_pointer(&cfg->publish.file_path, g_free);
}

/* Replaces *dst with [group] key if present (empty string -> NULL). */
static void take_string(GKeyFile *kf, const char *group, const char *key, gchar **dst) {
    gchar *v = g_key_file_get_string(kf, group, key, NULL);
    if (v == NULL)
        return;
    g_strstrip(v);
    g_free(*dst);
    if (*v == '\0') {
        g_free(v);
        *dst = NULL;
    } else {
        *dst = v;
    }
}

static gboolean invalid(GError **error, const char *group, const char *key, const char *value,
                        const char *expected) {
    g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                "[%s] %s: invalid value '%s' (expected %s)", group, key, value ? value : "",
                expected);
    return FALSE;
}

static gboolean take_uint(GKeyFile *kf, const char *group, const char *key, guint min, guint *dst,
                          GError **error) {
    if (!g_key_file_has_key(kf, group, key, NULL))
        return TRUE;
    gchar *v = g_key_file_get_string(kf, group, key, NULL);
    gchar *end = NULL;
    guint64 n = g_ascii_strtoull(g_strstrip(v), &end, 10);
    gboolean ok = *v != '\0' && end && *end == '\0' && n >= min && n <= G_MAXUINT;
    if (ok)
        *dst = (guint)n;
    else {
        gchar *exp = g_strdup_printf("an integer >= %u", min);
        invalid(error, group, key, v, exp);
        g_free(exp);
    }
    g_free(v);
    return ok;
}

static gboolean take_bool(GKeyFile *kf, const char *group, const char *key, gboolean *dst,
                          GError **error) {
    if (!g_key_file_has_key(kf, group, key, NULL))
        return TRUE;
    gchar *v = g_strstrip(g_key_file_get_string(kf, group, key, NULL));
    gboolean ok = TRUE;
    if (g_ascii_strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0)
        *dst = TRUE;
    else if (g_ascii_strcasecmp(v, "false") == 0 || strcmp(v, "0") == 0)
        *dst = FALSE;
    else
        ok = invalid(error, group, key, v, "true|false");
    g_free(v);
    return ok;
}

/* Matches [group] key against `names`; stores the index. */
static gboolean take_enum(GKeyFile *kf, const char *group, const char *key,
                          const char *const *names, guint n, guint *dst, const char *expected,
                          GError **error) {
    gchar *v = g_key_file_get_string(kf, group, key, NULL);
    if (v == NULL)
        return TRUE;
    g_strstrip(v);
    for (guint i = 0; i < n; i++) {
        if (strcmp(v, names[i]) == 0) {
            *dst = i;
            g_free(v);
            return TRUE;
        }
    }
    invalid(error, group, key, v, expected);
    g_free(v);
    return FALSE;
}

gboolean perceptra_app_config_load(const char *path, PerceptraAppConfig *cfg, GError **error) {
    GKeyFile *kf = g_key_file_new();
    gboolean ok = FALSE;
    PerceptraPublishConfig *p = &cfg->publish;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, error))
        goto out;

    take_string(kf, "service", "name", &cfg->service_name);
    if (!take_uint(kf, "service", "max_streams", 1, &cfg->max_streams, error))
        goto out;
    take_string(kf, "model", "name", &cfg->model_name);
    take_string(kf, "model", "version", &cfg->model_version);
    take_string(kf, "model", "task", &cfg->model_task);
    take_string(kf, "model", "family", &cfg->model_family);
    take_string(kf, "model", "license", &cfg->model_license);
    if (!cfg->service_name || !cfg->model_name || !cfg->model_version || !cfg->model_task) {
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                    "[service] name and [model] name/version/task must not be empty");
        goto out;
    }

    guint idx = p->backend;
    static const char *const backends[] = {"none", "kafka", "stdout", "file"};
    if (!take_enum(kf, "publish", "backend", backends, G_N_ELEMENTS(backends), &idx,
                   "kafka|stdout|file", error))
        goto out;
    p->backend = (PerceptraBackendKind)idx;
    if (!take_uint(kf, "publish", "interval_frames", 1, &p->interval_frames, error) ||
        !take_bool(kf, "publish", "include_empty_frames", &p->include_empty_frames, error) ||
        !take_bool(kf, "publish", "include_embeddings", &p->include_embeddings, error) ||
        !take_uint(kf, "publish", "queue_max_events", 1, &p->queue_max_events, error) ||
        !take_uint(kf, "publish", "shutdown_flush_timeout_ms", 0, &p->shutdown_flush_timeout_ms,
                   error))
        goto out;
    static const char *const policies[] = {"drop_oldest", "drop_newest"};
    idx = p->on_full;
    if (!take_enum(kf, "publish", "on_full", policies, 2, &idx, "drop_oldest|drop_newest", error))
        goto out;
    p->on_full = (PerceptraDropPolicy)idx;
    take_string(kf, "publish", "kafka_brokers", &p->kafka_brokers);
    take_string(kf, "publish", "kafka_topic", &p->kafka_topic);
    take_string(kf, "publish", "kafka_properties_file", &p->kafka_properties_file);
    take_string(kf, "publish", "file_path", &p->file_path);
    static const char *const keys[] = {"none", "sensor_id"};
    idx = p->kafka_key_sensor_id ? 1 : 0;
    if (!take_enum(kf, "publish", "kafka_key", keys, 2, &idx, "sensor_id|none", error))
        goto out;
    p->kafka_key_sensor_id = idx == 1;
    if (p->backend == PERCEPTRA_BACKEND_KAFKA && (!p->kafka_brokers || !p->kafka_topic)) {
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                    "[publish] backend=kafka needs kafka_brokers and kafka_topic");
        goto out;
    }
    if (p->backend == PERCEPTRA_BACKEND_FILE && !p->file_path) {
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                    "[publish] backend=file needs file_path");
        goto out;
    }

    gchar *ts = g_key_file_get_string(kf, "timestamps", "source", NULL);
    if (ts) {
        gboolean valid = perceptra_timestamp_source_parse(g_strstrip(ts), &cfg->timestamp_source);
        if (!valid)
            invalid(error, "timestamps", "source", ts, "ntp|uri_utc|system");
        g_free(ts);
        if (!valid)
            goto out;
    }

    if (!take_bool(kf, "tracker", "reid", &cfg->tracker_reid, error) ||
        !take_uint(kf, "tracker", "reid_store_age", 0, &cfg->tracker_reid_store_age, error))
        goto out;
    if (p->include_embeddings && !cfg->tracker_reid) {
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                    "[publish] include_embeddings=true needs [tracker] reid=true");
        goto out;
    }

    gchar *level = g_key_file_get_string(kf, "ops", "log_level", NULL);
    if (level) {
        gboolean valid = perceptra_log_parse_level(g_strstrip(level), &cfg->log_level);
        if (!valid)
            invalid(error, "ops", "log_level", level, "error|warn|info|debug");
        g_free(level);
        if (!valid)
            goto out;
    }
    if (!take_uint(kf, "probe", "log_every_n_frames", 0, &cfg->probe_log_every_n_frames, error))
        goto out;
    ok = TRUE;
out:
    g_key_file_free(kf);
    return ok;
}
