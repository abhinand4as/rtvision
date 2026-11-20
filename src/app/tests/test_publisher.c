/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

/* test_publisher.c - publisher queue tests with a mock backend (GLib only, no DeepStream).
 * Drop policies, ordering, shutdown flush, shutdown deadline, file backend. */
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "publisher.h"

static int failures = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const PerceptraEventContext kCtx = {"svc", "m", "1", "detect"};

typedef struct {
    PerceptraBackend base;
    GMutex lock;
    GCond go_cond;
    gboolean gate_open; /* send() blocks until open (simulates a slow broker) */
    GPtrArray *sent;    /* sensor ids in send order */
    gint send_delay_ms;
} MockBackend;

static gboolean mock_send(PerceptraBackend *b, const char *key, const char *data, gsize len,
                          GError **error) {
    (void)key;
    (void)len;
    (void)error;
    MockBackend *m = (MockBackend *)b;
    g_mutex_lock(&m->lock);
    while (!m->gate_open)
        g_cond_wait(&m->go_cond, &m->lock);
    /* data contains "id":"<sensor>" */
    const char *p = strstr(data, "\"sensor\":{\"id\":\"");
    g_ptr_array_add(m->sent, p ? g_strndup(p + 16, strcspn(p + 16, "\"")) : g_strdup("?"));
    g_mutex_unlock(&m->lock);
    if (m->send_delay_ms)
        g_usleep((gulong)m->send_delay_ms * 1000);
    return TRUE;
}

static gboolean mock_flush(PerceptraBackend *b, gint timeout_ms) {
    (void)b;
    (void)timeout_ms;
    return TRUE;
}

static void mock_destroy(PerceptraBackend *b) {
    MockBackend *m = (MockBackend *)b;
    g_ptr_array_free(m->sent, TRUE);
    g_mutex_clear(&m->lock);
    g_cond_clear(&m->go_cond);
    g_free(m);
}

static const PerceptraBackendOps mock_ops = {
    .name = "mock", .send = mock_send, .flush = mock_flush, .destroy = mock_destroy};

static MockBackend *mock_new(gboolean gate_open) {
    MockBackend *m = g_new0(MockBackend, 1);
    m->base.ops = &mock_ops;
    g_mutex_init(&m->lock);
    g_cond_init(&m->go_cond);
    m->gate_open = gate_open;
    m->sent = g_ptr_array_new_with_free_func(g_free);
    return m;
}

static void open_gate(MockBackend *m) {
    g_mutex_lock(&m->lock);
    m->gate_open = TRUE;
    g_cond_broadcast(&m->go_cond);
    g_mutex_unlock(&m->lock);
}

static PerceptraFrameEvent *ev(const char *sensor) {
    PerceptraFrameEvent *e = perceptra_event_new(0);
    perceptra_event_set_random_id(e);
    e->sensor_id = g_strdup(sensor);
    e->frame_width = e->frame_height = 1;
    return e;
}

/* Fill the queue while the backend is blocked on the first event. */
static void test_policy(PerceptraDropPolicy policy, const char *expect_order) {
    MockBackend *m = mock_new(FALSE);
    PerceptraPublisher *pub = perceptra_publisher_new(&m->base, &kCtx, 3, policy, TRUE);
    perceptra_publisher_enqueue(pub, ev("a")); /* taken by the worker, blocks in send */
    g_usleep(50 * 1000);
    const char *names[] = {"b", "c", "d", "e", "f"};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++)
        perceptra_publisher_enqueue(pub, ev(names[i]));
    PerceptraPublisherStats s;
    perceptra_publisher_get_stats(pub, &s);
    CHECK(s.depth == 3);
    CHECK(s.dropped == 2);
    open_gate(m);
    CHECK(perceptra_publisher_shutdown(pub, 2000) == FALSE); /* drops happened */
    perceptra_publisher_get_stats(pub, &s);
    CHECK(s.enqueued == (policy == PERCEPTRA_DROP_OLDEST ? 6u : 4u));
    CHECK(s.published == 4);
    GString *order = g_string_new(NULL);
    for (guint i = 0; i < m->sent->len; i++)
        g_string_append(order, g_ptr_array_index(m->sent, i));
    if (strcmp(order->str, expect_order) != 0) {
        fprintf(stderr, "policy %d: sent %s, expected %s\n", policy, order->str, expect_order);
        failures++;
    }
    g_string_free(order, TRUE);
    /* enqueue after shutdown is refused and counted as dropped */
    CHECK(!perceptra_publisher_enqueue(pub, ev("z")));
    perceptra_publisher_free(pub);
}

static void test_clean_flush(void) {
    MockBackend *m = mock_new(TRUE);
    PerceptraPublisher *pub =
        perceptra_publisher_new(&m->base, &kCtx, 100, PERCEPTRA_DROP_OLDEST, FALSE);
    for (int i = 0; i < 50; i++)
        perceptra_publisher_enqueue(pub, ev("s"));
    CHECK(perceptra_publisher_shutdown(pub, 5000) == TRUE);
    PerceptraPublisherStats s;
    perceptra_publisher_get_stats(pub, &s);
    CHECK(s.enqueued == 50 && s.published == 50 && s.dropped == 0 && s.depth == 0);
    CHECK(perceptra_publisher_shutdown(pub, 0) == TRUE); /* idempotent */
    perceptra_publisher_free(pub);
}

static void test_shutdown_deadline(void) {
    MockBackend *m = mock_new(TRUE);
    m->send_delay_ms = 50;
    PerceptraPublisher *pub =
        perceptra_publisher_new(&m->base, &kCtx, 100, PERCEPTRA_DROP_OLDEST, FALSE);
    for (int i = 0; i < 40; i++) /* ~2 s of work */
        perceptra_publisher_enqueue(pub, ev("s"));
    gint64 t0 = g_get_monotonic_time();
    CHECK(perceptra_publisher_shutdown(pub, 300) == FALSE);
    gint64 took_ms = (g_get_monotonic_time() - t0) / 1000;
    CHECK(took_ms < 1000);
    PerceptraPublisherStats s;
    perceptra_publisher_get_stats(pub, &s);
    CHECK(s.published + s.dropped == 40 && s.dropped > 0);
    perceptra_publisher_free(pub);
}

static void test_file_backend(void) {
    gchar *dir = g_dir_make_tmp("perceptra-pub-XXXXXX", NULL);
    gchar *path = g_build_filename(dir, "sub", "events.jsonl", NULL);
    GError *error = NULL;
    PerceptraBackend *b = perceptra_backend_file_new(path, &error);
    CHECK(b != NULL && error == NULL);
    PerceptraPublisher *pub = perceptra_publisher_new(b, &kCtx, 10, PERCEPTRA_DROP_OLDEST, FALSE);
    perceptra_publisher_enqueue(pub, ev("f1"));
    perceptra_publisher_enqueue(pub, ev("f2"));
    CHECK(perceptra_publisher_shutdown(pub, 2000));
    perceptra_publisher_free(pub);
    gchar *text = NULL;
    CHECK(g_file_get_contents(path, &text, NULL, NULL));
    gchar **lines = g_strsplit(text ? text : "", "\n", -1);
    CHECK(g_strv_length(lines) == 3 && lines[2][0] == '\0'); /* 2 lines + trailing newline */
    CHECK(strstr(lines[0], "\"id\":\"f1\"") && strstr(lines[1], "\"id\":\"f2\""));
    g_strfreev(lines);
    g_free(text);
    CHECK(perceptra_backend_file_new("", &error) == NULL && error != NULL);
    g_clear_error(&error);
    g_unlink(path);
    gchar *sub = g_path_get_dirname(path);
    g_rmdir(sub);
    g_rmdir(dir);
    g_free(sub);
    g_free(path);
    g_free(dir);
}

int main(void) {
    perceptra_log_set_level(PERCEPTRA_LOG_ERROR);
    test_policy(PERCEPTRA_DROP_OLDEST, "adef"); /* b, c dropped */
    test_policy(PERCEPTRA_DROP_NEWEST, "abcd"); /* e, f dropped */
    test_clean_flush();
    test_shutdown_deadline();
    test_file_backend();
    printf("%s: publisher tests (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
