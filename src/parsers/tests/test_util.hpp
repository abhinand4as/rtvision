/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

// test_util.hpp - helpers to build synthetic tensors and params for the decoder tests.
#pragma once

#include <vector>

#include "../core/detection.hpp"

namespace tu {

inline perceptra::TensorView view(const std::vector<float> &data, std::vector<int> dims,
                                  const char *name = "t") {
    perceptra::TensorView t;
    t.data = data.empty() ? nullptr : data.data();
    t.dims = std::move(dims);
    t.name = name;
    return t;
}

inline perceptra::ParseParams params(int num_classes, float thr = 0.5f, int net_w = 640,
                                     int net_h = 640) {
    perceptra::ParseParams p;
    p.net_w = net_w;
    p.net_h = net_h;
    p.num_classes = num_classes;
    p.class_threshold.assign(static_cast<size_t>(num_classes), thr);
    return p;
}

} // namespace tu
