// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureSampleLevel.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureSampleLevel",
    "Samples a texture.");

CTS_TEST(testGroup, "sampled_1d_coords")
    .desc("textureSampleLevel sampled 1d coordinates.")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, true, true)
            .filter(isSampledColorTextureFormatParam)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampled1D(t); });

CTS_TEST(testGroup, "sampled_2d_coords")
    .desc("textureSampleLevel sampled 2d coordinates.")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, true, true)
            .filter(isSampledColorTextureFormatParam)
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampled2D(t); });

CTS_TEST(testGroup, "sampled_2d_coords,lodClamp")
    .desc("textureSampleLevel sampled 2d coordinates with baseMipLevel and LOD clamps.")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, false, false)
            .filter(isSampledColorTextureFormatParam)
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combineWithParams(lodClampParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampled2DLodClamp(t); });

CTS_TEST(testGroup, "sampled_2d_coords,compressed_unaligned")
    .desc(R"(
Tests textureSampleLevel with block-compressed textures whose mip level 0 size is NOT a multiple of
the texel block size, using the 'texture-compression-unaligned' feature.

The blocks at the unaligned edges of the texture are only partially inside the texture. The texels
of those blocks that are outside the texture must never be sampled. In particular:
 * with 'repeat', sampling at the edge must blend the last texel with texel 0 (and vice versa).
 * with 'clamp-to-edge' and 'mirror-repeat', sampling at the edge must only use the edge texel.

The texture is filled with random blocks, so the texels outside the texture have different values
than the ones inside. The expected values are computed only from the texels inside the texture.

In addition to the usual sample points, samples every row and/or column on and around the
unaligned edges (and the edges opposite to them, which wrap around to the unaligned edges).
)")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {"f", "c"})
            .combine("format", compressedTextureFormats())
            .filter(isPotentiallyFilterableAndFillable)
            .combine("filt", {"nearest", "linear"})
            .combine("mode", {"c", "r", "m"})
            .beginSubcases()
            // Which dimension(s) of mip level 0 have a partial edge block.
            .combine("partialEdge", {"width", "height", "both"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampled2DCompressedUnaligned(t); });

CTS_TEST(testGroup, "sampled_array_2d_coords")
    .desc("textureSampleLevel sampled 2d-array coordinates.")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, true, true)
            .filter(isSampledColorTextureFormatParam)
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampled2DArray(t); });

CTS_TEST(testGroup, "sampled_3d_coords")
    .desc("textureSampleLevel sampled 3d and cube coordinates.")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, false, false)
            .filter(isSampledColorTextureFormatParam)
            .combine("dim", {"3d", "cube"})
            .combine("mode", shortAddressModes())
            .combine("offset", {false, true})
            .filter(cubeOffsetsUnsupported)
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .filter(cubeEdgesOnlyForCube);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampled3D(t); });

CTS_TEST(testGroup, "sampled_3d_coords,lodClamp")
    .desc("textureSampleLevel sampled 3d and cube coordinates with baseMipLevel and LOD clamps.")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, false, false)
            .filter(isSampledColorTextureFormatParam)
            .combine("dim", {"3d", "cube"})
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .filter(cubeEdgesOnlyForCube)
            .combineWithParams(lodClampParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampled3DLodClamp(t); });

CTS_TEST(testGroup, "sampled_array_3d_coords")
    .desc("textureSampleLevel sampled cube-array coordinates.")
    .params([](ParamsBuilder u) {
        return addSampledTextureCommonParams(u, false, false)
            .filter(isSampledColorTextureFormatParam)
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("A", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelSampledCubeArray(t); });

CTS_TEST(testGroup, "depth_2d_coords")
    .desc("textureSampleLevel depth 2d coordinates.")
    .params([](ParamsBuilder u) {
        return addDepthTextureCommonParams(u)
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("L", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelDepth2D(t); });

CTS_TEST(testGroup, "depth_array_2d_coords")
    .desc("textureSampleLevel depth 2d-array coordinates.")
    .params([](ParamsBuilder u) {
        return addDepthTextureCommonParams(u)
            .combine("offset", {false, true})
            .beginSubcases()
            .combine("samplePoints", samplePointMethods())
            .combine("A", {"i32", "u32"})
            .combine("L", {"i32", "u32"})
            .combine("depthOrArrayLayers", {1, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelDepth2DArray(t); });

CTS_TEST(testGroup, "depth_3d_coords")
    .desc("textureSampleLevel depth cube and cube-array coordinates.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", shortShaderStages())
            .combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .combineWithParams(depth3DViewDimensionParams())
            .combine("mode", shortAddressModes())
            .beginSubcases()
            .combine("samplePoints", cubeSamplePointMethods())
            .combine("L", {"i32", "u32"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureSampleLevelDepth3D(t); });

} // namespace
