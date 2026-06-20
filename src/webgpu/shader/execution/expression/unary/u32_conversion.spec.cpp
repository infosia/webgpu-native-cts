// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/u32_conversion.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the u32 conversion operations. Only the exact (integer/bool/abstract-int
// source) conversions are ported; the float-source conversions (f32/f16/abstract_float) are
// deferred to Stage B (need the FP-interval/quantization acceptance framework).

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
    "shader,execution,expression,unary,u32_conversion",
    "Execution Tests for the u32 conversion operations");

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
ExpressionBuilder convBuilder(int vec) {
    return conversion(vec == 0 ? "u32" : ("vec" + std::to_string(vec) + "<u32>"));
}

const ExprType U32 = scalarType(ScalarKind::U32);

// kValue.u32.max, and the f32-representable extremum (see upstream cache).
constexpr double kU32Max = 4294967295.0;                   // 2^32 - 1
constexpr double kLargestU32WhichIsF32 = 4294967040.0;     // 0xffffff00

// A u32 Scalar from a non-negative integral double.
Scalar u32From(double value) { return u32(static_cast<uint32_t>(value)); }

// An f32 Scalar carrying quantizeToF32(value) (i.e. f32(value)).
Scalar f32From(double value) {
    float f = static_cast<float>(fp::quantize(fp::FPKind::F32, value));
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return f32Bits(bits);
}

CTS_TEST(testGroup, "bool").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases = {
        {{CaseValue(boolean(true))}, CaseValue(u32(1))},
        {{CaseValue(boolean(false))}, CaseValue(u32(0))},
    };
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::Bool)}, U32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "u32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t v : fullU32Range()) {
        cases.push_back({{CaseValue(u32(v))}, CaseValue(u32(v))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {U32}, U32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "i32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t v : fullI32Range()) {
        cases.push_back({{CaseValue(i32(v))}, CaseValue(u32(static_cast<uint32_t>(v)))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::I32)}, U32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "abstract_int").params(constParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t v : fullU32Range()) {
        cases.push_back({{CaseValue(abstractInt64(static_cast<int64_t>(v)))}, CaseValue(u32(v))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::AbstractInt)}, U32, InputSource::Const, vec, cases);
});

// u32(f32): round-toward-zero, bit-EXACT (no FP interval). Mirrors the upstream cache exactly.
CTS_TEST(testGroup, "f32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (double f : fp::scalarF32Range()) {
        double expected;
        if (f < 1.0) {
            expected = 0.0;
        } else if (f >= kLargestU32WhichIsF32) {
            expected = kLargestU32WhichIsF32;
        } else if (f <= 16777216.0 /* 2^24 */) {
            expected = std::floor(f);
        } else {
            expected = std::floor(fp::quantize(fp::FPKind::F32, f));
        }
        cases.push_back({{CaseValue(f32From(f))}, CaseValue(u32From(expected))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::F32)}, U32,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});

// u32(abstract_float): const-eval round-toward-zero at f64 precision, bit-EXACT.
CTS_TEST(testGroup, "abstract_float").params(constParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<double> inputs = fp::scalarF64Range();
    inputs.push_back(-1.0);
    std::vector<Case> cases;
    for (double f : inputs) {
        double expected;
        if (f < 1.0) {
            expected = 0.0;
        } else if (f >= kU32Max) {
            expected = kU32Max;
        } else {
            expected = std::floor(f);
        }
        cases.push_back({{CaseValue(abstractFloatValue(f))}, CaseValue(u32From(expected))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::AbstractFloat)}, U32, InputSource::Const, vec,
        cases);
});

CTS_TEST(testGroup, "f16").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 source deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});

} // namespace
