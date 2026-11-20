# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Export a pretrained RF-DETR detector (Roboflow, Apache-2.0 sizes only) to ONNX for Perceptra.

Runs in its own virtualenv with the `rfdetr` package (see export.sh / README.md); never imported
by the service. The ONNX has a dynamic batch dimension (Perceptra batches up to
service.max_streams frames), input `input` [B,3,R,R] and outputs `dets` [B,300,4] (cxcywh,
normalised) and `labels` [B,300,91] (logits, index = COCO category id) -> parser family `detr`.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import shutil
import sys
import tempfile
from pathlib import Path

# Only the sizes whose weights are Apache-2.0. XLarge/2XLarge are under Roboflow's PML-1.0.
SIZES = {
    "nano": "RFDETRNano",
    "small": "RFDETRSmall",
    "medium": "RFDETRMedium",
    "large": "RFDETRLarge",
}
REPO = Path(__file__).resolve().parents[3]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--size", choices=sorted(SIZES), default="small")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=REPO / "models" / "examples" / "rfdetr-s-coco",
        help="directory for rfdetr-<size>.onnx (default: models/examples/rfdetr-s-coco)",
    )
    args = ap.parse_args(argv)

    import rfdetr  # noqa: PLC0415 (only available in the export venv)

    model = getattr(rfdetr, SIZES[args.size])()
    resolution = model.model_config.resolution
    args.out_dir.mkdir(parents=True, exist_ok=True)
    target = args.out_dir / f"rfdetr-{args.size}.onnx"
    with tempfile.TemporaryDirectory() as tmp:
        exported = Path(model.export(output_dir=tmp, dynamic_batch=True, verbose=False))
        shutil.move(str(exported), target)

    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    print(f"exported {target} ({target.stat().st_size / 1e6:.0f} MB, sha256 {digest[:16]}...)")
    version = importlib.metadata.version("rfdetr")
    print(f"rfdetr {version}, input {resolution}x{resolution}")
    if args.size != "small":
        print(
            f"note: the example model.yaml files expect rfdetr-small.onnx at 512x512; for --size "
            f"{args.size} set files.onnx to {target.name} and input.width/height to {resolution}."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
