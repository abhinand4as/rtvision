<!-- SPDX-License-Identifier: Apache-2.0 -->
# Instructions for the implementing agent (Claude Code)

You are building **Perceptra**, a real-time multi-stream object-detection microservice, from the specification in this repository. Read these files **in this order** before writing code:

1. `ARCHITECTURE.md` — what to build and why.
2. `docs/IMPLEMENTATION_PLAN.md` — milestone order, file list, acceptance criteria.
3. `docs/LICENSING.md` — **mandatory rules**; violations block a release.
4. `docs/CONFIGURATION.md`, `docs/PARSERS.md`, `docs/EVENT_SCHEMA.md`, `docs/API.md`, `docs/TESTING.md` — contracts.
5. `docs/ROADMAP.md` — extension points you must leave in place (do not implement them in v1).

## Ground rules

1. **Work milestone by milestone** (M0 → M8). Do not start a milestone until the previous one meets its acceptance criteria. After each milestone, update `CHANGELOG.md` and tick the checklist in `docs/IMPLEMENTATION_PLAN.md`.
2. **Never commit** model weights (`*.pt`, `*.onnx`, `*.engine`, `*.plan`, `*.etlt`), videos, DeepStream binaries/headers/sources, or anything from NGC. `.gitignore` already covers these; keep it that way.
3. **SPDX header on every source file you create:** `SPDX-License-Identifier: Apache-2.0` plus `Copyright (c) <YEAR> <PROJECT_OWNER>`. For files **derived from RT-CV**, keep NVIDIA's original copyright and SPDX lines, add `Modifications Copyright (c) <YEAR> <PROJECT_OWNER>`, and add a one-line summary of the changes at the top (Apache-2.0 §4(b)). Record every derived file in `NOTICE`.
4. **Do not invent DeepStream APIs.** Check the headers in the container (`/opt/nvidia/deepstream/deepstream/sources/includes/` and `/opt/nvidia/deepstream/deepstream/sources/apps/sample_apps/deepstream-app/`) before calling anything. If something in this spec contradicts the headers, the headers win: document the deviation in `docs/DEVIATIONS.md`.
5. **Probe code must never block** the streaming thread: no network I/O, no unbounded allocation, no `cudaDeviceSynchronize`, no logging per object at `info` level.
6. **Ask the user before:** changing the repository licence; adding any dependency that is GPL/AGPL, or one whose licence is not listed in `docs/LICENSING.md`; adding a default model; publishing images to any registry.
7. Prefer **small, reviewable commits**, one logical change each, with Conventional Commit messages (`feat:`, `fix:`, `docs:`, `test:`, `build:`, `chore:`).
8. Keep **`<PROJECT_OWNER>` and the project name** as replaceable tokens until the user provides them. The default project name is `perceptra`, used in: the binary name, the Python package `perceptra_tools`, the metrics prefix `perceptra_`, the env prefix `PERCEPTRA_`, the image name and the schema id `perceptra.detection.v1`.

## Sourcing RT-CV code (derived files)

Derive from a **pinned commit** so provenance is exact:

```bash
git clone --depth 1 --filter=blob:none --sparse \
  https://github.com/NVIDIA-AI-Blueprints/video-search-and-summarization.git /tmp/vss
cd /tmp/vss && git fetch --depth 1 origin cf2160322a67580d750bfd1a0ad60faf1234541b \
  && git checkout cf2160322a67580d750bfd1a0ad60faf1234541b \
  && git sparse-checkout set services/rtvi/rt-cv
```

Files to port from (read, then port only what the plan asks for — do not copy the whole file):
- `services/rtvi/rt-cv/src/metropolis_perception_app.c` → `src/app/main.c`, `src/app/probe.c`, `src/app/embeddings.c`, `src/app/ota.c`, `src/app/perf.c`
- `services/rtvi/rt-cv/src/perception_utc.c` → `src/app/timestamps.c`
- `services/rtvi/rt-cv/src/Makefile` → `src/app/Makefile`

**Known traps in the RT-CV code** (it targets NVIDIA's internal `rtvi_ds9.1.1` base image, not public DeepStream):
- `msgapi_init_early()`, `msgapi_report_error_and_cleanup()`, `g_nvds_last_error_message`, `appCtx->last_error`: **not in public DeepStream. Remove them.**
- `get_sensor_info(appCtx, source_id)` and `NvDsSensorInfo`: verify they exist in the image's `deepstream_app.h`. If not, map `source_id → camera_id/name` from the `nvmultiurisrcbin` stream-add events (document it in `DEVIATIONS.md`).
- `NVDS_CUSTOM_MSG_SPARSE4D`, `NvDsBbox3dObjectList`, FSL dual-head code, `model_used` enum, vehicle/person dummy metadata: **drop**.
- Makefile `-DDS_VERSION_MAJOR=5 -DDS_VERSION_MINOR=0` and `CUDA_VER`: detect the CUDA version in the image (`ls -d /usr/local/cuda-*`) instead of hard-coding it.
- RT-CV's `-lcrypto`, `-lnvds_batch_jpegenc`, `frame_dump.cpp`: drop unless needed.

## Environment expectations

- **Full build and run** need Docker, an NVIDIA GPU, the NVIDIA Container Toolkit, and access to `nvcr.io` (public DeepStream images may need `docker login nvcr.io` with an NGC API key).
- **Without a GPU** you can still do and verify: M0, the parser core + unit tests (M2), the Python tools + tests (M3), the serializer/publisher unit tests (M4), docs and CI. Mark GPU-only acceptance items as `PENDING-GPU` in the plan checklist and tell the user exactly which commands to run.

## Definition of done (whole project)

- `make -C src/parsers test` and `pytest -q` pass in CI (no GPU).
- `docker compose -f deploy/compose/compose.yaml up` plus `tests/integration/run_all.sh` passes on a GPU host: health → add stream → ≥ 1 valid event on Kafka (validated against the JSON Schema) → remove stream → graceful shutdown.
- `docs/LICENSING.md` checklist complete; `NOTICE` and `THIRD_PARTY_NOTICES.md` up to date.
- README quick start works verbatim on a clean GPU host.
