<!-- SPDX-License-Identifier: Apache-2.0 -->
# rfdetr-s-coco

COCO object detection (80 classes) with **RF-DETR Small** from Roboflow. Code and weights are
**Apache-2.0**, so no licence acceptance is needed. This is the default model of the compose stack
and the integration test. `../rfdetr-s-people` uses the same files filtered to `person`.

**Licence note:** only the Nano, Small, Medium and Large RF-DETR weights are Apache-2.0. XLarge and
2XLarge are under Roboflow's PML-1.0 (commercial self-hosting needs a paid licence); the export
tool refuses them. The weights are trained on COCO (annotations CC-BY-4.0, images from Flickr under
their own licences).

## Produce the ONNX (once, on the host, no GPU needed)

```bash
tools/export/rfdetr/export.sh          # writes models/examples/rfdetr-s-coco/rfdetr-small.onnx
```

The ONNX (~115 MB) is never committed (`.gitignore`). Details and other sizes:
`tools/export/rfdetr/README.md`.

## Model facts (verified 2026-11-20, rfdetr 1.12.1)

| | |
|---|---|
| Input | `input` [B, 3, 512, 512], RGB, ImageNet mean/std, stretched (no letterbox) |
| Outputs | `dets` [B, 300, 4] cx, cy, w, h normalised; `labels` [B, 300, 91] logits (sigmoid) |
| Classes | logit index = COCO category id (1 = person, 3 = car); `labels.txt` has 91 lines, the 11 unused ids are `unused_<id>` and dropped by `classes.keep` |
| Family | `detr` (no NMS needed) |

nvinfer applies one scale factor to all channels, so `scale` uses the mean std (0.226). On a
sample frame this gives the same detections as rfdetr's own PyTorch inference (13 of 13 above 0.5,
boxes within 2 px).
