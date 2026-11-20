<!-- SPDX-License-Identifier: Apache-2.0 -->
# Changelog

All notable changes are documented here (format: Keep a Changelog; versioning: SemVer).

## [Unreleased]
### Added
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
