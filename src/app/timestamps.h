/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* timestamps.h - per-stream event timestamps (ARCHITECTURE §7).
 *
 *   ntp      NvDsFrameMeta.ntp_timestamp (RTCP sender reports; streammux attach-sys-ts-as-ntp=0).
 *            0 (no RTCP, e.g. file sources) -> system time for that event. A timestamp going
 *            backwards logs a rate-limited warning.
 *   uri_utc  base UTC parsed from the stream URI (`…__M_DD_YYYY_H_MIN_SEC_MSEC_AM_UTC±HH_MM.mp4`)
 *            + (buffer PTS - PTS of the stream's first frame). Unparsable URI -> system time.
 *   system   wall clock when the probe runs.
 * The source actually used is returned per event (`timestamp_source`). GLib only (unit-tested
 * without DeepStream). Called from the streaming thread only; not thread-safe across threads.
 */
#ifndef RTVISION_TIMESTAMPS_H
#define RTVISION_TIMESTAMPS_H

#include <glib.h>

#include "event_model.h"

G_BEGIN_DECLS

#define RTVISION_TS_MAX_STREAMS 1024 /* >= DeepStream MAX_SOURCE_BINS */

void rtvision_timestamps_init(RTVisionTimestampSource configured);

/* Updates the stream clock with one frame and returns Unix epoch milliseconds (UTC).
 * frame_num going back to 0 / decreasing marks a new stream on a reused source id.
 * `used` receives the source actually used. */
gint64 rtvision_timestamp_for_frame(guint source_id, gint frame_num, guint64 ntp_ns,
                                    guint64 buf_pts_ns, const char *uri,
                                    RTVisionTimestampSource *used);

/* Parses the UTC time encoded in a file name/URI (see above), in Unix nanoseconds.
 * Returns FALSE if the URI does not follow the format. */
gboolean rtvision_utc_from_uri(const char *uri, gint64 *unix_ns);

/* Number of backward-NTP warnings seen (all streams). */
guint64 rtvision_timestamps_backward_ntp_count(void);

G_END_DECLS

#endif /* RTVISION_TIMESTAMPS_H */
