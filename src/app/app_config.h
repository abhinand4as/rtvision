/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* app_config.h - settings from the generated app.ini (GKeyFile), see docs/CONFIGURATION.md.
 *
 * M1 reads [model] identity, [ops] log_level and [probe] log_every_n_frames. Missing keys keep
 * their defaults; unknown groups/keys are ignored so newer generators work with this binary.
 */
#ifndef PERCEPTRA_APP_CONFIG_H
#define PERCEPTRA_APP_CONFIG_H

#include <glib.h>

#include "log.h"

G_BEGIN_DECLS

typedef struct {
    gchar *model_name;
    gchar *model_version;
    gchar *model_task;
    gchar *model_family;
    PerceptraLogLevel log_level;
    guint probe_log_every_n_frames;
} PerceptraAppConfig;

/* Fills defaults. Always pair with perceptra_app_config_clear(). */
void perceptra_app_config_init(PerceptraAppConfig *cfg);

/* Loads app.ini over the current values. Returns FALSE and sets error on I/O, syntax or value
 * errors. */
gboolean perceptra_app_config_load(const char *path, PerceptraAppConfig *cfg, GError **error);

void perceptra_app_config_clear(PerceptraAppConfig *cfg);

G_END_DECLS

#endif /* PERCEPTRA_APP_CONFIG_H */
