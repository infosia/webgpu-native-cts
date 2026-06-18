// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/pack2x16snorm.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'pack2x16snorm' builtin function.
//
// @const fn pack2x16snorm(e: vec2<f32>) -> u32
// Component e[i] is converted to floor(0.5 + 32767 * min(1, max(-1, e[i]))) as an i16,
// then placed in bits 16*i..16*i+15 of the result. The expected u32 is exact.

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
    "shader,execution,expression,call,builtin,pack2x16snorm",
    "Execution tests for the 'pack2x16snorm' builtin function.");

uint32_t pack2x16snorm(double x, double y) {
    auto gen = [](double n) -> uint32_t {
        const double clamped = std::min(1.0, std::max(-1.0, n));
        const double q = std::floor(0.5 + 32767.0 * clamped);
        const int16_t s = static_cast<int16_t>(static_cast<int32_t>(q));
        return static_cast<uint32_t>(static_cast<uint16_t>(s));
    };
    return gen(x) | (gen(y) << 16);
}

CTS_TEST(testGroup, "pack")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const double posMax = kF32::posMax();
        auto normalizeF32 = [&](double n) { return n / posMax; };

        std::vector<Case> cases;
        auto makeCase = [&](double x, double y) {
            x = quantizeToF32(x);
            y = quantizeToF32(y);
            cases.push_back({{vec2(f32Bits(f32ToBits(static_cast<float>(x))),
                                   f32Bits(f32ToBits(static_cast<float>(y))))},
                             u32(pack2x16snorm(x, y))});
        };

        for (const auto& v : vectorF32Range(2)) {
            makeCase(v[0], v[1]);
            makeCase(normalizeF32(v[0]), normalizeF32(v[1]));
        }

        run(t, builtin("pack2x16snorm"), {vecType(2, ScalarKind::F32)}, scalarType(ScalarKind::U32),
            inputSourceFromParam(t.param<std::string>("inputSource")), 0, cases);
    });

} // namespace
