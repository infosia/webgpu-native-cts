// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/pack4xI8Clamp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'pack4xI8Clamp' builtin function.

#include <algorithm>
#include <array>
#include <cstdint>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,pack4xI8Clamp",
    "Execution tests for the 'pack4xI8Clamp' builtin function.");

uint32_t pack4xI8Clamp(const std::array<int32_t, 4>& vals) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const int32_t clamped = std::max(-128, std::min(127, vals[i]));
        result |= (static_cast<uint32_t>(clamped) & 0xffu) << (i * 8);
    }
    return result;
}

CTS_TEST(testGroup, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<std::array<int32_t, 4>, 21> testInputs = {{
            {0, 0, 0, 0},
            {1, 2, 3, 4},
            {-1, 2, 3, 4},
            {1, -2, 3, 4},
            {1, 2, -3, 4},
            {1, 2, 3, -4},
            {-1, -2, 3, 4},
            {-1, 2, -3, 4},
            {-1, 2, 3, -4},
            {1, -2, -3, 4},
            {1, -2, 3, -4},
            {1, 2, -3, -4},
            {-1, -2, -3, 4},
            {-1, -2, 3, -4},
            {-1, 2, -3, -4},
            {1, -2, -3, -4},
            {-1, -2, -3, -4},
            {126, 127, 128, 129},
            {-130, -129, -128, -127},
            {127, 128, -128, -129},
            {32767, 32768, -32768, -32769},
        }};
        std::vector<Case> cases;
        for (const auto& v : testInputs) {
            cases.push_back(
                {{vec4(i32(v[0]), i32(v[1]), i32(v[2]), i32(v[3]))}, u32(pack4xI8Clamp(v))});
        }
        run(t, builtin("pack4xI8Clamp"), {vecType(4, ScalarKind::I32)}, scalarType(ScalarKind::U32),
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

} // namespace
