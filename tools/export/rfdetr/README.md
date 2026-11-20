<!-- SPDX-License-Identifier: Apache-2.0 -->
# RF-DETR → ONNX export

`export.sh` exports a pretrained RF-DETR detector to ONNX for the `detr` parser family. It runs on
the host (CPU is enough) in its own virtualenv and is never part of the Perceptra image.

```bash
tools/export/rfdetr/export.sh                 # Small -> models/examples/rfdetr-s-coco/rfdetr-small.onnx
tools/export/rfdetr/export.sh --size medium   # nano | small | medium | large
RFDETR_VENV=/path/to/venv tools/export/rfdetr/export.sh
```

What it does:
1. Creates `~/.venvs/rfdetr-export` (or `$RFDETR_VENV`) with PyTorch (CPU build) and
   `rfdetr[onnx]==1.12.1`, about 1.7 GB. Delete the directory to remove it.
2. Downloads the pretrained checkpoint to `~/.roboflow/models/` (Small: 386 MB).
3. Exports with a dynamic batch dimension and moves the ONNX into `--out-dir`.

Licences of the tooling (checked 2026-11-20): rfdetr, transformers, onnx, huggingface_hub
Apache-2.0; PyTorch BSD-3-Clause; onnxruntime, supervision MIT; certifi and tqdm MPL-2.0. None is
copyleft beyond file level, and none of it is shipped. **Only Nano/Small/Medium/Large weights are
Apache-2.0**; XLarge and 2XLarge (PML-1.0) are not offered.

Sizes other than Small have a different input resolution: copy `models/examples/rfdetr-s-coco`,
then set `files.onnx`, `input.width`/`input.height` (printed by the script) and `name`.
