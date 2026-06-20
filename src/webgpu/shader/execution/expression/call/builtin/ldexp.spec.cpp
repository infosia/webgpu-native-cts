// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/ldexp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'ldexp' builtin function. ldexp(e1, e2) = e1 * 2^e2 (correctly-rounded;
// e2 is an i32 / abstract-int). The e2 + bias <= 0 flush-to-zero case also accepts 0. f32 and
// abstract use their own trait. f16 is deferred (no Metal oracle).

#include <cmath>
#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,ldexp",
    "Execution tests for the 'ldexp' builtin function");

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
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// const e2 list: biasedRange(-bias-10, bias+1, 10) -> quantizeToI32.
std::vector<int32_t> constE2(fp::FPKind kind) {
    const int bias = fp::fpBias(kind);
    std::vector<int32_t> out;
    for (double e2 : fp::biasedRange(-bias - 10.0, bias + 1.0, 10)) {
        out.push_back(fp::quantizeToI32(e2));
    }
    return out;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // abstract has only the const stage (non_const is empty/unused). e2 is abstract-int.
    auto cases = fp::generateLdexpCases(fp::FPKind::Abstract, fp::sparseScalarF64Range(),
                                        constE2(fp::FPKind::Abstract), /*finite=*/false,
                                        /*constStage=*/true, /*e2IsAbstractInt=*/true);
    run(t, builtin("ldexp"), {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractInt)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    if (isConst(t)) {
        cases = fp::generateLdexpCases(fp::FPKind::F32, fp::sparseScalarF32Range(),
                                       constE2(fp::FPKind::F32), /*finite=*/false,
                                       /*constStage=*/true, /*e2IsAbstractInt=*/false);
    } else {
        cases = fp::generateLdexpCases(fp::FPKind::F32, fp::sparseScalarF32Range(),
                                       fp::sparseI32Range(), /*finite=*/false,
                                       /*constStage=*/false, /*e2IsAbstractInt=*/false);
    }
    run(t, builtin("ldexp"), {scalarType(ScalarKind::F32), scalarType(ScalarKind::I32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
