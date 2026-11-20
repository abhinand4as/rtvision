/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & AFFILIATES. All rights
 * reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Modifications Copyright (c) 2026 <PROJECT_OWNER>
 * Changes: derived from RT-CV src/perception_utc.c (extract_utc_from_uri) and the timestamp
 * logic of src/metropolis_perception_app.c (generate_ts_rfc3339_from_ts, backward-NTP warning);
 * GStreamer macros replaced by plain nanoseconds, URI parsing made bounds-checked and returning
 * a status, per-stream clock with reset on stream reuse, ntp/uri_utc/system selection with
 * per-event fallback to system time, rate-limited warnings.
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

/* timestamps.c - see timestamps.h. */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "timestamps.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"

#define NS_PER_MS G_GINT64_CONSTANT(1000000)
#define NS_PER_S G_GINT64_CONSTANT(1000000000)
#define BACKWARD_WARN_EVERY 1000

/** URI sample (from RT-CV):
 *   HWY_20_AND_BRYANT__WB__4_11_2018_4_59_59_485_AM_UTC-07_00.mp4
 * Format after the LAST "__":  M_DD_YYYY_H_MIN_SEC_MSEC_AM|PM_UTC<+|-HH_MM>
 * The local time given is <offset> away from UTC: UTC = local - offset. */
#define URI_UTC_START_DELIM "__"
#define URI_UTC_END_DELIM "_UTC"

gboolean perceptra_utc_from_uri(const char *uri, gint64 *unix_ns) {
    if (uri == NULL)
        return FALSE;
    /* the field after the last "__" */
    const char *field = NULL;
    for (const char *p = strstr(uri, URI_UTC_START_DELIM); p;
         p = strstr(p + 1, URI_UTC_START_DELIM))
        field = p + strlen(URI_UTC_START_DELIM);
    if (field == NULL)
        return FALSE;
    const char *utc = strstr(field, URI_UTC_END_DELIM);
    if (utc == NULL || utc - field < 4)
        return FALSE;

    /* "<date/time>_<msec>_<AM|PM>" before "_UTC": split off msec. */
    const char *ampm = utc - 2; /* "AM" / "PM" */
    if (ampm[-1] != '_')
        return FALSE;
    const char *ms_end = ampm - 1; /* '_' before AM/PM */
    const char *ms_start = ms_end;
    while (ms_start > field && ms_start[-1] != '_')
        ms_start--;
    if (ms_start == field || ms_start == ms_end)
        return FALSE;
    char *end = NULL;
    unsigned long ms = strtoul(ms_start, &end, 10);
    if (end != ms_end || ms > 999)
        return FALSE;

    /* Rebuild "M_DD_YYYY_H_MIN_SEC_AM" without the msec part and parse it. */
    char buf[128];
    gsize head = (gsize)(ms_start - field); /* includes trailing '_' */
    if (head + 2 >= sizeof(buf))
        return FALSE;
    memcpy(buf, field, head);
    memcpy(buf + head, ampm, 2);
    buf[head + 2] = '\0';
    struct tm tm_utc;
    memset(&tm_utc, 0, sizeof(tm_utc));
    const char *rest = strptime(buf, "%m_%d_%Y_%I_%M_%S_%p", &tm_utc);
    if (rest == NULL || *rest != '\0')
        return FALSE;

    /* Offset "+HH_MM" / "-HH_MM" after "_UTC". */
    const char *off = utc + strlen(URI_UTC_END_DELIM);
    long hours = strtol(off, &end, 10);
    if (end == off || *end != '_' || hours < -23 || hours > 23)
        return FALSE;
    const char *min_str = end + 1;
    long minutes = strtol(min_str, &end, 10);
    if (end == min_str || minutes < 0 || minutes > 59)
        return FALSE;
    gint64 offset_ns = ((gint64)labs(hours) * 60 + minutes) * 60 * NS_PER_S;
    if (off[0] == '-')
        offset_ns = -offset_ns;

    time_t secs = timegm(&tm_utc);
    *unix_ns = (gint64)secs * NS_PER_S + (gint64)ms * NS_PER_MS - offset_ns;
    return TRUE;
}

typedef struct {
    gboolean active;
    gint last_frame_num;
    guint64 last_ntp_ns;
    guint64 backward_count;
    gboolean uri_checked;
    gboolean uri_ok;
    gint64 uri_base_ns;
    guint64 first_pts_ns;
} StreamClock;

static StreamClock clocks[PERCEPTRA_TS_MAX_STREAMS];
static PerceptraTimestampSource configured_source = PERCEPTRA_TS_SYSTEM;
static guint64 backward_total;

void perceptra_timestamps_init(PerceptraTimestampSource configured) {
    configured_source = configured;
    memset(clocks, 0, sizeof(clocks));
    backward_total = 0;
}

guint64 perceptra_timestamps_backward_ntp_count(void) {
    return __atomic_load_n(&backward_total, __ATOMIC_RELAXED);
}

static gint64 system_ms(PerceptraTimestampSource *used) {
    *used = PERCEPTRA_TS_SYSTEM;
    return g_get_real_time() / 1000;
}

gint64 perceptra_timestamp_for_frame(guint source_id, gint frame_num, guint64 ntp_ns,
                                     guint64 buf_pts_ns, const char *uri,
                                     PerceptraTimestampSource *used) {
    if (source_id >= PERCEPTRA_TS_MAX_STREAMS)
        return system_ms(used);
    StreamClock *c = &clocks[source_id];
    if (!c->active || frame_num == 0 || frame_num < c->last_frame_num) {
        memset(c, 0, sizeof(*c)); /* new stream (or source id reused after removal) */
        c->active = TRUE;
    }
    c->last_frame_num = frame_num;

    switch (configured_source) {
    case PERCEPTRA_TS_NTP:
        if (ntp_ns == 0)
            return system_ms(used); /* no RTCP sender report (e.g. file source) */
        if (ntp_ns < c->last_ntp_ns) {
            __atomic_add_fetch(&backward_total, 1, __ATOMIC_RELAXED);
            if (c->backward_count++ % BACKWARD_WARN_EVERY == 0)
                PLOG_WARN("timestamps",
                          "event=ntp_backward source_id=%u current_ns=%" G_GUINT64_FORMAT
                          " previous_ns=%" G_GUINT64_FORMAT " count=%" G_GUINT64_FORMAT,
                          source_id, ntp_ns, c->last_ntp_ns, c->backward_count);
        }
        c->last_ntp_ns = ntp_ns;
        *used = PERCEPTRA_TS_NTP;
        return (gint64)(ntp_ns / (guint64)NS_PER_MS);
    case PERCEPTRA_TS_URI_UTC:
        if (!c->uri_checked) {
            c->uri_checked = TRUE;
            c->uri_ok = perceptra_utc_from_uri(uri, &c->uri_base_ns);
            c->first_pts_ns = buf_pts_ns;
            if (!c->uri_ok)
                PLOG_WARN("timestamps",
                          "event=uri_utc_unparsable source_id=%u uri=\"%s\" fallback=system",
                          source_id, uri ? uri : "");
        }
        if (!c->uri_ok)
            return system_ms(used);
        *used = PERCEPTRA_TS_URI_UTC;
        return (c->uri_base_ns + (gint64)(buf_pts_ns - c->first_pts_ns)) / NS_PER_MS;
    case PERCEPTRA_TS_SYSTEM:
    default:
        return system_ms(used);
    }
}
