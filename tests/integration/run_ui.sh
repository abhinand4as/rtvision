#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
#
# Integration test of the web UI and the RTSP replicator (GPU host): deploy compose.yaml +
# compose.ui.yaml -> upload a video through the UI -> configure 4 looping RTSP streams -> add them
# to RTVision -> all 4 at >= 25 FPS with detections -> annotated video and hardware readings ->
# streams still run after the video has looped -> remove the cameras.
#
#   tests/integration/run_ui.sh                 # KEEP_STACK=1 leaves it running
#   IT_VIDEO=/path/to/video.mp4 ...             # default: the sample video of the DeepStream image
#
# Runs in its own compose project (rtvision-it, its volumes are removed at the end); the ports must
# be free, so stop a running stack first.
set -euo pipefail
# shellcheck source=tests/integration/common.sh
source "$(dirname "$0")/common.sh"

: "${RTVISION_UI_PORT:=8080}"
: "${RTVISION_MODEL:=rfdetr-s-coco}"
: "${IT_VIDEO:=}"
# Own compose project: never touches the videos, streams or cameras of a normal `rtvision` stack.
: "${COMPOSE_PROJECT_NAME:=rtvision-it}"
export RTVISION_UI_PORT RTVISION_MODEL COMPOSE_PROJECT_NAME
UI="http://$RTVISION_HOST:$RTVISION_UI_PORT"
PYTHON="${PYTHON:-python3}"
NAME=it-video.mp4
work="$(mktemp -d)"

stack() { docker compose -f "$COMPOSE_FILE" -f "$REPO_ROOT/deploy/compose/compose.ui.yaml" "$@"; }
# ui METHOD PATH [curl args...] -> body; fails on HTTP errors
ui() { curl -sS --fail-with-body -m 120 -X "$1" -H 'X-RTVision: 1' "${@:3}" "$UI$2"; }
# overview 'python expression over d' -> printed value
overview() { curl -s -m 5 "$UI/api/overview" | "$PYTHON" -c "import sys,json; d=json.load(sys.stdin); print($1)" 2>/dev/null || true; }

finish() {
    local rc=$?
    if ((rc != 0)); then stack logs --no-color --tail 20 rtvision ui replicator >&2 2>&1 || true; fi
    if [[ "${KEEP_STACK:-0}" != 1 ]]; then
        stack down --volumes --remove-orphans >/dev/null 2>&1 || true
    fi
    rm -rf "$work"
    if ((rc == 0)); then echo "PASS: UI integration test"; else echo "FAIL: UI integration test" >&2; fi
}

image="${RTVISION_IMAGE:-rtvision:local}"
docker image inspect "$image" >/dev/null 2>&1 || stack build rtvision
trap finish EXIT
stack down --remove-orphans >/dev/null 2>&1 || true
stack up -d
"$REPO_ROOT/tests/integration/health.sh"
deadline=$((SECONDS + 60))
until [[ "$(overview 'd["online"] and d["ready"]')" == True ]]; do
    ((SECONDS < deadline)) || fail "the UI does not see RTVision: $(overview 'd.get("error")')"
    sleep 2
done
pass "UI online ($UI)"

code="$(curl -s -o /dev/null -w '%{http_code}' -X POST -d '{}' "$UI/api/streams")"
[[ "$code" == 403 ]] || fail "a request without the X-RTVision header was answered with $code"
pass "cross-site requests are refused"

if [[ -z "$IT_VIDEO" ]]; then
    stack cp "rtvision:${IT_CAMERA_URL#file://}" "$work/sample.mp4" >/dev/null 2>&1
    IT_VIDEO="$work/sample.mp4"
fi
info="$(ui PUT "/api/replicator/videos/$NAME" --data-binary @"$IT_VIDEO")" || fail "upload: $info"
pass "video uploaded: $info"
length="$("$PYTHON" -c 'import sys,json; print(int(json.loads(sys.argv[1])["duration_s"]) + 1)' "$info")"

streams=""
for i in 1 2 3 4; do streams+="{\"name\":\"it-$i\",\"video\":\"$NAME\",\"start_offset_s\":$(((i - 1) * 9))},"; done
ui PUT /api/replicator/config -d "{\"public_host\":\"replicator\",\"streams\":[${streams%,}]}" >/dev/null
for i in 1 2 3 4; do
    ui POST /api/streams -d "{\"id\":\"it-$i\",\"name\":\"IT $i\",\"url\":\"rtsp://replicator:8554/it-$i\"}" >/dev/null ||
        fail "cannot add it-$i"
done
pass "4 replicated cameras added"

fast='len([s for s in d["streams"] if s["fps"] >= 25])'
deadline=$((SECONDS + 60))
until [[ "$(overview "$fast")" == 4 && "$(overview 'len([s for s in d["streams"] if s["objects"]])')" == 4 ]]; do
    ((SECONDS < deadline)) || fail "streams: $(overview '[(s["id"], s["fps"], s["objects"]) for s in d["streams"]]')"
    sleep 2
done
pass "4 cameras at >= 25 FPS with detections: $(overview '[(s["id"], s["fps"], s["objects"]) for s in d["streams"]]')"

curl -s -m 6 "$UI/preview.mjpg" -o "$work/preview.bin" || true
frames="$(grep -a -c -- '--rtvisionframe' "$work/preview.bin" || true)"
((frames >= 10)) || fail "annotated video: only $frames frames in 6 s"
pass "annotated video: $frames frames in 6 s"

gpu="$(curl -s "$UI/api/hardware" | "$PYTHON" -c 'import sys,json; d=json.load(sys.stdin); print(d["gpus"][0]["name"] if d["gpus"] and d["cpu"]["cores"] and d["memory"]["total"] else "")')"
[[ -n "$gpu" ]] || fail "hardware readings are incomplete"
pass "hardware readings ($gpu)"

info "waiting ${length}s so that every stream loops"
sleep "$length"
loops="$(ui GET /api/replicator/status | "$PYTHON" -c 'import sys,json; print(min(s["loops"] for s in json.load(sys.stdin)["streams"]))')"
((loops >= 1)) || fail "not every stream looped"
[[ "$(overview "$fast")" == 4 ]] || fail "after looping: $(overview '[(s["id"], s["fps"]) for s in d["streams"]]')"
[[ "$(overview 'int(d["totals"]["rtvision_events_dropped_total"])')" == 0 ]] || fail "events were dropped"
pass "all 4 streams still >= 25 FPS after looping, no events dropped"

for i in 1 2 3 4; do ui DELETE "/api/streams/it-$i" >/dev/null || fail "cannot remove it-$i"; done
pass "cameras removed"
