// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureGatherCompare.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureGatherCompare",
    "Performs a depth comparison on the four gather texels and collects the 0/1 results.");

CTS_TEST(testGroup, "array_2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("compare", compareFunctions())
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherCompareArray2D(t); });

CTS_TEST(testGroup, "array_3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("compare", compareFunctions());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherCompareArray3D(t); });

CTS_TEST(testGroup, "sampled_2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .combine("mode", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("C", {"i32", "u32"})
            .combine("samplePoints", samplePointMethods())
            .combine("compare", compareFunctions());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherCompareSampled2D(t); });

CTS_TEST(testGroup, "sampled_3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("compare", compareFunctions());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherCompareSampled3D(t); });

} // namespace
