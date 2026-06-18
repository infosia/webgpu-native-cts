// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/textureNumLevels.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/texture_sampling/texture_utils.h"

using namespace cts;
using namespace cts::texture_utils;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,textureNumLevels",
    "Execution tests for textureNumLevels.");

CTS_TEST(testGroup, "sampled")
    .desc("textureNumLevels for sampled textures.")
    .params([](ParamsBuilder u) {
        return u.combine("texture_type", {"texture_1d", "texture_2d", "texture_2d_array", "texture_3d", "texture_cube", "texture_cube_array"})
            .beginSubcases()
            .combine("stage", shortShaderStages())
            .combine("sampled_type", {"f32", "i32", "u32"})
            .combine("view_type", {"full", "partial"})
            .filter([](const ParamRecord& record) {
                return !(valueAs<std::string>(*findParam(record, "texture_type")) == "texture_1d"
                    && valueAs<std::string>(*findParam(record, "view_type")) == "partial");
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureNumLevelsSampled(t); });

CTS_TEST(testGroup, "depth")
    .desc("textureNumLevels for depth textures.")
    .params([](ParamsBuilder u) {
        return u.combine("texture_type", {"texture_depth_2d", "texture_depth_2d_array", "texture_depth_cube", "texture_depth_cube_array"})
            .combine("view_type", {"full", "partial"})
            .beginSubcases()
            .combine("stage", shortShaderStages());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { executeTextureNumLevelsDepth(t); });

} // namespace
