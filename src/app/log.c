/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* log.c - levelled key=value logging (see log.h). */
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define LOG_LINE_MAX 2048

static gint g_level = PERCEPTRA_LOG_INFO;

static const char *const level_names[] = {"error", "warn", "info", "debug"};

void perceptra_log_set_level(PerceptraLogLevel level) {
    g_atomic_int_set(&g_level, (gint)level);
}

PerceptraLogLevel perceptra_log_get_level(void) {
    return (PerceptraLogLevel)g_atomic_int_get(&g_level);
}

gboolean perceptra_log_parse_level(const char *text, PerceptraLogLevel *out) {
    if (text == NULL)
        return FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(level_names); i++) {
        if (g_ascii_strcasecmp(text, level_names[i]) == 0) {
            *out = (PerceptraLogLevel)i;
            return TRUE;
        }
    }
    if (g_ascii_strcasecmp(text, "warning") == 0) {
        *out = PERCEPTRA_LOG_WARN;
        return TRUE;
    }
    return FALSE;
}

void perceptra_log(PerceptraLogLevel level, const char *component, const char *fmt, ...) {
    char line[LOG_LINE_MAX];
    struct timeval tv;
    struct tm tm_utc;
    gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &tm_utc);

    if ((guint)level >= G_N_ELEMENTS(level_names))
        level = PERCEPTRA_LOG_DEBUG;

    int n = snprintf(
        line, sizeof(line), "ts=%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ level=%s comp=%s ",
        tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min,
        tm_utc.tm_sec, (long)(tv.tv_usec / 1000), level_names[level], component ? component : "-");
    if (n < 0)
        return;
    if ((size_t)n < sizeof(line)) {
        va_list ap;
        va_start(ap, fmt);
        int m = vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
        va_end(ap);
        if (m > 0)
            n += m;
    }
    if ((size_t)n >= sizeof(line) - 1)
        n = (int)sizeof(line) - 2; /* truncated: keep room for the newline */
    line[n++] = '\n';
    /* One write() per line keeps lines atomic across threads. */
    ssize_t ignored = write(STDERR_FILENO, line, (size_t)n);
    (void)ignored;
}
