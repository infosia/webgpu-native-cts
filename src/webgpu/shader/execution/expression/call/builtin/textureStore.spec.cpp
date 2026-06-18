// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureStore.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureStore",
    "Writes a single texel to a texture.");

CTS_TEST(testGroup, "texel_formats")
    .desc("Test storage of texel formats.")
    .params([](ParamsBuilder u) {
        return u.combine("format", possibleStorageTextureFormats())
            .combine("viewDimension", storeViewDimensions())
            .combine("stage", {"compute", "fragment"})
            .combine("access", {"write", "read_write"})
            .filter(storeReadWriteAccessOrFormatSupported)
            .combine("mipLevel", {0, 1, 2})
            .filter(storeViewDimensionMipLevelValid);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureStoreTexelFormats(t); });

CTS_TEST(testGroup, "bgra8unorm_swizzle")
    .desc("Test bgra8unorm swizzling.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureStoreBgra8unormSwizzle(t); });

CTS_TEST(testGroup, "out_of_bounds")
    .desc("Test that textureStore on out-of-bounds coordinates have no effect.")
    .params([](ParamsBuilder u) {
        return u.combine("dim", storeDimensions())
            .combine("coords", {"i32", "u32"})
            .combine("mipCount", {1, 2, 3})
            .combine("mip", {0, 1, 2})
            .filter(storeOutOfBoundsMipValid);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureStoreOutOfBounds(t); });

CTS_TEST(testGroup, "out_of_bounds_array")
    .desc("Test that out-of-bounds array coordinates to textureStore have no effect.")
    .params([](ParamsBuilder u) {
        return u.combine("baseLevel", {0, 1, 2, 3})
            .combine("arrayLevels", {1, 2, 3, 4})
            .combine("type", {"i32", "u32"})
            .filter(storeOutOfBoundsArrayValid);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureStoreOutOfBoundsArray(t); });

} // namespace
