#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
#
# Demo: people detection on 4 RTSP cameras replicated from one openly licensed online video.
#
#   examples/demo/run_demo.sh            # download the video, start everything, add 4 cameras
#   examples/demo/run_demo.sh stop       # stop and remove the containers (data is kept)
#   examples/demo/run_demo.sh clean      # stop and delete the demo's volumes and the video
#
# Needs the exported model (tools/export/rfdetr/export.sh) and the requirements of the README.
# Runs in its own compose project (rtvision-demo), so a normal RTVision stack keeps its data; the
# ports must be free. Settings: RTVISION_UI_PORT (8080), DEMO_MODEL, DEMO_CACHE.
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
export COMPOSE_PROJECT_NAME=rtvision-demo
export RTVISION_MODEL="${DEMO_MODEL:-rfdetr-s-people}"
export RTVISION_UI_PORT="${RTVISION_UI_PORT:-8080}"
cache="${DEMO_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/rtvision-demo}"
ui="http://localhost:$RTVISION_UI_PORT"

# "store-aisle-detection.mp4" from Intel's sample-videos, CC-BY-4.0, pinned commit and checksum.
video=store-aisle-detection.mp4
url="https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/57978890822836f2b4743852f04f62fc511757e4/$video"
sha256=3526fee39cac70d6366471e4324a6d63b337c8f8ab99c015521bee0d60ed6e04

names=("Aisle north" "Aisle south" "Aisle east" "Aisle west")
offsets=(0 16 32 48) # seconds into the 65 s video

say() { echo "==> $*"; }
die() { echo "run_demo.sh: $*" >&2; exit 1; }
stack() { docker compose -f "$repo/deploy/compose/compose.yaml" -f "$repo/deploy/compose/compose.ui.yaml" "$@"; }
call() { curl -sS --fail-with-body -m 120 -X "$1" -H 'X-RTVision: 1' "${@:3}" "$ui$2"; }
json() { python3 -c "import sys,json; d=json.load(sys.stdin); print($1)"; }

start() {
    for tool in docker curl python3 sha256sum; do
        command -v "$tool" >/dev/null || die "$tool is required"
    done
    [[ -f "$repo/models/examples/rfdetr-s-coco/rfdetr-small.onnx" ]] ||
        die "the model is not exported yet; run: tools/export/rfdetr/export.sh"

    mkdir -p "$cache"
    if ! echo "$sha256  $cache/$video" | sha256sum --check --status 2>/dev/null; then
        say "downloading $video (9 MB, CC-BY-4.0, Intel sample-videos)"
        curl -fL --progress-bar -o "$cache/$video.part" "$url"
        echo "$sha256  $cache/$video.part" | sha256sum --check --status || die "checksum mismatch"
        mv "$cache/$video.part" "$cache/$video"
    fi

    say "starting Kafka, RTVision ($RTVISION_MODEL), replicator and web UI"
    stack up -d --build || die "docker compose failed (are the ports free? stop other RTVision stacks)"
    say "waiting for RTVision (the first start builds the TensorRT engine, about a minute)"
    for ((i = 0; ; i++)); do
        [[ "$(curl -s -m 5 "$ui/api/overview" | json 'd["online"] and d["ready"]' 2>/dev/null)" == True ]] && break
        ((i < 300)) || die "not ready after 10 minutes; see: docker compose -p rtvision-demo logs rtvision"
        sleep 2
    done

    say "uploading the video and configuring 4 looping RTSP streams"
    call PUT "/api/replicator/videos/$video" --data-binary @"$cache/$video" >/dev/null
    streams=""
    for i in 0 1 2 3; do
        streams+="{\"name\":\"aisle-$((i + 1))\",\"video\":\"$video\",\"start_offset_s\":${offsets[i]}},"
    done
    call PUT /api/replicator/config -d "{\"public_host\":\"replicator\",\"streams\":[${streams%,}]}" >/dev/null

    say "adding the cameras to RTVision"
    present="$(curl -s "$ui/api/overview" | json '" ".join(s["id"] for s in d["streams"])')"
    for i in 0 1 2 3; do
        id="aisle-$((i + 1))"
        [[ " $present " == *" $id "* ]] && continue
        call POST /api/streams -d "{\"id\":\"$id\",\"name\":\"${names[i]}\",\"url\":\"rtsp://replicator:8554/$id\"}" >/dev/null
    done
    sleep 8
    curl -s "$ui/api/overview" | python3 -c '
import json, sys
for s in json.load(sys.stdin)["streams"]:
    print("    {:<12} {:5.1f} FPS  {}".format(s["name"], s["fps"], s["objects"] or "nothing in view"))'
    cat <<TXT

Demo is running.
  Live view      $ui/live
  Performance    $ui/performance
  Replicator     $ui/replicator
  RTSP (VLC)     rtsp://localhost:${REPLICATOR_PORT:-8554}/aisle-1 ... aisle-4
  People counts  docker compose -p rtvision-demo exec -T kafka /opt/kafka/bin/kafka-console-consumer.sh \\
                   --bootstrap-server kafka:9092 --topic people-raw | python3 examples/consumer/occupancy.py
  Stop           examples/demo/run_demo.sh stop
TXT
}

case "${1:-start}" in
    start) start ;;
    stop) stack down --remove-orphans ;;
    clean)
        stack down --volumes --remove-orphans
        rm -f "$cache/$video" "$cache/$video.part"
        rmdir "$cache" 2>/dev/null || true
        ;;
    *) die "usage: run_demo.sh [start|stop|clean]" ;;
esac
