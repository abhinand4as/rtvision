/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* app_config.h - settings from the generated app.ini (GKeyFile), see docs/CONFIGURATION.md.
 *
 * Missing keys keep their defaults; unknown groups/keys are ignored so a newer generator works
 * with this binary. Invalid values are errors. Without a [publish] backend no events are
 * published (useful for pipeline-only runs).
 */
#ifndef RTVISION_APP_CONFIG_H
#define RTVISION_APP_CONFIG_H

#include <glib.h>

#include "event_model.h"
#include "log.h"
#include "publisher.h"

G_BEGIN_DECLS

typedef enum {
    RTVISION_BACKEND_NONE = 0,
    RTVISION_BACKEND_KAFKA,
    RTVISION_BACKEND_STDOUT,
    RTVISION_BACKEND_FILE,
} RTVisionBackendKind;

typedef struct {
    RTVisionBackendKind backend;
    guint interval_frames;
    gboolean include_empty_frames;
    gboolean include_embeddings;
    guint queue_max_events;
    RTVisionDropPolicy on_full;
    guint shutdown_flush_timeout_ms;
    gchar *kafka_brokers;
    gchar *kafka_topic;
    gboolean kafka_key_sensor_id;
    gchar *kafka_properties_file;
    gchar *file_path;
} RTVisionPublishConfig;

typedef struct {
    gchar *service_name;
    guint max_streams;
    gchar *model_name;
    gchar *model_version;
    gchar *model_task;
    gchar *model_family;
    gchar *model_license;
    gchar *model_precision;
    gchar *hot_swap_file; /* NULL: hot swap disabled */
    guint ops_port;       /* 0: ops server disabled */
    RTVisionPublishConfig publish;
    RTVisionTimestampSource timestamp_source;
    gboolean tracker_reid;
    guint tracker_reid_store_age;
    RTVisionLogLevel log_level;
    guint probe_log_every_n_frames;
} RTVisionAppConfig;

/* Fills defaults. Always pair with rtvision_app_config_clear(). */
void rtvision_app_config_init(RTVisionAppConfig *cfg);

/* Loads app.ini over the current values. Returns FALSE and sets error on I/O, syntax or value
 * errors (messages name the [group] key). */
gboolean rtvision_app_config_load(const char *path, RTVisionAppConfig *cfg, GError **error);

void rtvision_app_config_clear(RTVisionAppConfig *cfg);

const char *rtvision_backend_kind_name(RTVisionBackendKind kind);

G_END_DECLS

#endif /* RTVISION_APP_CONFIG_H */
