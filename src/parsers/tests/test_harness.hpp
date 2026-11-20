/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 <PROJECT_OWNER>
 */

// test_harness.hpp - tiny header-only test harness (own code, no third-party framework).
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); CHECK_THROWS(expr); }
//
// Run all tests: ./parsers_core_test        Run some: ./parsers_core_test e2e_ raw_layouts
// Exit code 0 when every selected test passes.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace th {

struct Case {
    const char *name;
    std::function<void()> fn;
};

inline std::vector<Case> &registry() {
    static std::vector<Case> r;
    return r;
}

inline int &failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char *name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void fail(const char *file, int line, const std::string &msg) {
    std::printf("    %s:%d: %s\n", file, line, msg.c_str());
    failures()++;
}

inline int run_all(int argc, char **argv) {
    int ran = 0, failed = 0;
    for (const Case &c : registry()) {
        bool selected = argc <= 1;
        for (int i = 1; i < argc; i++)
            if (std::strncmp(c.name, argv[i], std::strlen(argv[i])) == 0)
                selected = true;
        if (!selected)
            continue;
        const int before = failures();
        try {
            c.fn();
        } catch (const std::exception &e) {
            fail(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
        }
        ran++;
        const bool ok = failures() == before;
        failed += ok ? 0 : 1;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", c.name);
    }
    std::printf("%d tests, %d failed\n", ran, failed);
    return (failed == 0 && ran > 0) ? 0 : 1;
}

} // namespace th

#define TH_CAT2(a, b) a##b
#define TH_CAT(a, b) TH_CAT2(a, b)
#define TEST(name)                                                                                 \
    static void test_##name();                                                                     \
    static th::Registrar TH_CAT(th_reg_, name)(#name, test_##name);                                \
    static void test_##name()

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond))                                                                               \
            th::fail(__FILE__, __LINE__, "CHECK(" #cond ") failed");                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        const auto th_a = (a);                                                                     \
        const auto th_b = (b);                                                                     \
        if (!(th_a == th_b))                                                                       \
            th::fail(__FILE__, __LINE__,                                                           \
                     "CHECK_EQ(" #a ", " #b ") failed: " + std::to_string(th_a) +                  \
                         " != " + std::to_string(th_b));                                           \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double th_a = (a), th_b = (b);                                                       \
        if (!(std::fabs(th_a - th_b) <= (eps)))                                                    \
            th::fail(__FILE__, __LINE__,                                                           \
                     "CHECK_NEAR(" #a ", " #b ") failed: " + std::to_string(th_a) + " vs " +       \
                         std::to_string(th_b));                                                    \
    } while (0)

#define CHECK_THROWS(expr)                                                                         \
    do {                                                                                           \
        bool th_threw = false;                                                                     \
        try {                                                                                      \
            (void)(expr);                                                                          \
        } catch (...) {                                                                            \
            th_threw = true;                                                                       \
        }                                                                                          \
        if (!th_threw)                                                                             \
            th::fail(__FILE__, __LINE__, "CHECK_THROWS(" #expr ") did not throw");                 \
    } while (0)
