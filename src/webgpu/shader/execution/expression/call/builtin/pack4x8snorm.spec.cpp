// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/pack4x8snorm.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'pack4x8snorm' builtin function.
//
// @const fn pack4x8snorm(e: vec4<f32>) -> u32
// Component e[i] is converted to floor(0.5 + 127 * min(1, max(-1, e[i]))) as an i8,
// then placed in bits 8*i..8*i+7 of the result. The expected u32 is exact.

#include <array>
#include <cmath>
#include <cstdint>

#include "webgpu/shader/execution/expression/call/builtin/pack_unpack_utils.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::packunpack;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,pack4x8snorm",
    "Execution tests for the 'pack4x8snorm' builtin function.");

uint32_t pack4x8snorm(const std::array<double, 4>& vals) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        const double clamped = std::min(1.0, std::max(-1.0, vals[i]));
        const double q = std::floor(0.5 + 127.0 * clamped);
        const int8_t b = static_cast<int8_t>(static_cast<int32_t>(q));
        result |= (static_cast<uint32_t>(static_cast<uint8_t>(b))) << (i * 8);
    }
    return result;
}

CTS_TEST(testGroup, "pack")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const double posMax = kF32::posMax();
        auto normalizeF32 = [&](double n) { return n / posMax; };

        std::vector<Case> cases;
        auto makeCase = [&](std::array<double, 4> vals) {
            for (double& v : vals) {
                v = quantizeToF32(v);
            }
            std::vector<Scalar> els;
            for (double v : vals) {
                els.push_back(f32Bits(f32ToBits(static_cast<float>(v))));
            }
            cases.push_back({{CaseValue::vec(els)}, u32(pack4x8snorm(vals))});
        };

        for (const auto& v : vectorF32Range(4)) {
            makeCase({v[0], v[1], v[2], v[3]});
            makeCase({normalizeF32(v[0]), normalizeF32(v[1]), normalizeF32(v[2]),
                      normalizeF32(v[3])});
        }

        run(t, builtin("pack4x8snorm"), {vecType(4, ScalarKind::F32)}, scalarType(ScalarKind::U32),
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

} // namespace
