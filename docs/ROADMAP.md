<!-- SPDX-License-Identifier: Apache-2.0 -->
# Roadmap and extension points

v1 implements `task: detect`. The following are **designed in but not implemented**. The implementing agent must leave these seams intact.

## Extension seams that must exist in v1

| Seam | Where | v1 behaviour |
|---|---|---|
| Task registry | `src/app/task_registry.c` — `const RTVisionTaskHandler *rtvision_task_get(const char *task)` | `detect` returns the handler; `segment`/`pose` return a stub whose `init()` fails with "task not supported in this version" |
| Object struct reserved fields | `RTVisionObject.mask`, `RTVisionObject.attributes` (NULL in v1) | never serialized |
| Schema reserved fields | `objects[].mask`, `objects[].attributes` | not emitted |
| Model task enum | `model.schema.json` accepts `segment`, `pose` | `rtvision-run` rejects them with a clear message |
| Publisher backend vtable | `publisher.h` | kafka, stdout, file |
| Parser family table | `ds_adapter.cpp` | 4 detection families |

## Planned features

### Instance segmentation (`task: segment`)
- DeepStream: `nvinfer` `network-type=3`, `output-instance-mask=1`, `parse-bbox-instance-mask-func-name=NvDsInferParseRTVision_<Family>Seg`; masks arrive in `NvDsObjectMeta.mask_params`.
- Families: `yolo_seg_e2e` (YOLO26-seg: detections + mask coefficients + prototypes), `maskrcnn_like`.
- Event: `objects[].mask = {"format":"polygon","points":[[x,y],...]}` (default, compact) or `{"format":"rle","size":[h,w],"counts":"..."}` (COCO RLE). Configurable `publish.mask_format`, plus `publish.mask_simplify_px`.
- Cost: mask encoding happens on the publisher thread; the probe copies only the mask buffer pointer and dims (copying on GPU→CPU only for sampled frames).

### Secondary classifiers (attributes)
`[secondary-gie*]` generated from `model.yaml` `secondaries:`. Results go in `objects[].attributes = [{"name":"helmet","value":"yes","confidence":0.92}]`.

### Analytics: zones and lines
DeepStream `nvdsanalytics` (ROI occupancy, line crossing, direction). Events gain `frame.analytics` and per-object `zones`. Configured per camera via an extra REST route or a config map.

### Multiple models / per-camera models
Several `nvinfer` instances, or a stream-routing design. Needs an ADR first.

### Platforms
Jetson (L4T, the same multi-arch image family) and ARM SBSA validation; INT8 calibration tooling.

### Open-source backend
A pure GStreamer + ONNX Runtime/Triton + ByteTrack backend behind the same configs, schema and REST contract. The parser `core/` is already DeepStream-free for this reason.

### Security
Auth for the REST/ops APIs and the web UI (mTLS or a token via gateway), Kafka TLS/SASL examples.
