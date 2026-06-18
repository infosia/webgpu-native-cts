// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/dot4I8Packed.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'dot4I8Packed' builtin function.

#include <array>
#include <cstdint>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,dot4I8Packed",
    "Execution tests for the 'dot4I8Packed' builtin function.");

int32_t dot4I8Packed(uint32_t e1, uint32_t e2) {
    int32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        int32_t e1_i = static_cast<int32_t>((e1 >> (i * 8)) & 0xffu);
        if (e1_i >= 128) {
            e1_i -= 256;
        }
        int32_t e2_i = static_cast<int32_t>((e2 >> (i * 8)) & 0xffu);
        if (e2_i >= 128) {
            e2_i -= 256;
        }
        result += e1_i * e2_i;
    }
    return result;
}

CTS_TEST(testGroup, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<std::array<uint32_t, 2>, 8> testInputs = {{
            {0u, 0u},
            {0x7f7f7f7fu, 0x7f7f7f7fu},
            {0x80808080u, 0x80808080u},
            {0x7f7f7f7fu, 0x80808080u},
            {0x01020304u, 0x05060708u},
            {0x01020304u, 0xfffefdfcu},
            {0xfbfaf9f8u, 0x05060708u},
            {0xf7f6f5f4u, 0xf3f2f1f0u},
        }};
        std::vector<Case> cases;
        for (const auto& v : testInputs) {
            cases.push_back({{u32(v[0]), u32(v[1])}, i32(dot4I8Packed(v[0], v[1]))});
        }
        run(t, builtin("dot4I8Packed"), {scalarType(ScalarKind::U32), scalarType(ScalarKind::U32)},
            scalarType(ScalarKind::I32), inputSourceFromParam(t.param<std::string>("inputSource")),
            0, cases);
    });

} // namespace
