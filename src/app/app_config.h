/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* app_config.h - settings from the generated app.ini (GKeyFile), see docs/CONFIGURATION.md.
 *
 * Missing keys keep their defaults; unknown groups/keys are ignored so a newer generator works
 * with this binary. Invalid values are errors. Without a [publish] backend no events are
 * published (useful for pipeline-only runs).
 */
#ifndef PERCEPTRA_APP_CONFIG_H
#define PERCEPTRA_APP_CONFIG_H

#include <glib.h>

#include "event_model.h"
#include "log.h"
#include "publisher.h"

G_BEGIN_DECLS

typedef enum {
    PERCEPTRA_BACKEND_NONE = 0,
    PERCEPTRA_BACKEND_KAFKA,
    PERCEPTRA_BACKEND_STDOUT,
    PERCEPTRA_BACKEND_FILE,
} PerceptraBackendKind;

typedef struct {
    PerceptraBackendKind backend;
    guint interval_frames;
    gboolean include_empty_frames;
    gboolean include_embeddings;
    guint queue_max_events;
    PerceptraDropPolicy on_full;
    guint shutdown_flush_timeout_ms;
    gchar *kafka_brokers;
    gchar *kafka_topic;
    gboolean kafka_key_sensor_id;
    gchar *kafka_properties_file;
    gchar *file_path;
} PerceptraPublishConfig;

typedef struct {
    gchar *service_name;
    guint max_streams;
    gchar *model_name;
    gchar *model_version;
    gchar *model_task;
    gchar *model_family;
    gchar *model_license;
    PerceptraPublishConfig publish;
    PerceptraTimestampSource timestamp_source;
    PerceptraLogLevel log_level;
    guint probe_log_every_n_frames;
} PerceptraAppConfig;

/* Fills defaults. Always pair with perceptra_app_config_clear(). */
void perceptra_app_config_init(PerceptraAppConfig *cfg);

/* Loads app.ini over the current values. Returns FALSE and sets error on I/O, syntax or value
 * errors (messages name the [group] key). */
gboolean perceptra_app_config_load(const char *path, PerceptraAppConfig *cfg, GError **error);

void perceptra_app_config_clear(PerceptraAppConfig *cfg);

const char *perceptra_backend_kind_name(PerceptraBackendKind kind);

G_END_DECLS

#endif /* PERCEPTRA_APP_CONFIG_H */
