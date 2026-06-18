// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureSampleBias.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureSampleBias",
    "Samples a texture with a bias to the mip level.");

CTS_TEST(testGroup, "sampled_2d_coords")
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
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleBiasSampled2D(t); });

CTS_TEST(testGroup, "sampled_3d_coords")
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
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleBiasSampled3D(t); });

CTS_TEST(testGroup, "arrayed_2d_coords")
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
            .combine("A", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleBiasSampled2DArray(t); });

CTS_TEST(testGroup, "arrayed_3d_coords")
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
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleBiasSampledCubeArray(t); });

} // namespace
