#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
# POST /api/v1/stream/remove and wait for perceptra_streams_active == 0 (within 10 s).
set -euo pipefail
# shellcheck source=tests/integration/common.sh
source "$(dirname "$0")/common.sh"

body="$(mktemp)"
trap 'rm -f "$body"' EXIT
code="$(stream_payload remove | curl -s -m 30 -o "$body" -w '%{http_code}' -XPOST \
        "$REST_URL/api/v1/stream/remove" --data-binary @-)" || true
[[ "$code" == 200 ]] || fail "stream/remove returned $code: $(cat "$body")"
pass "stream/remove $IT_CAMERA_ID"

deadline=$((SECONDS + 10))
until [[ "$(metric perceptra_streams_active)" == 0 ]]; do
    ((SECONDS < deadline)) ||
        fail "perceptra_streams_active still $(metric perceptra_streams_active) after 10s"
    sleep 1
done
pass "perceptra_streams_active 0"
