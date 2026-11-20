<!-- SPDX-License-Identifier: Apache-2.0 -->
# Configuration

Perceptra is configured by two YAML files, both validated by JSON Schema at startup:

| File | Scope | Schema |
|---|---|---|
| `perceptra.yaml` | service: streams, publishing, tracker, outputs, observability | `schemas/service_config.schema.json` |
| `model.yaml` | one detector: weights, I/O format, classes, thresholds, licence | `schemas/model.schema.json` |

`perceptra-run` combines them and generates the DeepStream configs; users never edit DeepStream INI files directly.

Selecting files (precedence: CLI flag > env var > default):

| Purpose | CLI | Env | Default |
|---|---|---|---|
| service config | `--config` | `PERCEPTRA_CONFIG` | `/opt/perceptra/config/perceptra.yaml` |
| model | `--model` | `PERCEPTRA_MODEL` (a name under the models dir, or a path) | — (required) |
| models dir | `--models-dir` | `PERCEPTRA_MODELS_DIR` | `/opt/perceptra/models` |
| engine cache | `--engine-dir` | `PERCEPTRA_ENGINE_DIR` | `/opt/perceptra/engines` |

Any scalar in `perceptra.yaml` can be overridden by an env var with a `PERCEPTRA__` prefix and `__` as the path separator, e.g. `PERCEPTRA__PUBLISH__KAFKA__BROKERS=kafka:9092`.

---

## `perceptra.yaml`

```yaml
service:
  name: perceptra                 # appears in logs and metrics labels
  gpu_id: 0
  max_streams: 4                  # batch size everywhere (streammux, nvinfer, source-list)
  http_ip: 0.0.0.0                # stream REST server bind address
  rest_port: 9000
  streammux:
    width: 1920
    height: 1080
    batched_push_timeout_us: 33000
    live_source: true
  sources:
    latency_ms: 200
    force_tcp: true
    rtsp_reconnect_interval_s: 10
    rtsp_reconnect_attempts: -1   # -1 = forever
    drop_on_latency: true

inference:
  interval: 0                     # frames to skip between detector runs (tracker fills gaps)
  precision: null                 # override model.yaml precision (fp32|fp16|int8); null = use model's
  hot_swap_file: null             # INI watched for live model swaps (see "Model hot swap"); null = off

tracker:
  profile: nvdcf_perf             # nvdcf_perf | nvdcf_accuracy | nvsort | iou | none
  width: 960
  height: 544
  reid: false                     # true: nvdcf_accuracy profile with ReID tensor output (needs
                                  # NVIDIA's ReID model mounted, see README "Tracking and ReID")
  reid_store_age: 0               # frames a track's last ReID feature is reused when the tracker
                                  # did not compute one on a sampled frame (0 = only fresh features)

timestamps:
  source: ntp                     # ntp | uri_utc | system

publish:
  backend: kafka                  # kafka | stdout | file
  interval_frames: 30             # emit for frame_num % interval_frames == 0 (per stream)
  include_empty_frames: false     # emit events with objects: [] (heartbeat)
  include_embeddings: false
  queue:
    max_events: 10000
    on_full: drop_oldest          # drop_oldest | drop_newest
    shutdown_flush_timeout_ms: 5000
  kafka:
    brokers: kafka:9092
    topic: null                   # null = model.messaging.topic, else "perceptra.<model.name>"
    key: sensor_id                # sensor_id | none
    properties:                   # passthrough to librdkafka (e.g. security.protocol, sasl.*)
      linger.ms: "20"
      compression.type: lz4
  file:
    path: /opt/perceptra/output/events.jsonl

outputs:                          # optional visual outputs (debug)
  display: false                  # EGL tiled display + OSD (needs X)
  file: null                      # e.g. /opt/perceptra/output/annotated.mp4
  rtsp: null                      # e.g. { port: 8554, udp_port: 5400 }

observability:
  ops_port: 9464
  perf_interval_s: 5
  log_level: info                 # error | warn | info | debug
```

---

## `model.yaml`

```yaml
schema_version: 1
name: yolo26s-people              # [a-z0-9-]+, used in engine name, metrics, default topic
version: "2026.1"
task: detect                      # detect (v1) | segment | pose (reserved)
license:
  spdx: AGPL-3.0-only             # SPDX id of the WEIGHTS
  source: https://docs.ultralytics.com/models/yolo26/
  notes: "Commercial closed-source use requires an Ultralytics Enterprise licence."

files:
  onnx: yolo26s.onnx              # relative to this model's directory
  labels: labels.txt              # one label per line, line index = class id
  engine: null                    # optional prebuilt engine (else built + cached)
  int8_calibration: null          # calibration cache for int8

input:
  width: 640
  height: 640
  channels: 3
  color: RGB                      # RGB | BGR | GRAY
  scale: 0.0039215697906911373    # pixel * scale
  mean: [0.0, 0.0, 0.0]           # subtracted before scale (nvinfer offsets)
  resize: letterbox_center        # letterbox_center | letterbox_topleft | stretch
  dynamic_batch: true

output:
  family: yolo_e2e                # yolo_e2e | yolo_raw | detr | efficient_nms
  layers: [output0]               # names in ONNX order expected by the family
  box_format: xyxy_px             # family default if omitted
  activation: none                # family default if omitted (detr: sigmoid)
  has_background_class: false     # detr/softmax models
  num_classes: 80

classes:
  keep: [person]                  # labels to keep; omit or [] = all
  thresholds:
    default: 0.35
    per_class: {}                 # e.g. { person: 0.4 }
  nms_iou: 0.45                   # only for yolo_raw
  topk: 300

precision: fp16                   # fp32 | fp16 | int8

messaging:
  topic: people-raw               # optional; default "perceptra.<name>"
```

### Escape hatch: existing nvinfer config (`family: builtin`)

For models that DeepStream already supports with its own parser (e.g. the in-container TrafficCamNet sample used by the integration tests), set `files.nvinfer_config` to an absolute path and `output.family: builtin`. The generator then references that nvinfer config unchanged: it only overrides `batch-size` and `gpu-id` via `[primary-gie]` in `ds-main.ini`, and writes no `parser.ini`. `input`/`output` become informational. See `models/examples/trafficcamnet-sample/model.yaml`.

### Generated artefacts (for reference; never hand-edited)

| Generated file | Derived from |
|---|---|
| `ds-main.ini` | service.* (sources, streammux, sinks), tracker.*, inference.interval |
| `pgie.ini` | model.input → `net-scale-factor`, `offsets`, `model-color-format`, `maintain-aspect-ratio`, `symmetric-padding`; model.output.family → `parse-bbox-func-name`, `cluster-mode`; classes → `filter-out-class-ids`, `[class-attrs-*] pre-cluster-threshold`, `nms-iou-threshold`, `topk`; precision → `network-mode` |
| `parser.ini` | model.output.*, model.classes.keep (ids), num_classes |
| `app.ini` | publish.*, timestamps.*, model identity (name, version, task, family), observability.* |

Timestamps mapping: `timestamps.source: ntp` sets `[streammux] attach-sys-ts-as-ntp=0` (NTP from RTCP sender reports); `uri_utc`/`system` keep the muxer default (1). Events record the source actually used: an `ntp` stream without sender reports yet (or a file source) and an unparsable `uri_utc` file name fall back to `system` per event.

Resize mapping: `letterbox_center` → `maintain-aspect-ratio=1, symmetric-padding=1` (Ultralytics-style); `letterbox_topleft` → `maintain-aspect-ratio=1, symmetric-padding=0`; `stretch` → `maintain-aspect-ratio=0`.

### Model hot swap

With `inference.hot_swap_file` set, `perceptra` watches that file (inotify on its directory, so editors, `mv` into place and Kubernetes ConfigMap updates all work). On change it applies the new model to the running nvinfer without stopping the pipeline:

```ini
[primary-gie]
model-engine-file=/opt/perceptra/engines/<model>-trt<ver>/<onnx>_b4_gpu0_fp16.engine
# or: config-file=/path/to/another/pgie.ini
```

The new engine must keep the task, output family, input size and batch size; anything else needs a restart. Results are logged (`comp=ota event=model_swapped result=ok|failed`) and counted in `perceptra_model_swaps_total{result}`.

### Licence gate

If `license.spdx` is not one of `Apache-2.0, MIT, BSD-2-Clause, BSD-3-Clause, CC0-1.0, CC-BY-4.0`, `perceptra-run` prints a warning box and exits with code 3 unless `PERCEPTRA_ACCEPT_MODEL_LICENSE` equals that SPDX id. The accepted id is logged and exported as the metric label `license`.

---

## `app.ini` (generated, read by the C binary via GKeyFile)

```ini
[model]
name=yolo26s-people
version=2026.1
task=detect
family=yolo_e2e
license=AGPL-3.0-only
precision=fp16

[publish]
backend=kafka
interval_frames=30
include_empty_frames=false
include_embeddings=false
queue_max_events=10000
on_full=drop_oldest
shutdown_flush_timeout_ms=5000
kafka_brokers=kafka:9092
kafka_topic=people-raw
kafka_key=sensor_id
kafka_properties_file=/tmp/perceptra/generated/kafka.properties
file_path=

[timestamps]
source=ntp

[inference]
hot_swap_file=

[tracker]
reid=false
reid_store_age=0

[ops]
port=9464
perf_interval_s=5
log_level=info
```
