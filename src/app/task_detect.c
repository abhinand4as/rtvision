/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* task_detect.c - detect task handler: NvDsObjectMeta -> PerceptraObject.
 *
 * - bbox: rect_params are in streammux (pipeline) resolution; rescaled to the source resolution
 *   (frame_meta->source_frame_width/height). nvstreammux runs with enable-padding=0, so each
 *   axis scales independently.
 * - track_id: object_id, or null when UNTRACKED_OBJECT_ID (no tracker / not yet tracked).
 * - confidence: detector confidence; negative values (e.g. tracker-only frames) -> null.
 * - label: obj_label (interned once per distinct label).
 */
#include "task_registry.h"

#include "gstnvdsmeta.h"

static gboolean detect_init(GError **error) {
    (void)error;
    return TRUE;
}

static guint detect_fill(AppCtx *app_ctx, NvDsFrameMeta *fm, PerceptraFrameEvent *ev) {
    (void)app_ctx;
    guint overflow = 0;
    const double pw = fm->pipeline_width ? fm->pipeline_width : fm->source_frame_width;
    const double ph = fm->pipeline_height ? fm->pipeline_height : fm->source_frame_height;
    const double sx = (pw > 0 && fm->source_frame_width) ? fm->source_frame_width / pw : 1.0;
    const double sy = (ph > 0 && fm->source_frame_height) ? fm->source_frame_height / ph : 1.0;

    for (NvDsMetaList *l = fm->obj_meta_list; l != NULL; l = l->next) {
        NvDsObjectMeta *om = (NvDsObjectMeta *)l->data;
        if (om->class_id < 0)
            continue;
        if (ev->num_objects >= ev->capacity) {
            overflow++;
            continue;
        }
        PerceptraObject *o = &ev->objects[ev->num_objects++];
        o->has_track_id = om->object_id != UNTRACKED_OBJECT_ID;
        o->track_id = om->object_id;
        o->class_id = om->class_id;
        o->label = g_intern_string(om->obj_label);
        o->has_confidence = om->confidence >= 0.0f && om->confidence <= 1.0f;
        o->confidence = om->confidence;
        o->left = om->rect_params.left * sx;
        o->top = om->rect_params.top * sy;
        o->width = om->rect_params.width * sx;
        o->height = om->rect_params.height * sy;
    }
    return overflow;
}

static void detect_shutdown(void) {}

const PerceptraTaskHandler perceptra_task_detect = {"detect", detect_init, detect_fill,
                                                    detect_shutdown};
