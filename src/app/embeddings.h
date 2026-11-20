/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* embeddings.h - tracker ReID features for events (publish.include_embeddings).
 *
 * The tracker (NvDCF accuracy profile with ReID.outputReidTensor: 1) attaches
 * NVDS_TRACKER_OBJ_REID_META to objects, but not necessarily on every frame. With
 * tracker.reid_store_age > 0 the last feature of every track is cached for that many frames, so
 * sampled frames can still carry an embedding. Streaming thread only.
 */
#ifndef PERCEPTRA_EMBEDDINGS_H
#define PERCEPTRA_EMBEDDINGS_H

#include <glib.h>

#include "deepstream_app.h"
#include "event_model.h"

G_BEGIN_DECLS

#define PERCEPTRA_EMB_MAX_TRACKS_PER_STREAM 4096

void perceptra_embeddings_init(gboolean enabled, guint store_age_frames);
gboolean perceptra_embeddings_enabled(void);

/* Every frame (before building an event): caches the ReID features present on this frame and
 * evicts entries older than store_age. Resets the stream on frame_num == 0. */
void perceptra_embeddings_observe_frame(NvDsFrameMeta *frame_meta);

/* A newly allocated copy of the object's current (or cached) feature, or NULL. */
PerceptraEmbedding *perceptra_embeddings_for_object(NvDsFrameMeta *frame_meta,
                                                    NvDsObjectMeta *obj_meta);

void perceptra_embeddings_shutdown(void);

G_END_DECLS

#endif /* PERCEPTRA_EMBEDDINGS_H */
