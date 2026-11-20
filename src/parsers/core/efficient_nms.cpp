/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// efficient_nms.cpp - outputs of the TensorRT EfficientNMS plugin: num_dets [1], boxes [M, 4]
// (xyxy pixels by default), scores [M], classes [M]. Only the first num_dets rows are valid.
#include "detection.hpp"

#include <algorithm>
#include <cmath>

namespace perceptra {

std::vector<Detection> decode_efficient_nms(const std::vector<TensorView> &t,
                                            const ParseParams &p) {
    if (t.size() != 4)
        throw ParseError("efficient_nms: expected 4 output tensors "
                         "(num_dets, boxes, scores, classes), got " +
                         std::to_string(t.size()));
    const std::vector<int> nd = squeeze_to_rank(t[0].dims, 1);
    const std::vector<int> bd = squeeze_to_rank(t[1].dims, 2);
    const std::vector<int> sd = squeeze_to_rank(t[2].dims, 1);
    const std::vector<int> cd = squeeze_to_rank(t[3].dims, 1);
    if (element_count(nd) != 1)
        throw ParseError("efficient_nms: num_dets must have exactly 1 element");
    if (bd.size() != 2 || bd[1] != 4)
        throw ParseError("efficient_nms: boxes must have shape [M, 4]");
    const size_t m = static_cast<size_t>(std::max(bd[0], 0));
    if (element_count(sd) != m || element_count(cd) != m)
        throw ParseError("efficient_nms: scores and classes must have M elements");
    std::vector<Detection> out;
    if (t[0].data == nullptr)
        throw ParseError("efficient_nms: null num_dets");
    const float nf = t[0].data[0];
    if (!std::isfinite(nf) || nf <= 0.0f || m == 0)
        return out;
    const size_t n = std::min(m, static_cast<size_t>(nf));
    if (t[1].data == nullptr || t[2].data == nullptr || t[3].data == nullptr)
        throw ParseError("efficient_nms: null tensor data");
    out.reserve(n);
    for (size_t i = 0; i < n; i++) {
        const float *b = t[1].data + i * 4;
        Detection d;
        if (!float_to_class(t[3].data[i], d.class_id))
            continue;
        to_xyxy_px(b[0], b[1], b[2], b[3], p.box_format, p.net_w, p.net_h, d.x1, d.y1, d.x2, d.y2);
        d.score = p.activation == Activation::SIGMOID ? sigmoid(t[2].data[i]) : t[2].data[i];
        out.push_back(d);
    }
    postprocess(out, p);
    return out;
}

} // namespace perceptra
