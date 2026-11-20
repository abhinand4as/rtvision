#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
# POST /api/v1/stream/add for the in-container sample stream (IT_CAMERA_ID, IT_CAMERA_URL).
set -euo pipefail
# shellcheck source=tests/integration/common.sh
source "$(dirname "$0")/common.sh"

body="$(mktemp)"
trap 'rm -f "$body"' EXIT
code="$(stream_payload add | curl -s -m 30 -o "$body" -w '%{http_code}' -XPOST \
        "$REST_URL/api/v1/stream/add" --data-binary @-)" || true
[[ "$code" == 200 ]] || fail "stream/add returned $code: $(cat "$body")"
pass "stream/add $IT_CAMERA_ID ($IT_CAMERA_URL)"
