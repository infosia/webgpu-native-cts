// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/abs.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'abs' builtin function. u32/i32/abstract_int are bit-exact; f32 and
// abstract_float use the FP-interval framework (abs == correctly-rounded |e|).
// f16 is deferred (no Metal oracle).

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,abs",
    "Execution tests for the 'abs' builtin function");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder constOnlyVectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// kBit.powTwo.toN == 2^N as a u32 bit pattern (N = 0..31); kBit.negPowTwo.toN == -2^N as an i32
// bit pattern. negPowTwo.toN = bit pattern of -(2^N).
uint32_t powTwo(int n) { return 1u << n; }
uint32_t negPowTwo(int n) { return static_cast<uint32_t>(-static_cast<int32_t>(1u << n)); }

} // namespace

CTS_TEST(g, "abstract_int").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int64_t e : fp::fullI64Range()) {
        const int64_t a = e < 0 ? -e : e;
        cases.push_back(Case({CaseValue(abstractInt64(e))}, CaseValue(abstractInt64(a))));
    }
    run(t, builtin("abs"), {scalarType(ScalarKind::AbstractInt)}, scalarType(ScalarKind::AbstractInt),
        InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "u32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    auto add = [&](uint32_t in, uint32_t exp) {
        cases.push_back(Case({CaseValue(u32Bits(in))}, CaseValue(u32Bits(exp))));
    };
    add(0x00000000u, 0x00000000u); // min u32
    add(0xffffffffu, 0xffffffffu); // max u32
    for (int n = 0; n <= 31; ++n) {
        add(powTwo(n), powTwo(n));
    }
    run(t, builtin("abs"), {scalarType(ScalarKind::U32)}, scalarType(ScalarKind::U32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "i32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    auto add = [&](uint32_t in, uint32_t exp) {
        cases.push_back(Case({CaseValue(i32Bits(in))}, CaseValue(i32Bits(exp))));
    };
    // Min/max i32. If e is the largest negative value, the result is e (0x80000000 -> 0x80000000).
    add(0x80000000u, 0x80000000u); // i32 negative.min -> itself
    add(0x00000000u, 0x00000000u); // i32 negative.max (0) -> positive.min (0)
    add(0x7fffffffu, 0x7fffffffu); // i32 positive.max
    add(0x00000000u, 0x00000000u); // i32 positive.min (0)
    // -2^n -> 2^n, n = 0..31.
    for (int n = 0; n <= 31; ++n) {
        add(negPowTwo(n), powTwo(n));
    }
    run(t, builtin("abs"), {scalarType(ScalarKind::I32)}, scalarType(ScalarKind::I32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, fp::scalarF64Range(), /*finite=*/false,
        [](double n) { return fp::absInterval(fp::FPKind::Abstract, n); });
    run(t, builtin("abs"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, fp::scalarF32Range(), /*finite=*/false,
        [](double n) { return fp::absInterval(fp::FPKind::F32, n); });
    run(t, builtin("abs"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
