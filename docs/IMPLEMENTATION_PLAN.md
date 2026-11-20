<!-- SPDX-License-Identifier: Apache-2.0 -->
# Implementation plan

## Target repository layout

```text
perceptra/
├── README.md                     quick start, features, licence summary
├── ARCHITECTURE.md  CLAUDE.md  CHANGELOG.md
├── LICENSE                       Apache-2.0 (full text)
├── NOTICE                        attribution (RT-CV derived files, project copyright)
├── THIRD_PARTY_NOTICES.md        runtime deps + licences (kept current)
├── CONTRIBUTING.md  SECURITY.md  CODE_OF_CONDUCT.md
├── .gitignore  .dockerignore  .editorconfig  .clang-format
├── docs/                         API, CONFIGURATION, EVENT_SCHEMA, PARSERS, LICENSING, TESTING, ROADMAP, DEVIATIONS
├── schemas/
│   ├── detection_event.v1.schema.json
│   ├── service_config.schema.json
│   └── model.schema.json
├── src/
│   ├── app/                      C (gnu11) — the `perceptra` binary
│   │   ├── main.c                CLI, lifecycle, signals            (derived: RT-CV main())
│   │   ├── app_config.c/.h       reads app.ini (GKeyFile)
│   │   ├── probe.c/.h            perceptra_probe + task dispatch     (derived: RT-CV probe)
│   │   ├── task_detect.c         detect handler
│   │   ├── task_registry.c/.h    task → handler table (segment/pose = stubs returning "unsupported")
│   │   ├── event_model.h         PerceptraFrameEvent / PerceptraObject structs
│   │   ├── event_serialize.c/.h  JSON via json-glib (schema v1)
│   │   ├── publisher.c/.h        bounded queue + worker thread + backend vtable
│   │   ├── publisher_kafka.c     librdkafka backend
│   │   ├── publisher_stream.c    stdout / file (JSON Lines) backend
│   │   ├── timestamps.c/.h       ntp / uri_utc / system, RFC 3339  (derived: perception_utc.c)
│   │   ├── embeddings.c/.h       ReID cache per stream               (derived: RT-CV)
│   │   ├── ota.c/.h              model hot-swap watcher (inotify)    (derived: RT-CV)
│   │   ├── perf.c/.h             perf callback → metrics             (derived: RT-CV perf_cb)
│   │   ├── metrics.c/.h          counters/gauges registry, Prometheus text render
│   │   ├── ops_server.c/.h       libsoup-3.0 HTTP: /live /ready /startup /metrics
│   │   ├── log.c/.h              levelled key=value logging
│   │   └── Makefile
│   └── parsers/
│       ├── core/                 pure C++17, no DeepStream includes
│       │   ├── detection.hpp     Detection, ParseParams, BoxFormat, Activation
│       │   ├── yolo_e2e.cpp  yolo_raw.cpp  detr.cpp  efficient_nms.cpp
│       │   └── params_ini.cpp    parser.ini reader
│       ├── ds_adapter.cpp        NvDsInferParsePerceptra_* exports
│       ├── tests/                test_*.cpp + tiny header-only test harness (own code)
│       └── Makefile              targets: all, test, test-core (core needs no DeepStream)
├── tools/perceptra_tools/        Python ≥3.10 package
│   ├── run.py                    `perceptra-run` entrypoint
│   ├── gen_configs.py            yaml → DeepStream INI + app.ini + parser.ini
│   ├── onnx_inspect.py           output shapes / names → family check
│   ├── schemas.py                JSON Schema validation helpers
│   └── templates/                ds_main.ini.j2  pgie.ini.j2  app.ini.j2  parser.ini.j2
├── tools/export/                 OPTIONAL, separately installed exporters (see LICENSING)
│   └── rfdetr/README.md + export.sh + export_rfdetr.py   (own venv, never a dependency of the service)
├── config/perceptra.example.yaml
├── models/examples/              model.yaml + labels only (no weights)
│   ├── rfdetr-s-coco/            RF-DETR Small (Apache-2.0): compose + integration-test default
│   ├── rfdetr-s-people/          same network, `person` only
│   ├── trafficcamnet-sample/     `builtin` escape hatch (DeepStream sample model in the container)
│   └── rtdetr-r18-coco/
├── docker/Dockerfile  docker/entrypoint.sh
├── deploy/compose/compose.yaml  deploy/compose/.env.example
├── examples/consumer/print_events.py   examples/consumer/occupancy.py
├── tests/
│   ├── python/                   pytest: schemas, gen_configs, onnx_inspect, serializer golden files
│   ├── golden/                   expected JSON events, generated configs
│   └── integration/              run_all.sh, health.sh, add_stream.sh, consume_validate.py, remove_stream.sh
└── .github/workflows/ci.yml      lint + parser core tests + pytest + licence check (no GPU)
```

---

## Milestones

Each milestone ends with its **acceptance** commands passing. Mark items `[x]` when done, or `PENDING-GPU` where a GPU is needed but unavailable.

### M0 — Repository scaffolding and licensing
- [x] Create the layout above (empty modules with SPDX headers).
- [x] `LICENSE` (Apache-2.0 full text, from `LICENSE` in this spec package), `NOTICE`, `THIRD_PARTY_NOTICES.md`, `.gitignore`, `CONTRIBUTING.md` (DCO sign-off), `SECURITY.md`, `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1, referenced by URL).
- [x] CI skeleton: `ruff`, `clang-format --dry-run`, SPDX header check script `tools/ci/check_spdx.py`, forbidden-file check (weights/videos).

**Acceptance:** `python tools/ci/check_spdx.py` passes; `git ls-files | grep -E '\.(onnx|pt|engine|mp4)$'` is empty.

**Status:** done (2026-11-20). Both acceptance commands pass; `pytest -q` and `ruff` pass.

### M1 — Pipeline skeleton on DeepStream (sample model)
- [x] `docker/Dockerfile` (two stages, base `ARG DS_IMAGE=nvcr.io/nvidia/deepstream:9.1-samples-multiarch`), build deps: `librdkafka-dev libsoup-3.0-dev libjson-glib-dev`; runtime: `librdkafka1 libsoup-3.0-0` + Python tools.
- [x] `src/app/main.c`: derived from RT-CV `main()` minus the non-public APIs (see CLAUDE.md). CLI: `-c <ds-main.ini> -a <app.ini> [--log-level]`. Force TCP for RTSP. `buffer-pool-size=16` on streammux. SIGINT/SIGTERM → EOS → clean shutdown.
- [x] `src/app/probe.c` minimal: count frames and objects per stream; log every N frames at debug level.
- [x] Hand-written `ds-main.ini` using the in-container sample detector (`samples/configs/deepstream-app/config_infer_primary.txt`, ResNet TrafficCamNet) — **referenced by path, not copied into the repo**.

**Acceptance (GPU):** the container starts with 0 streams; `POST /api/v1/stream/add` with `file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4` → FPS logs appear; `stream/remove` works; SIGTERM exits 0.

**Status:** done (2026-11-20) on RTX 5070 Ti Laptop / driver 595.99 / `deepstream:9.1-samples-multiarch`. 0 streams at start; add → HTTP 200, ~30 FPS, 16–27 objects/frame; remove → HTTP 200, `active_sources=0`; `docker stop` → exit 0 in 8.6 s via the shutdown watchdog (DeepStream 9.1 teardown hang, `docs/DEVIATIONS.md` #8). Hand-written configs `config/dev/*.m1.ini` (removed in M3, replaced by generated configs).

### M2 — Parser library
- [x] `core/`: implement `yolo_e2e`, `yolo_raw` (both layouts, auto-detect by shape), `detr` (box formats `cxcywh_norm`, `xyxy_norm`, `xyxy_px`; activations `sigmoid`, `softmax`, `none`; optional background class), `efficient_nms`.
- [x] Common post-steps: score threshold per class, class allow-list, clamp to network size, drop boxes < 1 px, top-k.
- [x] `ds_adapter.cpp`: exports `NvDsInferParsePerceptra_YoloE2E`, `_YoloRaw`, `_Detr`, `_EfficientNms`; reads `parser.ini` once (thread-safe `std::call_once`) from `PERCEPTRA_PARSER_CONFIG`.
- [x] Unit tests with synthetic tensors per family (including edge cases: empty output, all below threshold, out-of-range class, NaN scores, transposed layout).

**Acceptance:** `make -C src/parsers test-core` passes without DeepStream; `make -C src/parsers` builds `libperceptra_parsers.so` in the container.

**Status:** done (2026-11-20). `make -C src/parsers test-core`: 15/15 pass on the host (GCC 11, no DeepStream) and in the image (GCC 13), under ASan + UBSan. In the Docker build stage `make -C src/parsers test` also runs 5 adapter tests (by-name layer lookup, nvinfer thresholds, shape error → `false`, family mismatch, INT32 conversion) and builds `libperceptra_parsers.so`, which exports exactly the 4 `NvDsInferParsePerceptra_*` symbols and is installed at `/opt/perceptra/lib/`. Accuracy against real models is verified with user-supplied ONNX files in M7.

### M3 — Configuration system (`perceptra-run`)
- [x] `schemas/service_config.schema.json`, `schemas/model.schema.json` (per `docs/CONFIGURATION.md`).
- [x] `gen_configs.py`: render the templates; derive every batch-size touch point from `service.max_streams`; derive `filter-out-class-ids` from `classes.keep`; engine file name `<model>_b<batch>_<precision>_gpu<id>_trt<ver>.engine`.
- [x] `onnx_inspect.py`: load ONNX (no inference), check output names/ranks against the family, and warn if a fixed batch dimension < `max_streams`.
- [x] `run.py`: validate → generate into `/tmp/perceptra/generated` → export `PERCEPTRA_PARSER_CONFIG` → `os.execv` the binary.
- [x] Licence gate: if `model.license.spdx` is not in the permissive allow-list, print a prominent warning, and refuse to start unless `PERCEPTRA_ACCEPT_MODEL_LICENSE=<spdx>` is set.

**Acceptance:** `pytest -q tests/python` passes (golden generated configs for the 3 example models); invalid YAML produces a clear error with the field path.

**Status:** done (2026-11-20). `pytest -q`: 83 passed (schemas, env overrides, event schema, golden configs for the 3 example models, mapping rules, licence gate exit 3, ONNX checks on synthetic graphs, `perceptra-run` dry runs). Errors name the file and field path, e.g. `service.max_streams: -1 is less than the minimum of 1`. GPU check: the image's entry point is now `perceptra-run`; TrafficCamNet (builtin) and a synthetic `yolo_e2e` ONNX (`tests/integration/make_synthetic_onnx.py`, read-only models mount) run from generated configs, the synthetic model yields exactly the 2 expected detections per frame through `libperceptra_parsers.so`, and its engine is cached and reused on restart (deviation #13).

### M4 — Events and publisher
- [x] `event_model.h`, `task_detect.c` (bbox rescale to source resolution, track id `null` when untracked, label from `obj_label`), `event_serialize.c` per `docs/EVENT_SCHEMA.md`.
- [x] `publisher.c` (bounded queue, `drop_oldest`/`drop_newest`, flush on shutdown with a timeout), `publisher_stream.c` (stdout/file JSONL), `publisher_kafka.c` (librdkafka; key = `sensor.id`; headers `schema`, `content-type`; passthrough of `publish.kafka.properties`; delivery-report callback → metrics).
- [x] Serializer golden test: C test binary builds a fixed event → JSON → compared to `tests/golden/event_basic.json` and validated by pytest against the schema.

**Acceptance (GPU):** with `publish.backend=stdout`, adding the sample stream prints valid JSON Lines (validated by `tests/integration/consume_validate.py --stdin`).

**Status:** done (2026-11-20). GPU: `docker logs -f … | consume_validate.py --stdin --min-events 5 --sensor-id m1-cam-0 --require-objects` → `PASS: 5 valid events (5 with objects)`; stdout carries only events; SIGTERM flushes the queue (`enqueued=11 published=11 dropped=0`) and exits 0. No GPU: `make -C src/app test` (serializer golden + publisher queue tests with a mock backend, ASan + UBSan) and pytest (golden event vs schema, `consume_validate`). **Pending M7:** the Kafka backend (`publisher_kafka.c`) compiles and is wired in, but is first exercised against a broker in the compose integration test. Timestamps are `system` until M5.

### M5 — Tracking, embeddings, timestamps
- [x] Tracker profiles in `gen_configs` (`nvdcf_perf`, `nvdcf_accuracy`, `nvsort`, `iou`, `none`), referencing the DeepStream sample tracker YAMLs **inside the container by path**. If a profile file is missing in the image, fail with a clear message.
- [x] Embeddings from tracker ReID (`NVDS_TRACKER_OBJ_REID_META`) with the optional cache (`reid_store_age`), encoded `base64-f32le`.
- [x] Timestamps: `ntp`, `uri_utc`, `system`; backward-NTP warning; RFC 3339 ms.

**Acceptance (GPU):** track ids stay stable for the same object across consecutive events in the sample video (manual check + log); timestamps are monotonic per stream.

**Status:** done (2026-11-20), one item PENDING-REID-MODEL.
- Track stability (`nvdcf_perf`, 44 events of the sample video, `tests/integration/track_stability.py`): every object tracked, persistence 0.65 between consecutive events (1 s apart), longest track 25 events; manual check: the longest track (a standing person) keeps its id across 12+ events and through a missed detection. `nvsort` and `iou` profiles run (all objects tracked); `none` → `track_id: null`.
- Timestamps monotonic per stream (`consume_validate.py --check-monotonic`): file source (`system`), RTSP with RTCP via `tests/integration/rtsp_test_server.py` (`ntp`, 28/29 events), `uri_utc` (formatted file name).
- **PENDING-REID-MODEL:** embeddings in events are implemented (`embeddings.c`, generated `tracker.yml` with `outputReidTensor: 1`) but not verified on the GPU, because NVIDIA's ReID network is not in the image and must be downloaded from NGC under NVIDIA's model licence (decision for the project owner). Verify with: mount the model at `/opt/nvidia/deepstream/deepstream/samples/models/Tracker/`, run with `PERCEPTRA__TRACKER__PROFILE=nvdcf_accuracy PERCEPTRA__TRACKER__REID=true PERCEPTRA__TRACKER__REID_STORE_AGE=30 PERCEPTRA__PUBLISH__INCLUDE_EMBEDDINGS=true`, and check `objects[].embedding.dim == 256`.

### M6 — Ops server, metrics, hot swap
- [x] `ops_server.c` (libsoup-3.0, own GMainContext thread), `/live` `/ready` `/startup` `/metrics`.
- [x] Metrics listed in ARCHITECTURE §8; `perf.c` feeds the FPS gauges.
- [x] `ota.c` hot swap (override file → set the nvinfer property → metric + log).

**Acceptance (GPU):** `curl :9464/metrics` shows non-zero `perceptra_frames_processed_total`; a hot swap to a second engine works without dropping the stream.

**Status:** done (2026-11-20). `/metrics` during a stream: `perceptra_frames_processed_total{sensor_id="m1-cam-0"} 347`, FPS 30.2, objects per label, event counters, `perceptra_model_info`; `/startup` 503→200, `/ready` `{"status":"ready","streams":1}`, `/info` as in docs/API.md; `perceptra_streams_active` 0 three seconds after `stream/remove`. Hot swap: synthetic model A (2 persons) → engine of model B (3 persons) via `inference.hot_swap_file` while streaming: objects per event 2→3 at frame 210 with no frame gap, swap 14 ms, `perceptra_model_swaps_total{result="ok"} 1`; an invalid engine path is rejected (`{result="failed"} 1`). No GPU: `make -C src/app test` adds `test-metrics` (Prometheus format).

### M7 — Container, compose, integration tests
- [x] Final Dockerfile (non-root uid 1000, `HEALTHCHECK` on `/live`), `entrypoint.sh` → `perceptra-run`.
- [x] `deploy/compose/compose.yaml`: `apache/kafka` (KRaft, single node) + `perceptra` (GPU reservation, ports 9000/9464, volumes).
- [x] `tests/integration/run_all.sh`: deploy → health → add stream → consume ≥ `MIN_EVENTS` and validate each against the schema → remove stream → `docker compose stop` exits cleanly.
- [x] ~~YOLO26 people profile~~ → RF-DETR Small people profile (`models/examples/rfdetr-s-people`, Apache-2.0) verified end to end; RF-DETR also replaces TrafficCamNet as the integration-test model (deviation #31).

**Acceptance (GPU):** `tests/integration/run_all.sh` passes on a clean machine.

**Status:** done (2026-11-20). Default model RF-DETR Small (Apache-2.0, deviation #31): `tools/export/rfdetr/export.sh` then `tests/integration/run_all.sh` → `PASS: integration test` with no licence acceptance: dry-run preflight, `/startup` 200 (37 s with the engine build, ~3 s from the engine cache), `/live`, `/ready`, stream add, Kafka events valid against the schema (sensor `it-cam-0`, monotonic; cars, persons, backpack, bus, truck at 0.55–0.96, all tracked), Kafka key `it-cam-0` with `schema`/`content-type` headers, `perceptra_events_published_total` > 0, stream remove → `perceptra_streams_active` 0, `docker compose stop` → exit 0 in 9 s. `PERCEPTRA_MODEL=rfdetr-s-people` passes the same test on topic `people-raw` with only `person` objects. First runs of the Kafka backend against a broker (apache/kafka 4.3.2, KRaft). The image runs as uid 1000 (`perceptra`) with a `/live` `HEALTHCHECK`. The previous default `trafficcamnet-sample` also passes (with `PERCEPTRA_ACCEPT_MODEL_LICENSE=LicenseRef-NVIDIA-AI-Product-License`, 12 s → 3 s startup with its engine now cached). Deviations #28–#31. CI lint adds shellcheck and `docker compose config`.

### M8 — Documentation and release hygiene
- [x] README quick start (build, run, add camera, see events), model onboarding guide, troubleshooting.
- [x] `examples/consumer/occupancy.py` (per-camera person count from events).
- [x] `docs/LICENSING.md` release checklist completed; `THIRD_PARTY_NOTICES.md` regenerated (`tools/ci/third_party_report.py` lists the apt and pip packages actually installed in the image with their licences). Three items are owner decisions (see below).
- [ ] Tag `v0.1.0`. **PENDING-OWNER**

**Status:** done except the tag (2026-11-20).
- README quick start executed verbatim from a fresh clone with an empty engine cache: `export.sh` (also creating its venv from scratch, 60 s, byte-identical ONNX), `docker compose up -d --build`, the `/ready` wait loop, stream add, events printed for the whole sample video. Found and documented: DeepStream removes a file stream by itself at end-of-file, so the final remove answers `No record found` (harmless).
- `docs/MODELS.md` (bring your own model, written from the RF-DETR onboarding) and `docs/TROUBLESHOOTING.md` (exit codes, startup, streams, events, shutdown; every metric/config name checked against the code).
- `examples/consumer/print_events.py` and `occupancy.py` (stdin JSON Lines, standard library only); `occupancy.py` verified on live Kafka output of `rfdetr-s-people` (smoothed counts, 0 after the stream ended). Unit tests in `tests/python/test_examples.py`.
- `tools/ci/third_party_report.py`: report from the built image; `--check` passes.
- **PENDING-OWNER** before tagging: project name (PyPI `perceptra` is taken by an unrelated CV SDK), `<PROJECT_OWNER>` placeholder, export-tooling licences (MPL-2.0 `certifi`/`tqdm`, host only). See `docs/LICENSING.md` §8.

---

## Coding standards

- **C:** gnu11, `-Wall -Wextra -Werror` for our files only (not DeepStream sources), `clang-format` (LLVM base, 100 columns), GLib types and memory functions, every allocation freed on shutdown (run under `valgrind`/ASan where feasible).
- **C++ (parsers):** C++17, no exceptions across the C ABI boundary, no allocations in hot loops beyond `reserve`.
- **Python:** 3.10+, `ruff`, type hints, `pytest`; tools only — never in the per-frame path.
- **Config:** every option documented in `docs/CONFIGURATION.md` with a default; unknown keys rejected by schema.
