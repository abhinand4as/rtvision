<!-- SPDX-License-Identifier: Apache-2.0 -->
# Testing strategy

| Layer | Runs where | Tooling | Gate |
|---|---|---|---|
| Parser core unit tests | CI (no GPU) | `make -C src/parsers test-core` | every PR |
| Python tools (schemas, generator, ONNX inspect, replicator, web UI, examples) | CI | `pytest -q tests/python` | every PR |
| Serializer golden + publisher queue tests | CI (GLib only) | `make -C src/app test` (`test-serialize`, `test-publisher`) + pytest schema check | every PR |
| Static checks | CI | ruff, clang-format, shellcheck, compose config, SPDX check, forbidden-files check | every PR |
| Integration (compose) | GPU host | `tests/integration/run_all.sh` | before release |
| Web UI + replicator | GPU host | `tests/integration/run_ui.sh` (own compose project, ports must be free) | before release |
| Demo | GPU host | `examples/demo/run_demo.sh` | when the UI or the replicator changes |
| Model accuracy smoke | GPU host | not scripted: compare with the model's own inference (docs/MODELS.md step 7) | when adding a model |
| Soak | GPU host | 1 h, 4 streams: RSS growth < 5 %, no dropped events at default rate. **Not run yet**; the replicator's looping streams make it possible | before release |

## Unit tests (no GPU)

- **Parsers:** see `docs/PARSERS.md` → *Required tests*.
- **gen_configs:**
  - golden output for each `models/examples/*` × `config/rtvision.example.yaml`;
  - `max_streams` propagates to all 3 batch keys;
  - `classes.keep` → correct `filter-out-class-ids`;
  - resize mapping;
  - licence gate exit code 3;
  - env overrides (`RTVISION__…`);
  - unknown keys rejected with the field path in the message.
- **onnx_inspect:** tiny synthetic ONNX graphs built in the test with `onnx.helper` (no downloaded models) for each family and shape.
- **Schema:** golden events validate; mutated events (missing field, bad timestamp) fail.

## Integration (`tests/integration/run_all.sh`)

1. `docker compose -f deploy/compose/compose.yaml up -d` (Kafka + RTVision with `RTVISION_MODEL=rfdetr-s-coco`, RF-DETR Small, Apache-2.0; export its ONNX once with `tools/export/rfdetr/export.sh`).
2. Wait for `GET :9464/startup` = 200 (timeout 600 s for the first engine build).
3. `GET :9464/ready` and `/live` = 200.
4. Add stream `file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4` with `camera_id=it-cam-0`.
5. Consume from Kafka for up to 90 s: ≥ `MIN_EVENTS` (default 3) events; each validates against the schema; `sensor.id == "it-cam-0"`; at least one has objects.
6. `GET :9464/metrics` shows `rtvision_events_published_total` > 0.
7. Remove the stream → 200; `rtvision_streams_active` drops to 0 within 10 s.
8. `docker compose stop rtvision` → container exit code 0 within 15 s.

Each script prints `PASS:`/`FAIL:` and exits non-zero on failure. Environment knobs live in `deploy/compose/.env.example` (`MIN_EVENTS`, `STARTUP_TIMEOUT_S`, `CONSUME_TIMEOUT_S`, `RTVISION_IMAGE`).

Running it (`tests/integration/run_ui.sh` does the same for the web UI and the replicator: upload, 4 looping streams, live video, hardware, removal):

```bash
tools/export/rfdetr/export.sh                         # once: models/examples/rfdetr-s-coco/rfdetr-small.onnx
tests/integration/run_all.sh                          # BUILD=1 to rebuild, KEEP_STACK=1 to keep it up
```

- The script starts with a `rtvision-run --dry-run` preflight: a missing ONNX or a non-permissive model licence (e.g. `RTVISION_MODEL=trafficcamnet-sample` needs `RTVISION_ACCEPT_MODEL_LICENSE=LicenseRef-NVIDIA-AI-Product-License`) fails in seconds with the fix (deviations #28, #31).
- Besides the steps above, the script checks the Kafka message key (`sensor.id`) and the `schema`/`content-type` headers, and runs `consume_validate.py --check-monotonic`.
- The stack is torn down at the end (`docker compose down`); the engine cache volume `rtvision_engines` is kept, so later runs skip the engine build.
- Python with `jsonschema` and `PyYAML` is needed on the host (the repo's `.venv` is used if present).
