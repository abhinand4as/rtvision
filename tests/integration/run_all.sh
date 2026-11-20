#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
#
# Integration test (GPU host), docs/TESTING.md:
#   deploy (compose: Kafka + Perceptra) -> health -> add stream -> consume >= MIN_EVENTS from
#   Kafka, each validated against the event schema -> key/headers -> metrics -> remove stream ->
#   `docker compose stop perceptra` exits 0 within STOP_TIMEOUT_S.
#
#   tests/integration/run_all.sh            # builds perceptra:local if the image is missing
#   BUILD=1 tests/integration/run_all.sh    # always rebuild the image first
#   KEEP_STACK=1 ...                        # leave the stack running afterwards (debugging)
#
# Needs: docker with compose v2, the NVIDIA Container Toolkit, curl, and a Python 3.10+ with
# jsonschema + PyYAML (the repo's .venv is used if present: `pip install -e '.[dev]'`).
set -euo pipefail
# shellcheck source=tests/integration/common.sh
source "$(dirname "$0")/common.sh"

if [[ -x "$REPO_ROOT/.venv/bin/python" ]]; then PYTHON="${PYTHON:-$REPO_ROOT/.venv/bin/python}"; fi
PYTHON="${PYTHON:-python3}"

finish() {
    local rc=$?
    if ((rc != 0)); then
        echo "---- perceptra logs (last 40 lines) ----" >&2
        compose logs --no-color --tail 40 perceptra >&2 2>&1 || true
    fi
    if [[ "${KEEP_STACK:-0}" != 1 ]]; then
        compose down --remove-orphans >/dev/null 2>&1 || true
    fi
    if ((rc == 0)); then echo "PASS: integration test"; else echo "FAIL: integration test" >&2; fi
}

# ---- 0. preflight ----------------------------------------------------------------------------
command -v docker >/dev/null || fail "docker not found"
docker compose version >/dev/null 2>&1 || fail "docker compose v2 not found"
command -v curl >/dev/null || fail "curl not found"
"$PYTHON" -c 'import jsonschema, yaml' 2>/dev/null ||
    fail "$PYTHON lacks jsonschema/PyYAML (pip install -e '.[dev]', or set PYTHON=...)"
image="${PERCEPTRA_IMAGE:-perceptra:local}"
if [[ "${BUILD:-0}" == 1 ]] || ! docker image inspect "$image" >/dev/null 2>&1; then
    info "building $image (docker/Dockerfile)"
    compose build perceptra
fi

# Config and licence gate, in seconds instead of a restart loop (exit 2 config, 3 licence).
set +e
dryrun="$(compose run --rm --no-deps -T perceptra --dry-run 2>&1 >/dev/null)"
rc=$?
set -e
if ((rc != 0)); then
    grep -v '^\s*$' <<<"$dryrun" | tail -12 >&2
    ((rc == 3)) && fail "model licence not accepted: set PERCEPTRA_ACCEPT_MODEL_LICENSE (shown" \
        "above) in deploy/compose/.env or the environment, after reading docs/LICENSING.md"
    model="${PERCEPTRA_MODEL:-rfdetr-s-coco}"
    if [[ "$dryrun" == *"files.onnx"* ]]; then
        hint="see models/examples/$model/README.md"
        [[ "$model" == rfdetr-* ]] && hint="run tools/export/rfdetr/export.sh first"
        fail "ONNX for $model missing: $hint"
    fi
    fail "perceptra-run --dry-run exit $rc (configuration error above)"
fi
pass "configuration valid (perceptra-run --dry-run)"

trap finish EXIT

# ---- 1. deploy (fresh Kafka; the engine cache volume is kept) --------------------------------
compose down --remove-orphans >/dev/null 2>&1 || true
compose up -d
pass "compose up ($image, model ${PERCEPTRA_MODEL:-rfdetr-s-coco})"

# ---- 2-3. health -----------------------------------------------------------------------------
"$REPO_ROOT/tests/integration/health.sh"

# ---- 4. add the sample stream ----------------------------------------------------------------
"$REPO_ROOT/tests/integration/add_stream.sh"

# ---- 5. consume from Kafka and validate every event ------------------------------------------
topic="${TOPIC:-$(compose exec -T perceptra sed -n 's/^kafka_topic=//p' \
    /tmp/perceptra/generated/app.ini | tr -d '\r')}"
[[ -n "$topic" ]] || fail "could not read kafka_topic from the generated app.ini"
info "consuming topic $topic (up to ${CONSUME_TIMEOUT_S}s)"
# `timeout` needs a real command, not the compose() shell function.
consumer=(docker compose -f "$COMPOSE_FILE" exec -T kafka /opt/kafka/bin/kafka-console-consumer.sh
                          --bootstrap-server kafka:9092 --topic "$topic" --from-beginning
                          --timeout-ms "$((CONSUME_TIMEOUT_S * 1000))")
set +e
timeout "$((CONSUME_TIMEOUT_S + 15))" "${consumer[@]}" 2>/dev/null |
    "$PYTHON" "$REPO_ROOT/tests/integration/consume_validate.py" --stdin \
        --min-events "$MIN_EVENTS" --sensor-id "$IT_CAMERA_ID" --require-objects \
        --check-monotonic --timeout "$CONSUME_TIMEOUT_S"
rc=${PIPESTATUS[1]}
set -e
((rc == 0)) || fail "Kafka events did not validate (consume_validate exit $rc)"

# Message key and headers (ARCHITECTURE §6: key = sensor.id; headers schema + content-type).
meta="$(compose exec -T kafka /opt/kafka/bin/kafka-console-consumer.sh \
    --bootstrap-server kafka:9092 --topic "$topic" --from-beginning --max-messages 1 \
    --timeout-ms 20000 --formatter-property print.key=true \
    --formatter-property print.headers=true --formatter-property print.value=false \
    --formatter-property key.separator='|' 2>/dev/null | head -1)"
[[ "$meta" == *"schema:perceptra.detection.v1"* ]] || fail "Kafka header schema missing: $meta"
[[ "$meta" == *"content-type:application/json"* ]] || fail "Kafka header content-type missing: $meta"
[[ "$meta" == *"|$IT_CAMERA_ID"* ]] || fail "Kafka key is not $IT_CAMERA_ID: $meta"
pass "Kafka key=$IT_CAMERA_ID, headers schema + content-type"

# ---- 6. metrics ------------------------------------------------------------------------------
published="$(metric perceptra_events_published_total)"
((published > 0)) || fail "perceptra_events_published_total is $published"
pass "perceptra_events_published_total $published"

# ---- 7. remove the stream --------------------------------------------------------------------
"$REPO_ROOT/tests/integration/remove_stream.sh"

# ---- 8. graceful stop ------------------------------------------------------------------------
cid="$(compose ps -q perceptra)"
start=$SECONDS
compose stop -t 20 perceptra >/dev/null
took=$((SECONDS - start))
exit_code="$(docker inspect -f '{{.State.ExitCode}}' "$cid")"
[[ "$exit_code" == 0 ]] || fail "perceptra exited with $exit_code after stop"
((took <= STOP_TIMEOUT_S)) || fail "stop took ${took}s (limit ${STOP_TIMEOUT_S}s)"
pass "compose stop: exit 0 in ${took}s"
