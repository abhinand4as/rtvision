<!-- SPDX-License-Identifier: Apache-2.0 -->
# Perceptra

Perceptra is a real-time, multi-stream object-detection microservice. You add and remove cameras
(RTSP or files) at runtime through a REST API. A configurable detector runs on the GPU, objects are
tracked across frames, and one JSON event per sampled frame is published to Kafka (or stdout/file).

It is built on NVIDIA DeepStream 9.1, with an architecture derived from NVIDIA's RT-CV
microservice (Apache-2.0).

- **Any ONNX detector** described by a small `model.yaml`: YOLO-style heads, DETR-style heads
  (RT-DETR, RF-DETR, D-FINE), TensorRT EfficientNMS outputs. The default is **RF-DETR Small
  (Apache-2.0)**.
- **Streams at runtime** over REST (`POST /api/v1/stream/add`), batched on one GPU.
- **Tracking** (NvDCF, NvSORT, IOU) with stable track ids, optional ReID embeddings.
- **Documented event schema** `perceptra.detection.v1`, JSON Schema in `schemas/`.
- **Operations:** `/live`, `/ready`, `/startup`, Prometheus `/metrics`, model hot swap without
  dropping streams, graceful shutdown.

## Requirements

| | |
|---|---|
| GPU | NVIDIA dGPU on x86_64 (Turing or newer) |
| Driver | **595 or newer** (DeepStream 9.1 needs CUDA 13.2) |
| Software | Docker with Compose v2, NVIDIA Container Toolkit, `curl`, Python 3.10+ with `venv` (`sudo apt install python3-venv` on Ubuntu) |
| Disk | about 35 GB (DeepStream base image ~28 GB, Kafka, model export tooling ~2 GB) |

Check the GPU setup with `docker run --rm --gpus all ubuntu nvidia-smi`.

## Quick start

All commands run from the repository root.

```bash
# 1. Export the default model (RF-DETR Small, Apache-2.0) to ONNX. CPU only, a few minutes;
#    creates ~/.venvs/rfdetr-export and downloads the checkpoint (~400 MB).
tools/export/rfdetr/export.sh

# 2. Build the image and start Kafka + Perceptra. The first build pulls the DeepStream base image
#    (~28 GB) and takes a while.
docker compose -f deploy/compose/compose.yaml up -d --build

# 3. Wait until the service is ready (the first start builds the TensorRT engine, ~1 minute;
#    later starts reuse it).
until curl -sf localhost:9464/ready; do sleep 3; done; echo

# 4. Add a camera. This uses the sample video inside the container; for a real camera put its
#    rtsp:// URL in camera_url.
curl -s -XPOST localhost:9000/api/v1/stream/add -d '{
  "key": "sensor",
  "value": {"camera_id": "cam-1", "camera_name": "Sample", "change": "camera_add",
            "camera_url": "file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4"},
  "headers": {"source": "quickstart"}}'; echo

# 5. Watch the events (Ctrl-C to stop).
docker compose -f deploy/compose/compose.yaml exec -T kafka \
  /opt/kafka/bin/kafka-console-consumer.sh --bootstrap-server kafka:9092 \
  --topic perceptra.rfdetr-s-coco | python3 examples/consumer/print_events.py

# 6. Remove the camera and stop.
curl -s -XPOST localhost:9000/api/v1/stream/remove -d '{
  "key": "sensor",
  "value": {"camera_id": "cam-1", "change": "camera_remove",
            "camera_url": "file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4"}}'; echo
docker compose -f deploy/compose/compose.yaml down
```

Step 5 prints one line per event, e.g.
`2026-11-20T14:10:41.487Z cam-1 frame=30 objects=11 car=6 person=3 backpack=1 bus=1`.
For a per-camera people count, run the people model (`PERCEPTRA_MODEL=rfdetr-s-people` before
step 2) and pipe topic `people-raw` into `python3 examples/consumer/occupancy.py`.

The sample video is about 50 seconds long and does not loop. At its end DeepStream removes the
stream by itself, so the remove in step 6 then answers `STREAM_REMOVE_FAIL, No record found`,
which is harmless. RTSP cameras stay until you remove them.

## Configuration

| What | Where |
|---|---|
| Model selection | `PERCEPTRA_MODEL` (a directory under `models/examples/`, mounted at `/opt/perceptra/models`) |
| Compose knobs (image, bind address, GPU, models dir) | `deploy/compose/.env.example` → copy to `.env` |
| Service settings (`perceptra.yaml`) | `config/perceptra.example.yaml`; override any value with `PERCEPTRA__SECTION__KEY` env vars |
| Full reference | [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) |

The REST API (9000) and ops server (9464) do not authenticate. Compose publishes them on
`127.0.0.1` only; set `PERCEPTRA_BIND=0.0.0.0` only on a trusted network.

## Documentation

| Document | Content |
|---|---|
| [`docs/MODELS.md`](docs/MODELS.md) | **Bring your own model**: export, `model.yaml`, verification |
| [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) | Common problems, exit codes, log lines |
| [`docs/API.md`](docs/API.md) | Stream REST API and ops endpoints |
| [`docs/EVENT_SCHEMA.md`](docs/EVENT_SCHEMA.md) | Kafka/JSON event format |
| [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) | `perceptra.yaml` and `model.yaml` reference |
| [`docs/PARSERS.md`](docs/PARSERS.md) | Supported model output families |
| [`docs/TESTING.md`](docs/TESTING.md) | Unit and integration tests |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Design, diagrams, decisions |
| [`docs/LICENSING.md`](docs/LICENSING.md) | Licence policy and release checklist |
| [`docs/DEVIATIONS.md`](docs/DEVIATIONS.md) | Where the implementation differs from the original specification |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Segmentation, pose and other planned features |

## Development

```bash
python3 -m venv .venv && .venv/bin/pip install -e '.[dev]'
.venv/bin/pytest -q                      # Python tools, examples, golden configs (no GPU)
make -C src/parsers test-core            # parser core (no GPU, no DeepStream)
make -C src/app test                     # serializer, publisher, timestamps, metrics (GLib only)
tests/integration/run_all.sh             # full stack on a GPU host (docs/TESTING.md)
```

Contributions: see [`CONTRIBUTING.md`](CONTRIBUTING.md) (DCO sign-off, Conventional Commits). The
project was implemented milestone by milestone from the specification in this repository;
`CLAUDE.md` holds the rules the implementing agent followed.

## Licence summary

- **Source code:** Apache-2.0 (see `LICENSE`, `NOTICE`). Parts of `src/app/` are derived from
  NVIDIA RT-CV (Apache-2.0); `NOTICE` lists them.
- **Container images and binaries:** also include NVIDIA DeepStream, TensorRT and CUDA, under
  NVIDIA's terms. Build images locally; do not publish them without reviewing those terms.
- **Model weights:** licensed separately by their owners and never committed. The default model,
  RF-DETR Small (`models/examples/rfdetr-s-coco`), is Apache-2.0; you export it locally with
  `tools/export/rfdetr/export.sh`. Ultralytics YOLO models (incl. YOLO26) are AGPL-3.0 unless you
  hold an Ultralytics Enterprise licence. `perceptra-run` refuses non-permissive model licences
  unless you accept them explicitly. See `docs/LICENSING.md`.
- **Video codecs:** H.264/H.265 are decoded by the GPU's hardware decoder through DeepStream; no
  software codec packages are added. Products that decode these formats may need patent licences
  (e.g. from the relevant patent pools); check this for your use.
- Third-party components and their licences: `THIRD_PARTY_NOTICES.md` (generated from the built
  image by `tools/ci/third_party_report.py`).
