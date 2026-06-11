// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/return.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,return",
    "\nFlow control tests for return statements.\n");

CTS_TEST(g, "return")
    .desc("Test that flow control does not execute after a 'return' statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return "  " + f.expect_order(0) + "\n"
                   "  return;\n"
                   "  " + f.expect_not_reached() + "\n";
        });
    });

CTS_TEST(g, "return_conditional_true")
    .desc("Test that flow control does not execute after a 'return' statement in a if (true) block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return wgslTemplate(R"(
  %%
  if (%%) {
    return;
  }
  %%
)",
                {f.expect_order(0), f.value(true), f.expect_not_reached()});
        });
    });

CTS_TEST(g, "return_conditional_false")
    .desc("Test that flow control does not execute after a 'return' statement in a if (false) block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) -> std::string {
            return wgslTemplate(R"(
  %%
  if (%%) {
    return;
  }
  %%
)",
                {f.expect_order(0), f.value(false), f.expect_order(1)});
        });
    });

} // namespace
