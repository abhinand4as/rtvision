<!-- SPDX-License-Identifier: Apache-2.0 -->
# Changelog

All notable changes are documented here (format: Keep a Changelog; versioning: SemVer).

## [Unreleased]
### Added
- RTSP stream replicator: `rtvision-replicator` / `tools/replicator/replicator.sh` serves video files
  as looping, real-time RTSP cameras without re-encoding; YAML configuration, HTTP control API.
- Web UI (`rtvision-ui`, `deploy/compose/compose.ui.yaml`): live annotated streams, performance and
  hardware, replicator management (upload, streams, start/stop).
- Ops server `GET /events/latest`; `outputs.tiled` and `outputs.rtsp.bitrate`; box labels and track
  ids in annotated outputs; `interval_frames` in `/info`; `tests/integration/run_ui.sh`.
- Demo `examples/demo` (four cameras from one CC-BY-4.0 online video) and screenshots in
  `docs/images`; all documents updated for the new features.

## [0.1.0] - 2026-11-20
### Added
- M8 documentation and release hygiene: product README with a verified quick start,
  `docs/MODELS.md` (bring your own model), `docs/TROUBLESHOOTING.md`,
  `examples/consumer/print_events.py` and `occupancy.py`, `tools/ci/third_party_report.py`
  (licences of what the built image contains; `THIRD_PARTY_NOTICES.md` regenerated), release
  checklist status in `docs/LICENSING.md` §8.
- M7 container, compose, integration tests: non-root image (uid 1000 `rtvision`), `HEALTHCHECK` on
  `/live`, `docker/entrypoint.sh` (engine-cache writability check, passes flags to `rtvision-run`);
  `deploy/compose/compose.yaml` (apache/kafka 4.3.2 KRaft + rtvision with GPU reservation, engine
  cache volume, ports on 127.0.0.1 by default) and `.env.example`; `tests/integration/run_all.sh`
  with `health.sh`, `add_stream.sh`, `remove_stream.sh`, `common.sh` (dry-run licence preflight,
  fail-fast on container restarts, Kafka key/header checks, graceful-stop timing); engine caching for
  `family: builtin` nvinfer configs; CI runs shellcheck and `docker compose config`.
- RF-DETR Small (Roboflow, Apache-2.0) as the default model: `models/examples/rfdetr-s-coco`
  (compose and integration-test default, 91-line COCO-id labels) and `rfdetr-s-people`;
  `tools/export/rfdetr/` (host venv exporter, Apache-2.0 sizes only).
- M6 ops server, metrics, hot swap: libsoup-3.0 ops server on its own thread (`/live`, `/ready`,
  `/startup`, `/metrics`, `/info`), Prometheus metrics from ARCHITECTURE §8 (per-stream FPS, frames,
  objects per label, event counters, queue depth, model info, swaps), model hot swap from
  `inference.hot_swap_file` (derived from RT-CV OTA), `make -C src/app test-metrics`, synthetic model
  variant `--variant b` for swap tests.
- M5 tracking, embeddings, timestamps: `timestamps.c` (ntp / uri_utc / system per stream with
  per-event fallback, backward-NTP warning, URI UTC parser ported from RT-CV), `embeddings.c`
  (tracker ReID features with a `reid_store_age` cache), generated `tracker.yml` for ReID tensor
  output, clear startup error when the ReID model is missing, `tracker.reid_store_age` setting,
  `attach-sys-ts-as-ntp` mapping; `make -C src/app test-timestamps`;
  `tests/integration/track_stability.py`, `rtsp_test_server.py`, `consume_validate.py --check-monotonic`.
- M4 events and publisher: `RTVisionFrameEvent`/`RTVisionObject` model, task registry (`detect`;
  `segment`/`pose` stubs), detect handler (bbox rescaled to source resolution, nullable track id and
  confidence), JSON serializer for `rtvision.detection.v1`, bounded publisher queue with
  `drop_oldest`/`drop_newest`, worker thread and bounded shutdown flush, backends `stdout`/`file`
  (JSON Lines) and `kafka` (librdkafka, key = sensor id, `schema`/`content-type` headers, property
  passthrough, delivery reports, health check); `make -C src/app test`; golden event
  `tests/golden/event_basic.json`; `tests/integration/consume_validate.py --stdin`; CI job `app-unit`.
- M3 configuration system: `rtvision-run` (CLI > env > default file selection, `RTVISION__…` env
  overrides, JSON Schema validation with field paths, task check, model licence gate with exit 3,
  ONNX output check, config generation, engine cache, `execv` of the binary); `gen_configs.py` with
  Jinja templates for `ds-main.ini`, `pgie.ini`, `parser.ini`, `app.ini`, `kafka.properties`;
  `onnx_inspect` CLI; golden configs for the 3 example models; COCO `labels.txt` for the YOLO26 and
  RT-DETR examples; `tests/integration/make_synthetic_onnx.py` (synthetic `yolo_e2e` test model).
- M2 parser library `librtvision_parsers.so`: pure C++17 core (`src/parsers/core/`) with the
  `yolo_e2e`, `yolo_raw` (both layouts), `detr` (4 box formats; sigmoid/softmax/none; background
  column) and `efficient_nms` decoders, common post-steps and a strict `parser.ini` reader; DeepStream
  adapter exporting `NvDsInferParseRTVision_{YoloE2E,YoloRaw,Detr,EfficientNms}`; own header-only test
  harness with 15 core and 5 adapter tests (ASan + UBSan); CI job `parser-core`.
- M1 pipeline skeleton on DeepStream 9.1: `rtvision` binary (`src/app/main.c`, derived from RT-CV
  `main()`) with `-c/-a/--log-level/--no-force-tcp/--shutdown-timeout`, SIGINT/SIGTERM handling,
  force-TCP for REST-added RTSP sources and `buffer-pool-size=16`; per-stream frame/object counters
  (`probe.c`); FPS logging (`perf.c`, derived from RT-CV `perf_cb`); levelled `key=value` logging
  (`log.c`); `app.ini` reader (`app_config.c`).
- `src/app/Makefile` (derived from RT-CV): builds against the deepstream-app/apps-common sources inside
  the image, detects DeepStream and CUDA versions, `-Werror` for RTVision sources only.
- Two-stage `docker/Dockerfile` (`ARG DS_IMAGE=nvcr.io/nvidia/deepstream:9.1-samples-multiarch`) and
  hand-written M1 configs in `config/dev/` using the in-container TrafficCamNet sample by path.
- M0 repository scaffolding: placeholder modules with SPDX headers for the full layout in
  `docs/IMPLEMENTATION_PLAN.md` (`src/app`, `src/parsers`, `tools/rtvision_tools`,
  `tools/export`, `docker`, `deploy/compose`, `examples`, `tests`).
- `CONTRIBUTING.md` (DCO sign-off), `SECURITY.md`, `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1),
  `.editorconfig`, `.clang-format`, `.dockerignore`.
- CI (`.github/workflows/ci.yml`): SPDX header check (`tools/ci/check_spdx.py`), forbidden-file
  check (`tools/ci/check_forbidden.py`), ruff, clang-format, pytest.
- `pyproject.toml` for the `rtvision-tools` package (`rtvision-run` entry point).
- Specification package: architecture, implementation plan, configuration, parser contract, event schema v1, API, testing, licensing policy, roadmap.

### Removed
- `models/examples/yolo26s-people` (AGPL-3.0 weights; now a metadata-only test fixture) and the
  `tools/export/ultralytics` placeholder.

### Fixed
- `.dockerignore` weight/media patterns only matched the build-context root; now `**/`. The onnx
  package's bundled test models are no longer copied into the image.
- NVIDIA's original copyright line in RT-CV-derived files had been re-wrapped by clang-format;
  restored, `ReflowComments: false`, and the SPDX check now enforces the verbatim line.

### Changed
- Project renamed from the working name Perceptra to **RTVision** (binary, Python package, CLI
  `rtvision-run`, metrics `rtvision_*`, env `RTVISION_*`, image, paths, Kafka topics, event schema
  id `rtvision.detection.v1`); owner set to Abhinand A S. See `docs/DEVIATIONS.md` #32.
- Image entry point is `rtvision-run`; the hand-written M1 configs in `config/dev/` were removed.
- JSON Schemas carry their SPDX identifier in a top-level `"$comment"` (see `docs/DEVIATIONS.md` #1).
