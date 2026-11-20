<!-- SPDX-License-Identifier: Apache-2.0 -->
# HTTP APIs

Perceptra exposes two HTTP servers:

| Port | Server | Purpose |
|---|---|---|
| `9000` (`service.rest_port`) | DeepStream `nvmultiurisrcbin` built-in REST server | stream management |
| `9464` (`observability.ops_port`) | Perceptra ops server (libsoup) | health probes, metrics, info |

Neither server authenticates requests. Expose them only on trusted networks or behind a gateway.

---

## Stream management (port 9000)

The payload format is DeepStream's (`Gst-nvmultiurisrcbin` documentation). Perceptra does not change it, so existing VST/RT-CV clients work unchanged. **Implementer:** verify the exact route list and response bodies against the DeepStream 9.1 docs, and record any differences in `docs/DEVIATIONS.md`.

### Add a stream
`POST /api/v1/stream/add`

```json
{
  "key": "sensor",
  "value": {
    "camera_id": "cam-entrance-01",
    "camera_name": "Entrance 1",
    "camera_url": "rtsp://10.0.0.21:554/stream1",
    "change": "camera_add",
    "metadata": { "resolution": "1920x1080", "codec": "h264", "framerate": 30 }
  },
  "headers": { "source": "orchestrator", "created_at": "2026-11-20T06:30:00.000Z" }
}
```

- `camera_id` → event `sensor.id`; `camera_name` → `sensor.name`.
- Adding beyond `service.max_streams` fails (DeepStream rejects it); the orchestrator must handle the error.
- URL schemes in scope: `rtsp://`, `rtsps://`, `file://`, `http(s)://`.

### Remove a stream
`POST /api/v1/stream/remove` — same envelope with `"change": "camera_remove"` and the `camera_id` (and `camera_url`) of the stream to remove.

### Examples
```bash
curl -sS -XPOST localhost:9000/api/v1/stream/add -d @add.json
curl -sS -XPOST localhost:9000/api/v1/stream/remove -d @remove.json
```

---

## Ops server (port 9464)

| Method | Path | 200 when | Body |
|---|---|---|---|
| GET | `/startup` | configs parsed and pipeline reached PLAYING at least once | `{"status":"started"}` |
| GET | `/ready` | pipeline PLAYING and publisher healthy (Kafka connected, or backend ≠ kafka) | `{"status":"ready","streams":2}` |
| GET | `/live` | main-loop heartbeat < 10 s old | `{"status":"alive"}` |
| GET | `/metrics` | always | Prometheus text exposition (v0.0.4) |
| GET | `/info` | always | `{"service":"perceptra","version":"0.1.0","model":{"name","version","task","family","precision","license"},"max_streams":4,"schema":"perceptra.detection.v1"}` |

Non-200 responses use 503 with `{"status":"<reason>"}` (e.g. `starting`, `pipeline not playing`, `publisher not connected`, `main loop stalled`). Other methods than GET/HEAD get 405, unknown paths 404. `/live` answers 200 while the service is still starting (pipeline creation and TensorRT engine builds run before the main loop); use `/startup` as the startup probe.

Kubernetes example:
```yaml
startupProbe:   { httpGet: { path: /startup, port: 9464 }, failureThreshold: 60, periodSeconds: 10 }  # engine build can take minutes
readinessProbe: { httpGet: { path: /ready,   port: 9464 }, periodSeconds: 5 }
livenessProbe:  { httpGet: { path: /live,    port: 9464 }, periodSeconds: 10 }
```
