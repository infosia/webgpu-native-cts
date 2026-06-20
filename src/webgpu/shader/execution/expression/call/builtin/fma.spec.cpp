// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/fma.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'fma' builtin function. fma(x,y,z) = additionInterval(mul(x,y), z) —
// NOT a*y then +z with double rounding. Inherited accuracy (abstract as accurate as f32).
// f16 is deferred (no Metal oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,fma",
    "Execution tests for the 'fma' builtin function");

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

std::vector<fp::ScalarTripleToInterval> fmaOps(fp::FPKind kind) {
    const fp::FPKind mathKind = kind == fp::FPKind::F16 ? fp::FPKind::F16 : fp::FPKind::F32;
    return {[mathKind](double x, double y, double z) { return fp::fmaInterval(mathKind, x, y, z); }};
}
std::vector<Case> fmaCases(fp::FPKind kind, bool constStage) {
    const std::vector<double>& r = fp::sparseScalarRange(kind);
    return fp::generateScalarTripleToIntervalCases(kind, r, r, r, /*finite=*/constStage, fmaOps(kind));
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fmaCases(fp::FPKind::Abstract, /*constStage=*/true);
    run(t, builtin("fma"),
        {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractFloat),
         scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fmaCases(fp::FPKind::F32, isConst(t));
    run(t, builtin("fma"),
        {scalarType(ScalarKind::F32), scalarType(ScalarKind::F32), scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fmaCases(fp::FPKind::F16, isConst(t));
    run(t, builtin("fma"),
        {scalarType(ScalarKind::F16), scalarType(ScalarKind::F16), scalarType(ScalarKind::F16)},
        scalarType(ScalarKind::F16), cfgInputSource(t), cfgVectorize(t), cases);
});
