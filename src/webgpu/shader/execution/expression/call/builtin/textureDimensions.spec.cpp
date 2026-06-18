// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureDimensions.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureDimensions",
    "Execution tests for textureDimensions.");

CTS_TEST(testGroup, "sampled_and_multisampled")
    .desc("textureDimensions for sampled and multisampled textures.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormats())
            .expand("aspect", textureMetadataAspectsForFormat)
            .expand("samples", textureMetadataSamplesForFormat)
            .beginSubcases()
            .combine("stage", shortShaderStages())
            .expand("dimensions", textureMetadataViewDimensions)
            .expand("textureMipCount", textureMetadataMipCounts)
            .expand("baseMipLevel", textureMetadataBaseMipLevels)
            .expand("textureDimensionsLevel", textureMetadataDimensionsLevels);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureDimensionsSampledAndMultisampled(t); });

CTS_TEST(testGroup, "depth")
    .desc("textureDimensions for depth textures.")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormats())
            .filter(isDepthTextureFormatParam)
            .expand("aspect", textureMetadataAspectsForFormat)
            .filter([](const ParamRecord& record) {
                return valueAs<std::string>(*findParam(record, "aspect")) != "stencil-only";
            })
            .expand("samples", textureMetadataSamplesForFormat)
            .beginSubcases()
            .combine("stage", shortShaderStages())
            .expand("dimensions", textureMetadataViewDimensions)
            .expand("textureMipCount", textureMetadataMipCounts)
            .expand("baseMipLevel", textureMetadataBaseMipLevels)
            .expand("textureDimensionsLevel", textureMetadataDimensionsLevels);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureDimensionsDepth(t); });

CTS_TEST(testGroup, "storage")
    .desc("textureDimensions for storage textures.")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleStorageTextureFormats())
            .expand("aspect", textureMetadataAspectsForFormat)
            .beginSubcases()
            .combine("stage", shortShaderStages())
            .combine("access", {"read", "write", "read_write"})
            .filter(isNotWritableStorageInVertexStage)
            .filter(isStorageReadWriteAccessOrFormatSupported)
            .expand("dimensions", textureMetadataStorageViewDimensions)
            .expand("textureMipCount", textureMetadataMipCounts)
            .expand("baseMipLevel", textureMetadataBaseMipLevels);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureDimensionsStorage(t); });

CTS_TEST(testGroup, "external")
    .desc("textureDimensions for external textures.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("stage", shortShaderStages())
            .combine("importExternalTexture", {false, true})
            .combine("width", {8, 16, 24})
            .combine("height", {8, 16, 24});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureDimensionsExternal(t); });

} // namespace
