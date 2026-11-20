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
│   └── ultralytics/README.md + export_yolo.py   (never a dependency of the service)
├── config/perceptra.example.yaml
├── models/examples/              model.yaml + labels only (no weights)
│   ├── trafficcamnet-sample/     uses the DeepStream sample model inside the container (tests)
│   ├── yolo26s-people/
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
- [ ] `docker/Dockerfile` (two stages, base `ARG DS_IMAGE=nvcr.io/nvidia/deepstream:9.1-samples-multiarch`), build deps: `librdkafka-dev libsoup-3.0-dev libjson-glib-dev`; runtime: `librdkafka1 libsoup-3.0-0` + Python tools.
- [ ] `src/app/main.c`: derived from RT-CV `main()` minus the non-public APIs (see CLAUDE.md). CLI: `-c <ds-main.ini> -a <app.ini> [--log-level]`. Force TCP for RTSP. `buffer-pool-size=16` on streammux. SIGINT/SIGTERM → EOS → clean shutdown.
- [ ] `src/app/probe.c` minimal: count frames and objects per stream; log every N frames at debug level.
- [ ] Hand-written `ds-main.ini` using the in-container sample detector (`samples/configs/deepstream-app/config_infer_primary.txt`, ResNet TrafficCamNet) — **referenced by path, not copied into the repo**.

**Acceptance (GPU):** the container starts with 0 streams; `POST /api/v1/stream/add` with `file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4` → FPS logs appear; `stream/remove` works; SIGTERM exits 0.

### M2 — Parser library
- [ ] `core/`: implement `yolo_e2e`, `yolo_raw` (both layouts, auto-detect by shape), `detr` (box formats `cxcywh_norm`, `xyxy_norm`, `xyxy_px`; activations `sigmoid`, `softmax`, `none`; optional background class), `efficient_nms`.
- [ ] Common post-steps: score threshold per class, class allow-list, clamp to network size, drop boxes < 1 px, top-k.
- [ ] `ds_adapter.cpp`: exports `NvDsInferParsePerceptra_YoloE2E`, `_YoloRaw`, `_Detr`, `_EfficientNms`; reads `parser.ini` once (thread-safe `std::call_once`) from `PERCEPTRA_PARSER_CONFIG`.
- [ ] Unit tests with synthetic tensors per family (including edge cases: empty output, all below threshold, out-of-range class, NaN scores, transposed layout).

**Acceptance:** `make -C src/parsers test-core` passes without DeepStream; `make -C src/parsers` builds `libperceptra_parsers.so` in the container.

### M3 — Configuration system (`perceptra-run`)
- [ ] `schemas/service_config.schema.json`, `schemas/model.schema.json` (per `docs/CONFIGURATION.md`).
- [ ] `gen_configs.py`: render the templates; derive every batch-size touch point from `service.max_streams`; derive `filter-out-class-ids` from `classes.keep`; engine file name `<model>_b<batch>_<precision>_gpu<id>_trt<ver>.engine`.
- [ ] `onnx_inspect.py`: load ONNX (no inference), check output names/ranks against the family, and warn if a fixed batch dimension < `max_streams`.
- [ ] `run.py`: validate → generate into `/tmp/perceptra/generated` → export `PERCEPTRA_PARSER_CONFIG` → `os.execv` the binary.
- [ ] Licence gate: if `model.license.spdx` is not in the permissive allow-list, print a prominent warning, and refuse to start unless `PERCEPTRA_ACCEPT_MODEL_LICENSE=<spdx>` is set.

**Acceptance:** `pytest -q tests/python` passes (golden generated configs for the 3 example models); invalid YAML produces a clear error with the field path.

### M4 — Events and publisher
- [ ] `event_model.h`, `task_detect.c` (bbox rescale to source resolution, track id `null` when untracked, label from `obj_label`), `event_serialize.c` per `docs/EVENT_SCHEMA.md`.
- [ ] `publisher.c` (bounded queue, `drop_oldest`/`drop_newest`, flush on shutdown with a timeout), `publisher_stream.c` (stdout/file JSONL), `publisher_kafka.c` (librdkafka; key = `sensor.id`; headers `schema`, `content-type`; passthrough of `publish.kafka.properties`; delivery-report callback → metrics).
- [ ] Serializer golden test: C test binary builds a fixed event → JSON → compared to `tests/golden/event_basic.json` and validated by pytest against the schema.

**Acceptance (GPU):** with `publish.backend=stdout`, adding the sample stream prints valid JSON Lines (validated by `tests/integration/consume_validate.py --stdin`).

### M5 — Tracking, embeddings, timestamps
- [ ] Tracker profiles in `gen_configs` (`nvdcf_perf`, `nvdcf_accuracy`, `nvsort`, `iou`, `none`), referencing the DeepStream sample tracker YAMLs **inside the container by path**. If a profile file is missing in the image, fail with a clear message.
- [ ] Embeddings from tracker ReID (`NVDS_TRACKER_OBJ_REID_META`) with the optional cache (`reid_store_age`), encoded `base64-f32le`.
- [ ] Timestamps: `ntp`, `uri_utc`, `system`; backward-NTP warning; RFC 3339 ms.

**Acceptance (GPU):** track ids stay stable for the same object across consecutive events in the sample video (manual check + log); timestamps are monotonic per stream.

### M6 — Ops server, metrics, hot swap
- [ ] `ops_server.c` (libsoup-3.0, own GMainContext thread), `/live` `/ready` `/startup` `/metrics`.
- [ ] Metrics listed in ARCHITECTURE §8; `perf.c` feeds the FPS gauges.
- [ ] `ota.c` hot swap (override file → set the nvinfer property → metric + log).

**Acceptance (GPU):** `curl :9464/metrics` shows non-zero `perceptra_frames_processed_total`; a hot swap to a second engine works without dropping the stream.

### M7 — Container, compose, integration tests
- [ ] Final Dockerfile (non-root uid 1000, `HEALTHCHECK` on `/live`), `entrypoint.sh` → `perceptra-run`.
- [ ] `deploy/compose/compose.yaml`: `apache/kafka` (KRaft, single node) + `perceptra` (GPU reservation, ports 9000/9464, volumes).
- [ ] `tests/integration/run_all.sh`: deploy → health → add stream → consume ≥ `MIN_EVENTS` and validate each against the schema → remove stream → `docker compose stop` exits cleanly.
- [ ] YOLO26 people profile verified end to end (requires the user to supply the ONNX; see `models/examples/yolo26s-people/README.md`).

**Acceptance (GPU):** `tests/integration/run_all.sh` passes on a clean machine.

### M8 — Documentation and release hygiene
- [ ] README quick start (build, run, add camera, see events), model onboarding guide, troubleshooting.
- [ ] `examples/consumer/occupancy.py` (per-camera person count from events).
- [ ] `docs/LICENSING.md` release checklist completed; `THIRD_PARTY_NOTICES.md` regenerated (`tools/ci/third_party_report.py` lists the apt and pip packages actually installed in the image with their licences).
- [ ] Tag `v0.1.0`.

---

## Coding standards

- **C:** gnu11, `-Wall -Wextra -Werror` for our files only (not DeepStream sources), `clang-format` (LLVM base, 100 columns), GLib types and memory functions, every allocation freed on shutdown (run under `valgrind`/ASan where feasible).
- **C++ (parsers):** C++17, no exceptions across the C ABI boundary, no allocations in hot loops beyond `reserve`.
- **Python:** 3.10+, `ruff`, type hints, `pytest`; tools only — never in the per-frame path.
- **Config:** every option documented in `docs/CONFIGURATION.md` with a default; unknown keys rejected by schema.
