/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* ota.h - model hot swap (ARCHITECTURE §5.3).
 *
 * Watches an override file (INI) and applies it to the running primary nvinfer:
 *
 *   [primary-gie]
 *   model-engine-file=/opt/perceptra/engines/.../new.engine   # or:
 *   config-file=/path/to/new-pgie.ini
 *
 * Triggers: the file is written (IN_CLOSE_WRITE), moved into place (IN_MOVED_TO), or a
 * Kubernetes ConfigMap update swaps the "..data" symlink. The new model must keep the task,
 * family and input size; otherwise restart. Results (nvinfer "model-updated") are logged and
 * counted (perceptra_model_swaps_total{result}).
 */
#ifndef PERCEPTRA_OTA_H
#define PERCEPTRA_OTA_H

#include <glib.h>

#include "deepstream_app.h"

G_BEGIN_DECLS

/* Starts the watcher thread. The directory of watch_file must exist; the file itself may be
 * created later. Must be called after create_pipeline(). */
gboolean perceptra_ota_start(AppCtx *app_ctx, const char *watch_file, GError **error);
void perceptra_ota_stop(void);
void perceptra_ota_counts(guint64 *ok, guint64 *failed);

G_END_DECLS

#endif /* PERCEPTRA_OTA_H */
