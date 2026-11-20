#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
#
# Export RF-DETR to ONNX on the host (no GPU needed). Creates a separate virtualenv with pinned,
# permissively licensed tooling (rfdetr Apache-2.0, PyTorch BSD-3-Clause, CPU build), downloads
# the pretrained checkpoint from Roboflow and writes models/examples/rfdetr-s-coco/rfdetr-small.onnx.
#
#   tools/export/rfdetr/export.sh                  # small (default)
#   tools/export/rfdetr/export.sh --size medium    # nano | small | medium | large
#
# RFDETR_VENV overrides the virtualenv location (default ~/.venvs/rfdetr-export, ~1.7 GB).
set -euo pipefail

RFDETR_VERSION=1.12.1
venv="${RFDETR_VENV:-$HOME/.venvs/rfdetr-export}"
here="$(cd "$(dirname "$0")" && pwd)"

if [[ ! -x "$venv/bin/python" ]]; then
    echo "creating $venv (rfdetr $RFDETR_VERSION, CPU PyTorch)"
    python3 -m venv "$venv"
    "$venv/bin/pip" install -q --upgrade pip
    "$venv/bin/pip" install -q torch torchvision --index-url https://download.pytorch.org/whl/cpu
    "$venv/bin/pip" install -q "rfdetr[onnx]==$RFDETR_VERSION"
fi
exec "$venv/bin/python" -W ignore "$here/export_rfdetr.py" "$@"
