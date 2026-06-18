// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureNumLayers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureNumLayers",
    "Execution tests for textureNumLayers.");

CTS_TEST(testGroup, "sampled")
    .desc("textureNumLayers for sampled array textures.")
    .params([](ParamsBuilder u) {
        return u.combine("texture_type", {"texture_2d_array", "texture_cube_array"})
            .combine("view_type", {"full", "partial"})
            .beginSubcases()
            .combine("sampled_type", {"f32", "i32", "u32"})
            .combine("stage", shortShaderStages());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureNumLayersSampled(t); });

CTS_TEST(testGroup, "arrayed")
    .desc("textureNumLayers for depth array textures.")
    .params([](ParamsBuilder u) {
        return u.combine("texture_type", {"texture_depth_2d_array", "texture_depth_cube_array"})
            .combine("view_type", {"full", "partial"})
            .beginSubcases()
            .combine("stage", shortShaderStages());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureNumLayersArrayed(t); });

CTS_TEST(testGroup, "storage")
    .desc("textureNumLayers for storage array textures.")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleStorageTextureFormats())
            .combine("view_type", {"full", "partial"})
            .beginSubcases()
            .combine("stage", shortShaderStages())
            .combine("access_mode", {"read", "write", "read_write"})
            .filter(isStorageReadWriteAccessOrFormatSupported)
            .filter(isNotWritableStorageInVertexStage);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureNumLayersStorage(t); });

} // namespace
