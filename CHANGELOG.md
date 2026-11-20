<!-- SPDX-License-Identifier: Apache-2.0 -->
# Changelog

All notable changes are documented here (format: Keep a Changelog; versioning: SemVer).

## [Unreleased]
### Added
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
- JSON Schemas carry their SPDX identifier in a top-level `"$comment"` (see `docs/DEVIATIONS.md` #1).
