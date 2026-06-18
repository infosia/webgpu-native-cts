// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureSampleGrad.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureSampleGrad",
    "Samples a texture using explicit gradients.");

CTS_TEST(testGroup, "sampled_2d_coords")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, true, true)
            .filter(isSampledColorTextureFormatParam)
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleGradSampled2D(t); });

CTS_TEST(testGroup, "sampled_3d_coords")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, true, true)
            .filter(isSampledColorTextureFormatParam)
            .combine("dim", {"3d", "cube"})
            .combine("modeW", shortAddressModes())
            .combine("offset", {false, true})
            .filter(cubeOffsetsUnsupported)
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .filter(cubeEdgesOnlyForCube);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleGradSampled3D(t); });

CTS_TEST(testGroup, "sampled_array_2d_coords")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, true, true)
            .filter(isSampledColorTextureFormatParam)
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleGradSampled2DArray(t); });

CTS_TEST(testGroup, "sampled_array_3d_coords")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, false, false)
            .filter(isSampledColorTextureFormatParam)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("A", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleGradSampledCubeArray(t); });

} // namespace
