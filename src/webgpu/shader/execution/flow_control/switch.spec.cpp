// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/switch.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,switch",
    "\nFlow control tests for switch statements.\n");

CTS_TEST(g, "switch")
    .desc("Test that flow control executes the correct switch case block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  switch (" + f.value(1) + ") {\n"
                "    case 0: {\n"
                "      " + f.expect_not_reached() + "\n"
                "      break;\n"
                "    }\n"
                "    case 1: {\n"
                "      " + f.expect_order(1) + "\n"
                "      break;\n"
                "    }\n"
                "    case 2: {\n"
                "      " + f.expect_not_reached() + "\n"
                "      break;\n"
                "    }\n"
                "    default: {\n"
                "      " + f.expect_not_reached() + "\n"
                "      break;\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(2) + "\n";
        });
    });

CTS_TEST(g, "switch_multiple_case")
    .desc("Test that flow control executes the correct switch case block with multiple cases per block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  switch (" + f.value(2) + ") {\n"
                "    case 0, 1: {\n"
                "      " + f.expect_not_reached() + "\n"
                "      break;\n"
                "    }\n"
                "    case 2, 3: {\n"
                "      " + f.expect_order(1) + "\n"
                "      break;\n"
                "    }\n"
                "    default: {\n"
                "      " + f.expect_not_reached() + "\n"
                "      break;\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(2) + "\n";
        });
    });

CTS_TEST(g, "switch_multiple_case_default")
    .desc("Test that flow control executes the correct switch case block with multiple cases per block (combined with default)")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "  switch (" + f.value(2) + ") {\n"
                "    case 0, 1: {\n"
                "      " + f.expect_not_reached() + "\n"
                "      break;\n"
                "    }\n"
                "    case 2, 3, default: {\n"
                "      " + f.expect_order(1) + "\n"
                "      break;\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(2) + "\n"
                "  switch (" + f.value(1) + ") {\n"
                "    case 0, 1: {\n"
                "      " + f.expect_order(3) + "\n"
                "      break;\n"
                "    }\n"
                "    case 2, 3, default: {\n"
                "      " + f.expect_not_reached() + "\n"
                "      break;\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(4) + "\n";
        });
    });

CTS_TEST(g, "switch_default")
    .desc("Test that flow control executes the switch default block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "switch (" + f.value(4) + ") {\n"
                "  case 0: {\n"
                "    " + f.expect_not_reached() + "\n"
                "    break;\n"
                "  }\n"
                "  case 1: {\n"
                "    " + f.expect_not_reached() + "\n"
                "    break;\n"
                "  }\n"
                "  case 2: {\n"
                "    " + f.expect_not_reached() + "\n"
                "    break;\n"
                "  }\n"
                "  default: {\n"
                "    " + f.expect_order(1) + "\n"
                "    break;\n"
                "  }\n"
                "}\n"
                "" + f.expect_order(2) + "\n";
        });
    });

CTS_TEST(g, "switch_default_only")
    .desc("Test that flow control executes the switch default block, which is the only case")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "switch (" + f.value(4) + ") {\n"
                "default: {\n"
                "  " + f.expect_order(1) + "\n"
                "  break;\n"
                "}\n"
                "}\n"
                "" + f.expect_order(2) + "\n";
        });
    });

CTS_TEST(g, "switch_inside_loop_with_continue")
    .desc("Test that flow control executes correct for a switch calling continue inside a loop")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                f.expect_order(0) + "\n"
                "var i = " + f.value(0) + ";\n"
                "loop {\n"
                "  switch (i) {\n"
                "    case 1: {\n"
                "      " + f.expect_order(4) + "\n"
                "      continue;\n"
                "    }\n"
                "    default: {\n"
                "      " + f.expect_order(1) + "\n"
                "      break;\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(2) + "\n"
                "\n"
                "  continuing {\n"
                "    " + f.expect_order(3, 5) + "\n"
                "    i++;\n"
                "    break if i >= 2;\n"
                "  }\n"
                "}\n"
                "" + f.expect_order(6) + "\n";
        });
    });

} // namespace
