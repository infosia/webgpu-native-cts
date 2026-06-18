// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/pack4xU8.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'pack4xU8' builtin function.

#include <array>
#include <cstdint>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,pack4xU8",
    "Execution tests for the 'pack4xU8' builtin function.");

uint32_t pack4xU8(const std::array<uint32_t, 4>& vals) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        result |= (vals[i] & 0xffu) << (i * 8);
    }
    return result;
}

CTS_TEST(testGroup, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<std::array<uint32_t, 4>, 5> testInputs = {{
            {0u, 0u, 0u, 0u},
            {1u, 2u, 3u, 4u},
            {255u, 255u, 255u, 255u},
            {254u, 255u, 256u, 257u},
            {65535u, 65536u, 255u, 254u},
        }};
        std::vector<Case> cases;
        for (const auto& v : testInputs) {
            cases.push_back({{vec4(u32(v[0]), u32(v[1]), u32(v[2]), u32(v[3]))}, u32(pack4xU8(v))});
        }
        run(t, builtin("pack4xU8"), {vecType(4, ScalarKind::U32)}, scalarType(ScalarKind::U32),
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

} // namespace
