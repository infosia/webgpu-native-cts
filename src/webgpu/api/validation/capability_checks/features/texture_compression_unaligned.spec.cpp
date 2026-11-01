// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/texture_compression_unaligned.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "feature_test_helpers.h"

using namespace cts;
using namespace cts::capability_features;

namespace {

TestGroup<FeatureGpuTest> testGroup = MakeTestGroup<FeatureGpuTest>(
    "api,validation,capability_checks,features,texture_compression_unaligned",
    "Tests for capability checking for the 'texture-compression-unaligned' feature.\n\n"
    "When the feature is not enabled, the behavior is unchanged: the size of mip level 0 of a\n"
    "block-compressed texture must be a multiple of the texel block size. When the feature is enabled,\n"
    "mip level 0 is allowed to have a size that is not a multiple of the texel block size (i.e. partial\n"
    "edge blocks).");

CTS_TEST(testGroup, "createTexture,unaligned_size")
    .desc("Test that creating a compressed texture whose mip level 0 size is not a multiple of the texel\n"
          "block size succeeds if and only if 'texture-compression-unaligned' is enabled.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kCompressedTextureFormats))
            .combine("enable_feature", {true, false})
            // The unaligned dimension(s) of mip level 0 being tested.
            .combine("sizeCase", {Value("width"), Value("height"), Value("both"), Value("single")});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enableFeature = t.param<bool>("enable_feature");
        const std::string sizeCase = t.param<std::string>("sizeCase");

        std::vector<WGPUFeatureName> requiredFeatures;
        const TextureFormatInfo& info = textureFormatInfo(format);
        if (info.hasRequiredFeature) {
            requiredFeatures.push_back(info.requiredFeature);
        }
        if (enableFeature) {
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
            requiredFeatures.push_back(WGPUFeatureName_TextureCompressionUnaligned);
#else
            // Not in wgpu-native's webgpu.h, so the feature cannot be requested there.
            t.skip("texture-compression-unaligned is not available on this backend");
#endif
        }

        t.selectDeviceOrSkipTestCase(requiredFeatures);
        t.skipIfTextureFormatNotSupported(format);

        const TextureBlockInfo block = getBlockInfoForTextureFormat(format);

        // Construct a mip level 0 size that is not a multiple of the texel block size.
        WGPUExtent3D size = {1, 1, 1};  // 'single': a single partial edge block in both dimensions.
        if (sizeCase == "width") {
            size = {block.blockWidth + 1, block.blockHeight, 1};
        } else if (sizeCase == "height") {
            size = {block.blockWidth, block.blockHeight + 1, 1};
        } else if (sizeCase == "both") {
            size = {block.blockWidth + 1, block.blockHeight + 1, 1};
        }

        WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.size = size;
        descriptor.format = format;
        descriptor.usage = WGPUTextureUsage_TextureBinding;

        // Without the feature, an unaligned mip level 0 size is a validation error.
        expectValidationError(t, [&] { t.createTextureTracked(descriptor); }, !enableFeature);
    });

}  // namespace
