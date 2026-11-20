/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* publisher_stream.c - JSON Lines backends: stdout (via a dedicated fd) and file. */
#include "publisher.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    PerceptraBackend base;
    int fd;
    gboolean ok; /* last write succeeded */
} StreamBackend;

/* write() all bytes, retrying on EINTR / short writes. */
static gboolean write_all(int fd, const char *data, gsize len, GError **error) {
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "write: %s",
                        g_strerror(errno));
            return FALSE;
        }
        data += n;
        len -= (gsize)n;
    }
    return TRUE;
}

static gboolean stream_send(PerceptraBackend *b, const char *key, const char *data, gsize len,
                            GError **error) {
    (void)key;
    StreamBackend *s = (StreamBackend *)b;
    /* One line per event: payload + '\n' (the JSON never contains raw newlines). */
    s->ok = write_all(s->fd, data, len, error) && write_all(s->fd, "\n", 1, error);
    return s->ok;
}

static gboolean stream_flush(PerceptraBackend *b, gint timeout_ms) {
    (void)timeout_ms;
    StreamBackend *s = (StreamBackend *)b;
    if (fsync(s->fd) != 0 && errno != EINVAL && errno != EROFS)
        return FALSE; /* EINVAL: pipes/ttys cannot be synced, nothing is buffered anyway */
    return TRUE;
}

static gboolean stream_healthy(PerceptraBackend *b) {
    return ((StreamBackend *)b)->ok;
}

static void stream_destroy(PerceptraBackend *b) {
    StreamBackend *s = (StreamBackend *)b;
    if (s->fd >= 0)
        close(s->fd);
    g_free(s);
}

static const PerceptraBackendOps stdout_ops = {
    .name = "stdout",
    .async_delivery = FALSE,
    .send = stream_send,
    .flush = stream_flush,
    .healthy = stream_healthy,
    .destroy = stream_destroy,
};

static const PerceptraBackendOps file_ops = {
    .name = "file",
    .async_delivery = FALSE,
    .send = stream_send,
    .flush = stream_flush,
    .healthy = stream_healthy,
    .destroy = stream_destroy,
};

PerceptraBackend *perceptra_backend_stream_new_fd(int fd, GError **error) {
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "invalid fd");
        return NULL;
    }
    StreamBackend *s = g_new0(StreamBackend, 1);
    s->base.ops = &stdout_ops;
    s->fd = fd;
    s->ok = TRUE;
    return &s->base;
}

PerceptraBackend *perceptra_backend_file_new(const char *path, GError **error) {
    if (path == NULL || *path == '\0') {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "publish.file.path is empty");
        return NULL;
    }
    gchar *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "mkdir %s: %s", dir,
                    g_strerror(errno));
        g_free(dir);
        return NULL;
    }
    g_free(dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "open %s: %s", path,
                    g_strerror(errno));
        return NULL;
    }
    StreamBackend *s = g_new0(StreamBackend, 1);
    s->base.ops = &file_ops;
    s->fd = fd;
    s->ok = TRUE;
    return &s->base;
}
