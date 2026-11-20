/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

// test_adapter.cpp - DeepStream adapter smoke tests (needs DeepStream/TensorRT headers only,
// no GPU). parser.ini is written to a temp file and PERCEPTRA_PARSER_CONFIG is set before the
// first parse call, because the adapter reads it once per process.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

#include "nvdsinfer_custom_impl.h"

#include "test_harness.hpp"

extern "C" bool NvDsInferParsePerceptra_YoloE2E(std::vector<NvDsInferLayerInfo> const &,
                                                NvDsInferNetworkInfo const &,
                                                NvDsInferParseDetectionParams const &,
                                                std::vector<NvDsInferParseObjectInfo> &);
extern "C" bool NvDsInferParsePerceptra_Detr(std::vector<NvDsInferLayerInfo> const &,
                                             NvDsInferNetworkInfo const &,
                                             NvDsInferParseDetectionParams const &,
                                             std::vector<NvDsInferParseObjectInfo> &);

static NvDsInferLayerInfo layer(const char *name, NvDsInferDataType type, void *buf,
                                std::vector<unsigned> dims) {
    NvDsInferLayerInfo l{};
    l.dataType = type;
    l.inferDims.numDims = static_cast<unsigned>(dims.size());
    unsigned n = 1;
    for (size_t i = 0; i < dims.size(); i++) {
        l.inferDims.d[i] = dims[i];
        n *= dims[i];
    }
    l.inferDims.numElements = n;
    l.layerName = name;
    l.buffer = buf;
    l.isInput = 0;
    return l;
}

static NvDsInferParseDetectionParams det_params(unsigned classes, float thr) {
    NvDsInferParseDetectionParams p;
    p.numClassesConfigured = classes;
    p.perClassPreclusterThreshold.assign(classes, thr);
    p.perClassPostclusterThreshold.assign(classes, thr);
    return p;
}

static const NvDsInferNetworkInfo kNet = {640, 640, 3};

TEST(adapter_yolo_e2e_by_name) {
    // Two outputs; the configured layer name selects the second one.
    float decoy[6] = {0, 0, 0, 0, 0, 0};
    float rows[12] = {10, 20, 110, 220, 0.9f, 2,  //
                      5,  5,  50,  50,  0.9f, 0}; // class 0 not in allow-list
    std::vector<NvDsInferLayerInfo> layers = {layer("decoy", FLOAT, decoy, {1, 6}),
                                              layer("output0", FLOAT, rows, {2, 6})};
    std::vector<NvDsInferParseObjectInfo> objs;
    CHECK(NvDsInferParsePerceptra_YoloE2E(layers, kNet, det_params(3, 0.5f), objs));
    CHECK_EQ(objs.size(), 1u);
    if (objs.size() == 1) {
        CHECK_EQ(objs[0].classId, 2u);
        CHECK_NEAR(objs[0].left, 10, 1e-6);
        CHECK_NEAR(objs[0].top, 20, 1e-6);
        CHECK_NEAR(objs[0].width, 100, 1e-6);
        CHECK_NEAR(objs[0].height, 200, 1e-6);
        CHECK_NEAR(objs[0].detectionConfidence, 0.9, 1e-6);
        CHECK_NEAR(objs[0].rotation_angle, 0, 0);
    }
}

TEST(adapter_threshold_from_nvinfer) {
    float rows[6] = {10, 20, 110, 220, 0.6f, 2};
    std::vector<NvDsInferLayerInfo> layers = {layer("output0", FLOAT, rows, {1, 6})};
    std::vector<NvDsInferParseObjectInfo> objs;
    CHECK(NvDsInferParsePerceptra_YoloE2E(layers, kNet, det_params(3, 0.7f), objs));
    CHECK_EQ(objs.size(), 0u);
}

TEST(adapter_shape_error_returns_false) {
    float bad[5] = {0, 0, 0, 0, 0};
    std::vector<NvDsInferLayerInfo> layers = {layer("output0", FLOAT, bad, {1, 5})};
    std::vector<NvDsInferParseObjectInfo> objs;
    CHECK(!NvDsInferParsePerceptra_YoloE2E(layers, kNet, det_params(3, 0.5f), objs));
    CHECK_EQ(objs.size(), 0u);
}

TEST(adapter_family_mismatch_returns_false) {
    float boxes[4] = {0.5f, 0.5f, 0.1f, 0.1f}, logits[3] = {5, 0, 0};
    std::vector<NvDsInferLayerInfo> layers = {layer("boxes", FLOAT, boxes, {1, 4}),
                                              layer("logits", FLOAT, logits, {1, 3})};
    std::vector<NvDsInferParseObjectInfo> objs;
    CHECK(!NvDsInferParsePerceptra_Detr(layers, kNet, det_params(3, 0.5f), objs));
}

TEST(adapter_int32_class_tensor) {
    // Integer tensors are converted (as for EfficientNMS outputs): use an INT32 [K,6] here.
    int32_t rows[6] = {10, 20, 110, 220, 1, 2}; // score 1 (int) passes
    std::vector<NvDsInferLayerInfo> layers = {layer("output0", INT32, rows, {1, 6})};
    std::vector<NvDsInferParseObjectInfo> objs;
    CHECK(NvDsInferParsePerceptra_YoloE2E(layers, kNet, det_params(3, 0.5f), objs));
    CHECK_EQ(objs.size(), 1u);
}

int main(int argc, char **argv) {
    char path[] = "/tmp/perceptra_parser_test_XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0)
        return 2;
    const char *ini = "[parser]\nfamily=yolo_e2e\nlayers=output0\nnum_classes=3\n"
                      "allowed_class_ids=1,2\ntopk=10\n";
    if (write(fd, ini, std::strlen(ini)) < 0)
        return 2;
    close(fd);
    setenv("PERCEPTRA_PARSER_CONFIG", path, 1);
    const int rc = th::run_all(argc, argv);
    unlink(path);
    return rc;
}
