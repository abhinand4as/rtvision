<!-- SPDX-License-Identifier: Apache-2.0 -->
# Perceptra — specification package

Perceptra is a real-time, multi-stream object-detection microservice. Cameras are added through a REST API; a configurable detector runs on the GPU with tracking; per-frame JSON events go to Kafka. It is built on NVIDIA DeepStream, with an architecture derived from NVIDIA's RT-CV microservice (Apache-2.0).

**This package is the specification.** Give it to Claude Code (or any developer) to build the service.

## How to use it with Claude Code

1. Create an empty GitHub repository (e.g. `perceptra`) and copy the contents of this package into it.
2. Replace `<PROJECT_OWNER>` in `NOTICE`, `CLAUDE.md` and `docs/LICENSING.md` with your name. If you rename the project, tell Claude Code the new name first; `CLAUDE.md` lists every place the name appears.
3. Commit: `git add . && git commit -s -m "docs: add Perceptra specification"`.
4. Start Claude Code in the repo and say:
   > Read CLAUDE.md and follow it. Implement milestone M0, then stop and summarise.
5. Continue one milestone at a time (M1 … M8). Milestones M1, M5, M6 and M7 need a machine with an NVIDIA GPU, Docker and the NVIDIA Container Toolkit.

## Package contents

| File | Purpose |
|---|---|
| `ARCHITECTURE.md` | system design, diagrams, decisions |
| `CLAUDE.md` | rules and context for the implementing agent |
| `docs/IMPLEMENTATION_PLAN.md` | target repo layout, milestones M0–M8, acceptance criteria |
| `docs/CONFIGURATION.md` | `perceptra.yaml` and `model.yaml` reference |
| `docs/PARSERS.md` | model output families and parser contract |
| `docs/EVENT_SCHEMA.md` | Kafka/JSON event format |
| `docs/API.md` | stream REST API and ops endpoints |
| `docs/TESTING.md` | unit, integration, soak tests |
| `docs/LICENSING.md` | licence policy and release checklist |
| `docs/ROADMAP.md` | segmentation and other future features, plus the seams to keep |
| `docs/DEVIATIONS.md` | log of differences found during implementation |
| `schemas/*.json` | JSON Schemas for events, service config and model config |
| `config/perceptra.example.yaml` | example service config |
| `models/examples/*` | example model descriptions (no weights) |
| `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, `.gitignore`, `CHANGELOG.md` | repository hygiene |

## Licence summary

- **Source code:** Apache-2.0 (see `LICENSE`, `NOTICE`).
- **Container images and binaries:** also include NVIDIA DeepStream, TensorRT and CUDA, under NVIDIA's terms. Build images locally; do not publish them without reviewing those terms.
- **Model weights:** licensed separately by their owners. Ultralytics YOLO models (incl. YOLO26) are AGPL-3.0 unless you hold an Ultralytics Enterprise licence. See `docs/LICENSING.md`.
