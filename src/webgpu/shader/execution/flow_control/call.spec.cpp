// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/call.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,call",
    "\nFlow control tests for function calls.\n");

CTS_TEST(g, "call_basic")
    .desc("Test that flow control enters a called function")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                std::string("\n  ") + f.expect_order(0) + "\n" +
                "  f();\n" +
                "  " + f.expect_order(2) + "\n",
                std::string("\nfn f() {\n") +
                "  " + f.expect_order(1) + "\n" +
                "}"
            };
        });
    });

CTS_TEST(g, "call_nested")
    .desc("Test that flow control enters a nested function calls")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                std::string("\n  ") + f.expect_order(0) + "\n" +
                "  a();\n" +
                "  " + f.expect_order(6) + "\n",
                std::string("\nfn a() {\n") +
                "  " + f.expect_order(1) + "\n" +
                "  b();\n" +
                "  " + f.expect_order(5) + "\n" +
                "}\n"
                "fn b() {\n" +
                "  " + f.expect_order(2) + "\n" +
                "  c();\n" +
                "  " + f.expect_order(4) + "\n" +
                "}\n"
                "fn c() {\n" +
                "  " + f.expect_order(3) + "\n" +
                "}"
            };
        });
    });

CTS_TEST(g, "call_repeated")
    .desc("Test that flow control enters a nested function calls")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                std::string("\n  ") + f.expect_order(0) + "\n" +
                "  a();\n" +
                "  " + f.expect_order(10) + "\n",
                std::string("\nfn a() {\n") +
                "  " + f.expect_order(1) + "\n" +
                "  b();\n" +
                "  " + f.expect_order(5) + "\n" +
                "  b();\n" +
                "  " + f.expect_order(9) + "\n" +
                "}\n"
                "fn b() {\n" +
                "  " + f.expect_order(2, 6) + "\n" +
                "  c();\n" +
                "  " + f.expect_order(4, 8) + "\n" +
                "}\n"
                "fn c() {\n" +
                "  " + f.expect_order(3, 7) + "\n" +
                "}"
            };
        });
    });

CTS_TEST(g, "arg_eval")
    .desc("Test that arguments are evaluated left to right")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                std::string("\n  ") + f.expect_order(0) + "\n" +
                "  a(b(), c(), d());\n" +
                "  " + f.expect_order(5) + "\n",
                std::string("\nfn a(p1 : u32, p2 : u32, p3 : u32) {\n") +
                "  " + f.expect_order(4) + "\n" +
                "}\n"
                "fn b() -> u32 {\n" +
                "  " + f.expect_order(1) + "\n" +
                "  return 0;\n" +
                "}\n"
                "fn c() -> u32 {\n" +
                "  " + f.expect_order(2) + "\n" +
                "  return 0;\n" +
                "}\n"
                "fn d() -> u32 {\n" +
                "  " + f.expect_order(3) + "\n" +
                "  return 0;\n" +
                "}"
            };
        });
    });

CTS_TEST(g, "arg_eval_logical_and")
    .desc("Test that arguments are evaluated left to right")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string v1 = f.value(1);
            std::string v0 = f.value(0);
            return FlowControlWgsl{
                std::string("\n  ") + f.expect_order(0) + "\n" +
                "  a(b(" + v1 + ") && c());\n" +
                "  a(b(" + v0 + ") && c());\n" +
                "  " + f.expect_order(6) + "\n",
                std::string("\nfn a(p : bool) {\n") +
                "  " + f.expect_order(3, 5) + "\n" +
                "}\n"
                "fn b(x : i32) -> bool {\n" +
                "  " + f.expect_order(1, 4) + "\n" +
                "  return x == 1;\n" +
                "}\n"
                "fn c() -> bool {\n" +
                "  " + f.expect_order(2) + "\n" +
                "  return true;\n" +
                "}"
            };
        });
    });

CTS_TEST(g, "arg_eval_logical_or")
    .desc("Test that arguments are evaluated left to right")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string v1 = f.value(1);
            std::string v0 = f.value(0);
            return FlowControlWgsl{
                std::string("\n  ") + f.expect_order(0) + "\n" +
                "  a(b(" + v1 + ") || c());\n" +
                "  a(b(" + v0 + ") || c());\n" +
                "  " + f.expect_order(6) + "\n",
                std::string("\nfn a(p : bool) {\n") +
                "  " + f.expect_order(3, 5) + "\n" +
                "}\n"
                "fn b(x : i32) -> bool {\n" +
                "  " + f.expect_order(1, 4) + "\n" +
                "  return x == 0;\n" +
                "}\n"
                "fn c() -> bool {\n" +
                "  " + f.expect_order(2) + "\n" +
                "  return true;\n" +
                "}"
            };
        });
    });

CTS_TEST(g, "arg_eval_pointers")
    .desc("Test that arguments are evaluated left to right")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string v0 = f.value(0);
            return FlowControlWgsl{
                std::string("\n  var x : i32 = ") + v0 + ";\n" +
                "  " + f.expect_order(0) + "\n" +
                "  _ = c(&x);\n" +
                "  a(b(&x), c(&x));\n" +
                "  " + f.expect_order(5) + "\n",
                std::string("\nfn a(p1 : i32, p2 : i32) {\n") +
                "  " + f.expect_order(4) + "\n" +
                "}\n"
                "fn b(p : ptr<function, i32>) -> i32 {\n" +
                "  (*p)++;\n" +
                "  " + f.expect_order(2) + "\n" +
                "  return 0;\n" +
                "}\n"
                "fn c(p : ptr<function, i32>) -> i32 {\n" +
                "  if (*p == 1) {\n" +
                "    " + f.expect_order(3) + "\n" +
                "  } else {\n" +
                "    " + f.expect_order(1) + "\n" +
                "  }\n" +
                "  return 0;\n" +
                "}"
            };
        });
    });

} // namespace
