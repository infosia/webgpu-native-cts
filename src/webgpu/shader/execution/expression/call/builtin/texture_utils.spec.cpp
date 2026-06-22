// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/texture_utils.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,texture_utils",
    "Tests for texture_utils.ts");

CTS_TEST(testGroup, "createTextureWithRandomDataAndGetTexels_with_generator")
    .desc(
        "Test createTextureWithRandomDataAndGetTexels with a generator. Generators "
        "are only used with textureXXXCompare builtins as we need specific random "
        "values to test these builtins with a depth reference value.")
    .params([](ParamsBuilder u) {
        return u.combine("format", textureUtilsDepthStencilFormats())
            .combine("viewDimension", textureUtilsGeneratorViewDimensions());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        executeCreateTextureWithRandomDataAndGetTexelsWithGenerator(t);
    });

CTS_TEST(testGroup, "readTextureToTexelViews")
    .desc("test readTextureToTexelViews for various formats and dimensions")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(readTextureToTexelViewsParams());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeReadTextureToTexelViews(t); });

CTS_TEST(testGroup, "weights")
    .desc("Test the mip level weights are linear.")
    .params([](ParamsBuilder u) { return u.combine("stage", textureUtilsShaderStages()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeWeights(t); });

} // namespace
