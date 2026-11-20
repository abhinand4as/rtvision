/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// yolo_raw.cpp - anchor-free YOLO head without NMS (YOLOv8 / YOLO11, YOLO26 end2end=False):
// one tensor [4+C, A] (channel-first) or [A, 4+C]. Per anchor: best class score, box -> xyxy.
// NMS is left to nvinfer (cluster-mode=2). Own implementation of the tensor layout.
#include "detection.hpp"

#include <cmath>

namespace perceptra {

std::vector<Detection> decode_yolo_raw(const std::vector<TensorView> &t, const ParseParams &p) {
    if (t.size() != 1)
        throw ParseError("yolo_raw: expected 1 output tensor, got " + std::to_string(t.size()));
    if (p.num_classes <= 0)
        throw ParseError("yolo_raw: num_classes must be > 0");
    const std::vector<int> dims = squeeze_to_rank(t[0].dims, 2);
    if (dims.size() != 2)
        throw ParseError("yolo_raw: expected a 2-D tensor [4+C, A] or [A, 4+C]");
    const int ch = 4 + p.num_classes;
    bool channel_first;
    if (dims[0] == ch)
        channel_first = true; // also when both dims match: [4+C, A] is the common export
    else if (dims[1] == ch)
        channel_first = false;
    else
        throw ParseError("yolo_raw: no dimension equals 4 + num_classes = " + std::to_string(ch));
    const int anchors = channel_first ? dims[1] : dims[0];
    const float *v = t[0].data;
    std::vector<Detection> out;
    if (anchors <= 0)
        return out;
    if (v == nullptr)
        throw ParseError("yolo_raw: null tensor data");

    // Element (channel c, anchor a).
    auto at = [&](int c, int a) -> float {
        return channel_first ? v[static_cast<size_t>(c) * anchors + a]
                             : v[static_cast<size_t>(a) * ch + c];
    };
    const bool sig = p.activation == Activation::SIGMOID;
    for (int a = 0; a < anchors; a++) {
        int best = -1;
        float best_score = 0.0f;
        for (int c = 0; c < p.num_classes; c++) {
            float s = at(4 + c, a);
            if (sig)
                s = sigmoid(s);
            if (std::isfinite(s) && (best < 0 || s > best_score))
                best = c, best_score = s;
        }
        if (best < 0)
            continue;
        // Early reject below threshold: most anchors are background.
        const size_t bc = static_cast<size_t>(best);
        if (bc < p.class_threshold.size() && best_score < p.class_threshold[bc])
            continue;
        Detection d;
        to_xyxy_px(at(0, a), at(1, a), at(2, a), at(3, a), p.box_format, p.net_w, p.net_h, d.x1,
                   d.y1, d.x2, d.y2);
        d.score = best_score;
        d.class_id = best;
        out.push_back(d);
    }
    postprocess(out, p);
    return out;
}

} // namespace perceptra
