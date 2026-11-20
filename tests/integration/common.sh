#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
#
# Shared helpers for tests/integration/*.sh (sourced, not executed).
# Settings come from the environment, then deploy/compose/.env, then these defaults.

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="$REPO_ROOT/deploy/compose/compose.yaml"
ENV_FILE="$REPO_ROOT/deploy/compose/.env"

if [[ -f "$ENV_FILE" ]]; then
    # Only KEY=VALUE lines; values already set in the environment win.
    while IFS='=' read -r key value; do
        [[ "$key" =~ ^[A-Z_][A-Z0-9_]*$ ]] || continue
        [[ -n "${!key+x}" ]] || export "$key=$value"
    done < "$ENV_FILE"
fi

: "${PERCEPTRA_HOST:=127.0.0.1}"
: "${REST_PORT:=9000}"
: "${OPS_PORT:=9464}"
: "${IT_CAMERA_ID:=it-cam-0}"
: "${IT_CAMERA_URL:=file:///opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4}"
: "${MIN_EVENTS:=3}"
: "${STARTUP_TIMEOUT_S:=600}"
: "${CONSUME_TIMEOUT_S:=90}"
: "${STOP_TIMEOUT_S:=15}"
export PERCEPTRA_HOST REST_PORT OPS_PORT IT_CAMERA_ID IT_CAMERA_URL MIN_EVENTS \
       STARTUP_TIMEOUT_S CONSUME_TIMEOUT_S STOP_TIMEOUT_S

# shellcheck disable=SC2034  # used by the scripts that source this file
REST_URL="http://$PERCEPTRA_HOST:$REST_PORT"
OPS_URL="http://$PERCEPTRA_HOST:$OPS_PORT"

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }
info() { echo "  .. $*"; }

compose() { docker compose -f "$COMPOSE_FILE" "$@"; }

# http_code URL -> prints the status code (000 if unreachable).
http_code() { curl -s -o /dev/null -m 5 -w '%{http_code}' "$1" || true; }

# metric NAME -> sum of all samples of a Prometheus metric family (0 if absent).
metric() {
    curl -s -m 5 "$OPS_URL/metrics" |
        awk -v n="$1" '$1 == n || index($1, n "{") == 1 { s += $NF } END { printf "%d\n", s }'
}

# stream_payload add|remove -> the nvmultiurisrcbin REST envelope for the test camera.
stream_payload() {
    local change="camera_$1" now
    now="$(date -u +%Y-%m-%dT%H:%M:%S.000Z)"
    cat <<JSON
{"key":"sensor","value":{"camera_id":"$IT_CAMERA_ID","camera_name":"Integration test",
"camera_url":"$IT_CAMERA_URL","change":"$change",
"metadata":{"resolution":"1920x1080","codec":"h264","framerate":30}},
"headers":{"source":"perceptra-integration","created_at":"$now"}}
JSON
}
