/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// detr.cpp - DETR-style set prediction heads (RT-DETR, RF-DETR, D-FINE): boxes [Q, 4] and
// logits [Q, C] or [Q, C+1] (background as the LAST column). Activation sigmoid (default for the
// family), softmax (background column dropped after normalisation) or none.
#include "detection.hpp"

#include <algorithm>
#include <cmath>

namespace perceptra {

std::vector<Detection> decode_detr(const std::vector<TensorView> &t, const ParseParams &p) {
    if (t.size() != 2)
        throw ParseError("detr: expected 2 output tensors (boxes, logits), got " +
                         std::to_string(t.size()));
    if (p.num_classes <= 0)
        throw ParseError("detr: num_classes must be > 0");
    std::vector<int> bdims = squeeze_to_rank(t[0].dims, 2);
    std::vector<int> ldims = squeeze_to_rank(t[1].dims, 2);
    const TensorView *boxes = &t[0], *logits = &t[1];
    // Tolerate swapped order when it is unambiguous.
    if (bdims.size() == 2 && ldims.size() == 2 && bdims[1] != 4 && ldims[1] == 4) {
        std::swap(boxes, logits);
        std::swap(bdims, ldims);
    }
    if (bdims.size() != 2 || bdims[1] != 4)
        throw ParseError("detr: boxes must have shape [Q, 4]");
    const int cols = p.num_classes + (p.has_background_class ? 1 : 0);
    if (ldims.size() != 2 || ldims[1] != cols)
        throw ParseError("detr: logits must have shape [Q, " + std::to_string(cols) + "]");
    if (ldims[0] != bdims[0])
        throw ParseError("detr: boxes and logits disagree on the number of queries");
    const int q = bdims[0];
    std::vector<Detection> out;
    if (q <= 0)
        return out;
    if (boxes->data == nullptr || logits->data == nullptr)
        throw ParseError("detr: null tensor data");
    out.reserve(static_cast<size_t>(q));

    for (int i = 0; i < q; i++) {
        const float *row = logits->data + static_cast<size_t>(i) * cols;
        int best = -1;
        float best_score = 0.0f;
        if (p.activation == Activation::SOFTMAX) {
            float mx = -INFINITY;
            for (int c = 0; c < cols; c++)
                if (std::isfinite(row[c]))
                    mx = std::max(mx, row[c]);
            if (!std::isfinite(mx))
                continue;
            float sum = 0.0f;
            for (int c = 0; c < cols; c++)
                if (std::isfinite(row[c]))
                    sum += std::exp(row[c] - mx);
            for (int c = 0; c < p.num_classes; c++) { // background column (last) skipped
                if (!std::isfinite(row[c]))
                    continue;
                const float s = std::exp(row[c] - mx) / sum;
                if (best < 0 || s > best_score)
                    best = c, best_score = s;
            }
        } else {
            for (int c = 0; c < p.num_classes; c++) {
                float s = p.activation == Activation::SIGMOID ? sigmoid(row[c]) : row[c];
                if (std::isfinite(s) && (best < 0 || s > best_score))
                    best = c, best_score = s;
            }
        }
        if (best < 0)
            continue;
        const float *b = boxes->data + static_cast<size_t>(i) * 4;
        Detection d;
        to_xyxy_px(b[0], b[1], b[2], b[3], p.box_format, p.net_w, p.net_h, d.x1, d.y1, d.x2, d.y2);
        d.score = best_score;
        d.class_id = best;
        out.push_back(d);
    }
    postprocess(out, p);
    return out;
}

} // namespace perceptra
