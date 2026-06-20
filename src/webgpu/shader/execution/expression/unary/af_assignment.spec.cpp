// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/af_assignment.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for assignment of AbstractFloats. The 'abstract' test extracts an AbstractFloat
// (subnormals flush to zero); 'f32' concretizes to f32 (correctly rounded); 'f16' concretizes to
// f16 (gated on the shader-f16 feature).

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,af_assignment",
    "Execution Tests for assignment of AbstractFloats");

ParamsBuilder sourceConst(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }

double reinterpretU64AsF64(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

bool isSubnormalF64(double n) {
    if (n == 0.0 || !(n == n)) {
        return false; // +/-0 and NaN are not subnormal
    }
    const double absn = n < 0.0 ? -n : n;
    return absn < reinterpretU64AsF64(0x0010000000000000ull); // < smallest positive normal
}

// Identity assignment builder: emits the value expression unchanged (mirrors upstream
// abstractFloatShaderBuilder(value => `${value}`) / basicExpressionBuilder for the concrete cases).
ExpressionBuilder identity() {
    return [](const std::vector<std::string>& values) { return values[0]; };
}

const ExprType AF = scalarType(ScalarKind::AbstractFloat);
const ExprType F32 = scalarType(ScalarKind::F32);

// The fixed debugging inputs (upstream), then the WebGPU implementation-stressing scalarF64Range().
std::vector<double> abstractInputs() {
    std::vector<double> v = {
        0.0,
        0.5,
        0.5,
        1.0,
        -1.0,
        reinterpretU64AsF64(0x7000000000000001ull), // smallest-magnitude negative subnormal (pattern)
        reinterpretU64AsF64(0x0000000000000001ull), // smallest-magnitude positive subnormal
        reinterpretU64AsF64(0x600aaaaa55555555ull), // negative subnormal, obvious pattern
        reinterpretU64AsF64(0x000aaaaa55555555ull), // positive subnormal, obvious pattern
        reinterpretU64AsF64(0x0010000000000001ull), // smallest-magnitude negative normal (pattern)
        reinterpretU64AsF64(0x0010000000000001ull), // smallest-magnitude positive normal
        reinterpretU64AsF64(0xf5555555aaaaaaaaull), // negative normal, obvious pattern
        reinterpretU64AsF64(0x5555555555555555ull), // positive normal, obvious pattern
        reinterpretU64AsF64(0xffefffffffffffffull), // largest-magnitude negative normal
        reinterpretU64AsF64(0x7fefffffffffffffull), // largest-magnitude positive normal
    };
    for (double f : fp::scalarF64Range()) {
        v.push_back(f);
    }
    return v;
}

// abstract: subnormals flush to 0, otherwise the exact value (correctly-rounded interval).
std::vector<Case> abstractCases() {
    auto op = [](double n) {
        return fp::correctlyRoundedInterval(fp::FPKind::Abstract, isSubnormalF64(n) ? 0.0 : n);
    };
    return fp::generateScalarToIntervalCases(fp::FPKind::Abstract, abstractInputs(),
                                             /*finiteFilter=*/false, op);
}

// f32: AbstractFloat input, correctly-rounded f32 result, over the f32 finite range
// (limitedScalarF64Range(f32.negative.min, f32.positive.max)).
std::vector<Case> f32Cases() {
    const double lo = fp::f32NegativeMin();              // most-negative finite f32
    float maxF;
    const uint32_t maxBits = 0x7f7fffffu;                // largest finite f32 bit pattern
    std::memcpy(&maxF, &maxBits, sizeof(maxF));
    const double hi = static_cast<double>(maxF);
    std::vector<double> inputs;
    for (double f : fp::scalarF64Range()) {
        if (f >= lo && f <= hi) {
            inputs.push_back(f);
        }
    }
    std::vector<Case> cases;
    for (double f : inputs) {
        const fp::FPInterval iv = fp::correctlyRoundedInterval(fp::FPKind::F32, f);
        Case c;
        c.inputs.push_back(CaseValue(abstractFloatValue(f)));
        c.expected = CaseValue(abstractFloatValue(f));
        if (iv.begin == -std::numeric_limits<double>::infinity() &&
            iv.end == std::numeric_limits<double>::infinity()) {
            c.expectedAccept.push_back(acceptUnbounded(32));
        } else {
            c.expectedAccept.push_back(acceptInterval(32, iv.begin, iv.end));
        }
        cases.push_back(std::move(c));
    }
    return cases;
}

} // namespace

CTS_TEST(g, "abstract").params(sourceConst).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = abstractCases();
    run(t, identity(), {AF}, AF, InputSource::Const, 0, cases);
});

CTS_TEST(g, "f32").params(sourceConst).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = f32Cases();
    run(t, identity(), {AF}, F32, InputSource::Const, 0, cases);
});

CTS_TEST(g, "f16").params(sourceConst).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // f16 concretization is out of scope (no Metal oracle); the test exists for listing parity and
    // is skipped unless the shader-f16 feature is present.
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    t.skip("f16 concretization out of scope (no Metal oracle)");
});
