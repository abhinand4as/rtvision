/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// test_util.hpp - helpers to build synthetic tensors and params for the decoder tests.
#pragma once

#include <vector>

#include "../core/detection.hpp"

namespace tu {

inline rtvision::TensorView view(const std::vector<float> &data, std::vector<int> dims,
                                 const char *name = "t") {
    rtvision::TensorView t;
    t.data = data.empty() ? nullptr : data.data();
    t.dims = std::move(dims);
    t.name = name;
    return t;
}

inline rtvision::ParseParams params(int num_classes, float thr = 0.5f, int net_w = 640,
                                    int net_h = 640) {
    rtvision::ParseParams p;
    p.net_w = net_w;
    p.net_h = net_h;
    p.num_classes = num_classes;
    p.class_threshold.assign(static_cast<size_t>(num_classes), thr);
    return p;
}

} // namespace tu
