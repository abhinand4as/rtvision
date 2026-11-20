/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* probe.c - perceptra_probe (see probe.h). */
#include "probe.h"

#include "embeddings.h"
#include "gstnvdsmeta.h"
#include "log.h"
#include "timestamps.h"

/* Per-source counters indexed by NvDsFrameMeta.source_id (< MAX_SOURCE_BINS). Fixed-size static
 * storage. A source id may be reused after a stream is removed; counters are cumulative per id. */
static guint64 frames_per_source[MAX_SOURCE_BINS];
static guint64 objects_per_source[MAX_SOURCE_BINS];
static guint64 frames_total;
static guint64 objects_total;
static guint64 events_built;
static guint64 objects_overflow;
static gint events_enabled; /* atomic gboolean */
static PerceptraProbeConfig config;

void perceptra_probe_init(const PerceptraProbeConfig *cfg) {
    config = *cfg;
    if (config.interval_frames == 0)
        config.interval_frames = 1;
    g_atomic_int_set(&events_enabled, cfg->publisher != NULL && cfg->handler != NULL);
}

void perceptra_probe_stop_events(void) {
    g_atomic_int_set(&events_enabled, 0);
}

guint64 perceptra_probe_frames_total(void) {
    return __atomic_load_n(&frames_total, __ATOMIC_RELAXED);
}

guint64 perceptra_probe_objects_total(void) {
    return __atomic_load_n(&objects_total, __ATOMIC_RELAXED);
}

guint64 perceptra_probe_events_built(void) {
    return __atomic_load_n(&events_built, __ATOMIC_RELAXED);
}

/* sensor.id / sensor.name: the frame's own sensor metadata (attached by nvmultiurisrcbin from the
 * REST stream-add call), else the deepstream-app sensor table, else "source-<id>". */
static void fill_sensor(AppCtx *app_ctx, NvDsFrameMeta *fm, PerceptraFrameEvent *ev) {
    const gchar *id = fm->sensorInfo_meta.sensor_id;
    const gchar *name = fm->sensorInfo_meta.sensor_name;
    if (id == NULL || *id == '\0') {
        NvDsSensorInfo *si =
            app_ctx->sensorInfoHash ? get_sensor_info(app_ctx, fm->source_id) : NULL;
        if (si) {
            id = si->sensor_id;
            name = si->sensor_name;
        }
    }
    ev->sensor_id = (id && *id) ? g_strdup(id) : g_strdup_printf("source-%u", fm->source_id);
    ev->sensor_name = (name && *name) ? g_strdup(name) : NULL;
}

static void build_and_enqueue(AppCtx *app_ctx, NvDsFrameMeta *fm, gint64 ts_ms,
                              PerceptraTimestampSource ts_used) {
    const guint n = MIN(fm->num_obj_meta, (guint)PERCEPTRA_MAX_OBJECTS_PER_EVENT);
    if (n == 0 && !config.include_empty_frames)
        return;
    PerceptraFrameEvent *ev = perceptra_event_new(n);
    perceptra_event_set_random_id(ev);
    ev->timestamp_ms = ts_ms;
    ev->timestamp_source = ts_used;
    fill_sensor(app_ctx, fm, ev);
    ev->stream_index = fm->source_id;
    ev->frame_number = fm->frame_num >= 0 ? (guint64)fm->frame_num : 0;
    ev->frame_width = fm->source_frame_width;
    ev->frame_height = fm->source_frame_height;

    guint overflow = config.handler->fill_event(app_ctx, fm, ev);
    if (overflow) {
        guint64 total = __atomic_add_fetch(&objects_overflow, overflow, __ATOMIC_RELAXED);
        if (total == overflow) /* first time only */
            PLOG_WARN("probe", "event=objects_capped cap=%d dropped=%u",
                      PERCEPTRA_MAX_OBJECTS_PER_EVENT, overflow);
    }
    if (ev->num_objects == 0 && !config.include_empty_frames) {
        perceptra_event_free(ev);
        return;
    }
    __atomic_add_fetch(&events_built, 1, __ATOMIC_RELAXED);
    perceptra_publisher_enqueue(config.publisher, ev); /* takes ownership, never blocks */
}

void perceptra_probe(AppCtx *app_ctx, GstBuffer *buf, NvDsBatchMeta *batch_meta, guint index) {
    (void)buf;
    (void)index;
    if (batch_meta == NULL)
        return;
    const gboolean emit = g_atomic_int_get(&events_enabled) != 0;

    for (NvDsMetaList *l = batch_meta->frame_meta_list; l != NULL; l = l->next) {
        NvDsFrameMeta *fm = (NvDsFrameMeta *)l->data;
        const guint source_id = fm->source_id;
        const guint num_objects = fm->num_obj_meta;

        __atomic_add_fetch(&frames_total, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&objects_total, num_objects, __ATOMIC_RELAXED);
        if (source_id < MAX_SOURCE_BINS) {
            guint64 frames = __atomic_add_fetch(&frames_per_source[source_id], 1, __ATOMIC_RELAXED);
            guint64 objects =
                __atomic_add_fetch(&objects_per_source[source_id], num_objects, __ATOMIC_RELAXED);
            if (fm->frame_num == 0)
                PLOG_INFO(
                    "probe", "event=first_frame source_id=%u sensor_id=\"%s\" width=%u height=%u",
                    source_id, fm->sensorInfo_meta.sensor_id ? fm->sensorInfo_meta.sensor_id : "",
                    fm->source_frame_width, fm->source_frame_height);
            if (config.log_every_n_frames > 0 &&
                fm->frame_num % (gint)config.log_every_n_frames == 0)
                PLOG_DEBUG(
                    "probe",
                    "event=frame source_id=%u frame=%d objects=%u frames_seen=%" G_GUINT64_FORMAT
                    " objects_seen=%" G_GUINT64_FORMAT,
                    source_id, fm->frame_num, num_objects, frames, objects);
        }
        if (!emit)
            continue;
        /* Every frame: keeps per-stream clocks (backward NTP, stream reuse) and the ReID cache
         * current; events only on sampled frames. */
        PerceptraTimestampSource ts_used;
        const gint64 ts_ms =
            perceptra_timestamp_for_frame(source_id, fm->frame_num, fm->ntp_timestamp, fm->buf_pts,
                                          fm->sensorInfo_meta.uri, &ts_used);
        perceptra_embeddings_observe_frame(fm);
        if (fm->frame_num >= 0 && fm->frame_num % (gint)config.interval_frames == 0)
            build_and_enqueue(app_ctx, fm, ts_ms, ts_used);
    }
}
