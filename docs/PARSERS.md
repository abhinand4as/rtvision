<!-- SPDX-License-Identifier: Apache-2.0 -->
# Parser library contract (`libperceptra_parsers.so`)

## Layering

```text
nvinfer ──► ds_adapter.cpp (DeepStream types) ──► core/*.cpp (plain C++17) ──► std::vector<Detection>
                 ▲ reads parser.ini once (PERCEPTRA_PARSER_CONFIG)
```

`core/` must compile and be unit-tested **without** DeepStream or CUDA. It is also intended for reuse by a future open-source backend.

## Core types (`core/detection.hpp`)

```cpp
namespace perceptra {
enum class BoxFormat { XYXY_PX, XYXY_NORM, CXCYWH_PX, CXCYWH_NORM };
enum class Activation { NONE, SIGMOID, SOFTMAX };

struct Detection { float x1, y1, x2, y2; float score; int class_id; };  // network-input pixels

struct TensorView { const float* data; std::vector<int> dims; std::string name; }; // no batch dim

struct ParseParams {
  int net_w, net_h, num_classes;
  std::vector<float> class_threshold;      // size num_classes
  std::vector<bool>  class_allowed;        // size num_classes (empty = all)
  BoxFormat box_format; Activation activation;
  bool has_background_class = false;
  int topk = 300;
};

std::vector<Detection> decode_yolo_e2e     (const std::vector<TensorView>&, const ParseParams&);
std::vector<Detection> decode_yolo_raw     (const std::vector<TensorView>&, const ParseParams&);
std::vector<Detection> decode_detr         (const std::vector<TensorView>&, const ParseParams&);
std::vector<Detection> decode_efficient_nms(const std::vector<TensorView>&, const ParseParams&);
}
```

## Families

| Family | Inputs (per frame, batch dim removed) | Decoding |
|---|---|---|
| `yolo_e2e` | `[K, 6]`: x1, y1, x2, y2, score, class | threshold, allow-list, clamp. **No NMS.** |
| `yolo_raw` | `[4+C, A]` **or** `[A, 4+C]` (pick the layout where one dim == 4 + num_classes; ties → the larger dim is A) | per anchor: argmax class score; cx, cy, w, h → xyxy. NMS is done by nvinfer (`cluster-mode=2`). |
| `detr` | boxes `[Q, 4]` + logits `[Q, C(+1)]` (layer order from `parser.ini`) | activation (sigmoid default, or softmax dropping the background column); best class per query; box format → px; threshold; top-k by score. |
| `efficient_nms` | `num_dets [1]`, `boxes [M, 4]` xyxy px, `scores [M]`, `classes [M]` | first `num_dets` entries; threshold; allow-list. |

Common post-steps (all families, in order):
1. Drop NaN or Inf values.
2. Drop `class_id` outside `[0, num_classes)`.
3. Apply the allow-list.
4. Apply the per-class threshold.
5. Clamp to `[0, net_w] × [0, net_h]`.
6. Drop width or height < 1 px.
7. Keep the top-k by score.

## DeepStream adapter

Exported symbols (each with `CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE`):

```cpp
extern "C" bool NvDsInferParsePerceptra_YoloE2E    (PARSE_ARGS);
extern "C" bool NvDsInferParsePerceptra_YoloRaw    (PARSE_ARGS);
extern "C" bool NvDsInferParsePerceptra_Detr       (PARSE_ARGS);
extern "C" bool NvDsInferParsePerceptra_EfficientNms(PARSE_ARGS);
// PARSE_ARGS = std::vector<NvDsInferLayerInfo> const&, NvDsInferNetworkInfo const&,
//              NvDsInferParseDetectionParams const&, std::vector<NvDsInferParseObjectInfo>&
```

Rules:
- Find layers **by name** from `parser.ini` (`layers=`); fall back to output order if the names are missing; return `false` with a one-time error log if shapes mismatch.
- Thresholds: use `detectionParams.perClassPreclusterThreshold` (driven by `pgie.ini`), so DeepStream and parser agree.
- Convert `Detection` → `NvDsInferParseObjectInfo{classId, left=x1, top=y1, width=x2-x1, height=y2-y1, detectionConfidence=score}`. nvinfer then maps from network to streammux coordinates (letterbox-aware when `maintain-aspect-ratio`/`symmetric-padding` are set).
- No exceptions may escape; catch all and return `false`.

## `parser.ini` (generated)

```ini
[parser]
family=yolo_e2e
layers=output0
box_format=xyxy_px
activation=none
has_background_class=false
num_classes=80
allowed_class_ids=0
topk=300
```

## Required tests (`src/parsers/tests/`)

| Test | Checks |
|---|---|
| e2e_basic | 3 rows → 2 kept (one under threshold) |
| e2e_allowlist | class not allowed is dropped |
| e2e_clamp | out-of-image box clamped; degenerate box dropped |
| raw_layouts | same detections from `[84, 8400]` and `[8400, 84]` |
| detr_sigmoid / detr_softmax_bg | activations and background-column handling |
| detr_box_formats | all 4 box formats convert correctly |
| effnms_numdets | only the first `num_dets` used |
| robustness | empty tensors, NaN scores, negative class ids |

## Verifying a new model (manual, documented in README)
1. `python -m perceptra_tools.onnx_inspect model.onnx` — prints output names and shapes, and suggests a family.
2. Run Perceptra with `publish.backend=stdout` and `outputs.file=annotated.mp4` on a sample clip.
3. Compare against the model vendor's own predictions on the same frames (box IoU > 0.9 for matched objects at the same threshold).
