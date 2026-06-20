// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/sqrt.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'sqrt' builtin function. sqrt has inherited accuracy (defined via
// 1/inverseSqrt -> a composed ULP interval), so both f32 and abstract use the f32 sqrtInterval.
// f16 is deferred (no Metal oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,sqrt",
    "Execution tests for the 'sqrt' builtin function");

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

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // abstract_const: FP.abstract generator (identity quantize, f64 scalarRange) with the f32
    // sqrtInterval op (inherited accuracy), finite filter. Result materialized at f64 precision.
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, fp::scalarF64Range(), /*finite=*/true,
        [](double n) { return fp::sqrtInterval(fp::FPKind::F32, n); });
    run(t, builtin("sqrt"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, fp::scalarF32Range(), /*finite=*/isConst,
        [](double n) { return fp::sqrtInterval(fp::FPKind::F32, n); });
    run(t, builtin("sqrt"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F16, fp::scalarF16Range(), /*finite=*/isConst,
        [](double n) { return fp::sqrtInterval(fp::FPKind::F16, n); });
    run(t, builtin("sqrt"), {scalarType(ScalarKind::F16)}, scalarType(ScalarKind::F16),
        cfgInputSource(t), cfgVectorize(t), cases);
});
