/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// detection.hpp - core types and decoders of the RTVision parser library (docs/PARSERS.md).
//
// Pure C++17: no DeepStream, TensorRT or CUDA includes, so the decoders are unit-tested in CI
// without a GPU and can be reused by a future open-source backend.
//
// Decoders take per-frame output tensors (batch dimension removed) plus ParseParams and return
// detections in network-input pixels after the common post-steps (docs/PARSERS.md):
//   1. drop NaN/Inf  2. drop class ids outside [0, num_classes)  3. allow-list
//   4. per-class threshold (score >= threshold)  5. clamp to [0, net_w] x [0, net_h]
//   6. drop boxes with width or height < 1 px  7. keep the top-k by score
// Shape mismatches throw rtvision::ParseError; the DeepStream adapter catches every exception.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace rtvision {

enum class BoxFormat { XYXY_PX, XYXY_NORM, CXCYWH_PX, CXCYWH_NORM };
enum class Activation { NONE, SIGMOID, SOFTMAX };

struct Detection {
    float x1, y1, x2, y2; // network-input pixels
    float score;
    int class_id;
};

// One output tensor of one frame. dims exclude the batch dimension; leading 1-sized dims are
// beyond the family's rank are ignored (e.g. [1, 84, 8400] is treated as [84, 8400]).
struct TensorView {
    const float *data = nullptr;
    std::vector<int> dims;
    std::string name;
};

struct ParseParams {
    int net_w = 0, net_h = 0, num_classes = 0;
    std::vector<float> class_threshold; // size num_classes; empty = threshold 0 for all
    std::vector<bool> class_allowed;    // size num_classes; empty = all allowed
    BoxFormat box_format = BoxFormat::XYXY_PX;
    Activation activation = Activation::NONE;
    bool has_background_class = false; // detr: the LAST logit column is background
    int topk = 300;                    // <= 0 = unlimited
};

class ParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// yolo_e2e: [K, 6] = x1, y1, x2, y2, score, class. No NMS. Activation: NONE or SIGMOID (score).
std::vector<Detection> decode_yolo_e2e(const std::vector<TensorView> &, const ParseParams &);
// yolo_raw: [4+C, A] or [A, 4+C] = box (cx, cy, w, h by default) + C class scores per anchor.
// The layout is chosen by which dim equals 4 + num_classes (if both do, the first dim is 4+C).
// NMS is done afterwards by nvinfer (cluster-mode=2).
std::vector<Detection> decode_yolo_raw(const std::vector<TensorView> &, const ParseParams &);
// detr: boxes [Q, 4] + logits [Q, C] (or [Q, C+1] with has_background_class), in that order.
std::vector<Detection> decode_detr(const std::vector<TensorView> &, const ParseParams &);
// efficient_nms: num_dets [1], boxes [M, 4], scores [M], classes [M] (TensorRT EfficientNMS
// outputs; integer tensors must be converted to float by the caller).
std::vector<Detection> decode_efficient_nms(const std::vector<TensorView> &, const ParseParams &);

// ---- helpers shared by the decoders (exposed for tests and the adapter) ----

// Applies the common post-steps 1-7 in place.
void postprocess(std::vector<Detection> &dets, const ParseParams &p);

// Converts one box in `fmt` to xyxy network pixels.
void to_xyxy_px(float a, float b, float c, float d, BoxFormat fmt, int net_w, int net_h, float &x1,
                float &y1, float &x2, float &y2);

float sigmoid(float x);

// Rounds a float class value to an int. Returns false for NaN/Inf or |v| > 1e6 (the caller drops
// the row); negative/out-of-range ids are dropped later by postprocess().
bool float_to_class(float v, int &out);

// Removes leading 1-sized dims while the tensor has more than `rank` dims (so [1, 1, 6] -> [1, 6]
// for rank 2, but a real single-row [1, 6] is kept).
std::vector<int> squeeze_to_rank(const std::vector<int> &dims, size_t rank);
size_t element_count(const std::vector<int> &dims);

bool parse_box_format(const std::string &s, BoxFormat &out);
bool parse_activation(const std::string &s, Activation &out);
const char *to_string(BoxFormat f);
const char *to_string(Activation a);

} // namespace rtvision
