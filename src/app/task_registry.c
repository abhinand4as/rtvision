/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* task_registry.c - task -> handler table (see task_registry.h). */
#include "task_registry.h"

#include <gio/gio.h>
#include <string.h>

static gboolean unsupported_init(GError **error) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "task not supported in this version");
    return FALSE;
}

static guint unsupported_fill(AppCtx *app_ctx, NvDsFrameMeta *frame_meta, RTVisionFrameEvent *ev) {
    (void)app_ctx;
    (void)frame_meta;
    (void)ev;
    return 0;
}

static void unsupported_shutdown(void) {}

/* Reserved seams (docs/ROADMAP.md): instance segmentation and pose. */
static const RTVisionTaskHandler task_segment = {"segment", unsupported_init, unsupported_fill,
                                                 unsupported_shutdown};
static const RTVisionTaskHandler task_pose = {"pose", unsupported_init, unsupported_fill,
                                              unsupported_shutdown};

static const RTVisionTaskHandler *const registry[] = {&rtvision_task_detect, &task_segment,
                                                      &task_pose};

const RTVisionTaskHandler *rtvision_task_get(const char *task) {
    for (guint i = 0; task && i < G_N_ELEMENTS(registry); i++)
        if (strcmp(registry[i]->name, task) == 0)
            return registry[i];
    return NULL;
}
