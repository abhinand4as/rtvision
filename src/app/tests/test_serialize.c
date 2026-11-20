/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* test_serialize.c - serializer golden test + unit checks (GLib only, no DeepStream).
 *
 *   test_serialize <golden.json>          compare the fixed event with the golden file
 *   test_serialize --write <golden.json>  regenerate the golden file
 * The golden file is also validated against the JSON Schema by pytest
 * (tests/python/test_event_golden.py).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "event_model.h"
#include "event_serialize.h"

static int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            fprintf(stderr, "%s:%d: got  %s\n%*swant %s\n", __FILE__, __LINE__, (got),             \
                    (int)strlen(__FILE__) + 6, "", (want));                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const RTVisionEventContext kCtx = {
    .service = "rtvision",
    .model_name = "yolo26s-people",
    .model_version = "2026.1",
    .model_task = "detect",
};

/* The fixed event of the golden file (mirrors the example in docs/EVENT_SCHEMA.md). */
static RTVisionFrameEvent *golden_event(void) {
    RTVisionFrameEvent *ev = rtvision_event_new(2);
    g_strlcpy(ev->event_id, "5f0c2a3e-6a51-4c1e-9d7a-2b8c0f6e9a11", sizeof(ev->event_id));
    ev->timestamp_ms = 1795156200123LL; /* 2026-11-20T06:30:00.123Z */
    ev->timestamp_source = RTVISION_TS_NTP;
    ev->sensor_id = g_strdup("cam-entrance-01");
    ev->sensor_name = g_strdup("Entrance 1");
    ev->stream_index = 0;
    ev->frame_number = 1230;
    ev->frame_width = 1920;
    ev->frame_height = 1080;
    ev->num_objects = 2;
    RTVisionObject *a = &ev->objects[0];
    a->has_track_id = TRUE;
    a->track_id = 17;
    a->class_id = 0;
    a->label = g_intern_string("person");
    a->has_confidence = TRUE;
    a->confidence = 0.873f;
    a->left = 812.44;
    a->top = 301.0;
    a->width = 96.2;
    a->height = 254.66;
    RTVisionObject *b = &ev->objects[1]; /* untracked, confidence unknown */
    b->has_track_id = FALSE;
    b->class_id = 2;
    b->label = g_intern_string("car");
    b->has_confidence = FALSE;
    b->left = 0.04;
    b->top = 10.0;
    b->width = 300.0;
    b->height = 120.5;
    return ev;
}

static void unit_checks(void) {
    GString *s = g_string_new(NULL);

    /* string escaping: quotes, backslash, control chars, UTF-8 kept, invalid UTF-8 replaced */
    g_string_truncate(s, 0);
    rtvision_json_append_string(s, "a\"b\\c\nd\te\x01 \xc3\xa9");
    CHECK_STR(s->str, "\"a\\\"b\\\\c\\nd\\te\\u0001 \xc3\xa9\"");
    g_string_truncate(s, 0);
    rtvision_json_append_string(s, "bad\xff");
    CHECK(g_utf8_validate(s->str, -1, NULL));
    g_string_truncate(s, 0);
    rtvision_json_append_string(s, NULL);
    CHECK_STR(s->str, "null");

    /* timestamps */
    char ts[32];
    rtvision_format_rfc3339_ms(0, ts, sizeof(ts));
    CHECK_STR(ts, "1970-01-01T00:00:00.000Z");
    rtvision_format_rfc3339_ms(1795156200123LL, ts, sizeof(ts));
    CHECK_STR(ts, "2026-11-20T06:30:00.123Z");

    /* null sensor name, empty objects, confidence trimming, embedding */
    RTVisionFrameEvent *ev = rtvision_event_new(1);
    g_strlcpy(ev->event_id, "00000000-0000-4000-8000-000000000000", sizeof(ev->event_id));
    ev->sensor_id = g_strdup("cam \"x\"");
    ev->timestamp_source = RTVISION_TS_SYSTEM;
    ev->frame_width = 640;
    ev->frame_height = 480;
    rtvision_event_to_json(ev, &kCtx, s);
    CHECK(strstr(s->str, "\"sensor\":{\"id\":\"cam \\\"x\\\"\",\"name\":null,") != NULL);
    CHECK(strstr(s->str, "\"timestamp_source\":\"system\"") != NULL);
    CHECK(g_str_has_suffix(s->str, "\"objects\":[]}"));
    CHECK(strchr(s->str, '\n') == NULL);

    ev->num_objects = 1;
    RTVisionObject *o = &ev->objects[0];
    o->has_confidence = TRUE;
    o->confidence = 0.9f;
    o->label = "person";
    o->width = -3.0; /* negative sizes clamp to 0 */
    o->left = -0.04; /* "-0.0" never emitted */
    o->embedding = g_new0(RTVisionEmbedding, 1);
    o->embedding->dim = 2;
    o->embedding->data = g_new(gfloat, 2);
    o->embedding->data[0] = 1.0f;
    o->embedding->data[1] = -2.5f;
    rtvision_event_to_json(ev, &kCtx, s);
    CHECK(strstr(s->str, "\"confidence\":0.9,") != NULL);
    CHECK(strstr(s->str, "\"left\":0.0,") != NULL);
    CHECK(strstr(s->str, "\"width\":0.0,") != NULL);
    /* 1.0f, -2.5f little-endian = 00 00 80 3f 00 00 20 c0 */
    CHECK(strstr(s->str, "\"embedding\":{\"dim\":2,\"encoding\":\"base64-f32le\","
                         "\"data\":\"AACAPwAAIMA=\"}") != NULL);
    o->confidence = 1.0f;
    rtvision_event_to_json(ev, &kCtx, s);
    CHECK(strstr(s->str, "\"confidence\":1.0,") != NULL);
    o->confidence = NAN;
    rtvision_event_to_json(ev, &kCtx, s);
    CHECK(strstr(s->str, "\"confidence\":null,") != NULL);
    rtvision_event_free(ev); /* frees the embedding too */

    /* timestamp source names round-trip */
    RTVisionTimestampSource src;
    CHECK(rtvision_timestamp_source_parse("uri_utc", &src) && src == RTVISION_TS_URI_UTC);
    CHECK(!rtvision_timestamp_source_parse("gps", &src));

    /* random ids are UUID v4 */
    RTVisionFrameEvent *r = rtvision_event_new(0);
    rtvision_event_set_random_id(r);
    CHECK(strlen(r->event_id) == 36 && r->event_id[14] == '4');
    rtvision_event_free(r);
    g_string_free(s, TRUE);
}

int main(int argc, char **argv) {
    gboolean write = argc == 3 && strcmp(argv[1], "--write") == 0;
    if (argc != 2 && !write) {
        fprintf(stderr, "usage: %s [--write] <golden.json>\n", argv[0]);
        return 2;
    }
    const char *golden = argv[argc - 1];

    unit_checks();

    GString *json = g_string_new(NULL);
    RTVisionFrameEvent *ev = golden_event();
    rtvision_event_to_json(ev, &kCtx, json);
    g_string_append_c(json, '\n'); /* JSON Lines */
    rtvision_event_free(ev);

    if (write) {
        GError *err = NULL;
        if (!g_file_set_contents(golden, json->str, (gssize)json->len, &err)) {
            fprintf(stderr, "write %s: %s\n", golden, err->message);
            return 2;
        }
        printf("wrote %s\n", golden);
    } else {
        gchar *expected = NULL;
        if (!g_file_get_contents(golden, &expected, NULL, NULL)) {
            fprintf(stderr, "cannot read %s (run with --write to create it)\n", golden);
            failures++;
        } else {
            CHECK_STR(json->str, expected);
            g_free(expected);
        }
    }
    g_string_free(json, TRUE);
    printf("%s: serializer tests (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
