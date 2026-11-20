/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

// common.cpp - post-steps and helpers shared by all decoders (see detection.hpp).
#include "detection.hpp"

#include <algorithm>
#include <cmath>

namespace perceptra {

float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

bool float_to_class(float v, int &out) {
    if (!std::isfinite(v) || std::fabs(v) > 1e6f)
        return false;
    out = static_cast<int>(std::lround(v));
    return true;
}

std::vector<int> squeeze_to_rank(const std::vector<int> &dims, size_t rank) {
    size_t i = 0;
    while (dims.size() - i > rank && dims[i] == 1)
        i++;
    return std::vector<int>(dims.begin() + static_cast<long>(i), dims.end());
}

size_t element_count(const std::vector<int> &dims) {
    if (dims.empty())
        return 0;
    size_t n = 1;
    for (int d : dims) {
        if (d < 0)
            return 0;
        n *= static_cast<size_t>(d);
    }
    return n;
}

void to_xyxy_px(float a, float b, float c, float d, BoxFormat fmt, int net_w, int net_h, float &x1,
                float &y1, float &x2, float &y2) {
    const float sx = static_cast<float>(net_w), sy = static_cast<float>(net_h);
    switch (fmt) {
    case BoxFormat::XYXY_PX:
        x1 = a, y1 = b, x2 = c, y2 = d;
        break;
    case BoxFormat::XYXY_NORM:
        x1 = a * sx, y1 = b * sy, x2 = c * sx, y2 = d * sy;
        break;
    case BoxFormat::CXCYWH_PX:
        x1 = a - c / 2, y1 = b - d / 2, x2 = a + c / 2, y2 = b + d / 2;
        break;
    case BoxFormat::CXCYWH_NORM:
        x1 = (a - c / 2) * sx, y1 = (b - d / 2) * sy;
        x2 = (a + c / 2) * sx, y2 = (b + d / 2) * sy;
        break;
    }
}

void postprocess(std::vector<Detection> &dets, const ParseParams &p) {
    const float max_x = static_cast<float>(p.net_w), max_y = static_cast<float>(p.net_h);
    auto keep = [&](Detection &d) {
        // 1. NaN / Inf
        if (!std::isfinite(d.x1) || !std::isfinite(d.y1) || !std::isfinite(d.x2) ||
            !std::isfinite(d.y2) || !std::isfinite(d.score))
            return false;
        // 2. class range
        if (d.class_id < 0 || d.class_id >= p.num_classes)
            return false;
        const size_t c = static_cast<size_t>(d.class_id);
        // 3. allow-list
        if (!p.class_allowed.empty() && (c >= p.class_allowed.size() || !p.class_allowed[c]))
            return false;
        // 4. per-class threshold
        const float thr = c < p.class_threshold.size() ? p.class_threshold[c] : 0.0f;
        if (d.score < thr)
            return false;
        // 5. clamp
        d.x1 = std::clamp(d.x1, 0.0f, max_x);
        d.x2 = std::clamp(d.x2, 0.0f, max_x);
        d.y1 = std::clamp(d.y1, 0.0f, max_y);
        d.y2 = std::clamp(d.y2, 0.0f, max_y);
        // 6. degenerate
        return (d.x2 - d.x1) >= 1.0f && (d.y2 - d.y1) >= 1.0f;
    };
    size_t w = 0;
    for (size_t r = 0; r < dets.size(); r++) {
        if (keep(dets[r]))
            dets[w++] = dets[r];
    }
    dets.resize(w);
    // 7. top-k by score (stable: equal scores keep decoder order)
    std::stable_sort(dets.begin(), dets.end(),
                     [](const Detection &a, const Detection &b) { return a.score > b.score; });
    if (p.topk > 0 && dets.size() > static_cast<size_t>(p.topk))
        dets.resize(static_cast<size_t>(p.topk));
}

bool parse_box_format(const std::string &s, BoxFormat &out) {
    if (s == "xyxy_px")
        out = BoxFormat::XYXY_PX;
    else if (s == "xyxy_norm")
        out = BoxFormat::XYXY_NORM;
    else if (s == "cxcywh_px")
        out = BoxFormat::CXCYWH_PX;
    else if (s == "cxcywh_norm")
        out = BoxFormat::CXCYWH_NORM;
    else
        return false;
    return true;
}

bool parse_activation(const std::string &s, Activation &out) {
    if (s == "none")
        out = Activation::NONE;
    else if (s == "sigmoid")
        out = Activation::SIGMOID;
    else if (s == "softmax")
        out = Activation::SOFTMAX;
    else
        return false;
    return true;
}

const char *to_string(BoxFormat f) {
    switch (f) {
    case BoxFormat::XYXY_PX:
        return "xyxy_px";
    case BoxFormat::XYXY_NORM:
        return "xyxy_norm";
    case BoxFormat::CXCYWH_PX:
        return "cxcywh_px";
    case BoxFormat::CXCYWH_NORM:
        return "cxcywh_norm";
    }
    return "?";
}

const char *to_string(Activation a) {
    switch (a) {
    case Activation::NONE:
        return "none";
    case Activation::SIGMOID:
        return "sigmoid";
    case Activation::SOFTMAX:
        return "softmax";
    }
    return "?";
}

} // namespace perceptra
