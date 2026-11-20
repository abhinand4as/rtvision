/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modifications Copyright (c) 2026 <PROJECT_OWNER>
 * Changes: derived from the tracker-ReID embedding cache of RT-CV
 * src/metropolis_perception_app.c (push/pop/retrieve_embedding_queue, reid_store_age); the
 * per-stream queue of per-frame lists is reimplemented as a per-stream hash map
 * object_id -> latest feature with reused buffers and a size cap; plain memcpy from the host
 * pointer instead of cudaMemcpy; SGIE tensor path dropped.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* embeddings.c - see embeddings.h. */
#include "embeddings.h"

#include <string.h>

#include "gstnvdsmeta.h"
#include "log.h"
#include "nvds_tracker_meta.h"

#define EVICT_EVERY_N_FRAMES 30

typedef struct {
    gint frame_num;
    guint dim;
    guint capacity;
    gfloat *data;
} CachedFeature;

static gboolean enabled;
static guint store_age;
static GHashTable *cache[MAX_SOURCE_BINS]; /* guint64* object_id -> CachedFeature* */
static gboolean cap_warned;

static void cached_free(gpointer p) {
    CachedFeature *c = p;
    g_free(c->data);
    g_free(c);
}

void perceptra_embeddings_init(gboolean on, guint store_age_frames) {
    enabled = on;
    store_age = store_age_frames;
}

gboolean perceptra_embeddings_enabled(void) {
    return enabled;
}

static const NvDsObjReid *find_reid(NvDsObjectMeta *om) {
    for (NvDsMetaList *l = om->obj_user_meta_list; l != NULL; l = l->next) {
        NvDsUserMeta *um = (NvDsUserMeta *)l->data;
        if (um && um->base_meta.meta_type == NVDS_TRACKER_OBJ_REID_META) {
            const NvDsObjReid *r = (const NvDsObjReid *)um->user_meta_data;
            if (r && r->ptr_host && r->featureSize > 0)
                return r;
        }
    }
    return NULL;
}

static gboolean evict_old(gpointer key, gpointer value, gpointer user_data) {
    (void)key;
    gint now = GPOINTER_TO_INT(user_data);
    return ((CachedFeature *)value)->frame_num < now - (gint)store_age;
}

void perceptra_embeddings_observe_frame(NvDsFrameMeta *fm) {
    if (!enabled || store_age == 0 || fm->source_id >= MAX_SOURCE_BINS)
        return;
    GHashTable **t = &cache[fm->source_id];
    if (*t == NULL)
        *t = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, cached_free);
    if (fm->frame_num == 0)
        g_hash_table_remove_all(*t); /* new stream on this source id */

    for (NvDsMetaList *l = fm->obj_meta_list; l != NULL; l = l->next) {
        NvDsObjectMeta *om = (NvDsObjectMeta *)l->data;
        if (om->object_id == UNTRACKED_OBJECT_ID)
            continue;
        const NvDsObjReid *r = find_reid(om);
        if (r == NULL)
            continue;
        CachedFeature *c = g_hash_table_lookup(*t, &om->object_id);
        if (c == NULL) {
            if (g_hash_table_size(*t) >= PERCEPTRA_EMB_MAX_TRACKS_PER_STREAM) {
                if (!cap_warned) {
                    cap_warned = TRUE;
                    PLOG_WARN("embeddings", "event=cache_full source_id=%u cap=%d", fm->source_id,
                              PERCEPTRA_EMB_MAX_TRACKS_PER_STREAM);
                }
                continue;
            }
            c = g_new0(CachedFeature, 1);
            g_hash_table_insert(*t, g_memdup2(&om->object_id, sizeof(guint64)), c);
        }
        if (c->capacity < r->featureSize) {
            c->data = g_renew(gfloat, c->data, r->featureSize);
            c->capacity = r->featureSize;
        }
        memcpy(c->data, r->ptr_host, r->featureSize * sizeof(gfloat));
        c->dim = r->featureSize;
        c->frame_num = fm->frame_num;
    }
    if (fm->frame_num % EVICT_EVERY_N_FRAMES == 0)
        g_hash_table_foreach_remove(*t, evict_old, GINT_TO_POINTER(fm->frame_num));
}

static PerceptraEmbedding *copy_embedding(const gfloat *data, guint dim) {
    PerceptraEmbedding *e = g_new(PerceptraEmbedding, 1);
    e->dim = dim;
    e->data = g_memdup2(data, (gsize)dim * sizeof(gfloat));
    return e;
}

PerceptraEmbedding *perceptra_embeddings_for_object(NvDsFrameMeta *fm, NvDsObjectMeta *om) {
    if (!enabled || om->object_id == UNTRACKED_OBJECT_ID)
        return NULL;
    const NvDsObjReid *r = find_reid(om);
    if (r)
        return copy_embedding(r->ptr_host, r->featureSize);
    if (store_age == 0 || fm->source_id >= MAX_SOURCE_BINS || cache[fm->source_id] == NULL)
        return NULL;
    const CachedFeature *c = g_hash_table_lookup(cache[fm->source_id], &om->object_id);
    if (c && c->dim > 0 && c->frame_num >= fm->frame_num - (gint)store_age)
        return copy_embedding(c->data, c->dim);
    return NULL;
}

void perceptra_embeddings_shutdown(void) {
    for (guint i = 0; i < MAX_SOURCE_BINS; i++)
        g_clear_pointer(&cache[i], g_hash_table_destroy);
}
