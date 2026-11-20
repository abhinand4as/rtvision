/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

/* ops_server.h - health, metrics and info HTTP server (docs/API.md, port 9464).
 *
 *   GET /startup  200 once configs are parsed and the pipeline reached PLAYING
 *   GET /ready    200 while PLAYING and the publisher is healthy (Kafka connected, or other
 * backend) GET /live     200 while the main-loop heartbeat is younger than 10 s GET /metrics
 * Prometheus text exposition 0.0.4 GET /info     service, version, model, max_streams, schema
 * Non-200 health answers are 503 {"status":"<reason>"}. Runs on its own thread and GMainContext,
 * independent of DeepStream's REST server. Unauthenticated: bind to trusted networks.
 */
#ifndef PERCEPTRA_OPS_SERVER_H
#define PERCEPTRA_OPS_SERVER_H

#include <glib.h>

#include "app_config.h"
#include "publisher.h"

G_BEGIN_DECLS

#define PERCEPTRA_LIVE_MAX_AGE_S 10

/* cfg and publisher must stay valid until perceptra_ops_stop(). */
gboolean perceptra_ops_start(guint port, const PerceptraAppConfig *cfg,
                             PerceptraPublisher *publisher, const char *version, GError **error);
void perceptra_ops_stop(void);

void perceptra_ops_set_started(void);             /* /startup -> 200 (sticky) */
void perceptra_ops_set_playing(gboolean playing); /* /ready gate */
void perceptra_ops_heartbeat(void);               /* /live: call from the main loop */

G_END_DECLS

#endif /* PERCEPTRA_OPS_SERVER_H */
