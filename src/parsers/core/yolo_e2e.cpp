/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// yolo_e2e.cpp - NMS-free end-to-end YOLO head: one tensor [K, 6] = x1, y1, x2, y2, score,
// class (e.g. YOLO26 default export, YOLOv10). Own implementation of the tensor layout.
#include "detection.hpp"

#include <cmath>

namespace rtvision {

std::vector<Detection> decode_yolo_e2e(const std::vector<TensorView> &t, const ParseParams &p) {
    if (t.size() != 1)
        throw ParseError("yolo_e2e: expected 1 output tensor, got " + std::to_string(t.size()));
    const std::vector<int> dims = squeeze_to_rank(t[0].dims, 2);
    if (dims.size() != 2 || dims[1] != 6)
        throw ParseError("yolo_e2e: expected shape [K, 6]");
    const int k = dims[0];
    const float *v = t[0].data;
    std::vector<Detection> out;
    if (k <= 0)
        return out;
    if (v == nullptr)
        throw ParseError("yolo_e2e: null tensor data");
    out.reserve(static_cast<size_t>(k));
    for (int i = 0; i < k; i++, v += 6) {
        Detection d;
        to_xyxy_px(v[0], v[1], v[2], v[3], p.box_format, p.net_w, p.net_h, d.x1, d.y1, d.x2, d.y2);
        d.score = p.activation == Activation::SIGMOID ? sigmoid(v[4]) : v[4];
        if (!float_to_class(v[5], d.class_id))
            continue;
        out.push_back(d);
    }
    postprocess(out, p);
    return out;
}

} // namespace rtvision
