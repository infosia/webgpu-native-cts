// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/while.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,while",
    "\nFlow control tests for while-loops.\n");

CTS_TEST(g, "while_basic")
    .desc("Test that flow control executes a while-loop body the correct number of times")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  while (i < " + f.value(5) + ") {\n"
                "    " + f.expect_order(1, 2, 3, 4, 5) + "\n"
                "    i++;\n"
                "  }\n"
                "  " + f.expect_order(6) + "\n";
        });
    });

CTS_TEST(g, "while_break")
    .desc("Test that flow control exits a while-loop when reaching a break statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  while (i < " + f.value(5) + ") {\n"
                "    " + f.expect_order(1, 3, 5, 7) + "\n"
                "    if (i == 3) {\n"
                "      break;\n"
                "      " + f.expect_not_reached() + "\n"
                "    }\n"
                "    " + f.expect_order(2, 4, 6) + "\n"
                "    i++;\n"
                "  }\n"
                "  " + f.expect_order(8) + "\n";
        });
    });

CTS_TEST(g, "while_continue")
    .desc("Test flow control for a while-loop continue statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  while (i < " + f.value(5) + ") {\n"
                "    " + f.expect_order(1, 3, 5, 7, 8) + "\n"
                "    if (i == 3) {\n"
                "      i++;\n"
                "      continue;\n"
                "      " + f.expect_not_reached() + "\n"
                "    }\n"
                "    " + f.expect_order(2, 4, 6, 9) + "\n"
                "    i++;\n"
                "  }\n"
                "  " + f.expect_order(10) + "\n";
        });
    });

CTS_TEST(g, "while_nested_break")
    .desc("Test that flow control exits a nested while-loop when reaching a break statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  while (i < " + f.value(3) + ") {\n"
                "    " + f.expect_order(1, 5, 11) + "\n"
                "    i++;\n"
                "    var j = " + f.value(0) + ";\n"
                "    while (j < i) {\n"
                "      " + f.expect_order(2, 6, 8, 12) + "\n"
                "      j++;\n"
                "      if ((i+j) & 2) == 0 {\n"
                "        " + f.expect_order(9, 13) + "\n"
                "        break;\n"
                "        " + f.expect_not_reached() + "\n"
                "      }\n"
                "      " + f.expect_order(3, 7) + "\n"
                "    }\n"
                "    " + f.expect_order(4, 10, 14) + "\n"
                "  }\n"
                "  " + f.expect_order(15) + "\n";
        });
    });

CTS_TEST(g, "while_nested_continue")
    .desc("Test flow control for a nested while-loop with a continue statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  while (i < " + f.value(3) + ") {\n"
                "    " + f.expect_order(1, 5, 11) + "\n"
                "    i++;\n"
                "    var j = " + f.value(0) + ";\n"
                "    while (j < i) {\n"
                "      " + f.expect_order(2, 6, 8, 12, 14, 16) + "\n"
                "      j++;\n"
                "      if ((i+j) & 2) == 0 {\n"
                "        " + f.expect_order(9, 13, 15) + "\n"
                "        continue;\n"
                "        " + f.expect_not_reached() + "\n"
                "      }\n"
                "      " + f.expect_order(3, 7, 17) + "\n"
                "    }\n"
                "    " + f.expect_order(4, 10, 18) + "\n"
                "  }\n"
                "  " + f.expect_order(19) + "\n";
        });
    });

CTS_TEST(g, "while_logical_and_condition")
    .desc("Test flow control for a while-loop with a logical and condition")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  while (a(i) && b(i)) {\n"
                "    " + f.expect_order(3, 6) + "\n"
                "    i++;\n"
                "  }\n"
                "  " + f.expect_order(8) + "\n";
            std::string extra =
                std::string("fn a(i : i32) -> bool {\n") +
                "  " + f.expect_order(1, 4, 7) + "\n"
                "  return i < " + f.value(2) + ";\n"
                "}\n"
                "fn b(i : i32) -> bool {\n"
                "  " + f.expect_order(2, 5) + "\n"
                "  return i < " + f.value(5) + ";\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "while_logical_or_condition")
    .desc("Test flow control for a while-loop with a logical or condition")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                f.expect_order(0) + "\n"
                "  var i = " + f.value(0) + ";\n"
                "  while (a(i) || b(i)) {\n"
                "    " + f.expect_order(2, 4, 7, 10) + "\n"
                "    i++;\n"
                "  }\n"
                "  " + f.expect_order(13) + "\n";
            std::string extra =
                std::string("fn a(i : i32) -> bool {\n") +
                "  " + f.expect_order(1, 3, 5, 8, 11) + "\n"
                "  return i < " + f.value(2) + ";\n"
                "}\n"
                "fn b(i : i32) -> bool {\n"
                "  " + f.expect_order(6, 9, 12) + "\n"
                "  return i < " + f.value(4) + ";\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

} // namespace
