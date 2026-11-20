/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* log.h - levelled, line-oriented key=value logging.
 *
 * Every line looks like:
 *   ts=2026-11-20T06:30:00.123Z level=info comp=main event=started streams=0
 * The caller's format string supplies everything after "comp=<component> ".
 * Lines are written to stderr with a single write, so lines from different threads do not
 * interleave. Formatting happens on a fixed stack buffer (no heap allocation per line).
 */
#ifndef PERCEPTRA_LOG_H
#define PERCEPTRA_LOG_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    PERCEPTRA_LOG_ERROR = 0,
    PERCEPTRA_LOG_WARN = 1,
    PERCEPTRA_LOG_INFO = 2,
    PERCEPTRA_LOG_DEBUG = 3,
} PerceptraLogLevel;

void perceptra_log_set_level(PerceptraLogLevel level);
PerceptraLogLevel perceptra_log_get_level(void);

/* Parses "error" | "warn" | "info" | "debug" (case-insensitive). Returns FALSE if unknown. */
gboolean perceptra_log_parse_level(const char *text, PerceptraLogLevel *out);

static inline gboolean perceptra_log_enabled(PerceptraLogLevel level) {
    return level <= perceptra_log_get_level();
}

void perceptra_log(PerceptraLogLevel level, const char *component, const char *fmt, ...)
    G_GNUC_PRINTF(3, 4);

#define PLOG(level, comp, ...)                                                                     \
    do {                                                                                           \
        if (perceptra_log_enabled(level))                                                          \
            perceptra_log((level), (comp), __VA_ARGS__);                                           \
    } while (0)

#define PLOG_ERROR(comp, ...) PLOG(PERCEPTRA_LOG_ERROR, comp, __VA_ARGS__)
#define PLOG_WARN(comp, ...) PLOG(PERCEPTRA_LOG_WARN, comp, __VA_ARGS__)
#define PLOG_INFO(comp, ...) PLOG(PERCEPTRA_LOG_INFO, comp, __VA_ARGS__)
#define PLOG_DEBUG(comp, ...) PLOG(PERCEPTRA_LOG_DEBUG, comp, __VA_ARGS__)

G_END_DECLS

#endif /* PERCEPTRA_LOG_H */
