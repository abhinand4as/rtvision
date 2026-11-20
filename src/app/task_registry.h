/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* task_registry.h - model task -> handler table (extension seam, docs/ROADMAP.md).
 *
 * v1: "detect" is implemented; "segment" and "pose" return stubs whose init() fails with
 * "task not supported in this version".
 */
#ifndef PERCEPTRA_TASK_REGISTRY_H
#define PERCEPTRA_TASK_REGISTRY_H

#include <glib.h>

#include "deepstream_app.h"
#include "event_model.h"

G_BEGIN_DECLS

typedef struct {
    const char *name;
    /* Called once at startup (main thread). */
    gboolean (*init)(GError **error);
    /* Fills ev->objects (up to ev->capacity) from one frame. Runs on the streaming thread:
     * must not block, do I/O or allocate without bound. Returns the number of objects that did
     * not fit (overflow). */
    guint (*fill_event)(AppCtx *app_ctx, NvDsFrameMeta *frame_meta, PerceptraFrameEvent *ev);
    void (*shutdown)(void);
} PerceptraTaskHandler;

/* NULL for an unknown task name. */
const PerceptraTaskHandler *perceptra_task_get(const char *task);

/* detect handler (task_detect.c). */
extern const PerceptraTaskHandler perceptra_task_detect;

G_END_DECLS

#endif /* PERCEPTRA_TASK_REGISTRY_H */
