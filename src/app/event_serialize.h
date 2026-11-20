/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* event_serialize.h - RTVisionFrameEvent -> one line of JSON (schema rtvision.detection.v1).
 *
 * Own small JSON writer on GString (RFC 8259 string escaping, invalid UTF-8 replaced) so number
 * formatting is controlled: bbox values with 1 decimal, confidence with up to 3 decimals.
 * json-glib prints doubles with %.17g ("812.39999999999998"), see docs/DEVIATIONS.md.
 */
#ifndef RTVISION_EVENT_SERIALIZE_H
#define RTVISION_EVENT_SERIALIZE_H

#include <glib.h>

#include "event_model.h"

G_BEGIN_DECLS

/* Truncates `out` and writes the event as compact JSON (no trailing newline). */
void rtvision_event_to_json(const RTVisionFrameEvent *ev, const RTVisionEventContext *ctx,
                            GString *out);

/* Appends a JSON string literal (with quotes) for `s` (UTF-8; invalid bytes replaced). */
void rtvision_json_append_string(GString *out, const char *s);

/* "2026-11-20T06:30:00.123Z" (UTC, milliseconds) into buf (>= 25 bytes). */
void rtvision_format_rfc3339_ms(gint64 unix_ms, char *buf, gsize len);

G_END_DECLS

#endif /* RTVISION_EVENT_SERIALIZE_H */
