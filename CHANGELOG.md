<!-- SPDX-License-Identifier: Apache-2.0 -->
# Changelog

All notable changes are documented here (format: Keep a Changelog; versioning: SemVer).

## [Unreleased]
### Added
- M4 events and publisher: `PerceptraFrameEvent`/`PerceptraObject` model, task registry (`detect`;
  `segment`/`pose` stubs), detect handler (bbox rescaled to source resolution, nullable track id and
  confidence), JSON serializer for `perceptra.detection.v1`, bounded publisher queue with
  `drop_oldest`/`drop_newest`, worker thread and bounded shutdown flush, backends `stdout`/`file`
  (JSON Lines) and `kafka` (librdkafka, key = sensor id, `schema`/`content-type` headers, property
  passthrough, delivery reports, health check); `make -C src/app test`; golden event
  `tests/golden/event_basic.json`; `tests/integration/consume_validate.py --stdin`; CI job `app-unit`.
- M3 configuration system: `perceptra-run` (CLI > env > default file selection, `PERCEPTRA__…` env
  overrides, JSON Schema validation with field paths, task check, model licence gate with exit 3,
  ONNX output check, config generation, engine cache, `execv` of the binary); `gen_configs.py` with
  Jinja templates for `ds-main.ini`, `pgie.ini`, `parser.ini`, `app.ini`, `kafka.properties`;
  `onnx_inspect` CLI; golden configs for the 3 example models; COCO `labels.txt` for the YOLO26 and
  RT-DETR examples; `tests/integration/make_synthetic_onnx.py` (synthetic `yolo_e2e` test model).
- M2 parser library `libperceptra_parsers.so`: pure C++17 core (`src/parsers/core/`) with the
  `yolo_e2e`, `yolo_raw` (both layouts), `detr` (4 box formats; sigmoid/softmax/none; background
  column) and `efficient_nms` decoders, common post-steps and a strict `parser.ini` reader; DeepStream
  adapter exporting `NvDsInferParsePerceptra_{YoloE2E,YoloRaw,Detr,EfficientNms}`; own header-only test
  harness with 15 core and 5 adapter tests (ASan + UBSan); CI job `parser-core`.
- M1 pipeline skeleton on DeepStream 9.1: `perceptra` binary (`src/app/main.c`, derived from RT-CV
  `main()`) with `-c/-a/--log-level/--no-force-tcp/--shutdown-timeout`, SIGINT/SIGTERM handling,
  force-TCP for REST-added RTSP sources and `buffer-pool-size=16`; per-stream frame/object counters
  (`probe.c`); FPS logging (`perf.c`, derived from RT-CV `perf_cb`); levelled `key=value` logging
  (`log.c`); `app.ini` reader (`app_config.c`).
- `src/app/Makefile` (derived from RT-CV): builds against the deepstream-app/apps-common sources inside
  the image, detects DeepStream and CUDA versions, `-Werror` for Perceptra sources only.
- Two-stage `docker/Dockerfile` (`ARG DS_IMAGE=nvcr.io/nvidia/deepstream:9.1-samples-multiarch`) and
  hand-written M1 configs in `config/dev/` using the in-container TrafficCamNet sample by path.
- M0 repository scaffolding: placeholder modules with SPDX headers for the full layout in
  `docs/IMPLEMENTATION_PLAN.md` (`src/app`, `src/parsers`, `tools/perceptra_tools`,
  `tools/export`, `docker`, `deploy/compose`, `examples`, `tests`).
- `CONTRIBUTING.md` (DCO sign-off), `SECURITY.md`, `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1),
  `.editorconfig`, `.clang-format`, `.dockerignore`.
- CI (`.github/workflows/ci.yml`): SPDX header check (`tools/ci/check_spdx.py`), forbidden-file
  check (`tools/ci/check_forbidden.py`), ruff, clang-format, pytest.
- `pyproject.toml` for the `perceptra-tools` package (`perceptra-run` entry point).
- Specification package: architecture, implementation plan, configuration, parser contract, event schema v1, API, testing, licensing policy, roadmap.

### Changed
- Image entry point is `perceptra-run`; the hand-written M1 configs in `config/dev/` were removed.
- JSON Schemas carry their SPDX identifier in a top-level `"$comment"` (see `docs/DEVIATIONS.md` #1).
