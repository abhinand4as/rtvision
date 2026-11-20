/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* probe.c - perceptra_probe (see probe.h). M1: per-stream frame/object counters. */
#include "probe.h"

#include "gstnvdsmeta.h"
#include "log.h"

/* Per-source counters indexed by NvDsFrameMeta.source_id (< MAX_SOURCE_BINS). Fixed-size static
 * storage: no allocation on the streaming thread. A source id may be reused after a stream is
 * removed; counters are cumulative per id. */
static guint64 frames_per_source[MAX_SOURCE_BINS];
static guint64 objects_per_source[MAX_SOURCE_BINS];
static guint64 frames_total;
static guint64 objects_total;
static guint log_every_n = 0;

void perceptra_probe_init(guint every_n_frames) {
    log_every_n = every_n_frames;
}

guint64 perceptra_probe_frames_total(void) {
    return __atomic_load_n(&frames_total, __ATOMIC_RELAXED);
}

guint64 perceptra_probe_objects_total(void) {
    return __atomic_load_n(&objects_total, __ATOMIC_RELAXED);
}

void perceptra_probe(AppCtx *app_ctx, GstBuffer *buf, NvDsBatchMeta *batch_meta, guint index) {
    (void)app_ctx;
    (void)buf;
    (void)index;
    if (batch_meta == NULL)
        return;

    for (NvDsMetaList *l = batch_meta->frame_meta_list; l != NULL; l = l->next) {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *)l->data;
        guint source_id = frame_meta->source_id;
        guint num_objects = frame_meta->num_obj_meta;

        __atomic_add_fetch(&frames_total, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&objects_total, num_objects, __ATOMIC_RELAXED);
        if (source_id >= MAX_SOURCE_BINS)
            continue;
        guint64 frames = __atomic_add_fetch(&frames_per_source[source_id], 1, __ATOMIC_RELAXED);
        guint64 objects =
            __atomic_add_fetch(&objects_per_source[source_id], num_objects, __ATOMIC_RELAXED);

        if (frame_meta->frame_num == 0)
            PLOG_INFO("probe", "event=first_frame source_id=%u width=%u height=%u", source_id,
                      frame_meta->source_frame_width, frame_meta->source_frame_height);

        if (log_every_n > 0 && frame_meta->frame_num % (gint)log_every_n == 0)
            PLOG_DEBUG("probe",
                       "event=frame source_id=%u frame=%d objects=%u frames_seen=%" G_GUINT64_FORMAT
                       " objects_seen=%" G_GUINT64_FORMAT,
                       source_id, frame_meta->frame_num, num_objects, frames, objects);
    }
}
