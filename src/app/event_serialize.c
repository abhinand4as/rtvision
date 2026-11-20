/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* event_serialize.c - JSON serializer for perceptra.detection.v1 (see event_serialize.h). */
#include "event_serialize.h"

#include <math.h>
#include <string.h>
#include <time.h>

void perceptra_format_rfc3339_ms(gint64 unix_ms, char *buf, gsize len) {
    gint64 secs = unix_ms / 1000;
    gint64 ms = unix_ms % 1000;
    if (ms < 0) {
        ms += 1000;
        secs -= 1;
    }
    time_t t = (time_t)secs;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    g_snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm_utc.tm_year + 1900,
               tm_utc.tm_mon + 1, tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
               (int)ms);
}

void perceptra_json_append_string(GString *out, const char *s) {
    if (s == NULL) {
        g_string_append(out, "null");
        return;
    }
    gchar *valid = NULL;
    if (!g_utf8_validate(s, -1, NULL))
        s = valid = g_utf8_make_valid(s, -1);
    g_string_append_c(out, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':
            g_string_append(out, "\\\"");
            break;
        case '\\':
            g_string_append(out, "\\\\");
            break;
        case '\n':
            g_string_append(out, "\\n");
            break;
        case '\r':
            g_string_append(out, "\\r");
            break;
        case '\t':
            g_string_append(out, "\\t");
            break;
        default:
            if (*p < 0x20)
                g_string_append_printf(out, "\\u%04x", *p);
            else
                g_string_append_c(out, (char)*p);
        }
    }
    g_string_append_c(out, '"');
    g_free(valid);
}

/* Fixed decimals, locale independent; non-finite -> 0 (schema requires numbers). */
static void append_fixed(GString *out, double v, int decimals) {
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    if (!isfinite(v))
        v = 0.0;
    if (decimals == 1)
        g_ascii_formatd(buf, sizeof(buf), "%.1f", v);
    else
        g_ascii_formatd(buf, sizeof(buf), "%.3f", v);
    /* "-0.0" -> "0.0" */
    if (strcmp(buf, "-0.0") == 0 || strcmp(buf, "-0.000") == 0)
        memmove(buf, buf + 1, strlen(buf));
    g_string_append(out, buf);
}

/* Confidence: up to 3 decimals, trailing zeros trimmed (0.900 -> 0.9, 1.000 -> 1.0). */
static void append_confidence(GString *out, double v) {
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(buf, sizeof(buf), "%.3f", CLAMP(v, 0.0, 1.0));
    gsize n = strlen(buf);
    while (n > 0 && buf[n - 1] == '0' && n >= 2 && buf[n - 2] != '.')
        buf[--n] = '\0';
    g_string_append(out, buf);
}

static void append_key(GString *out, const char *key) {
    g_string_append_c(out, '"');
    g_string_append(out, key); /* keys are constant ASCII */
    g_string_append(out, "\":");
}

static void append_object(GString *out, const PerceptraObject *o) {
    g_string_append_c(out, '{');
    append_key(out, "track_id");
    if (o->has_track_id)
        g_string_append_printf(out, "%" G_GUINT64_FORMAT, o->track_id);
    else
        g_string_append(out, "null");
    g_string_append_c(out, ',');
    append_key(out, "class_id");
    g_string_append_printf(out, "%d", MAX(o->class_id, 0));
    g_string_append_c(out, ',');
    append_key(out, "label");
    perceptra_json_append_string(out, o->label ? o->label : "");
    g_string_append_c(out, ',');
    append_key(out, "confidence");
    if (o->has_confidence && isfinite(o->confidence))
        append_confidence(out, o->confidence);
    else
        g_string_append(out, "null");
    g_string_append_c(out, ',');
    append_key(out, "bbox");
    g_string_append_c(out, '{');
    append_key(out, "left");
    append_fixed(out, o->left, 1);
    g_string_append_c(out, ',');
    append_key(out, "top");
    append_fixed(out, o->top, 1);
    g_string_append_c(out, ',');
    append_key(out, "width");
    append_fixed(out, MAX(o->width, 0.0), 1);
    g_string_append_c(out, ',');
    append_key(out, "height");
    append_fixed(out, MAX(o->height, 0.0), 1);
    g_string_append_c(out, '}');
    if (o->embedding && o->embedding->dim > 0 && o->embedding->data) {
        g_string_append_c(out, ',');
        append_key(out, "embedding");
        g_string_append_printf(out, "{\"dim\":%u,\"encoding\":\"base64-f32le\",\"data\":\"",
                               o->embedding->dim);
#if G_BYTE_ORDER != G_LITTLE_ENDIAN
#error "base64-f32le encoding assumes a little-endian host"
#endif
        gchar *b64 = g_base64_encode((const guchar *)o->embedding->data,
                                     (gsize)o->embedding->dim * sizeof(gfloat));
        g_string_append(out, b64);
        g_free(b64);
        g_string_append(out, "\"}");
    }
    /* mask / attributes: reserved, never serialized in v1 */
    g_string_append_c(out, '}');
}

void perceptra_event_to_json(const PerceptraFrameEvent *ev, const PerceptraEventContext *ctx,
                             GString *out) {
    char ts[32];
    g_string_truncate(out, 0);
    g_string_append_c(out, '{');
    append_key(out, "schema");
    perceptra_json_append_string(out, PERCEPTRA_SCHEMA_ID);
    g_string_append_c(out, ',');
    append_key(out, "event_id");
    perceptra_json_append_string(out, ev->event_id);
    g_string_append_c(out, ',');
    append_key(out, "service");
    perceptra_json_append_string(out, ctx->service);
    g_string_append_c(out, ',');
    append_key(out, "timestamp");
    perceptra_format_rfc3339_ms(ev->timestamp_ms, ts, sizeof(ts));
    perceptra_json_append_string(out, ts);
    g_string_append_c(out, ',');
    append_key(out, "timestamp_source");
    perceptra_json_append_string(out, perceptra_timestamp_source_name(ev->timestamp_source));
    g_string_append_c(out, ',');

    append_key(out, "sensor");
    g_string_append_c(out, '{');
    append_key(out, "id");
    perceptra_json_append_string(out, ev->sensor_id ? ev->sensor_id : "unknown");
    g_string_append_c(out, ',');
    append_key(out, "name");
    perceptra_json_append_string(out, ev->sensor_name); /* NULL -> null */
    g_string_append_c(out, ',');
    append_key(out, "stream_index");
    g_string_append_printf(out, "%u", ev->stream_index);
    g_string_append(out, "},");

    append_key(out, "frame");
    g_string_append_printf(out, "{\"number\":%" G_GUINT64_FORMAT ",\"width\":%u,\"height\":%u},",
                           ev->frame_number, MAX(ev->frame_width, 1u), MAX(ev->frame_height, 1u));

    append_key(out, "model");
    g_string_append_c(out, '{');
    append_key(out, "name");
    perceptra_json_append_string(out, ctx->model_name);
    g_string_append_c(out, ',');
    append_key(out, "version");
    perceptra_json_append_string(out, ctx->model_version);
    g_string_append_c(out, ',');
    append_key(out, "task");
    perceptra_json_append_string(out, ctx->model_task);
    g_string_append(out, "},");

    append_key(out, "objects");
    g_string_append_c(out, '[');
    for (guint i = 0; i < ev->num_objects; i++) {
        if (i)
            g_string_append_c(out, ',');
        append_object(out, &ev->objects[i]);
    }
    g_string_append(out, "]}");
}
