/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// test_yolo.cpp - yolo_e2e and yolo_raw decoders.
#include <cmath>

#include "test_harness.hpp"
#include "test_util.hpp"

using namespace rtvision;

TEST(e2e_basic) {
    // 3 rows -> 2 kept (one under threshold); output sorted by score.
    const std::vector<float> d = {
        10,  20,  110, 220, 0.90f, 0, //
        50,  60,  150, 160, 0.20f, 1, // below 0.5
        300, 300, 400, 420, 0.70f, 2  //
    };
    auto out = decode_yolo_e2e({tu::view(d, {3, 6})}, tu::params(3));
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0].class_id, 0);
    CHECK_NEAR(out[0].score, 0.90, 1e-6);
    CHECK_NEAR(out[0].x1, 10, 1e-6);
    CHECK_NEAR(out[0].y2, 220, 1e-6);
    CHECK_EQ(out[1].class_id, 2);
}

TEST(e2e_allowlist) {
    const std::vector<float> d = {10, 10, 100, 100, 0.9f, 0, 10, 10, 100, 100, 0.9f, 1};
    ParseParams p = tu::params(2);
    p.class_allowed = {false, true};
    auto out = decode_yolo_e2e({tu::view(d, {2, 6})}, p);
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0].class_id, 1);
}

TEST(e2e_clamp) {
    const std::vector<float> d = {
        -20, -30, 700,    650, 0.9f, 0, // out of image -> clamped to [0,640]
        100, 100, 100.5f, 200, 0.9f, 0, // width 0.5 px -> dropped
        650, 10,  700,    50,  0.9f, 0, // fully right of the image -> zero width -> dropped
    };
    auto out = decode_yolo_e2e({tu::view(d, {3, 6})}, tu::params(1));
    CHECK_EQ(out.size(), 1u);
    CHECK_NEAR(out[0].x1, 0, 1e-6);
    CHECK_NEAR(out[0].y1, 0, 1e-6);
    CHECK_NEAR(out[0].x2, 640, 1e-6);
    CHECK_NEAR(out[0].y2, 640, 1e-6);
}

TEST(e2e_leading_batch_dim_and_shape_error) {
    const std::vector<float> d = {10, 10, 100, 100, 0.9f, 0};
    CHECK_EQ(decode_yolo_e2e({tu::view(d, {1, 1, 6})}, tu::params(1)).size(), 1u); // batch dim
    CHECK_EQ(decode_yolo_e2e({tu::view(d, {1, 6})}, tu::params(1)).size(), 1u);    // K = 1
    CHECK_THROWS(decode_yolo_e2e({tu::view(d, {1, 6, 1})}, tu::params(1)));
    CHECK_THROWS(decode_yolo_e2e({}, tu::params(1)));
}

// Builds a yolo_raw tensor for C classes and A anchors; boxes as cx,cy,w,h px.
static std::vector<float> raw_tensor(int C, int A, bool channel_first,
                                     const std::vector<std::vector<float>> &anchor_rows) {
    const int ch = 4 + C;
    std::vector<float> t(static_cast<size_t>(ch * A), 0.0f);
    for (int a = 0; a < A; a++) {
        for (int c = 0; c < ch; c++) {
            const float v = a < (int)anchor_rows.size() ? anchor_rows[a][c] : 0.0f;
            if (channel_first)
                t[static_cast<size_t>(c * A + a)] = v;
            else
                t[static_cast<size_t>(a * ch + c)] = v;
        }
    }
    return t;
}

TEST(raw_layouts) {
    // Same detections from [84, 8400] and [8400, 84].
    const int C = 80, A = 8400;
    std::vector<std::vector<float>> rows(3, std::vector<float>(4 + C, 0.0f));
    rows[0][0] = 100, rows[0][1] = 100, rows[0][2] = 40, rows[0][3] = 80, rows[0][4 + 0] = 0.9f;
    rows[1][0] = 300, rows[1][1] = 200, rows[1][2] = 60, rows[1][3] = 60, rows[1][4 + 7] = 0.6f;
    rows[1][4 + 2] = 0.3f; // lower score for another class: argmax picks 7
    rows[2][0] = 500, rows[2][1] = 500, rows[2][2] = 10, rows[2][3] = 10, rows[2][4 + 1] = 0.1f;
    const auto cf = raw_tensor(C, A, true, rows);
    const auto cl = raw_tensor(C, A, false, rows);
    ParseParams p = tu::params(C);
    p.box_format = BoxFormat::CXCYWH_PX; // yolo_raw family default
    auto a = decode_yolo_raw({tu::view(cf, {84, 8400})}, p);
    auto b = decode_yolo_raw({tu::view(cl, {8400, 84})}, p);
    CHECK_EQ(a.size(), 2u);
    CHECK_EQ(b.size(), 2u);
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        CHECK_EQ(a[i].class_id, b[i].class_id);
        CHECK_NEAR(a[i].x1, b[i].x1, 1e-5);
        CHECK_NEAR(a[i].y1, b[i].y1, 1e-5);
        CHECK_NEAR(a[i].x2, b[i].x2, 1e-5);
        CHECK_NEAR(a[i].y2, b[i].y2, 1e-5);
        CHECK_NEAR(a[i].score, b[i].score, 1e-6);
    }
    CHECK_EQ(a[0].class_id, 0);
    CHECK_NEAR(a[0].x1, 80, 1e-5);
    CHECK_NEAR(a[0].y1, 60, 1e-5);
    CHECK_NEAR(a[0].x2, 120, 1e-5);
    CHECK_NEAR(a[0].y2, 140, 1e-5);
    CHECK_EQ(a[1].class_id, 7);
}

TEST(raw_shape_errors_and_sigmoid) {
    const std::vector<float> t(85 * 10, 0.0f);
    CHECK_THROWS(decode_yolo_raw({tu::view(t, {85, 10})}, tu::params(80))); // 4+80 != 85
    // Logits with activation=sigmoid: 2.0 -> 0.881 passes 0.5.
    std::vector<std::vector<float>> rows(1, std::vector<float>(6, -10.0f));
    rows[0][0] = 50, rows[0][1] = 50, rows[0][2] = 20, rows[0][3] = 20, rows[0][5] = 2.0f;
    const auto cf = raw_tensor(2, 4, true, rows);
    ParseParams p = tu::params(2);
    p.box_format = BoxFormat::CXCYWH_PX;
    p.activation = Activation::SIGMOID;
    auto out = decode_yolo_raw({tu::view(cf, {6, 4})}, p);
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0].class_id, 1);
    CHECK_NEAR(out[0].score, 1.0 / (1.0 + std::exp(-2.0)), 1e-6);
}
