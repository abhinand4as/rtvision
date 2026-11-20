/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// test_detr_nms.cpp - detr and efficient_nms decoders.
#include <cmath>

#include "test_harness.hpp"
#include "test_util.hpp"

using namespace perceptra;

static ParseParams detr_params(int C, Activation act, bool bg) {
    ParseParams p = tu::params(C);
    p.box_format = BoxFormat::CXCYWH_NORM;
    p.activation = act;
    p.has_background_class = bg;
    return p;
}

TEST(detr_sigmoid) {
    // 3 queries, 2 classes, logits.
    const std::vector<float> boxes = {0.5f, 0.5f, 0.25f, 0.5f, 0.2f, 0.2f,
                                      0.1f, 0.1f, 0.8f,  0.8f, 0.1f, 0.1f};
    const std::vector<float> logits = {3.0f,  -2.0f, // q0: class 0, sigmoid 0.953
                                       -5.0f, -4.0f, // q1: below threshold
                                       -1.0f, 1.0f}; // q2: class 1, sigmoid 0.731
    auto out = decode_detr({tu::view(boxes, {3, 4}), tu::view(logits, {3, 2})},
                           detr_params(2, Activation::SIGMOID, false));
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0].class_id, 0);
    CHECK_NEAR(out[0].score, 1.0 / (1.0 + std::exp(-3.0)), 1e-6);
    CHECK_NEAR(out[0].x1, 240, 1e-3); // (0.5-0.125)*640
    CHECK_NEAR(out[0].y1, 160, 1e-3); // (0.5-0.25)*640
    CHECK_NEAR(out[0].x2, 400, 1e-3);
    CHECK_NEAR(out[0].y2, 480, 1e-3);
    CHECK_EQ(out[1].class_id, 1);
}

TEST(detr_softmax_bg) {
    // 2 classes + background (last column). q0: background wins the softmax but class 1 is the
    // best non-background class with prob < threshold -> dropped. q1: class 0 dominates.
    const std::vector<float> boxes = {0.5f, 0.5f, 0.2f, 0.2f, 0.3f, 0.3f, 0.2f, 0.2f};
    const std::vector<float> logits = {0.0f, 1.0f, 5.0f, //
                                       6.0f, 0.0f, 0.0f};
    auto out = decode_detr({tu::view(boxes, {2, 4}), tu::view(logits, {2, 3})},
                           detr_params(2, Activation::SOFTMAX, true));
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0].class_id, 0);
    const double e = std::exp(6.0), z = e + 2.0;
    CHECK_NEAR(out[0].score, e / z, 1e-5);
    // Without the background flag the logits width must be C.
    CHECK_THROWS(decode_detr({tu::view(boxes, {2, 4}), tu::view(logits, {2, 3})},
                             detr_params(2, Activation::SOFTMAX, false)));
}

TEST(detr_box_formats) {
    // The same physical box (x 64..192, y 128..384 on 640x640) in all 4 formats.
    struct Case {
        BoxFormat fmt;
        std::vector<float> box;
    } cases[] = {
        {BoxFormat::XYXY_PX, {64, 128, 192, 384}},
        {BoxFormat::XYXY_NORM, {0.1f, 0.2f, 0.3f, 0.6f}},
        {BoxFormat::CXCYWH_PX, {128, 256, 128, 256}},
        {BoxFormat::CXCYWH_NORM, {0.2f, 0.4f, 0.2f, 0.4f}},
    };
    const std::vector<float> logits = {4.0f};
    for (const Case &c : cases) {
        ParseParams p = detr_params(1, Activation::SIGMOID, false);
        p.box_format = c.fmt;
        auto out = decode_detr({tu::view(c.box, {1, 4}), tu::view(logits, {1, 1})}, p);
        CHECK_EQ(out.size(), 1u);
        if (out.size() != 1)
            continue;
        CHECK_NEAR(out[0].x1, 64, 1e-3);
        CHECK_NEAR(out[0].y1, 128, 1e-3);
        CHECK_NEAR(out[0].x2, 192, 1e-3);
        CHECK_NEAR(out[0].y2, 384, 1e-3);
    }
}

TEST(detr_swapped_order_and_topk) {
    std::vector<float> boxes, logits;
    for (int i = 0; i < 10; i++) {
        boxes.insert(boxes.end(), {0.5f, 0.5f, 0.1f, 0.1f});
        logits.push_back(static_cast<float>(i)); // increasing scores
    }
    ParseParams p = detr_params(1, Activation::SIGMOID, false);
    p.class_threshold.assign(1, 0.0f); // not "= {0.0f}": GCC 13 -Warray-bounds false positive
    p.topk = 3;
    auto out = decode_detr({tu::view(logits, {10, 1}), tu::view(boxes, {10, 4})}, p);
    CHECK_EQ(out.size(), 3u);
    CHECK_NEAR(out[0].score, 1.0 / (1.0 + std::exp(-9.0)), 1e-6);
    CHECK(out[0].score >= out[1].score && out[1].score >= out[2].score);
}

static std::vector<TensorView> nms_tensors(const std::vector<float> &nd,
                                           const std::vector<float> &b, const std::vector<float> &s,
                                           const std::vector<float> &c, int m) {
    return {tu::view(nd, {1}), tu::view(b, {m, 4}), tu::view(s, {m}), tu::view(c, {m})};
}

TEST(effnms_numdets) {
    // M = 4 slots, num_dets = 2: rows 3 and 4 hold garbage that must be ignored.
    const std::vector<float> nd = {2};
    const std::vector<float> b = {10, 10, 50,  50,  100, 100, 200, 220,
                                  0,  0,  640, 640, 5,   5,   600, 600};
    const std::vector<float> s = {0.8f, 0.6f, 0.99f, 0.99f};
    const std::vector<float> c = {1, 0, 2, 2};
    auto out = decode_efficient_nms(nms_tensors(nd, b, s, c, 4), tu::params(3));
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0].class_id, 1);
    CHECK_NEAR(out[0].score, 0.8, 1e-6);
    CHECK_EQ(out[1].class_id, 0);
    // num_dets larger than M is capped; zero/negative num_dets -> nothing.
    const std::vector<float> big = {99}, zero = {0}, neg = {-3};
    CHECK_EQ(decode_efficient_nms(nms_tensors(big, b, s, c, 4), tu::params(3)).size(), 4u);
    CHECK_EQ(decode_efficient_nms(nms_tensors(zero, b, s, c, 4), tu::params(3)).size(), 0u);
    CHECK_EQ(decode_efficient_nms(nms_tensors(neg, b, s, c, 4), tu::params(3)).size(), 0u);
    CHECK_THROWS(decode_efficient_nms({tu::view(nd, {1})}, tu::params(3)));
}
