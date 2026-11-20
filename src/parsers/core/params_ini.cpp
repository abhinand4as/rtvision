/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Abhinand A S
 */

// params_ini.cpp - parser.ini reader (see params_ini.hpp). Own minimal INI parser, no deps.
#include "params_ini.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rtvision {

namespace {

std::string trim(const std::string &s) {
    const char *ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return "";
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

std::vector<std::string> split_list(const std::string &s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty())
            out.push_back(item);
    }
    return out;
}

bool parse_int(const std::string &s, long min, long max, int &out) {
    if (s.empty())
        return false;
    errno = 0;
    char *end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0' || v < min || v > max)
        return false;
    out = static_cast<int>(v);
    return true;
}

bool parse_bool(const std::string &s, bool &out) {
    if (s == "true" || s == "1" || s == "yes") {
        out = true;
        return true;
    }
    if (s == "false" || s == "0" || s == "no") {
        out = false;
        return true;
    }
    return false;
}

} // namespace

bool family_defaults(const std::string &family, BoxFormat &box, Activation &act) {
    if (family == "yolo_e2e" || family == "efficient_nms") {
        box = BoxFormat::XYXY_PX, act = Activation::NONE;
    } else if (family == "yolo_raw") {
        box = BoxFormat::CXCYWH_PX, act = Activation::NONE;
    } else if (family == "detr") {
        box = BoxFormat::CXCYWH_NORM, act = Activation::SIGMOID;
    } else {
        return false;
    }
    return true;
}

bool parse_parser_ini(const std::string &text, ParserConfig &out, std::string &error) {
    ParserConfig cfg;
    bool in_parser = false, saw_parser = false;
    bool box_set = false, act_set = false;
    BoxFormat box{};
    Activation act{};
    std::istringstream in(text);
    std::string raw;
    int lineno = 0;

    auto fail = [&](const std::string &msg) {
        error = "parser.ini line " + std::to_string(lineno) + ": " + msg;
        return false;
    };

    while (std::getline(in, raw)) {
        lineno++;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[') {
            if (line.back() != ']')
                return fail("malformed section header");
            const std::string sec = trim(line.substr(1, line.size() - 2));
            if (sec != "parser")
                return fail("unknown section [" + sec + "]");
            in_parser = saw_parser = true;
            continue;
        }
        if (!in_parser)
            return fail("key outside [parser]");
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            return fail("expected key=value");
        const std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        const size_t hash = val.find_first_of("#;");
        if (hash != std::string::npos)
            val = trim(val.substr(0, hash));

        if (key == "family") {
            cfg.family = val;
        } else if (key == "layers") {
            cfg.layers = split_list(val);
        } else if (key == "box_format") {
            if (!parse_box_format(val, box))
                return fail("invalid box_format '" + val + "'");
            box_set = true;
        } else if (key == "activation") {
            if (!parse_activation(val, act))
                return fail("invalid activation '" + val + "'");
            act_set = true;
        } else if (key == "has_background_class") {
            if (!parse_bool(val, cfg.has_background_class))
                return fail("invalid has_background_class '" + val + "'");
        } else if (key == "num_classes") {
            if (!parse_int(val, 0, 1000000, cfg.num_classes))
                return fail("invalid num_classes '" + val + "'");
        } else if (key == "allowed_class_ids") {
            cfg.allowed_class_ids.clear();
            for (const std::string &item : split_list(val)) {
                int id;
                if (!parse_int(item, 0, 1000000, id))
                    return fail("invalid class id '" + item + "' in allowed_class_ids");
                cfg.allowed_class_ids.push_back(id);
            }
        } else if (key == "topk") {
            if (!parse_int(val, 0, INT_MAX, cfg.topk))
                return fail("invalid topk '" + val + "'");
        } else {
            return fail("unknown key '" + key + "'");
        }
    }
    lineno = 0;
    if (!saw_parser)
        return fail("missing [parser] section");
    if (cfg.family.empty())
        return fail("missing family");
    BoxFormat dbox;
    Activation dact;
    if (!family_defaults(cfg.family, dbox, dact))
        return fail("unknown family '" + cfg.family + "'");
    cfg.box_format = box_set ? box : dbox;
    cfg.activation = act_set ? act : dact;
    out = cfg;
    return true;
}

bool load_parser_ini(const std::string &path, ParserConfig &out, std::string &error) {
    std::ifstream f(path);
    if (!f) {
        error = "cannot open parser.ini '" + path + "'";
        return false;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    return parse_parser_ini(buf.str(), out, error);
}

} // namespace rtvision
