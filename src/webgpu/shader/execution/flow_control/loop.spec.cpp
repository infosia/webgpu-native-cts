// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/loop.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,loop",
    "\nFlow control tests for loops.\n");

CTS_TEST(g, "loop_break")
    .desc("Test that flow control exits a loop when reaching a break statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  loop {\n"
                "    " + f.expect_order(1, 3, 5, 7) + "\n"
                "    if i == 3 {\n"
                "      break;\n"
                "    }\n"
                "    " + f.expect_order(2, 4, 6) + "\n"
                "    i++;\n"
                "  }\n"
                "  " + f.expect_order(8) + "\n";
        });
    });

CTS_TEST(g, "loop_continue")
    .desc("Test flow control for a loop continue statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  loop {\n"
                "    " + f.expect_order(1, 3, 5, 7, 8) + "\n"
                "    if i == 3 {\n"
                "      i++;\n"
                "      continue;\n"
                "      " + f.expect_not_reached() + "\n"
                "    }\n"
                "    " + f.expect_order(2, 4, 6, 9) + "\n"
                "    if i == 4 {\n"
                "      break;\n"
                "    }\n"
                "    i++;\n"
                "  }\n"
                "  " + f.expect_order(10) + "\n";
        });
    });

CTS_TEST(g, "loop_continuing_basic")
    .desc("Test basic flow control for a loop continuing block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  loop {\n"
                "    " + f.expect_order(1, 3, 5) + "\n"
                "    i++;\n"
                "\n"
                "    continuing {\n"
                "      " + f.expect_order(2, 4, 6) + "\n"
                "      break if i == 3;\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(7) + "\n";
        });
    });

CTS_TEST(g, "nested_loops")
    .desc("Test flow control for a loop nested in another loop")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  loop {\n"
                "    " + f.expect_order(1, 11, 21) + "\n"
                "    if i == " + f.value(6) + " {\n"
                "      " + f.expect_order(22) + "\n"
                "      break;\n"
                "      " + f.expect_not_reached() + "\n"
                "    }\n"
                "    " + f.expect_order(2, 12) + "\n"
                "    loop {\n"
                "      i++;\n"
                "      " + f.expect_order(3, 6, 9, 13, 16, 19) + "\n"
                "      if (i % " + f.value(3) + ") == 0 {\n"
                "        " + f.expect_order(10, 20) + "\n"
                "        break;\n"
                "        " + f.expect_not_reached() + "\n"
                "      }\n"
                "      " + f.expect_order(4, 7, 14, 17) + "\n"
                "      if (i & " + f.value(1) + ") == 0 {\n"
                "        " + f.expect_order(8, 15) + "\n"
                "        continue;\n"
                "        " + f.expect_not_reached() + "\n"
                "      }\n"
                "      " + f.expect_order(5, 18) + "\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(23) + "\n";
        });
    });

CTS_TEST(g, "loop_break_if_logical_and_condition")
    .desc("Test flow control for a loop with a logical and break if")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  loop {\n"
                "    " + f.expect_order(1, 4, 7) + "\n"
                "    continuing {\n"
                "      i++;\n"
                "      break if !(a(i) && b(i));\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(9) + "\n";
            std::string extra =
                std::string("fn a(i : i32) -> bool {\n") +
                "  " + f.expect_order(2, 5, 8) + "\n"
                "  return i < " + f.value(3) + ";\n"
                "}\n"
                "fn b(i : i32) -> bool {\n"
                "  " + f.expect_order(3, 6) + "\n"
                "  return i < " + f.value(5) + ";\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "loop_break_if_logical_or_condition")
    .desc("Test flow control for a loop with a logical or break if")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  loop {\n"
                "    " + f.expect_order(1, 3, 6, 9) + "\n"
                "    continuing {\n"
                "      i++;\n"
                "      break if !(a(i) || b(i));\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(12) + "\n";
            std::string extra =
                std::string("fn a(i : i32) -> bool {\n") +
                "  " + f.expect_order(2, 4, 7, 10) + "\n"
                "  return i < " + f.value(2) + ";\n"
                "}\n"
                "fn b(i : i32) -> bool {\n"
                "  " + f.expect_order(5, 8, 11) + "\n"
                "  return i < " + f.value(4) + ";\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

} // namespace
