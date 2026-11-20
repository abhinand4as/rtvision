#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
#
# Container entrypoint.
#   (no args) or flags  -> exec perceptra-run [flags]   (validate, generate configs, run)
#   any other command   -> exec it (e.g. `bash`, `perceptra-run --dry-run`)
# Checks up front that the engine cache is writable, because a root-owned bind mount is the most
# common first-run failure of the non-root image (TensorRT would fail much later and less
# clearly).
set -euo pipefail

if [[ $# -gt 0 && "$1" != -* ]]; then
    exec "$@"
fi

engine_dir="${PERCEPTRA_ENGINE_DIR:-/opt/perceptra/engines}"
if [[ -d "$engine_dir" && ! -w "$engine_dir" ]]; then
    echo "level=error comp=entrypoint event=engine_dir_not_writable dir=$engine_dir" \
         "uid=$(id -u) hint=\"use a named volume, or chown 1000:1000 the host directory\"" >&2
    exit 2
fi

exec perceptra-run "$@"
