<!-- SPDX-License-Identifier: Apache-2.0 -->
# Licensing policy

> This document records engineering decisions about licence compliance. It is **not legal advice**; have a qualified person review it before a commercial release.

## 1. Repository licence: Apache-2.0

The whole repository is licensed under **Apache-2.0** (`LICENSE`). Reasons:
- Parts of `src/app/` are **derived from NVIDIA RT-CV**, which is Apache-2.0 (root `LICENSE` and `SPDX-License-Identifier: Apache-2.0` headers in `services/rtvi/rt-cv/src/*` of `NVIDIA-AI-Blueprints/video-search-and-summarization`, commit `cf2160322a67580d750bfd1a0ad60faf1234541b`). Using the same licence avoids mixed-licence confusion.
- Apache-2.0 allows commercial use and includes an explicit patent grant.

## 2. Obligations for derived files (Apache-2.0 §4)

For every file ported from RT-CV:
1. Keep the original header lines, e.g. `SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.` and `SPDX-License-Identifier: Apache-2.0`.
2. Add `Modifications Copyright (c) <YEAR> <PROJECT_OWNER>` and a short "Changes:" note (§4(b)).
3. List the file in `NOTICE` under "Portions derived from NVIDIA RT-CV".
4. Do not use NVIDIA trademarks in the project name, logo or package names. Descriptive use is fine ("built on NVIDIA DeepStream").

## 3. What must never be in the repository

| Item | Why |
|---|---|
| DeepStream SDK binaries, headers, sample sources (`deepstream-app`, `apps-common`) | NVIDIA EULA / their own licence terms. They are **compiled from inside the base image at build time**, not vendored. |
| DeepStream sample models, configs, streams (TrafficCamNet, tracker YAMLs, sample videos) | NVIDIA AI Product License / EULA. Reference them **by path inside the container** only. |
| Model weights of any kind (`.pt`, `.onnx`, `.engine`, `.etlt`) | Each has its own licence; users supply their own. |
| Test videos | Unknown rights. Tests use the in-container DeepStream samples at runtime. |
| Code copied from GPL/AGPL projects (e.g. Ultralytics) | Would force a copyleft licence on the repo. |

## 4. Build outputs and container images

- The built `perceptra` binary **links object code compiled from DeepStream's sample sources** and runs on proprietary DeepStream, TensorRT and CUDA libraries. The **source repository** is clean Apache-2.0; **binaries and images** also carry NVIDIA's terms.
- Base image: `nvcr.io/nvidia/deepstream:*`, governed by the DeepStream EULA and NGC container terms. By pulling it, users accept those terms.
- **Default policy:** do not publish Perceptra images to public registries. Users build locally (`docker build`). Publishing images requires a reviewed decision recorded in this file.
- Codecs: H.264/H.265 use hardware NVDEC/NVENC. Do not add software codec packages (`ffmpeg` with x264/x265, `libav*` with patented codecs) unless reviewed, and document codec patent considerations in the README.

## 5. Models

- Every `model.yaml` **must** declare `license.spdx` for the weights. `perceptra-run` blocks non-permissive licences unless explicitly accepted (`PERCEPTRA_ACCEPT_MODEL_LICENSE`).
- **Ultralytics YOLO (incl. YOLO26):** AGPL-3.0. Models trained with Ultralytics are also AGPL-3.0 per Ultralytics. Closed-source or commercial use needs an Ultralytics Enterprise licence. Consequences for this repo:
  - The service never imports or depends on `ultralytics`.
  - `tools/export/ultralytics/` is an **optional, separately installed** helper with its own README stating the licence. It contains only our own small script that calls the user's installed `ultralytics` package; no Ultralytics code is copied.
  - The `yolo_e2e` / `yolo_raw` parsers are our own implementations of tensor layouts, not copies of Ultralytics or DeepStream-Yolo code.
  - `models/examples/yolo26s-people/model.yaml` declares `AGPL-3.0-only`.
- **Recommended permissive defaults** for commercial users: RT-DETR (Apache-2.0), RF-DETR (Apache-2.0 in most sizes, verify per size), D-FINE (Apache-2.0), YOLOX (Apache-2.0). Verify each before use.
- Training datasets can carry their own restrictions (e.g. research-only). Check them for any weights you ship to customers.

## 6. Third-party runtime dependencies (allow-list)

| Component | Licence | Use | Linking |
|---|---|---|---|
| GStreamer, GLib, GObject | LGPL-2.1-or-later | pipeline, main loop | dynamic (from base image) |
| json-glib | LGPL-2.1-or-later | JSON serializer | dynamic |
| libsoup 3 | LGPL-2.0-or-later | ops HTTP server | dynamic |
| librdkafka | BSD-2-Clause | Kafka producer | dynamic |
| NVIDIA DeepStream, TensorRT, CUDA | NVIDIA proprietary (EULA) | runtime | dynamic (from base image) |
| PyYAML | MIT | tools | pip |
| Jinja2 | BSD-3-Clause | tools | pip |
| jsonschema | MIT | tools | pip |
| onnx | Apache-2.0 | tools (shape inspection) | pip |
| pytest, ruff | MIT | dev only | pip |
| Transitive Python deps of the above (MarkupSafe, attrs, referencing, rpds-py, jsonschema-specifications, numpy, protobuf, ml_dtypes, typing_extensions) | BSD-3-Clause, MIT, Apache-2.0, **PSF-2.0**, **0BSD**, **Zlib**, CC0-1.0 | tools (pulled in by Jinja2, jsonschema, onnx) | pip |

**Allow-list decision (2026-11-20, project owner):** the permissive licences **PSF-2.0** (typing_extensions), **0BSD** and **Zlib** (bundled in numpy), which reach the image only as transitive dependencies of the allow-listed `onnx` and `jsonschema`, are approved. The exact packages and versions are in `THIRD_PARTY_NOTICES.md`.

Adding anything not on this list, especially GPL/AGPL/SSPL/BUSL/"non-commercial", requires asking the project owner first. LGPL libraries must stay dynamically linked and unmodified (or modifications published).

Verify every licence in this table against the installed package metadata before release (`tools/ci/third_party_report.py`); the table is the intent, the report is the evidence.

## 7. Files

| File | Content |
|---|---|
| `LICENSE` | Apache-2.0 full text |
| `NOTICE` | project copyright; "Portions derived from NVIDIA RT-CV (Apache-2.0)" with the file list; "Built to run on NVIDIA DeepStream (separate licence)" |
| `THIRD_PARTY_NOTICES.md` | generated report: package, version, licence, URL |
| SPDX headers | every source/config/script file |
| `CONTRIBUTING.md` | DCO sign-off (`git commit -s`); contributions licensed under Apache-2.0 |

## 8. Release checklist
- [ ] `tools/ci/check_spdx.py` passes; forbidden-file check passes.
- [ ] `NOTICE` lists every RT-CV-derived file; each has a modification notice.
- [ ] `THIRD_PARTY_NOTICES.md` regenerated from the built image.
- [ ] No NVIDIA/Ultralytics trademarks in name or logo.
- [ ] Example `model.yaml` files declare correct licences.
- [ ] README states: the source is Apache-2.0; images and binaries include NVIDIA components under NVIDIA terms; model weights are licensed separately.
- [ ] Decision on publishing images recorded (default: no).
- [ ] Project name checked for conflicts (GitHub, PyPI, trademarks).
