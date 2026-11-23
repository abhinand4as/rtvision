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
2. Add `Modifications Copyright (c) <YEAR> Abhinand A S` and a short "Changes:" note (§4(b)).
3. List the file in `NOTICE` under "Portions derived from NVIDIA RT-CV".
4. Do not use NVIDIA trademarks in the project name, logo or package names. Descriptive use is fine ("built on NVIDIA DeepStream").

## 3. What must never be in the repository

| Item | Why |
|---|---|
| DeepStream SDK binaries, headers, sample sources (`deepstream-app`, `apps-common`) | NVIDIA EULA / their own licence terms. They are **compiled from inside the base image at build time**, not vendored. |
| DeepStream sample models, configs, streams (TrafficCamNet, tracker YAMLs, sample videos) | NVIDIA AI Product License / EULA. Reference them **by path inside the container** only. |
| Model weights of any kind (`.pt`, `.onnx`, `.engine`, `.etlt`) | Each has its own licence; users supply their own. |
| Test and demo videos | Tests use the in-container DeepStream samples at runtime. The demo downloads a CC-BY-4.0 video (pinned commit, checksum) at run time; it is never committed. |
| Screenshots of DeepStream samples or of videos without a clear licence | Pictures show their content. `docs/images/` holds only screenshots of openly licensed videos, with attribution in `docs/images/README.md`. |
| Code copied from GPL/AGPL projects (e.g. Ultralytics) | Would force a copyleft licence on the repo. |

## 4. Build outputs and container images

- The built `rtvision` binary **links object code compiled from DeepStream's sample sources** and runs on proprietary DeepStream, TensorRT and CUDA libraries. The **source repository** is clean Apache-2.0; **binaries and images** also carry NVIDIA's terms.
- Base image: `nvcr.io/nvidia/deepstream:*`, governed by the DeepStream EULA and NGC container terms. By pulling it, users accept those terms.
- **Default policy:** do not publish RTVision images to public registries. Users build locally (`docker build`). Publishing images requires a reviewed decision recorded in this file.
- Codecs: H.264/H.265 use hardware NVDEC/NVENC. Do not add software codec packages (`ffmpeg` with x264/x265, `libav*` with patented codecs) unless reviewed, and document codec patent considerations in the README.

## 5. Models

- Every `model.yaml` **must** declare `license.spdx` for the weights. `rtvision-run` blocks non-permissive licences unless explicitly accepted (`RTVISION_ACCEPT_MODEL_LICENSE`).
- **Ultralytics YOLO (incl. YOLO26):** AGPL-3.0. Models trained with Ultralytics are also AGPL-3.0 per Ultralytics. Closed-source or commercial use needs an Ultralytics Enterprise licence. Consequences for this repo:
  - The service never imports or depends on `ultralytics`.
  - The repository ships **no** Ultralytics model example and no Ultralytics export tool (decision of the project owner, 2026-11-20). A test fixture (`tests/python/fixtures/models/yolo26s-people`, metadata only) declares `AGPL-3.0-only` to test the licence gate.
  - The `yolo_e2e` / `yolo_raw` parsers are our own implementations of tensor layouts, not copies of Ultralytics or DeepStream-Yolo code; users may bring such models under their own licence acceptance.
- **Default models: RF-DETR** (Roboflow), Small, **Apache-2.0** code and weights (verified 2026-11-20): `models/examples/rfdetr-s-coco` (compose and integration-test default) and `rfdetr-s-people`. Only Nano/Small/Medium/Large are Apache-2.0; XLarge/2XLarge are PML-1.0 and refused by `tools/export/rfdetr`. The exporter runs in its own host virtualenv (rfdetr, PyTorch CPU, onnx: Apache-2.0/BSD/MIT; certifi and tqdm MPL-2.0) and is never part of the image. The ONNX is never committed (`.gitignore`) nor copied into the image (`.dockerignore`).
- **Other permissive options:** RT-DETR, D-FINE, DEIM (Apache-2.0; export without the post-processor for the `detr` family), YOLOX (Apache-2.0; needs an objectness-aware parser). Verify each before use.
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

**Allow-list decision (2026-11-20, project owner):** **MPL-2.0** is approved for the host-only model export tooling (`tools/export/rfdetr`: `certifi`, `tqdm`), which is never part of the image. Also: the permissive licences **PSF-2.0** (typing_extensions), **0BSD** and **Zlib** (bundled in numpy), which reach the image only as transitive dependencies of the allow-listed `onnx` and `jsonschema`, are approved. The exact packages and versions are in `THIRD_PARTY_NOTICES.md`.

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

Status for `v0.1.0` (checked 2026-11-20): all items complete; the owner decisions were taken on 2026-11-20.

- [x] `tools/ci/check_spdx.py` passes; forbidden-file check passes. (128 files; 0 violations; also enforced in CI.)
- [x] `NOTICE` lists every RT-CV-derived file; each has a modification notice. (6 files: `main.c`, `perf.c`, `Makefile`, `timestamps.c`, `embeddings.c`, `ota.c`; the verbatim NVIDIA line is enforced by `check_spdx.py`.)
- [x] `THIRD_PARTY_NOTICES.md` regenerated from the built image (`tools/ci/third_party_report.py --image rtvision:local --check --write`: 14 Python packages, 0 apt packages added on top of the base image, 69 linked libraries, nothing needing review).
- [x] No NVIDIA/Ultralytics trademarks in name or logo. (No logo; NVIDIA/Ultralytics names appear only descriptively and in the `NOTICE` trademark statement.)
- [x] Example `model.yaml` files declare correct licences: `rfdetr-s-coco`, `rfdetr-s-people`, `rtdetr-r18-coco` Apache-2.0; `trafficcamnet-sample` `LicenseRef-NVIDIA-AI-Product-License` (gated). No AGPL example ships (the YOLO26 file is a test fixture).
- [x] README states: the source is Apache-2.0; images and binaries include NVIDIA components under NVIDIA terms; model weights are licensed separately; H.264/H.265 patent note (§4).
- [x] Decision on publishing images recorded: **no images are published** for `v0.1.0`; users build locally (§4 default policy). Changing this needs a recorded owner decision.
- [x] Project name checked: the working name `perceptra` was dropped (PyPI `perceptra` is taken by an unrelated computer-vision MLOps SDK, and a company "Perceptra.ai" lists computer-vision products). The owner chose **RTVision** (2026-11-20): `rtvision` and `rtvision-tools` are free on PyPI; on GitHub an unrelated organisation "RTVision" publishes JavaScript tooling. No trademark search was done (USPTO/EUIPO); do one before a commercial release.
- [x] Copyright owner set: **Abhinand A S** (2026-11-20) in `NOTICE`, every copyright line (97 files), `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`.
- [x] Export tooling licences approved by the project owner (2026-11-20): `tools/export/rfdetr` installs, in its own host virtualenv (never in the image), Apache-2.0/BSD/MIT packages plus **MPL-2.0** (`certifi`, `tqdm`; file-level copyleft, used unmodified). MPL-2.0 is allowed for this host-only tooling (§6).
