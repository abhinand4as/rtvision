/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* app_config.c - app.ini reader (see app_config.h). */
#include "app_config.h"

#define DEFAULT_PROBE_LOG_EVERY_N_FRAMES 300

void perceptra_app_config_init(PerceptraAppConfig *cfg) {
    cfg->model_name = g_strdup("unknown");
    cfg->model_version = g_strdup("unknown");
    cfg->model_task = g_strdup("detect");
    cfg->model_family = g_strdup("unknown");
    cfg->log_level = PERCEPTRA_LOG_INFO;
    cfg->probe_log_every_n_frames = DEFAULT_PROBE_LOG_EVERY_N_FRAMES;
}

void perceptra_app_config_clear(PerceptraAppConfig *cfg) {
    g_clear_pointer(&cfg->model_name, g_free);
    g_clear_pointer(&cfg->model_version, g_free);
    g_clear_pointer(&cfg->model_task, g_free);
    g_clear_pointer(&cfg->model_family, g_free);
}

/* Replaces *dst with [group] key if present. */
static void take_string(GKeyFile *kf, const char *group, const char *key, gchar **dst) {
    gchar *v = g_key_file_get_string(kf, group, key, NULL);
    if (v) {
        g_free(*dst);
        *dst = g_strstrip(v);
    }
}

gboolean perceptra_app_config_load(const char *path, PerceptraAppConfig *cfg, GError **error) {
    GKeyFile *kf = g_key_file_new();
    gboolean ok = FALSE;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, error))
        goto out;

    take_string(kf, "model", "name", &cfg->model_name);
    take_string(kf, "model", "version", &cfg->model_version);
    take_string(kf, "model", "task", &cfg->model_task);
    take_string(kf, "model", "family", &cfg->model_family);

    gchar *level = g_key_file_get_string(kf, "ops", "log_level", NULL);
    if (level) {
        gboolean valid = perceptra_log_parse_level(g_strstrip(level), &cfg->log_level);
        if (!valid)
            g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                        "[ops] log_level: invalid value '%s' (error|warn|info|debug)", level);
        g_free(level);
        if (!valid)
            goto out;
    }

    if (g_key_file_has_key(kf, "probe", "log_every_n_frames", NULL)) {
        GError *e = NULL;
        gint n = g_key_file_get_integer(kf, "probe", "log_every_n_frames", &e);
        if (e || n < 0) {
            g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                        "[probe] log_every_n_frames: expected an integer >= 0");
            g_clear_error(&e);
            goto out;
        }
        cfg->probe_log_every_n_frames = (guint)n;
    }
    ok = TRUE;
out:
    g_key_file_free(kf);
    return ok;
}
