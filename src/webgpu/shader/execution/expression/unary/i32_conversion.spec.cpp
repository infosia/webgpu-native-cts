// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/i32_conversion.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the i32 conversion operations. Only the exact (integer/bool/abstract-int
// source) conversions are ported here; the float-source conversions (f32/f16/abstract_float) are
// deferred to Stage B because they require the FP-interval/quantization acceptance framework.

#include <cstdint>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
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

// NOTE: g.test('f32'), g.test('f16'), g.test('abstract_float') are DEFERRED to Stage B (they convert
// from a float source, rounding towards zero with f32/f16 quantization, which needs the FP-interval
// acceptance framework).

} // namespace
