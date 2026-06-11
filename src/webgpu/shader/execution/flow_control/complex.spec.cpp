// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/complex.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,complex",
    "Flow control tests for interesting complex cases.");

CTS_TEST(g, "continue_in_switch_in_for_loop")
    .desc("Test flow control for a continue statement in a switch, in a for-loop")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            return wgslTemplate(R"(
  %%
  for (var i = %%; i < 3; i++) {
    %%
    switch (i) {
      case 2: {
        %%
        break;
      }
      case 1: {
        %%
        continue;
      }
      default: {
        %%
        break;
      }
    }
    %%
  }
  %%
)",
                {f.expect_order(0), f.value(0),
                 f.expect_order(1, 4, 6),
                 f.expect_order(7),
                 f.expect_order(5),
                 f.expect_order(2),
                 f.expect_order(3, 8),
                 f.expect_order(9)});
        });
    });

} // namespace
