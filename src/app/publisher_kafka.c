/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* publisher_kafka.c - librdkafka producer backend.
 *
 * - bootstrap.servers = publish.kafka.brokers; client.id = service name; then every key=value
 *   of kafka.properties (publish.kafka.properties passthrough, e.g. SASL/TLS settings).
 * - Message key = sensor.id (or none); headers schema=rtvision.detection.v1 and
 *   content-type=application/json.
 * - Delivery reports -> rtvision_publisher_report_delivery() (published / failed counters).
 * - Health: a metadata request every few seconds while idle, plus delivery results.
 * All calls happen on the publisher worker thread (send/poll/flush) or librdkafka's own threads
 * (error callback); nothing here runs on the GStreamer streaming thread.
 */
#include "publisher.h"

#include <gio/gio.h>
#include <librdkafka/rdkafka.h>
#include <string.h>

#include "event_model.h"
#include "log.h"

#define QUEUE_FULL_RETRY_MS 100
#define HEALTH_CHECK_INTERVAL_US (5 * G_TIME_SPAN_SECOND)
#define HEALTH_CHECK_TIMEOUT_MS 1000

typedef struct {
    RTVisionBackend base;
    rd_kafka_t *rk;
    gchar *topic;
    gint connected; /* atomic gboolean */
    gint64 last_health_check_us;
} KafkaBackend;

static void set_connected(KafkaBackend *k, gboolean v) {
    gint old = g_atomic_int_exchange(&k->connected, v ? 1 : 0);
    if (old != (v ? 1 : 0))
        PLOG_INFO("kafka", "event=%s", v ? "connected" : "disconnected");
}

static void dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *msg, void *opaque) {
    (void)rk;
    KafkaBackend *k = opaque;
    gboolean ok = msg->err == RD_KAFKA_RESP_ERR_NO_ERROR;
    if (k->base.publisher)
        rtvision_publisher_report_delivery(k->base.publisher, ok);
    if (ok)
        set_connected(k, TRUE);
    else
        PLOG_DEBUG("kafka", "event=delivery_failed error=\"%s\"", rd_kafka_err2str(msg->err));
}

static void error_cb(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
    (void)rk;
    KafkaBackend *k = opaque;
    if (err == RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN || err == RD_KAFKA_RESP_ERR__TRANSPORT)
        set_connected(k, FALSE);
    PLOG_WARN("kafka", "event=error code=%s reason=\"%s\"", rd_kafka_err2name(err), reason);
}

static gboolean conf_set(rd_kafka_conf_t *conf, const char *key, const char *value,
                         GError **error) {
    char errstr[512];
    if (rd_kafka_conf_set(conf, key, value, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "kafka property %s: %s", key,
                    errstr);
        return FALSE;
    }
    return TRUE;
}

/* kafka.properties: "key=value" lines, '#' comments. */
static gboolean load_properties(rd_kafka_conf_t *conf, const char *path, GError **error) {
    if (path == NULL || *path == '\0')
        return TRUE;
    gchar *text = NULL;
    if (!g_file_get_contents(path, &text, NULL, error))
        return FALSE;
    gchar **lines = g_strsplit(text, "\n", -1);
    gboolean ok = TRUE;
    for (gchar **l = lines; ok && *l; l++) {
        gchar *line = g_strstrip(*l);
        if (*line == '\0' || *line == '#')
            continue;
        gchar *eq = strchr(line, '=');
        if (eq == NULL) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: expected key=value: %s",
                        path, line);
            ok = FALSE;
            break;
        }
        *eq = '\0';
        ok = conf_set(conf, g_strstrip(line), g_strstrip(eq + 1), error);
    }
    g_strfreev(lines);
    g_free(text);
    return ok;
}

static gboolean kafka_send(RTVisionBackend *b, const char *key, const char *data, gsize len,
                           GError **error) {
    KafkaBackend *k = (KafkaBackend *)b;
    for (int attempt = 0; attempt < 2; attempt++) {
        rd_kafka_headers_t *hdrs = rd_kafka_headers_new(2);
        rd_kafka_header_add(hdrs, "schema", -1, RTVISION_SCHEMA_ID, -1);
        rd_kafka_header_add(hdrs, "content-type", -1, "application/json", -1);
        rd_kafka_resp_err_t err = rd_kafka_producev(
            k->rk, RD_KAFKA_V_TOPIC(k->topic), RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
            RD_KAFKA_V_KEY(key, key ? strlen(key) : 0), RD_KAFKA_V_VALUE((void *)data, len),
            RD_KAFKA_V_HEADERS(hdrs), RD_KAFKA_V_END);
        if (err == RD_KAFKA_RESP_ERR_NO_ERROR)
            return TRUE; /* headers now owned by librdkafka */
        rd_kafka_headers_destroy(hdrs);
        if (err == RD_KAFKA_RESP_ERR__QUEUE_FULL && attempt == 0) {
            rd_kafka_poll(k->rk, QUEUE_FULL_RETRY_MS); /* serve delivery reports, then retry */
            continue;
        }
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "produce: %s", rd_kafka_err2str(err));
        return FALSE;
    }
    return FALSE;
}

static void kafka_poll(RTVisionBackend *b) {
    KafkaBackend *k = (KafkaBackend *)b;
    rd_kafka_poll(k->rk, 0);
    gint64 now = g_get_monotonic_time();
    if (now - k->last_health_check_us >= HEALTH_CHECK_INTERVAL_US) {
        k->last_health_check_us = now;
        const struct rd_kafka_metadata *md = NULL;
        rd_kafka_resp_err_t err = rd_kafka_metadata(k->rk, 0, NULL, &md, HEALTH_CHECK_TIMEOUT_MS);
        set_connected(k, err == RD_KAFKA_RESP_ERR_NO_ERROR && md && md->broker_cnt > 0);
        if (md)
            rd_kafka_metadata_destroy(md);
    }
}

static gboolean kafka_flush(RTVisionBackend *b, gint timeout_ms) {
    KafkaBackend *k = (KafkaBackend *)b;
    rd_kafka_resp_err_t err = rd_kafka_flush(k->rk, MAX(timeout_ms, 0));
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR)
        PLOG_WARN("kafka", "event=flush_incomplete in_queue=%d error=\"%s\"",
                  rd_kafka_outq_len(k->rk), rd_kafka_err2str(err));
    return err == RD_KAFKA_RESP_ERR_NO_ERROR;
}

static gboolean kafka_healthy(RTVisionBackend *b) {
    return g_atomic_int_get(&((KafkaBackend *)b)->connected) != 0;
}

static void kafka_destroy(RTVisionBackend *b) {
    KafkaBackend *k = (KafkaBackend *)b;
    if (k->rk)
        rd_kafka_destroy(k->rk);
    g_free(k->topic);
    g_free(k);
}

static const RTVisionBackendOps kafka_ops = {
    .name = "kafka",
    .async_delivery = TRUE,
    .send = kafka_send,
    .poll = kafka_poll,
    .flush = kafka_flush,
    .healthy = kafka_healthy,
    .destroy = kafka_destroy,
};

RTVisionBackend *rtvision_backend_kafka_new(const char *brokers, const char *topic,
                                            const char *properties_file, const char *client_id,
                                            GError **error) {
    if (brokers == NULL || *brokers == '\0' || topic == NULL || *topic == '\0') {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "kafka backend needs brokers and topic");
        return NULL;
    }
    KafkaBackend *k = g_new0(KafkaBackend, 1);
    k->base.ops = &kafka_ops;
    k->topic = g_strdup(topic);

    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    if (!conf_set(conf, "bootstrap.servers", brokers, error) ||
        !conf_set(conf, "client.id", client_id ? client_id : "rtvision", error) ||
        !load_properties(conf, properties_file, error)) {
        rd_kafka_conf_destroy(conf);
        g_free(k->topic);
        g_free(k);
        return NULL;
    }
    rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);
    rd_kafka_conf_set_error_cb(conf, error_cb);
    rd_kafka_conf_set_opaque(conf, k);

    char errstr[512];
    k->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (k->rk == NULL) { /* conf is destroyed only on success */
        rd_kafka_conf_destroy(conf);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "rd_kafka_new: %s", errstr);
        g_free(k->topic);
        g_free(k);
        return NULL;
    }
    PLOG_INFO("kafka", "event=producer_created brokers=%s topic=%s librdkafka=%s", brokers, topic,
              rd_kafka_version_str());
    return &k->base;
}
