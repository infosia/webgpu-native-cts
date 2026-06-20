// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/i32_conversion.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the i32 conversion operations. Only the exact (integer/bool/abstract-int
// source) conversions are ported here; the float-source conversions (f32/f16/abstract_float) are
// deferred to Stage B because they require the FP-interval/quantization acceptance framework.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"
#include "webgpu/shader/execution/expression/unary/unary_ranges_common.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::unary_ranges;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,i32_conversion",
    "Execution Tests for the i32 conversion operations");

ParamsBuilder allSourceParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder constParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}
// i32(x) for scalar; vecN<i32>(x) for vectorized.
ExpressionBuilder convBuilder(int vec) {
    return conversion(vec == 0 ? "i32" : ("vec" + std::to_string(vec) + "<i32>"));
}

const ExprType I32 = scalarType(ScalarKind::I32);

// kValue.i32.negative.min / positive.max, and the f32-representable extrema (see upstream cache).
constexpr int32_t kI32Min = -2147483647 - 1;          // -2^31
constexpr int32_t kI32Max = 2147483647;               // 2^31 - 1
constexpr double kLargestI32WhichIsAlsoF32 = 2147483520.0; // 0x7fffff80

// An f32 Scalar carrying quantizeToF32(value) (i.e. f32(value)).
Scalar f32From(double value) {
    float f = static_cast<float>(fp::quantize(fp::FPKind::F32, value));
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return f32Bits(bits);
}

CTS_TEST(testGroup, "bool").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases = {
        {{CaseValue(boolean(true))}, CaseValue(i32(1))},
        {{CaseValue(boolean(false))}, CaseValue(i32(0))},
    };
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::Bool)}, I32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "u32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t v : fullU32Range()) {
        cases.push_back({{CaseValue(u32(v))}, CaseValue(i32(static_cast<int32_t>(v)))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::U32)}, I32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "i32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t v : fullI32Range()) {
        cases.push_back({{CaseValue(i32(v))}, CaseValue(i32(v))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {I32}, I32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "abstract_int").params(constParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t v : fullI32Range()) {
        cases.push_back({{CaseValue(abstractInt64(static_cast<int64_t>(v)))}, CaseValue(i32(v))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::AbstractInt)}, I32, InputSource::Const, vec, cases);
});

// i32(f32): round-toward-zero, bit-EXACT (no FP interval). Mirrors the upstream cache exactly.
CTS_TEST(testGroup, "f32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (double f : fp::scalarF32Range()) {
        int32_t expected;
        if (std::abs(f) < 1.0) {
            expected = 0;
        } else if (f <= static_cast<double>(kI32Min)) {
            expected = kI32Min;
        } else if (f >= kLargestI32WhichIsAlsoF32) {
            expected = static_cast<int32_t>(kLargestI32WhichIsAlsoF32);
        } else if (std::abs(f) <= 16777216.0 /* 2^24 */) {
            expected = static_cast<int32_t>(std::trunc(f));
        } else {
            expected = static_cast<int32_t>(std::trunc(fp::quantize(fp::FPKind::F32, f)));
        }
        cases.push_back({{CaseValue(f32From(f))}, CaseValue(i32(expected))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::F32)}, I32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});

// i32(abstract_float): const-eval round-toward-zero at f64 precision, bit-EXACT.
CTS_TEST(testGroup, "abstract_float").params(constParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (double f : fp::scalarF64Range()) {
        int32_t expected;
        if (std::abs(f) < 1.0) {
            expected = 0;
        } else if (f <= static_cast<double>(kI32Min)) {
            expected = kI32Min;
        } else if (f >= static_cast<double>(kI32Max)) {
            expected = kI32Max;
        } else {
            expected = static_cast<int32_t>(std::trunc(f));
        }
        cases.push_back({{CaseValue(abstractFloatValue(f))}, CaseValue(i32(expected))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::AbstractFloat)}, I32, InputSource::Const, vec,
        cases);
});

CTS_TEST(testGroup, "f16").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 source deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});

} // namespace
