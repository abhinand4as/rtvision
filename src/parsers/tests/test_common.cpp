/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// test_common.cpp - robustness of all families, post-steps and parser.ini.
#include <cmath>
#include <limits>

#include "../core/params_ini.hpp"
#include "test_harness.hpp"
#include "test_util.hpp"

using namespace perceptra;

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const float kInf = std::numeric_limits<float>::infinity();

TEST(robustness) {
    // Empty tensors.
    const std::vector<float> none;
    CHECK_EQ(decode_yolo_e2e({tu::view(none, {0, 6})}, tu::params(2)).size(), 0u);
    CHECK_EQ(decode_yolo_raw({tu::view(none, {6, 0})}, tu::params(2)).size(), 0u);
    CHECK_EQ(decode_detr({tu::view(none, {0, 4}), tu::view(none, {0, 2})}, tu::params(2)).size(),
             0u);
    const std::vector<float> zero = {0};
    CHECK_EQ(decode_efficient_nms({tu::view(zero, {1}), tu::view(none, {0, 4}), tu::view(none, {0}),
                                   tu::view(none, {0})},
                                  tu::params(2))
                 .size(),
             0u);

    // NaN / Inf scores and boxes, negative / out-of-range / NaN class ids: only row 0 survives.
    const std::vector<float> d = {
        10,   10, 100, 100, 0.9f, 0,    // ok
        10,   10, 100, 100, kNaN, 0,    // NaN score
        10,   10, 100, 100, kInf, 1,    // Inf score
        kNaN, 10, 100, 100, 0.9f, 1,    // NaN box
        10,   10, 100, 100, 0.9f, -1,   // negative class
        10,   10, 100, 100, 0.9f, 2,    // class == num_classes
        10,   10, 100, 100, 0.9f, kNaN, // NaN class
        10,   10, 100, 100, 0.9f, 3e9f, // absurd class
    };
    auto out = decode_yolo_e2e({tu::view(d, {8, 6})}, tu::params(2));
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(out[0].class_id, 0);

    // All below threshold.
    const std::vector<float> low = {10, 10, 100, 100, 0.1f, 0, 10, 10, 100, 100, 0.49f, 1};
    CHECK_EQ(decode_yolo_e2e({tu::view(low, {2, 6})}, tu::params(2)).size(), 0u);

    // NaN class scores in yolo_raw and detr are ignored, not chosen.
    const std::vector<float> raw = {50, 50, 20, 20, kNaN, 0.8f}; // [A=1, 4+C=6]
    auto r = decode_yolo_raw({tu::view(raw, {1, 6})}, [] {
        auto p = tu::params(2);
        p.box_format = BoxFormat::CXCYWH_PX;
        return p;
    }());
    CHECK_EQ(r.size(), 1u);
    CHECK_EQ(r[0].class_id, 1);
}

TEST(per_class_threshold_and_topk) {
    const std::vector<float> d = {10, 10, 100, 100, 0.45f, 0, //
                                  10, 10, 100, 100, 0.45f, 1, //
                                  10, 10, 100, 100, 0.95f, 1};
    ParseParams p = tu::params(2);
    p.class_threshold = {0.4f, 0.5f}; // class 0 passes at 0.45, class 1 does not
    auto out = decode_yolo_e2e({tu::view(d, {3, 6})}, p);
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0].class_id, 1); // 0.95 first
    CHECK_EQ(out[1].class_id, 0);
    p.topk = 1;
    CHECK_EQ(decode_yolo_e2e({tu::view(d, {3, 6})}, p).size(), 1u);
    p.topk = 0; // unlimited
    CHECK_EQ(decode_yolo_e2e({tu::view(d, {3, 6})}, p).size(), 2u);
}

TEST(params_ini_full) {
    ParserConfig c;
    std::string err;
    const char *ini = "# generated\n[parser]\nfamily=detr\nlayers=boxes, logits\n"
                      "activation=softmax ; inline comment\nhas_background_class=true\n"
                      "num_classes=80\nallowed_class_ids=0, 2,5\ntopk=100\n";
    CHECK(parse_parser_ini(ini, c, err));
    CHECK(c.family == "detr");
    CHECK_EQ(c.layers.size(), 2u);
    CHECK(c.layers[1] == "logits");
    CHECK(c.box_format == BoxFormat::CXCYWH_NORM); // family default
    CHECK(c.activation == Activation::SOFTMAX);    // explicit
    CHECK(c.has_background_class);
    CHECK_EQ(c.num_classes, 80);
    CHECK_EQ(c.allowed_class_ids.size(), 3u);
    CHECK_EQ(c.allowed_class_ids[2], 5);
    CHECK_EQ(c.topk, 100);
}

TEST(params_ini_defaults_and_errors) {
    ParserConfig c;
    std::string err;
    CHECK(parse_parser_ini("[parser]\nfamily=yolo_raw\n", c, err));
    CHECK(c.box_format == BoxFormat::CXCYWH_PX);
    CHECK(c.activation == Activation::NONE);
    CHECK(c.allowed_class_ids.empty());
    CHECK_EQ(c.topk, 300);
    const char *bad[] = {
        "",                                             // no section
        "[parser]\n",                                   // no family
        "[parser]\nfamily=yolo_seg\n",                  // unknown family
        "[parser]\nfamily=detr\nbox_format=xywh\n",     // bad enum
        "[parser]\nfamily=detr\nfoo=1\n",               // unknown key
        "[other]\nfamily=detr\n",                       // unknown section
        "[parser]\nfamily=detr\nnum_classes=-1\n",      // negative
        "[parser]\nfamily=detr\nallowed_class_ids=a\n", // not an int
        "family=detr\n",                                // key outside section
    };
    for (const char *b : bad) {
        err.clear();
        CHECK(!parse_parser_ini(b, c, err));
        CHECK(!err.empty());
    }
    CHECK(!load_parser_ini("/nonexistent/parser.ini", c, err));
}
