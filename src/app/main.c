/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & AFFILIATES. All rights
 * reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Modifications Copyright (c) 2026 <PROJECT_OWNER>
 * Changes: derived from main() in RT-CV src/metropolis_perception_app.c; reduced to a single
 * headless pipeline instance; removed X11 windows, keyboard handling, msgapi error propagation,
 * model enum, FSL label handling and input-file options; SIGINT/SIGTERM handled with
 * g_unix_signal_add; force-TCP applied to nvmultiurisrcbin sources; key=value logging; app.ini.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* main.c - `perceptra` binary: CLI, pipeline lifecycle, signal handling.
 *
 *   perceptra -c <ds-main.ini|.yml> [-a <app.ini>] [--log-level LEVEL] [--no-force-tcp]
 *             [--shutdown-timeout S]
 *
 * Lifecycle: parse configs -> task handler + publisher -> create_pipeline() -> PAUSED -> PLAYING
 * -> main loop. SIGINT/SIGTERM (or the pipeline reporting quit) -> leave the loop -> stop event
 * creation -> flush the publisher -> destroy_pipeline() (EOS, drain bus, NULL) -> exit. The whole
 * shutdown is bounded by a watchdog (--shutdown-timeout, see shutdown_watchdog()).
 *
 * Exit codes: 0 clean shutdown, 1 runtime/pipeline error, 2 usage or configuration error.
 */
#include <glib-unix.h>
#include <glib.h>
#include <gst/gst.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "deepstream_app.h"

#include "app_config.h"
#include "embeddings.h"
#include "event_model.h"
#include "log.h"
#include "perf.h"
#include "probe.h"
#include "publisher.h"
#include "task_registry.h"
#include "timestamps.h"

#define PERCEPTRA_VERSION "0.1.0-dev"
#define EXIT_RUNTIME_ERROR 1
#define EXIT_USAGE_ERROR 2

/* RTSP lower transport TCP (GstRTSPLowerTrans GST_RTSP_LOWER_TRANS_TCP). Forcing TCP avoids UDP
 * port problems inside containers, where RTCP sender reports were observed not to arrive. */
#define RTP_PROTOCOL_TCP 0x04
/* Raised from the nvstreammux default because many sources can be connected (from RT-CV). */
#define STREAMMUX_BUFFER_POOL_SIZE 16
#define QUIT_POLL_INTERVAL_MS 100
/* Below Docker/compose's default 10 s stop grace period, so we exit before SIGKILL. */
#define DEFAULT_SHUTDOWN_TIMEOUT_S 8

/* deepstream-app sources expect the application to define this debug category. */
GST_DEBUG_CATEGORY(NVDS_APP);

static GMainLoop *main_loop = NULL;
static AppCtx *app_ctx = NULL;
static gboolean signal_received = FALSE;
static PerceptraPublisher *publisher = NULL;

static gchar *opt_ds_config = NULL;
static gchar *opt_app_config = NULL;
static gchar *opt_log_level = NULL;
static gboolean opt_force_tcp = TRUE;
static gboolean opt_version = FALSE;
static gint opt_shutdown_timeout_s = DEFAULT_SHUTDOWN_TIMEOUT_S;

static GOptionEntry option_entries[] = {
    {"config", 'c', 0, G_OPTION_ARG_FILENAME, &opt_ds_config,
     "DeepStream main config (ds-main.ini or .yml)", "FILE"},
    {"app-config", 'a', 0, G_OPTION_ARG_FILENAME, &opt_app_config, "Perceptra app config (app.ini)",
     "FILE"},
    {"log-level", 0, 0, G_OPTION_ARG_STRING, &opt_log_level,
     "error|warn|info|debug (overrides app.ini)", "LEVEL"},
    {"no-force-tcp", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_force_tcp,
     "Do not force TCP for RTSP RTP/RTCP", NULL},
    {"shutdown-timeout", 0, 0, G_OPTION_ARG_INT, &opt_shutdown_timeout_s,
     "Max seconds for pipeline teardown before forced exit (0 = wait forever, default 8)", "S"},
    {"version", 0, 0, G_OPTION_ARG_NONE, &opt_version, "Print version and exit", NULL},
    {NULL},
};

static gboolean on_signal(gpointer user_data) {
    int signum = GPOINTER_TO_INT(user_data);
    if (!signal_received) {
        PLOG_INFO("main", "event=signal signal=%s action=shutdown",
                  signum == SIGTERM ? "SIGTERM" : "SIGINT");
        signal_received = TRUE;
    }
    if (main_loop && g_main_loop_is_running(main_loop))
        g_main_loop_quit(main_loop);
    return G_SOURCE_CONTINUE;
}

/* The deepstream-app bus callback sets appCtx->quit on EOS (all sources done) or fatal error. */
static gboolean check_pipeline_quit(gpointer user_data) {
    (void)user_data;
    if (app_ctx && app_ctx->quit) {
        PLOG_INFO("main", "event=pipeline_quit return_value=%d", app_ctx->return_value);
        g_main_loop_quit(main_loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* Shutdown watchdog. In DeepStream 9.1, once a stream has been added to nvmultiurisrcbin,
 * setting the pipeline to NULL can block forever inside nvstreammux (gst_pad_stop_task on the
 * muxer's push-loop task), see docs/DEVIATIONS.md #8. Shutdown is therefore bounded: if it has
 * not finished after --shutdown-timeout seconds, log and _exit() with the exit code already
 * decided. The event publisher is flushed BEFORE pipeline teardown (within this budget), so no
 * queued event depends on the teardown; the OS reclaims memory and GPU contexts on exit. */
static gint watchdog_exit_code = 0;

static gpointer shutdown_watchdog(gpointer user_data) {
    gint timeout_s = GPOINTER_TO_INT(user_data);
    g_usleep((gulong)timeout_s * G_USEC_PER_SEC);
    PLOG_WARN("main", "event=shutdown_timeout timeout_s=%d action=exit code=%d", timeout_s,
              g_atomic_int_get(&watchdog_exit_code));
    _exit(g_atomic_int_get(&watchdog_exit_code));
    return NULL;
}

static void arm_shutdown_watchdog(int exit_code) {
    if (opt_shutdown_timeout_s <= 0)
        return;
    g_atomic_int_set(&watchdog_exit_code, exit_code);
    GThread *t = g_thread_new("shutdown-watchdog", shutdown_watchdog,
                              GINT_TO_POINTER(opt_shutdown_timeout_s));
    g_thread_unref(t); /* detached: the process exits normally if teardown completes */
}

/* Events on stdout: keep a private dup of fd 1 for the JSON Lines and point fd 1 at stderr, so
 * DeepStream/GStreamer console output (g_print) cannot interleave with the event stream. */
static int take_stdout_for_events(void) {
    fflush(stdout);
    int fd = dup(STDOUT_FILENO);
    if (fd >= 0 && dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static PerceptraPublisher *create_publisher(const PerceptraAppConfig *cfg,
                                            const PerceptraEventContext *ctx, GError **error) {
    const PerceptraPublishConfig *p = &cfg->publish;
    PerceptraBackend *backend = NULL;
    switch (p->backend) {
    case PERCEPTRA_BACKEND_NONE:
        return NULL;
    case PERCEPTRA_BACKEND_STDOUT:
        backend = perceptra_backend_stream_new_fd(take_stdout_for_events(), error);
        break;
    case PERCEPTRA_BACKEND_FILE:
        backend = perceptra_backend_file_new(p->file_path, error);
        break;
    case PERCEPTRA_BACKEND_KAFKA:
        backend = perceptra_backend_kafka_new(p->kafka_brokers, p->kafka_topic,
                                              p->kafka_properties_file, cfg->service_name, error);
        break;
    }
    if (backend == NULL)
        return NULL;
    return perceptra_publisher_new(backend, ctx, p->queue_max_events, p->on_full,
                                   p->backend == PERCEPTRA_BACKEND_KAFKA && p->kafka_key_sensor_id);
}

static gboolean is_yaml(const char *path) {
    return g_str_has_suffix(path, ".yml") || g_str_has_suffix(path, ".yaml");
}

static void apply_force_tcp(NvDsConfig *config) {
    /* nvmultiurisrcbin: sources added over REST take their attributes from [source-attr-all]. */
    config->source_attr_all_config.select_rtp_protocol = RTP_PROTOCOL_TCP;
    /* Statically configured sources ([sourceN] groups). */
    for (guint j = 0; j < config->num_source_sub_bins; j++)
        config->multi_source_config[j].select_rtp_protocol = RTP_PROTOCOL_TCP;
}

static int parse_cli(int *argc, char ***argv) {
    GError *error = NULL;
    GOptionContext *ctx = g_option_context_new("- Perceptra multi-stream detection service");
    g_option_context_add_main_entries(ctx, option_entries, NULL);
    g_option_context_add_group(ctx, gst_init_get_option_group());
    gboolean ok = g_option_context_parse(ctx, argc, argv, &error);
    if (!ok) {
        PLOG_ERROR("main", "event=usage_error error=\"%s\"", error->message);
        g_clear_error(&error);
    } else if (!opt_version && !opt_ds_config) {
        gchar *help = g_option_context_get_help(ctx, TRUE, NULL);
        PLOG_ERROR("main", "event=usage_error error=\"missing -c/--config\"");
        g_printerr("%s", help);
        g_free(help);
        ok = FALSE;
    }
    g_option_context_free(ctx);
    return ok ? 0 : EXIT_USAGE_ERROR;
}

int main(int argc, char *argv[]) {
    int exit_code = 0;
    PerceptraAppConfig app_cfg;
    perceptra_app_config_init(&app_cfg);

    if (parse_cli(&argc, &argv) != 0) {
        exit_code = EXIT_USAGE_ERROR;
        goto done;
    }
    if (opt_version) {
        g_print("perceptra %s\n", PERCEPTRA_VERSION);
        goto done;
    }
    GST_DEBUG_CATEGORY_INIT(NVDS_APP, "NVDS_APP", 0, NULL);

    if (opt_app_config) {
        GError *error = NULL;
        if (!perceptra_app_config_load(opt_app_config, &app_cfg, &error)) {
            PLOG_ERROR("main", "event=config_error file=%s error=\"%s\"", opt_app_config,
                       error->message);
            g_clear_error(&error);
            exit_code = EXIT_USAGE_ERROR;
            goto done;
        }
    }
    perceptra_log_set_level(app_cfg.log_level);
    if (opt_log_level) {
        PerceptraLogLevel level;
        if (!perceptra_log_parse_level(opt_log_level, &level)) {
            PLOG_ERROR("main", "event=usage_error error=\"invalid --log-level '%s'\"",
                       opt_log_level);
            exit_code = EXIT_USAGE_ERROR;
            goto done;
        }
        perceptra_log_set_level(level);
    }

    PLOG_INFO("main",
              "event=starting version=%s ds_config=%s app_config=%s model=%s model_version=%s "
              "task=%s family=%s force_tcp=%d",
              PERCEPTRA_VERSION, opt_ds_config, opt_app_config ? opt_app_config : "-",
              app_cfg.model_name, app_cfg.model_version, app_cfg.model_task, app_cfg.model_family,
              opt_force_tcp);

    app_ctx = g_malloc0(sizeof(AppCtx));
    app_ctx->person_class_id = -1;
    app_ctx->car_class_id = -1;
    app_ctx->index = 0;
    app_ctx->active_source_index = -1;

    gboolean parsed = is_yaml(opt_ds_config)
                          ? parse_config_file_yaml(&app_ctx->config, opt_ds_config)
                          : parse_config_file(&app_ctx->config, opt_ds_config);
    if (!parsed) {
        PLOG_ERROR("main", "event=config_error file=%s error=\"failed to parse DeepStream config\"",
                   opt_ds_config);
        exit_code = EXIT_USAGE_ERROR;
        goto done;
    }
    if (opt_force_tcp)
        apply_force_tcp(&app_ctx->config);

    const PerceptraTaskHandler *handler = perceptra_task_get(app_cfg.model_task);
    GError *task_error = NULL;
    if (handler == NULL || !handler->init(&task_error)) {
        PLOG_ERROR("main", "event=config_error error=\"task '%s': %s\"", app_cfg.model_task,
                   task_error ? task_error->message : "unknown task");
        g_clear_error(&task_error);
        exit_code = EXIT_USAGE_ERROR;
        goto done;
    }
    perceptra_timestamps_init(app_cfg.timestamp_source);
    perceptra_embeddings_init(app_cfg.publish.include_embeddings, app_cfg.tracker_reid_store_age);
    PLOG_INFO("main", "event=events_config timestamps=%s embeddings=%d reid_store_age=%u",
              perceptra_timestamp_source_name(app_cfg.timestamp_source),
              app_cfg.publish.include_embeddings, app_cfg.tracker_reid_store_age);

    const PerceptraEventContext event_ctx = {
        .service = app_cfg.service_name,
        .model_name = app_cfg.model_name,
        .model_version = app_cfg.model_version,
        .model_task = app_cfg.model_task,
    };
    GError *pub_error = NULL;
    publisher = create_publisher(&app_cfg, &event_ctx, &pub_error);
    if (pub_error) {
        PLOG_ERROR("main", "event=publisher_error backend=%s error=\"%s\"",
                   perceptra_backend_kind_name(app_cfg.publish.backend), pub_error->message);
        g_clear_error(&pub_error);
        exit_code = EXIT_USAGE_ERROR;
        goto done;
    }
    if (publisher == NULL)
        PLOG_WARN("main", "event=no_publisher reason=\"[publish] backend not set\"");

    const PerceptraProbeConfig probe_cfg = {
        .log_every_n_frames = app_cfg.probe_log_every_n_frames,
        .publisher = publisher,
        .handler = handler,
        .interval_frames = app_cfg.publish.interval_frames,
        .include_empty_frames = app_cfg.publish.include_empty_frames,
    };
    perceptra_probe_init(&probe_cfg);

    if (!create_pipeline(app_ctx, perceptra_probe, NULL, perceptra_perf_cb, NULL)) {
        PLOG_ERROR("main", "event=pipeline_error error=\"create_pipeline failed\"");
        exit_code = EXIT_RUNTIME_ERROR;
        goto done;
    }

    GstElement *mux = app_ctx->pipeline.multi_src_bin.streammux;
    if (mux)
        g_object_set(mux, "buffer-pool-size", STREAMMUX_BUFFER_POOL_SIZE, NULL);

    main_loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, on_signal, GINT_TO_POINTER(SIGINT));
    g_unix_signal_add(SIGTERM, on_signal, GINT_TO_POINTER(SIGTERM));
    g_timeout_add(QUIT_POLL_INTERVAL_MS, check_pipeline_quit, NULL);

    if (gst_element_set_state(app_ctx->pipeline.pipeline, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
        PLOG_ERROR("main", "event=pipeline_error error=\"failed to set PAUSED\"");
        exit_code = EXIT_RUNTIME_ERROR;
        goto done;
    }
    if (gst_element_set_state(app_ctx->pipeline.pipeline, GST_STATE_PLAYING) ==
        GST_STATE_CHANGE_FAILURE) {
        PLOG_ERROR("main", "event=pipeline_error error=\"failed to set PLAYING\"");
        exit_code = EXIT_RUNTIME_ERROR;
        goto done;
    }
    PLOG_INFO("main", "event=pipeline_playing max_streams=%u rest=%s:%s",
              app_ctx->config.max_batch_size,
              app_ctx->config.http_ip ? app_ctx->config.http_ip : "-",
              app_ctx->config.http_port ? app_ctx->config.http_port : "-");

    g_main_loop_run(main_loop);

    if (!signal_received && app_ctx->return_value == -1)
        exit_code = EXIT_RUNTIME_ERROR;

done:
    if (app_ctx && app_ctx->return_value == -1 && exit_code == 0 && !signal_received)
        exit_code = EXIT_RUNTIME_ERROR;
    if (app_ctx && app_ctx->pipeline.pipeline)
        arm_shutdown_watchdog(exit_code); /* bounds flush + teardown */
    /* 1. no new events  2. flush the publisher  3. tear down the pipeline */
    perceptra_probe_stop_events();
    if (publisher) {
        guint budget = app_cfg.publish.shutdown_flush_timeout_ms;
        if (opt_shutdown_timeout_s > 0) /* keep >= 2 s of the watchdog budget for teardown */
            budget = MIN(budget, (guint)MAX(opt_shutdown_timeout_s * 1000 - 2000, 0));
        PLOG_INFO("main", "event=shutdown stage=flush_publisher timeout_ms=%u", budget);
        perceptra_publisher_shutdown(publisher, budget);
    }
    if (app_ctx) {
        if (app_ctx->pipeline.pipeline) {
            PLOG_INFO("main", "event=shutdown stage=destroy_pipeline timeout_s=%d",
                      opt_shutdown_timeout_s);
            destroy_pipeline(app_ctx); /* sends EOS, drains the bus, sets NULL */
            PLOG_INFO("main", "event=shutdown stage=pipeline_destroyed");
        }
        g_free(app_ctx);
        app_ctx = NULL;
    }
    g_clear_pointer(&publisher, perceptra_publisher_free);
    perceptra_embeddings_shutdown();
    if (main_loop)
        g_main_loop_unref(main_loop);
    perceptra_app_config_clear(&app_cfg);
    g_free(opt_ds_config);
    g_free(opt_app_config);
    g_free(opt_log_level);
    PLOG_INFO("main", "event=exit code=%d", exit_code);
    if (gst_is_initialized())
        gst_deinit();
    return exit_code;
}
