// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureGather.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureGather",
    "Reads the four texels of a single channel that linear filtering would use, from mip level 0.");

CTS_TEST(testGroup, "sampled_2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", allTextureFormats())
            .filter(isGatherFillableFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isGatherFilterNearestOrPossiblyFilterableParam)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("C", {"i32", "u32"})
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherSampled2D(t); });

CTS_TEST(testGroup, "sampled_3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", allTextureFormats())
            .filter(isGatherFillableFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isGatherFilterNearestOrPossiblyFilterableParam)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("C", {"i32", "u32"})
            .combine("samplePoints", cubeSamplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherSampled3D(t); });

CTS_TEST(testGroup, "sampled_array_2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", allTextureFormats())
            .filter(isGatherFillableFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isGatherFilterNearestOrPossiblyFilterableParam)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("A", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherSampledArray2D(t); });

CTS_TEST(testGroup, "sampled_array_3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", allTextureFormats())
            .filter(isGatherFillableFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isGatherFilterNearestOrPossiblyFilterableParam)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("A", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherSampledArray3D(t); });

CTS_TEST(testGroup, "depth_2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherDepth2D(t); });

CTS_TEST(testGroup, "depth_3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherDepth3D(t); });

CTS_TEST(testGroup, "depth_array_2d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherDepthArray2D(t); });

CTS_TEST(testGroup, "depth_array_3d_coords")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("A", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureGatherDepthArray3D(t); });

} // namespace
