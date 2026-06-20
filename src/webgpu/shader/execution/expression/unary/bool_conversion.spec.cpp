// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/bool_conversion.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the boolean conversion operations. Only the exact (bool/u32/i32 source)
// conversions are ported; the float-source conversions (f32/f16) are deferred to Stage B because
// they accept either result for subnormals (anyOf), which needs the FP-aware acceptance framework.

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
    "shader,execution,expression,unary,bool_conversion",
    "Execution Tests for the boolean conversion operations");

ParamsBuilder allSourceParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}
ExpressionBuilder convBuilder(int vec) {
    return conversion(vec == 0 ? "bool" : ("vec" + std::to_string(vec) + "<bool>"));
}

const ExprType BOOL = scalarType(ScalarKind::Bool);

CTS_TEST(testGroup, "bool").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases = {
        {{CaseValue(boolean(true))}, CaseValue(boolean(true))},
        {{CaseValue(boolean(false))}, CaseValue(boolean(false))},
    };
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {BOOL}, BOOL,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "u32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t v : fullU32Range()) {
        cases.push_back({{CaseValue(u32(v))}, CaseValue(boolean(v != 0u))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::U32)}, BOOL,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});
CTS_TEST(testGroup, "i32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t v : fullI32Range()) {
        cases.push_back({{CaseValue(i32(v))}, CaseValue(boolean(v != 0))});
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::I32)}, BOOL,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});

// isSubnormalNumberF32(n) = n in the open interval (f32.negative.max, f32.positive.min) (includes 0).
bool isSubnormalNumberF32(double n) {
    return n > fp::f32NegativeMax() && n < fp::f32PositiveMin();
}

// An f32 Scalar carrying quantizeToF32(value) (i.e. f32(value)).
Scalar f32From(double value) {
    float f = static_cast<float>(fp::quantize(fp::FPKind::F32, value));
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return f32Bits(bits);
}

// bool(f32): false if 0.0/-0.0, true otherwise; subnormals accept either (anyOf). Bit-EXACT.
CTS_TEST(testGroup, "f32").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (double f : fp::scalarF32Range()) {
        std::vector<uint32_t> accept;
        if (f != 0.0) {
            accept.push_back(1u); // bool(true)
        }
        if (isSubnormalNumberF32(f)) {
            accept.push_back(0u); // bool(false)
        }
        // Use the first accepted value as the nominal expected (width only); accept set governs.
        const bool nominal = !accept.empty() && accept[0] == 1u;
        cases.push_back(Case({CaseValue(f32From(f))}, CaseValue(boolean(nominal)),
                             {acceptBitsSet(accept)}));
    }
    const int vec = cfgVectorize(t);
    run(t, convBuilder(vec), {scalarType(ScalarKind::F32)}, BOOL,
        inputSourceFromParam(t.param<std::string>("inputSource")), vec, cases);
});

CTS_TEST(testGroup, "f16").params(allSourceParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 source deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});

} // namespace
