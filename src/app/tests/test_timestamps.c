/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* test_timestamps.c - URI UTC parsing and per-stream clocks (GLib only, no DeepStream). */
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "timestamps.h"

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define MS(ns) ((ns) / 1000000)

static void test_uri_parsing(void) {
    gint64 ns = 0;
    /* RT-CV's documented sample: 04:59:59.485 AM local at UTC-07:00 -> 11:59:59.485Z */
    CHECK(rtvision_utc_from_uri(
        "file:///v/HWY_20_AND_BRYANT__WB__4_11_2018_4_59_59_485_AM_UTC-07_00.mp4", &ns));
    CHECK(MS(ns) == 1523447999485LL);
    /* PM, positive half-hour offset: 12:30:00.000 PM at UTC+05:30 -> 07:00:00.000Z */
    CHECK(rtvision_utc_from_uri("rtsp://h/cam__11_20_2026_12_30_00_000_PM_UTC+05_30", &ns));
    CHECK(MS(ns) == 1795158000000LL);
    /* 12 AM = midnight; "-00_30" keeps its sign: 00:00:00.007 at UTC-00:30 -> 00:30:00.007Z */
    CHECK(rtvision_utc_from_uri("/x/a__1_2_2026_12_00_00_007_AM_UTC-00_30.mkv", &ns));
    CHECK(MS(ns) == 1767313800007LL);

    const char *bad[] = {
        NULL,
        "file:///no_delimiter.mp4",
        "file:///a__4_11_2018_4_59_59_AM_UTC-07_00.mp4",      /* no msec */
        "file:///a__4_11_2018_4_59_59_4850_AM_UTC-07_00.mp4", /* msec > 999 */
        "file:///a__4_11_2018_4_59_59_485_XM_UTC-07_00.mp4",  /* not AM/PM */
        "file:///a__13_11_2018_4_59_59_485_AM_UTC-07_00.mp4", /* month 13 */
        "file:///a__4_11_2018_4_59_59_485_AM_UTC-25_00.mp4",  /* offset hours */
        "file:///a__4_11_2018_4_59_59_485_AM_UTC-07_75.mp4",  /* offset minutes */
        "file:///a__4_11_2018_4_59_59_485_AM.mp4",            /* no _UTC */
    };
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++) {
        if (rtvision_utc_from_uri(bad[i], &ns)) {
            fprintf(stderr, "accepted bad uri #%u: %s\n", i, bad[i] ? bad[i] : "(null)");
            failures++;
        }
    }
}

static void test_ntp_clock(void) {
    RTVisionTimestampSource used;
    rtvision_timestamps_init(RTVISION_TS_NTP);
    const guint64 t0 = 1795156200123000000ULL; /* 2026-11-20T06:30:00.123Z */
    CHECK(rtvision_timestamp_for_frame(0, 0, t0, 0, NULL, &used) == 1795156200123LL);
    CHECK(used == RTVISION_TS_NTP);
    CHECK(rtvision_timestamp_for_frame(0, 1, t0 + 33000000, 0, NULL, &used) == 1795156200156LL);
    /* backwards -> still used, counted */
    CHECK(rtvision_timestamp_for_frame(0, 2, t0, 0, NULL, &used) == 1795156200123LL);
    CHECK(rtvision_timestamps_backward_ntp_count() == 1);
    /* no RTCP (0) -> system time for that event */
    gint64 now = g_get_real_time() / 1000;
    gint64 ts = rtvision_timestamp_for_frame(1, 0, 0, 0, NULL, &used);
    CHECK(used == RTVISION_TS_SYSTEM && ts >= now - 1000 && ts <= now + 1000);
    /* stream 0 restarts (frame 0): earlier NTP is not "backwards" */
    rtvision_timestamp_for_frame(0, 0, t0 - 1000000000ULL, 0, NULL, &used);
    CHECK(rtvision_timestamps_backward_ntp_count() == 1);
}

static void test_uri_clock(void) {
    RTVisionTimestampSource used;
    rtvision_timestamps_init(RTVISION_TS_URI_UTC);
    const char *uri = "file:///v/x__11_20_2026_6_30_00_123_AM_UTC+00_00.mp4";
    /* first frame at PTS 5 s -> base; later frames add the PTS delta */
    CHECK(rtvision_timestamp_for_frame(2, 0, 0, 5000000000ULL, uri, &used) == 1795156200123LL);
    CHECK(used == RTVISION_TS_URI_UTC);
    CHECK(rtvision_timestamp_for_frame(2, 30, 0, 6000000000ULL, uri, &used) == 1795156201123LL);
    /* unparsable URI -> system */
    rtvision_timestamp_for_frame(3, 0, 0, 0, "file:///plain.mp4", &used);
    CHECK(used == RTVISION_TS_SYSTEM);
    /* out-of-range source id -> system */
    rtvision_timestamp_for_frame(RTVISION_TS_MAX_STREAMS, 0, 0, 0, uri, &used);
    CHECK(used == RTVISION_TS_SYSTEM);
}

static void test_system_clock(void) {
    RTVisionTimestampSource used;
    rtvision_timestamps_init(RTVISION_TS_SYSTEM);
    rtvision_timestamp_for_frame(0, 0, 123456789ULL, 0, NULL, &used);
    CHECK(used == RTVISION_TS_SYSTEM);
}

int main(void) {
    rtvision_log_set_level(RTVISION_LOG_ERROR);
    test_uri_parsing();
    test_ntp_clock();
    test_uri_clock();
    test_system_clock();
    printf("%s: timestamp tests (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
