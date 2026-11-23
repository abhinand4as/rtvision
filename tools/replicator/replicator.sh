#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
#
# RTSP stream replicator in a terminal: plays video files as looping, real-time RTSP cameras.
#
#   tools/replicator/replicator.sh a.mp4 b.mp4 --streams 4     # rtsp://localhost:8554/cam-1 ... 4
#   tools/replicator/replicator.sh --config my-streams.yaml    # videos next to the config file
#   tools/replicator/replicator.sh --port 8600 a.mp4           # another RTSP port
#   tools/replicator/replicator.sh --help                      # all options
#
# Runs natively if this host has the GStreamer RTSP server bindings, else in the RTVision image
# (RTVISION_IMAGE, default rtvision:local; no GPU needed). REPLICATOR_BIND=0.0.0.0 makes the
# streams reachable from other machines, REPLICATOR_NETWORK attaches the container to a Docker
# network (e.g. rtvision_default, then use rtsp://rtvision-replicator:8554/cam-1 there).
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
image="${RTVISION_IMAGE:-rtvision:local}"
bind="${REPLICATOR_BIND:-127.0.0.1}"

for py in "$repo/.venv/bin/python" python3; do
    if [[ -z "${REPLICATOR_DOCKER:-}" && -z "${REPLICATOR_NETWORK:-}" ]] &&
        PYTHONPATH="$repo/tools" "$py" -c 'import gi, jsonschema, yaml
gi.require_version("GstRtspServer", "1.0")' 2>/dev/null; then
        PYTHONPATH="$repo/tools" RTVISION_SCHEMA_DIR="$repo/schemas" exec "$py" \
            -m rtvision_tools.replicator.cli "$@"
    fi
done

command -v docker >/dev/null || { echo "replicator.sh: docker not found" >&2; exit 2; }
docker image inspect "$image" >/dev/null 2>&1 || {
    echo "replicator.sh: image $image not found; build it with:" >&2
    echo "  docker build -f docker/Dockerfile -t $image $repo" >&2
    exit 2
}

# Mount the directory of the videos / config read-only and pass the file names inside it.
dir="" args=() port=8554 host_set="" want=""
for arg in "$@"; do
    case "$want" in
        port) port="$arg" ;;
        host) host_set=1 ;;
    esac
    if [[ "$want" != port && "$want" != host && "$want" != skip && -f "$arg" ]]; then
        d="$(cd "$(dirname "$arg")" && pwd)"
        if [[ -n "$dir" && "$dir" != "$d" ]]; then
            echo "replicator.sh: all files must be in the same directory" >&2
            exit 2
        fi
        dir="$d"
        args+=("/media/$(basename "$arg")")
    else
        args+=("$arg")
    fi
    want=""
    case "$arg" in
        --port) want=port ;;
        --port=*) port="${arg#*=}" ;;
        --public-host) want=host ;;
        --public-host=*) host_set=1 ;;
        --streams | --prefix | --stagger) want=skip ;;
        --videos-dir | --api-port | --api-host)
            echo "replicator.sh: $arg is for the compose service (deploy/compose/compose.ui.yaml)" >&2
            exit 2
            ;;
        -*) ;;
        *)
            if [[ "${args[-1]}" == "$arg" && ! -f "$arg" && "$arg" =~ \.(mp4|mov|m4v|mkv|ts|ya?ml)$ ]]; then
                echo "replicator.sh: file '$arg' not found" >&2
                exit 2
            fi
            ;;
    esac
done

opts=(--rm --init --name "rtvision-replicator" -p "$bind:$port:$port" --no-healthcheck)
[[ -t 0 && -t 1 ]] && opts+=(-it)
[[ -n "$dir" ]] && opts+=(-v "$dir:/media:ro")
[[ -n "${REPLICATOR_NETWORK:-}" ]] && opts+=(--network "$REPLICATOR_NETWORK")
if [[ -z "$host_set" ]]; then
    if [[ -n "${REPLICATOR_NETWORK:-}" ]]; then args+=(--public-host rtvision-replicator); else args+=(--public-host localhost); fi
fi
exec docker run "${opts[@]}" "$image" rtvision-replicator "${args[@]}" 2> >(grep -v -e "Failed to load plugin" -e '^$' >&2)
