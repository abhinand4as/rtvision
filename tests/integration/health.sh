#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
# Wait for /startup (first start builds the TensorRT engine: up to STARTUP_TIMEOUT_S), then check
# /live and /ready (ready needs the Kafka connection, so it gets a short grace period).
set -euo pipefail
# shellcheck source=tests/integration/common.sh
source "$(dirname "$0")/common.sh"

# Fail fast if the service container dies or restarts while we wait (when run under compose).
check_container() {
    local cid state
    cid="$(compose ps -a -q perceptra 2>/dev/null || true)"
    [[ -n "$cid" ]] || return 0
    state="$(docker inspect -f '{{.State.Status}} {{.RestartCount}} {{.State.ExitCode}}' "$cid")"
    read -r status restarts code <<<"$state"
    if [[ "$status" != running || "$restarts" != 0 ]]; then
        compose logs --no-color --tail 15 perceptra >&2 || true
        fail "perceptra container $status (restarts=$restarts, last exit code $code)"
    fi
}

deadline=$((SECONDS + STARTUP_TIMEOUT_S))
until [[ "$(http_code "$OPS_URL/startup")" == 200 ]]; do
    ((SECONDS < deadline)) || fail "/startup not 200 after ${STARTUP_TIMEOUT_S}s"
    check_container
    sleep 3
done
pass "/startup 200 after ${SECONDS}s"

[[ "$(http_code "$OPS_URL/live")" == 200 ]] || fail "/live not 200"
pass "/live 200"

deadline=$((SECONDS + 30))
until [[ "$(http_code "$OPS_URL/ready")" == 200 ]]; do
    ((SECONDS < deadline)) || fail "/ready not 200: $(curl -s -m 5 "$OPS_URL/ready")"
    sleep 1
done
pass "/ready 200 $(curl -s -m 5 "$OPS_URL/ready")"
