// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/unpack4x8snorm.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// (and unpack4x8snorm.cache.ts). SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'unpack4x8snorm' builtin function.
//
// @const fn unpack4x8snorm(e: u32) -> vec4<f32>
// Component i is max(v / 127, -1), where v is the i8 interpretation of bits 8*i..8*i+7.
// Acceptance is FP.f32.unpack4x8snormInterval = ulpInterval(value, 3) per component.

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/call/builtin/pack_unpack_utils.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::packunpack;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,unpack4x8snorm",
    "Execution tests for the 'unpack4x8snorm' builtin function.");

CTS_TEST(testGroup, "unpack")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const InputSource src = inputSourceFromParam(t.param<std::string>("inputSource"));

        std::vector<Case> cases;
        for (uint32_t n : fullU32Range()) {
            Case c;
            c.inputs = {CaseValue(u32(n))};
            c.expected = CaseValue::vec({f32Bits(0), f32Bits(0), f32Bits(0), f32Bits(0)});
            c.expectedAccept.reserve(4);
            for (int i = 0; i < 4; ++i) {
                const int8_t byte = static_cast<int8_t>((n >> (i * 8)) & 0xFFu);
                const double value = std::max(static_cast<double>(byte) / 127.0, -1.0);
                const Interval iv = ulpIntervalF32(value, 3.0);
                c.expectedAccept.push_back(acceptInterval(32, iv.lo, iv.hi));
            }
            cases.push_back(std::move(c));
        }

        run(t, builtin("unpack4x8snorm"), {scalarType(ScalarKind::U32)},
            vecType(4, ScalarKind::F32), src, 0, cases);
    });

} // namespace
