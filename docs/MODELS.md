<!-- SPDX-License-Identifier: Apache-2.0 -->
# Bringing your own model

RTVision runs any ONNX detector whose output matches one of four **output families**. You
describe the model in a `model.yaml`; `rtvision-run` validates it, generates the DeepStream
configs, builds and caches the TensorRT engine, and decodes the outputs with RTVision's own parser
library. No C/C++ changes are needed.

The RF-DETR example (`models/examples/rfdetr-s-coco`) was onboarded exactly this way; its README
records the facts found in each step.

## 1. Check the licence first

Every `model.yaml` declares the SPDX id of the **weights** (`license.spdx`). `rtvision-run` starts
models under Apache-2.0, BSD-2-Clause, BSD-3-Clause, CC-BY-4.0, CC0-1.0 or MIT. Any other licence
(e.g. AGPL-3.0 for Ultralytics YOLO, NVIDIA's model licence, non-commercial research licences)
stops the service with exit code 3 until you set `RTVISION_ACCEPT_MODEL_LICENSE=<that spdx id>`.
Also check the training data's terms. See `docs/LICENSING.md` §5 for permissive model options.

## 2. Export to ONNX

Export with a **dynamic batch dimension**: RTVision batches one frame per stream, up to
`service.max_streams`. A fixed batch smaller than that fails at engine build (and
`onnx_inspect` warns about it).

The network must have **one image input** `[B, 3, H, W]`. Exports that bundle post-processing with
a second input (e.g. the official RT-DETR / D-FINE / DEIM exports take `orig_target_sizes`) do not
fit nvinfer: export the network without the post-processor so it outputs raw boxes and logits.

## 3. Inspect the outputs

```bash
.venv/bin/python -m rtvision_tools.onnx_inspect path/to/model.onnx
```

It prints the input and output names and shapes and suggests a family. If the exporter left
dimensions symbolic (e.g. `Concatdets_dim_1`), the suggestion is `unknown`: run the model once
(e.g. with `onnxruntime`) to see the real shapes.

| Family | Outputs (batch dim removed) | Typical models |
|---|---|---|
| `yolo_e2e` | one `[K, 6]`: x1, y1, x2, y2, score, class (network-input pixels) | YOLOv10, YOLO26 default export |
| `yolo_raw` | one `[4+C, A]` or `[A, 4+C]`: cx, cy, w, h + class scores; NMS by nvinfer | YOLOv8 / YOLO11 |
| `detr` | boxes `[Q, 4]` + logits `[Q, C]` (layer order in `output.layers`) | RT-DETR, RF-DETR, D-FINE, DEIM (raw head) |
| `efficient_nms` | `num_dets`, `boxes [M,4]`, `scores [M]`, `classes [M]` | TensorRT EfficientNMS plugin exports |

Details and edge cases: `docs/PARSERS.md`. A head with an objectness column (YOLOX, YOLOv5) is not
supported yet.

## 4. Find the preprocessing

Look it up in the model's own inference code, not a guess:

| Question | `model.yaml` |
|---|---|
| Input size | `input.width`, `input.height` |
| RGB or BGR | `input.color` |
| Normalisation `(pixel - mean) / std` | `input.mean` = mean × 255 per channel, `input.scale` = 1 / (255 × std) |
| Letterbox or stretch | `input.resize`: `letterbox_center`, `letterbox_topleft` or `stretch` |

nvinfer has a **single** scale factor for all channels. For per-channel std (ImageNet:
0.229, 0.224, 0.225) use the mean std (0.226 → `scale: 0.017352806`) and check the effect in step 7.

## 5. Write `model.yaml` and `labels.txt`

```bash
mkdir models/examples/my-model
cp path/to/model.onnx models/examples/my-model/        # never committed (.gitignore)
```

Start from the closest example (`rfdetr-s-coco` for DETR heads) and set `name`, `license`,
`files`, `input`, `output` and `classes`. `labels.txt` has one label per line; **line index =
class id** the network outputs. If the class ids have gaps (COCO category ids in RF-DETR: 91
logits, 80 classes), fill the gaps with placeholder labels and drop them with `classes.keep`.

`classes.keep` filters labels (empty = all); `classes.thresholds` sets the default and per-class
confidence thresholds. The full field reference is in `docs/CONFIGURATION.md`.

## 6. Validate without a GPU

```bash
.venv/bin/rtvision-run --config config/rtvision.example.yaml \
    --models-dir models/examples --model my-model \
    --dry-run --no-check-files --out-dir /tmp/rtvision-check
cat /tmp/rtvision-check/pgie.ini /tmp/rtvision-check/parser.ini
```

`--no-check-files` skips the checks for files that only exist inside the image (DeepStream
tracker configs); the model's ONNX is still loaded and its outputs are checked against
`output.family`, `output.layers` and `num_classes`. Errors name the file and field (`model.output.num_classes: 80 but the labels file has 91 lines`).
Check `num-detected-classes`, `net-scale-factor`, `offsets` and `filter-out-class-ids` in
`pgie.ini`.

## 7. Verify on the GPU

```bash
RTVISION_MODEL=my-model docker compose -f deploy/compose/compose.yaml up -d
until curl -sf localhost:9464/ready; do sleep 3; done
```

Easiest with the web UI: add `-f deploy/compose/compose.ui.yaml`, create test cameras from your own
videos on the *Replicator* page and look at the boxes on the *Live* page ([UI.md](UI.md)).

The first start builds the engine (about a minute for small models) into the `rtvision_engines`
volume. Add the sample stream (README quick start, step 4) and read the events: the labels,
confidences and box positions must be plausible. For a quantitative check, run the same frame
through the model's reference inference and compare the detections; for RF-DETR Small the
pipeline matched the reference within 2 px.

To run the full integration test against your model:

```bash
RTVISION_MODEL=my-model tests/integration/run_all.sh
```

## 8. Swap models without restarting

Set `inference.hot_swap_file` and write the path of a new engine into it (see "Model hot swap" in
`docs/CONFIGURATION.md`). The swap keeps all streams running. The new engine must have the same
outputs and classes as the running model.
