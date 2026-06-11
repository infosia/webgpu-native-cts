// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/phony.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,phony",
    "\nFlow control tests for phony assignments.\n");

CTS_TEST(g, "phony_assign_call_basic")
    .desc("Test flow control for a phony assigned with a single function call")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                /* entrypoint */
                "\n" +
                f.expect_order(0) + "\n" +
                "  _ = f();\n" +
                f.expect_order(2) + "\n",
                /* extra */
                std::string("\nfn f() -> i32 {\n") +
                "  " + f.expect_order(1) + "\n" +
                "  return 1;\n" +
                "}\n"};
        });
    });

CTS_TEST(g, "phony_assign_call_must_use")
    .desc("Test flow control for a phony assigned with a single function call annotated with @must_use")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                /* entrypoint */
                "\n" +
                f.expect_order(0) + "\n" +
                "  _ = f();\n" +
                f.expect_order(2) + "\n",
                /* extra */
                std::string("\n@must_use\nfn f() -> i32 {\n") +
                "  " + f.expect_order(1) + "\n" +
                "  return 1;\n" +
                "}\n"};
        });
    });

CTS_TEST(g, "phony_assign_call_nested")
    .desc("Test flow control for a phony assigned with nested function calls")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                /* entrypoint */
                "\n" +
                f.expect_order(0) + "\n" +
                "_ = c(a(), b());\n" +
                f.expect_order(4) + "\n",
                /* extra */
                std::string("\nfn a() -> i32 {\n") +
                "  " + f.expect_order(1) + "\n" +
                "  return 1;\n" +
                "}\n" +
                "fn b() -> i32 {\n" +
                "  " + f.expect_order(2) + "\n" +
                "  return 1;\n" +
                "}\n" +
                "fn c(x : i32, y : i32) -> i32 {\n" +
                "  " + f.expect_order(3) + "\n" +
                "  return x + y;\n" +
                "}\n"};
        });
    });

CTS_TEST(g, "phony_assign_call_nested_must_use")
    .desc("Test flow control for a phony assigned with nested function calls, all annotated with @must_use")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                /* entrypoint */
                "\n" +
                f.expect_order(0) + "\n" +
                "_ = c(a(), b());\n" +
                f.expect_order(4) + "\n",
                /* extra */
                std::string("\n@must_use\nfn a() -> i32 {\n") +
                "  " + f.expect_order(1) + "\n" +
                "  return 1;\n" +
                "}\n" +
                "@must_use\nfn b() -> i32 {\n" +
                "  " + f.expect_order(2) + "\n" +
                "  return 1;\n" +
                "}\n" +
                "@must_use\nfn c(x : i32, y : i32) -> i32 {\n" +
                "  " + f.expect_order(3) + "\n" +
                "  return x + y;\n" +
                "}\n"};
        });
    });

CTS_TEST(g, "phony_assign_call_builtin")
    .desc("Test flow control for a phony assigned with a builtin call, with two function calls as arguments")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            return FlowControlWgsl{
                /* entrypoint */
                "\n" +
                f.expect_order(0) + "\n" +
                "_ = max(a(), b());\n" +
                f.expect_order(3) + "\n",
                /* extra */
                std::string("\nfn a() -> i32 {\n") +
                "  " + f.expect_order(1) + "\n" +
                "  return 1;\n" +
                "}\n" +
                "fn b() -> i32 {\n" +
                "  " + f.expect_order(2) + "\n" +
                "  return 1;\n" +
                "}\n"};
        });
    });

} // namespace
