// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureNumSamples.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureNumSamples",
    "Execution tests for textureNumSamples.");

CTS_TEST(testGroup, "sampled")
    .desc("textureNumSamples for multisampled textures.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("stage", shortShaderStages())
            .combine("sampled_type", {"f32", "i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureNumSamplesSampled(t); });

CTS_TEST(testGroup, "depth")
    .desc("textureNumSamples for depth multisampled textures.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("stage", shortShaderStages());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureNumSamplesDepth(t); });

} // namespace
