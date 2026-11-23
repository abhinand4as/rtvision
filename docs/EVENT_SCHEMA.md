<!-- SPDX-License-Identifier: Apache-2.0 -->
# Event schema — `rtvision.detection.v1`

One JSON message per **sampled frame per stream** (sampling: `publish.interval_frames`). The machine-readable definition is `schemas/detection_event.v1.schema.json`; the serializer output must validate against it.

## Transport

| Item | Value |
|---|---|
| Kafka topic | `publish.kafka.topic`, else `model.messaging.topic`, else `rtvision.<model.name>` |
| Kafka key | `sensor.id` (keeps per-camera ordering), or none |
| Kafka headers | `schema: rtvision.detection.v1`, `content-type: application/json` |
| stdout / file | JSON Lines (one event per line, no pretty-printing) |

## Example

```json
{
  "schema": "rtvision.detection.v1",
  "event_id": "5f0c2a3e-6a51-4c1e-9d7a-2b8c0f6e9a11",
  "service": "rtvision",
  "timestamp": "2026-11-20T06:30:00.123Z",
  "timestamp_source": "ntp",
  "sensor": { "id": "cam-entrance-01", "name": "Entrance 1", "stream_index": 0 },
  "frame": { "number": 1230, "width": 1920, "height": 1080 },
  "model": { "name": "rfdetr-s-people", "version": "rfdetr-1.12.1-small", "task": "detect" },
  "objects": [
    {
      "track_id": 17,
      "class_id": 0,
      "label": "person",
      "confidence": 0.873,
      "bbox": { "left": 812.4, "top": 301.0, "width": 96.2, "height": 254.7 }
    }
  ]
}
```

## Field reference

| Field | Type | Notes |
|---|---|---|
| `schema` | const string | `rtvision.detection.v1` |
| `event_id` | UUID v4 string | unique per message (`g_uuid_string_random`) |
| `service` | string | `service.name` |
| `timestamp` | RFC 3339 UTC, ms | see ARCHITECTURE §7 |
| `timestamp_source` | `ntp` \| `uri_utc` \| `system` | |
| `sensor.id` | string | `camera_id` from the stream-add REST call |
| `sensor.name` | string \| null | `camera_name` from the REST call |
| `sensor.stream_index` | int ≥ 0 | DeepStream source id (may be reused after a removal) |
| `frame.number` | int ≥ 0 | per-stream frame counter |
| `frame.width/height` | int | **source** resolution; bboxes are in this space |
| `model.*` | strings | from `model.yaml` |
| `objects[]` | array (may be empty only if `include_empty_frames=true`) | |
| `objects[].track_id` | int \| null | null when the tracker is disabled or the object is untracked |
| `objects[].class_id` | int ≥ 0 | index in `labels.txt` |
| `objects[].label` | string | label text |
| `objects[].confidence` | number 0–1 | detector confidence (tracker-only frames: last detector confidence, or −1 → serialized as `null`) |
| `objects[].bbox` | object | left, top, width, height in source pixels (floats, 1 decimal) |
| `objects[].embedding` | object, optional | `{ "dim": 256, "encoding": "base64-f32le", "data": "…" }`, only when `include_embeddings=true` |
| `objects[].mask` | reserved | **not emitted in v1**; reserved for `task: segment` (see ROADMAP) |
| `objects[].attributes` | reserved | **not emitted in v1**; reserved for secondary classifiers |

## Compatibility rules
- Consumers must ignore unknown fields.
- New optional fields may be added within v1. Removing or renaming fields, or changing their types, requires `rtvision.detection.v2`.
