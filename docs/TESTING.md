<!-- SPDX-License-Identifier: Apache-2.0 -->
# Testing strategy

| Layer | Runs where | Tooling | Gate |
|---|---|---|---|
| Parser core unit tests | CI (no GPU) | `make -C src/parsers test-core` | every PR |
| Python tools (schemas, generator, ONNX inspect) | CI | `pytest -q tests/python` | every PR |
| Serializer golden + publisher queue tests | CI (GLib only) | `make -C src/app test` (`test-serialize`, `test-publisher`) + pytest schema check | every PR |
| Static checks | CI | ruff, clang-format, SPDX check, forbidden-files check | every PR |
| Integration (compose) | GPU host | `tests/integration/run_all.sh` | before release |
| Model accuracy smoke | GPU host | `tests/integration/accuracy_smoke.py` (optional, user-supplied clip + golden JSON) | when adding a model |
| Soak | GPU host | 1 h, 4 streams: RSS growth < 5 %, no dropped events at default rate | before release |

## Unit tests (no GPU)

- **Parsers:** see `docs/PARSERS.md` → *Required tests*.
- **gen_configs:**
  - golden output for each `models/examples/*` × `config/perceptra.example.yaml`;
  - `max_streams` propagates to all 3 batch keys;
  - `classes.keep` → correct `filter-out-class-ids`;
  - resize mapping;
  - licence gate exit code 3;
  - env overrides (`PERCEPTRA__…`);
  - unknown keys rejected with the field path in the message.
- **onnx_inspect:** tiny synthetic ONNX graphs built in the test with `onnx.helper` (no downloaded models) for each family and shape.
- **Schema:** golden events validate; mutated events (missing field, bad timestamp) fail.

## Integration (`tests/integration/run_all.sh`)

1. `docker compose -f deploy/compose/compose.yaml up -d` (Kafka + Perceptra with `PERCEPTRA_MODEL=trafficcamnet-sample`, which needs no user-supplied weights).
2. Wait for `GET :9464/startup` = 200 (timeout 600 s for the first engine build).
3. `GET :9464/ready` and `/live` = 200.
4. Add stream `file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4` with `camera_id=it-cam-0`.
5. Consume from Kafka for up to 90 s: ≥ `MIN_EVENTS` (default 3) events; each validates against the schema; `sensor.id == "it-cam-0"`; at least one has objects.
6. `GET :9464/metrics` shows `perceptra_events_published_total` > 0.
7. Remove the stream → 200; `perceptra_streams_active` drops to 0 within 10 s.
8. `docker compose stop perceptra` → container exit code 0 within 15 s.

Each script prints `PASS:`/`FAIL:` and exits non-zero on failure. Environment knobs live in `deploy/compose/.env.example` (`MIN_EVENTS`, `STARTUP_TIMEOUT_S`, `CONSUME_TIMEOUT_S`, `PERCEPTRA_IMAGE`).
