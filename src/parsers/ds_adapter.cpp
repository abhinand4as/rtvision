/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// ds_adapter.cpp - thin DeepStream nvinfer adapter over the pure C++ decoders in core/.
//
// Exports (custom bbox parse functions, see nvdsinfer_custom_impl.h):
//   NvDsInferParsePerceptra_YoloE2E, _YoloRaw, _Detr, _EfficientNms
// Family parameters come from parser.ini, read ONCE per process (std::call_once) from the path
// in env PERCEPTRA_PARSER_CONFIG; nvinfer has no other channel for custom parser parameters.
// Without the env var, family defaults are used, layers are taken in output order and
// num_classes comes from nvinfer's num-detected-classes.
//
// Rules (docs/PARSERS.md): layers by name, else output order; thresholds from
// detectionParams.perClassPreclusterThreshold (driven by pgie.ini) so DeepStream and the parser
// agree; shape errors -> return false with a one-time error log per family; no exception
// escapes the C ABI.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "nvdsinfer_custom_impl.h"

#include "core/detection.hpp"
#include "core/params_ini.hpp"

#define PERCEPTRA_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

using namespace perceptra;

enum class Family { YOLO_E2E = 0, YOLO_RAW = 1, DETR = 2, EFFICIENT_NMS = 3 };
constexpr int kNumFamilies = 4;

const char *family_name(Family f) {
    switch (f) {
    case Family::YOLO_E2E:
        return "yolo_e2e";
    case Family::YOLO_RAW:
        return "yolo_raw";
    case Family::DETR:
        return "detr";
    case Family::EFFICIENT_NMS:
        return "efficient_nms";
    }
    return "?";
}

size_t expected_tensors(Family f) {
    switch (f) {
    case Family::DETR:
        return 2;
    case Family::EFFICIENT_NMS:
        return 4;
    default:
        return 1;
    }
}

void log_line(const char *level, const char *family, const std::string &msg) {
    std::fprintf(stderr, "level=%s comp=parser family=%s msg=\"%s\"\n", level, family, msg.c_str());
}

std::atomic<bool> g_error_logged[kNumFamilies];
std::atomic<bool> g_warn_logged[kNumFamilies];

void error_once(Family f, const std::string &msg) {
    if (!g_error_logged[static_cast<int>(f)].exchange(true))
        log_line("error", family_name(f), msg + " (further errors for this family suppressed)");
}

void warn_once(Family f, const std::string &msg) {
    if (!g_warn_logged[static_cast<int>(f)].exchange(true))
        log_line("warn", family_name(f), msg);
}

struct LoadedConfig {
    bool have_file = false;
    bool ok = true;
    ParserConfig cfg;
};

LoadedConfig g_config;
std::once_flag g_config_once;

const LoadedConfig &config() {
    std::call_once(g_config_once, [] {
        const char *path = std::getenv("PERCEPTRA_PARSER_CONFIG");
        if (path == nullptr || *path == '\0') {
            log_line("warn", "-",
                     "PERCEPTRA_PARSER_CONFIG not set: using family defaults, output layer order "
                     "and nvinfer num-detected-classes");
            return;
        }
        g_config.have_file = true;
        std::string err;
        if (!load_parser_ini(path, g_config.cfg, err)) {
            g_config.ok = false;
            log_line("error", "-", err);
        } else {
            log_line("info", g_config.cfg.family.c_str(),
                     std::string("loaded parser config ") + path);
        }
    });
    return g_config;
}

// Per-thread scratch reused across calls (no allocation in steady state).
struct Scratch {
    ParseParams params;
    std::vector<TensorView> views;
    std::vector<std::vector<float>> converted;
    std::vector<const NvDsInferLayerInfo *> layers;
};

bool select_layers(Family fam, const std::vector<NvDsInferLayerInfo> &all, const ParserConfig *cfg,
                   std::vector<const NvDsInferLayerInfo *> &out) {
    out.clear();
    const size_t need = expected_tensors(fam);
    if (cfg != nullptr && !cfg->layers.empty()) {
        for (const std::string &name : cfg->layers) {
            const NvDsInferLayerInfo *found = nullptr;
            for (const NvDsInferLayerInfo &l : all)
                if (!l.isInput && l.layerName != nullptr && name == l.layerName)
                    found = &l;
            if (found == nullptr) {
                warn_once(fam, "layer '" + name + "' not found; falling back to output order");
                out.clear();
                break;
            }
            out.push_back(found);
        }
        if (out.size() == need)
            return true;
        out.clear();
    }
    for (const NvDsInferLayerInfo &l : all)
        if (!l.isInput && out.size() < need)
            out.push_back(&l);
    if (out.size() != need) {
        error_once(fam, "expected " + std::to_string(need) + " output layers, model has " +
                            std::to_string(out.size()));
        return false;
    }
    return true;
}

bool to_view(Family fam, const NvDsInferLayerInfo &l, std::vector<float> &conv, TensorView &v) {
    const size_t n = l.inferDims.numElements;
    v.name = l.layerName ? l.layerName : "";
    v.dims.assign(l.inferDims.d, l.inferDims.d + l.inferDims.numDims);
    switch (l.dataType) {
    case FLOAT:
        v.data = static_cast<const float *>(l.buffer);
        return true;
    case INT32: {
        const int32_t *src = static_cast<const int32_t *>(l.buffer);
        conv.resize(n);
        for (size_t i = 0; i < n; i++)
            conv[i] = static_cast<float>(src[i]);
        v.data = conv.data();
        return true;
    }
    case INT64: {
        const int64_t *src = static_cast<const int64_t *>(l.buffer);
        conv.resize(n);
        for (size_t i = 0; i < n; i++)
            conv[i] = static_cast<float>(src[i]);
        v.data = conv.data();
        return true;
    }
    default:
        error_once(fam, "layer '" + v.name + "' has unsupported data type " +
                            std::to_string(static_cast<int>(l.dataType)) +
                            " (FLOAT, INT32, INT64 supported)");
        return false;
    }
}

bool parse(Family fam, const std::vector<NvDsInferLayerInfo> &outputs,
           const NvDsInferNetworkInfo &net, const NvDsInferParseDetectionParams &dp,
           std::vector<NvDsInferParseObjectInfo> &objects) {
    try {
        const LoadedConfig &lc = config();
        if (!lc.ok) {
            error_once(fam, "invalid parser.ini (see earlier error)");
            return false;
        }
        const ParserConfig *cfg = lc.have_file ? &lc.cfg : nullptr;
        if (cfg != nullptr && cfg->family != family_name(fam)) {
            error_once(fam, "parser.ini family '" + cfg->family +
                                "' does not match this parse function");
            return false;
        }

        thread_local Scratch s;
        ParseParams &p = s.params;
        p.net_w = static_cast<int>(net.width);
        p.net_h = static_cast<int>(net.height);
        p.num_classes = (cfg != nullptr && cfg->num_classes > 0)
                            ? cfg->num_classes
                            : static_cast<int>(dp.numClassesConfigured);
        const size_t nc = static_cast<size_t>(std::max(p.num_classes, 0));
        p.class_threshold.assign(nc, 0.0f);
        for (size_t c = 0; c < nc && c < dp.perClassPreclusterThreshold.size(); c++)
            p.class_threshold[c] = dp.perClassPreclusterThreshold[c];
        p.class_allowed.clear();
        if (cfg != nullptr && !cfg->allowed_class_ids.empty()) {
            p.class_allowed.assign(nc, false);
            for (int id : cfg->allowed_class_ids)
                if (id >= 0 && static_cast<size_t>(id) < nc)
                    p.class_allowed[static_cast<size_t>(id)] = true;
        }
        if (cfg != nullptr) {
            p.box_format = cfg->box_format;
            p.activation = cfg->activation;
            p.has_background_class = cfg->has_background_class;
            p.topk = cfg->topk;
        } else {
            family_defaults(family_name(fam), p.box_format, p.activation);
            p.has_background_class = false;
            p.topk = 300;
        }

        if (!select_layers(fam, outputs, cfg, s.layers))
            return false;
        s.views.resize(s.layers.size());
        if (s.converted.size() < s.layers.size())
            s.converted.resize(s.layers.size());
        for (size_t i = 0; i < s.layers.size(); i++)
            if (!to_view(fam, *s.layers[i], s.converted[i], s.views[i]))
                return false;

        std::vector<Detection> dets;
        switch (fam) {
        case Family::YOLO_E2E:
            dets = decode_yolo_e2e(s.views, p);
            break;
        case Family::YOLO_RAW:
            dets = decode_yolo_raw(s.views, p);
            break;
        case Family::DETR:
            dets = decode_detr(s.views, p);
            break;
        case Family::EFFICIENT_NMS:
            dets = decode_efficient_nms(s.views, p);
            break;
        }

        objects.reserve(objects.size() + dets.size());
        for (const Detection &d : dets) {
            // nvinfer contract: class ids < numClassesConfigured, confidence in [0, 1].
            if (static_cast<unsigned>(d.class_id) >= dp.numClassesConfigured)
                continue;
            NvDsInferParseObjectInfo o{}; // zero-init (incl. rotation_angle = axis-aligned)
            o.classId = static_cast<unsigned>(d.class_id);
            o.left = d.x1;
            o.top = d.y1;
            o.width = d.x2 - d.x1;
            o.height = d.y2 - d.y1;
            o.detectionConfidence = std::min(1.0f, std::max(0.0f, d.score));
            objects.push_back(o);
        }
        return true;
    } catch (const std::exception &e) {
        error_once(fam, e.what());
    } catch (...) {
        error_once(fam, "unknown exception");
    }
    return false;
}

} // namespace

#define PERCEPTRA_PARSE_ARGS                                                                       \
    std::vector<NvDsInferLayerInfo> const &outputLayersInfo,                                       \
        NvDsInferNetworkInfo const &networkInfo,                                                   \
        NvDsInferParseDetectionParams const &detectionParams,                                      \
        std::vector<NvDsInferParseObjectInfo> &objectList

PERCEPTRA_EXPORT bool NvDsInferParsePerceptra_YoloE2E(PERCEPTRA_PARSE_ARGS) {
    return parse(Family::YOLO_E2E, outputLayersInfo, networkInfo, detectionParams, objectList);
}

PERCEPTRA_EXPORT bool NvDsInferParsePerceptra_YoloRaw(PERCEPTRA_PARSE_ARGS) {
    return parse(Family::YOLO_RAW, outputLayersInfo, networkInfo, detectionParams, objectList);
}

PERCEPTRA_EXPORT bool NvDsInferParsePerceptra_Detr(PERCEPTRA_PARSE_ARGS) {
    return parse(Family::DETR, outputLayersInfo, networkInfo, detectionParams, objectList);
}

PERCEPTRA_EXPORT bool NvDsInferParsePerceptra_EfficientNms(PERCEPTRA_PARSE_ARGS) {
    return parse(Family::EFFICIENT_NMS, outputLayersInfo, networkInfo, detectionParams, objectList);
}

CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParsePerceptra_YoloE2E);
CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParsePerceptra_YoloRaw);
CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParsePerceptra_Detr);
CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParsePerceptra_EfficientNms);
