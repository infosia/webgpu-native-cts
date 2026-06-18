// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/unpack4xI8.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'unpack4xI8' builtin function.

#include <array>
#include <cstdint>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,unpack4xI8",
    "Execution tests for the 'unpack4xI8' builtin function.");

std::array<int32_t, 4> unpack4xI8(uint32_t e) {
    std::array<int32_t, 4> result = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        int32_t intValue = static_cast<int32_t>((e >> (8 * i)) & 0xffu);
        if (intValue > 127) {
            intValue -= 256;
        }
        result[i] = intValue;
    }
    return result;
}

CTS_TEST(testGroup, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<uint32_t, 18> testInputs = {
            0u,          0x01020304u, 0xfcfdfeffu, 0x040302ffu, 0x0403fe01u, 0x04fd0201u,
            0xfc030201u, 0xfcfdfe01u, 0xfcfd02ffu, 0xfc03feffu, 0x04fdfeffu, 0x0403feffu,
            0x04fd02ffu, 0xfc0302ffu, 0x04fdfe01u, 0xfc03fe01u, 0xfcfd0201u, 0x80817f7eu,
        };
        std::vector<Case> cases;
        for (uint32_t e : testInputs) {
            std::array<int32_t, 4> r = unpack4xI8(e);
            cases.push_back({{u32(e)}, vec4(i32(r[0]), i32(r[1]), i32(r[2]), i32(r[3]))});
        }
        run(t, builtin("unpack4xI8"), {scalarType(ScalarKind::U32)}, vecType(4, ScalarKind::I32),
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

} // namespace
