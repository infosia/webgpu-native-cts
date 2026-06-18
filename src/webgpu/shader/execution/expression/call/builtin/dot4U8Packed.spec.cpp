// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/dot4U8Packed.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'dot4U8Packed' builtin function.

#include <array>
#include <cstdint>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,dot4U8Packed",
    "Execution tests for the 'dot4U8Packed' builtin function.");

uint32_t dot4U8Packed(uint32_t e1, uint32_t e2) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const uint32_t e1_i = (e1 >> (i * 8)) & 0xffu;
        const uint32_t e2_i = (e2 >> (i * 8)) & 0xffu;
        result += e1_i * e2_i;
    }
    return result;
}

CTS_TEST(testGroup, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<std::array<uint32_t, 2>, 4> testInputs = {{
            {0u, 0u},
            {0xffffffffu, 0xffffffffu},
            {0x01020304u, 0x05060708u},
            {0x785a3c1eu, 0x326496c8u},
        }};
        std::vector<Case> cases;
        for (const auto& v : testInputs) {
            cases.push_back({{u32(v[0]), u32(v[1])}, u32(dot4U8Packed(v[0], v[1]))});
        }
        run(t, builtin("dot4U8Packed"), {scalarType(ScalarKind::U32), scalarType(ScalarKind::U32)},
            scalarType(ScalarKind::U32), inputSourceFromParam(t.param<std::string>("inputSource")),
            0, cases);
    });

} // namespace
