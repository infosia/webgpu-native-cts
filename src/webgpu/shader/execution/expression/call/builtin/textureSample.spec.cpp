// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureSample.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureSample",
    "Samples a texture.");

CTS_TEST(testGroup, "sampled_1d_coords")
    .desc("textureSample sampled 1d coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .filter(isSampled1DColorTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isFilterNearestOrFormatPossiblyFilterableAsTextureF32)
            .combine("modeU", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleSampled1D(t); });

CTS_TEST(testGroup, "sampled_2d_coords")
    .desc("textureSample sampled 2d coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .filter(isSampledColorTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isFilterNearestOrFormatPossiblyFilterableAsTextureF32)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleSampled2D(t); });

CTS_TEST(testGroup, "sampled_2d_coords,lodClamp")
    .desc("textureSample sampled 2d coordinates with LOD clamps.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .filter(isSampledColorTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isFilterNearestOrFormatPossiblyFilterableAsTextureF32)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combineWithParams(lodClampParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleSampled2DLodClamp(t); });

CTS_TEST(testGroup, "sampled_3d_coords")
    .desc("textureSample sampled 3d and cube coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .filter(isSampledColorTextureFormatParam)
            .combine("dim", {"3d", "cube"})
            .combine("filt", {"nearest", "linear"})
            .filter(isFilterNearestOrFormatPossiblyFilterableAsTextureF32)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("modeW", shortAddressModes())
            .combine("offset", {false, true})
            .filter(cubeOffsetsUnsupported)
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .filter(cubeEdgesOnlyForCube);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleSampled3D(t); });

CTS_TEST(testGroup, "depth_2d_coords")
    .desc("textureSample depth 2d coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleDepth2D(t); });

CTS_TEST(testGroup, "sampled_array_2d_coords")
    .desc("textureSample sampled 2d-array coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .filter(isSampledColorTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isFilterNearestOrFormatPossiblyFilterableAsTextureF32)
            .combine("modeU", shortAddressModes())
            .combine("modeV", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleSampled2DArray(t); });

CTS_TEST(testGroup, "sampled_array_3d_coords")
    .desc("textureSample sampled cube-array coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .filter(isSampledColorTextureFormatParam)
            .combine("filt", {"nearest", "linear"})
            .filter(isFilterNearestOrFormatPossiblyFilterableAsTextureF32)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("A", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleSampledCubeArray(t); });

CTS_TEST(testGroup, "depth_3d_coords")
    .desc("textureSample depth cube and cube-array coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combineWithParams(depth3DViewDimensionParams())
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleDepth3D(t); });

CTS_TEST(testGroup, "depth_array_2d_coords")
    .desc("textureSample depth 2d-array coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("mode", shortAddressModes())
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("L", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleDepth2DArray(t); });

CTS_TEST(testGroup, "depth_array_3d_coords")
    .desc("textureSample depth cube-array coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("A", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleDepthCubeArray(t); });

} // namespace
