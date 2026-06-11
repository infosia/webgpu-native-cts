// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/for.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,for",
    "\nFlow control tests for for-loops.\n");

CTS_TEST(g, "for_basic")
    .desc("Test that flow control executes a for-loop body the correct number of times")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; i < " + f.value(3) + "; i++) {\n"
                "    " + f.expect_order(1, 2, 3) + "\n"
                "  }\n"
                "  " + f.expect_order(4) + "\n";
        });
    });

CTS_TEST(g, "for_break")
    .desc("Test that flow control exits a for-loop when reaching a break statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; i < " + f.value(5) + "; i++) {\n"
                "    " + f.expect_order(1, 3, 5, 7) + "\n"
                "    if (i == 3) {\n"
                "      break;\n"
                "      " + f.expect_not_reached() + "\n"
                "    }\n"
                "    " + f.expect_order(2, 4, 6) + "\n"
                "  }\n"
                "  " + f.expect_order(8) + "\n";
        });
    });

CTS_TEST(g, "for_continue")
    .desc("Test flow control for a for-loop continue statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; i < " + f.value(5) + "; i++) {\n"
                "    " + f.expect_order(1, 3, 5, 7, 8) + "\n"
                "    if (i == 3) {\n"
                "      continue;\n"
                "      " + f.expect_not_reached() + "\n"
                "    }\n"
                "    " + f.expect_order(2, 4, 6, 9) + "\n"
                "  }\n"
                "  " + f.expect_order(10) + "\n";
        });
    });

CTS_TEST(g, "for_initializer")
    .desc("Test flow control for a for-loop initializer")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = initializer(); i < " + f.value(3) + "; i++) {\n"
                "    " + f.expect_order(2, 3, 4) + "\n"
                "  }\n"
                "  " + f.expect_order(5) + "\n";
            std::string extra =
                std::string("fn initializer() -> i32 {\n") +
                "  " + f.expect_order(1) + "\n"
                "  return " + f.value(0) + ";\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "for_complex_initializer")
    .desc("Test flow control for a complex for-loop initializer")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = initializer(max(a(), b())); i < " + f.value(5) + "; i++) {\n"
                "    " + f.expect_order(4, 5, 6) + "\n"
                "  }\n"
                "  " + f.expect_order(7) + "\n";
            std::string extra =
                std::string("fn a() -> i32 {\n") +
                "  " + f.expect_order(1) + "\n"
                "  return " + f.value(1) + ";\n"
                "}\n"
                "fn b() -> i32 {\n"
                "  " + f.expect_order(2) + "\n"
                "  return " + f.value(2) + ";\n"
                "}\n"
                "fn initializer(v : i32) -> i32 {\n"
                "  " + f.expect_order(3) + "\n"
                "  return v;\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "for_condition")
    .desc("Test flow control for a for-loop condition")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; condition(i); i++) {\n"
                "    " + f.expect_order(2, 4, 6) + "\n"
                "  }\n"
                "  " + f.expect_order(8) + "\n";
            std::string extra =
                std::string("fn condition(i : i32) -> bool {\n") +
                "  " + f.expect_order(1, 3, 5, 7) + "\n"
                "  return i < " + f.value(3) + ";\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "for_complex_condition")
    .desc("Test flow control for a for-loop condition")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; condition(i, a() * b()); i++) {\n"
                "    " + f.expect_order(4, 8) + "\n"
                "  }\n"
                "  " + f.expect_order(12) + "\n";
            std::string extra =
                std::string("fn a() -> i32 {\n") +
                "  " + f.expect_order(1, 5, 9) + "\n"
                "  return " + f.value(1) + ";\n"
                "}\n"
                "fn b() -> i32 {\n"
                "  " + f.expect_order(2, 6, 10) + "\n"
                "  return " + f.value(2) + ";\n"
                "}\n"
                "fn condition(i : i32, j : i32) -> bool {\n"
                "  " + f.expect_order(3, 7, 11) + "\n"
                "  return i < j;\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "for_continuing")
    .desc("Test flow control for a for-loop continuing statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; i < " + f.value(3) + "; i = cont(i)) {\n"
                "    " + f.expect_order(1, 3, 5) + "\n"
                "  }\n"
                "  " + f.expect_order(7) + "\n";
            std::string extra =
                std::string("fn cont(i : i32) -> i32 {\n") +
                "  " + f.expect_order(2, 4, 6) + "\n"
                "  return i + 1;\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "for_complex_continuing")
    .desc("Test flow control for a for-loop continuing statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; i < " + f.value(3) + "; i += cont(a(), b())) {\n"
                "    " + f.expect_order(1, 5, 9) + "\n"
                "  }\n"
                "  " + f.expect_order(13) + "\n";
            std::string extra =
                std::string("fn a() -> i32 {\n") +
                "  " + f.expect_order(2, 6, 10) + "\n"
                "  return " + f.value(1) + ";\n"
                "}\n"
                "fn b() -> i32 {\n"
                "  " + f.expect_order(3, 7, 11) + "\n"
                "  return " + f.value(2) + ";\n"
                "}\n"
                "fn cont(i : i32, j : i32) -> i32 {\n"
                "  " + f.expect_order(4, 8, 12) + "\n"
                "  return j >> u32(i);\n"
                "}\n";
            return FlowControlWgsl{std::move(entrypoint), std::move(extra)};
        });
    });

CTS_TEST(g, "nested_for_break")
    .desc("Test flow control for a for-loop break statement in an outer for-loop")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; i < " + f.value(2) + "; i++) {\n"
                "    " + f.expect_order(1, 5) + "\n"
                "    for (var j = " + f.value(5) + "; j < " + f.value(7) + "; j++) {\n"
                "      " + f.expect_order(2, 4, 6, 8) + "\n"
                "      if (j == " + f.value(6) + ") {\n"
                "        break;\n"
                "        " + f.expect_not_reached() + "\n"
                "      }\n"
                "      " + f.expect_order(3, 7) + "\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(9) + "\n";
        });
    });

CTS_TEST(g, "nested_for_continue")
    .desc("Test flow control for a for-loop continue statement in an outer for-loop")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; i < " + f.value(2) + "; i++) {\n"
                "    " + f.expect_order(1, 5) + "\n"
                "    for (var j = " + f.value(5) + "; j < " + f.value(7) + "; j++) {\n"
                "      " + f.expect_order(2, 3, 6, 7) + "\n"
                "      if (j == " + f.value(5) + ") {\n"
                "        continue;\n"
                "        " + f.expect_not_reached() + "\n"
                "      }\n"
                "      " + f.expect_order(4, 8) + "\n"
                "    }\n"
                "  }\n"
                "  " + f.expect_order(9) + "\n";
        });
    });

CTS_TEST(g, "for_logical_and_condition")
    .desc("Test flow control for a for-loop with a logical and condition")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; a(i) && b(i); i++) {\n"
                "    " + f.expect_order(3, 6) + "\n"
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

CTS_TEST(g, "for_logical_or_condition")
    .desc("Test flow control for a for-loop with a logical or condition")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> FlowControlWgsl {
            std::string entrypoint =
                "  " + f.expect_order(0) + "\n"
                "  for (var i = " + f.value(0) + "; a(i) || b(i); i++) {\n"
                "    " + f.expect_order(2, 4, 7, 10) + "\n"
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
