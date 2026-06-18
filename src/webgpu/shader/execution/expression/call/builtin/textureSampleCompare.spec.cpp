// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureSampleCompare.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureSampleCompare",
    "Samples a depth texture and compares the sampled depth values against a reference value.");

CTS_TEST(testGroup, "2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("compare", compareFunctions());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleCompare2D(t); });

CTS_TEST(testGroup, "3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("compare", compareFunctions());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleCompareCube(t); });

CTS_TEST(testGroup, "arrayed_2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
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
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleCompare2DArray(t); });

CTS_TEST(testGroup, "arrayed_3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("compare", compareFunctions());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleCompareCubeArray(t); });

} // namespace
