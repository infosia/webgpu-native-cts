// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureSampleBaseClampToEdge.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureSampleBaseClampToEdge",
    "Execution tests for textureSampleBaseClampToEdge.");

CTS_TEST(testGroup, "2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("filt", {"nearest", "linear"})
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleBaseClampToEdge2D(t); });

} // namespace
