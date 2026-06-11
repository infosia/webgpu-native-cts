// Ported from gpuweb/cts src/webgpu/shader/execution/flow_control/if.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include "cts/gpu.h"
#include "cts/test.h"
#include "harness.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,flow_control,if",
    "\nFlow control tests for if-statements.\n");

CTS_TEST(g, "if_true")
    .desc("Test that flow control executes the 'true' block of an if statement and not the 'false' block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            return wgslTemplate(R"(
  %%
  if (%%) {
    %%
  } else {
    %%
  }
  %%
)",
                {f.expect_order(0), f.value(true), f.expect_order(1),
                 f.expect_not_reached(), f.expect_order(2)});
        });
    });

CTS_TEST(g, "if_false")
    .desc("Test that flow control executes the 'false' block of an if statement and not the 'true' block")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            return wgslTemplate(R"(
  %%
  if (%%) {
    %%
  } else {
    %%
  }
  %%
)",
                {f.expect_order(0), f.value(false), f.expect_not_reached(),
                 f.expect_order(1), f.expect_order(2)});
        });
    });

CTS_TEST(g, "else_if")
    .desc("Test that flow control executes the correct 'else if' block of an if statement")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            return wgslTemplate(R"(
  %%
  if (%%) {
    %%
  } else if (%%) {
    %%
  } else if (%%) {
    %%
  } else if (%%) {
    %%
  }
  %%
)",
                {f.expect_order(0),
                 f.value(false), f.expect_not_reached(),
                 f.value(false), f.expect_not_reached(),
                 f.value(true),  f.expect_order(1),
                 f.value(false), f.expect_not_reached(),
                 f.expect_order(2)});
        });
    });

CTS_TEST(g, "nested_if_else")
    .desc("Test flow control for nested if-else statements")
    .params([](ParamsBuilder u) {
        return u.combine("preventValueOptimizations", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runFlowControlTest(t, [](FlowControlTestBuilder& f) {
            return wgslTemplate(R"(
%%
if (%%) {
  %%
  if (%%) {
    %%
  } else {
    %%
    if (%%) {
      %%
    } else {
      %%
    }
    %%
  }
  %%
} else {
  %%
}
%%
)",
                {f.expect_order(0),
                 f.value(true),  f.expect_order(1),
                 f.value(false), f.expect_not_reached(),
                                 f.expect_order(2),
                 f.value(true),  f.expect_order(3),
                                 f.expect_not_reached(),
                                 f.expect_order(4),
                                 f.expect_order(5),
                                 f.expect_not_reached(),
                 f.expect_order(6)});
        });
    });

} // namespace
