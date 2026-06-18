// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureLoad.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureLoad",
    "Execution tests for textureLoad.");

CTS_TEST(testGroup, "sampled_1d")
    .desc("textureLoad sampled 1d textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", allTextureFormats())
            .filter(textureLoadFormatCompatibleWith1D)
            .filter(textureLoadFormatNotCompressed)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("L", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadSampled1D(t); });

CTS_TEST(testGroup, "sampled_2d")
    .desc("textureLoad sampled 2d textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", allTextureFormats())
            .filter(textureLoadFormatNotCompressedFloat)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("L", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadSampled2D(t); });

CTS_TEST(testGroup, "sampled_3d")
    .desc("textureLoad sampled 3d textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", allTextureFormats())
            .filter(textureLoadFormatCompatibleWith3D)
            .filter(textureLoadFormatNotCompressedFloat)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("L", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadSampled3D(t); });

CTS_TEST(testGroup, "multisampled")
    .desc("textureLoad multisampled textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("texture_type", {"texture_multisampled_2d", "texture_depth_multisampled_2d"})
            .combine("format", textureLoadMultisampledFormats())
            .filter(textureLoadDepthTextureTypeMatchesFormat)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("S", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadMultisampled(t); });

CTS_TEST(testGroup, "depth")
    .desc("textureLoad depth textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", depthStencilFormats())
            .filter(textureLoadFormatHasDepth)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("L", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadDepth(t); });

CTS_TEST(testGroup, "external")
    .desc("textureLoad external textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .beginSubcases()
            .combine("importExternalTexture", {false, true})
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("L", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadExternal(t); });

CTS_TEST(testGroup, "arrayed")
    .desc("textureLoad sampled and depth array textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", allTextureFormats())
            .filter(textureLoadFormatFillable)
            .combine("texture_type", {"texture_2d_array", "texture_depth_2d_array"})
            .filter(textureLoadDepthTextureTypeMatchesFormat)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combineWithParams(textureLoadArrayedCoordinateParams())
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadArrayed(t); });

CTS_TEST(testGroup, "storage_textures_1d")
    .desc("textureLoad storage 1d textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", possibleStorageTextureFormats())
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadStorage1D(t); });

CTS_TEST(testGroup, "storage_textures_2d")
    .desc("textureLoad storage 2d textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", possibleStorageTextureFormats())
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("baseMipLevel", {0, 1})
            .combine("C", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadStorage2D(t); });

CTS_TEST(testGroup, "storage_textures_2d_array")
    .desc("textureLoad storage 2d-array textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", possibleStorageTextureFormats())
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"})
            .combine("A", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8})
            .combine("baseMipLevel", {0, 1})
            .combine("baseArrayLayer", {0, 1})
            .filter(textureLoadArrayLayerBaseValid);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadStorage2DArray(t); });

CTS_TEST(testGroup, "storage_textures_3d")
    .desc("textureLoad storage 3d textures.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", textureLoadShaderStages())
            .combine("format", possibleStorageTextureFormats())
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("C", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureLoadStorage3D(t); });

} // namespace
