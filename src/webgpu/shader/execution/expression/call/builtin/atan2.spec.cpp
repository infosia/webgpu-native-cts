// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atan2.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'atan2' builtin function. atan2(y, x) has ULP(4096) accuracy with a
// domain split and a discontinuity at y/x = 0 (handled via extrema) for f32; abstract uses the f32
// atan2Interval. f16 is deferred (no Metal oracle).

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atan2",
    "Execution tests for the 'atan2' builtin function");

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

// sparseScalarRange() ++ linearRange(negative.max, positive.min, 10). Used for both params.
std::vector<double> atan2Range(fp::FPKind kind) {
    const std::vector<double>& s = fp::sparseScalarRange(kind);
    std::vector<double> v(s.begin(), s.end());
    const double negMax = kind == fp::FPKind::Abstract  ? fp::f64NegativeMax()
                          : kind == fp::FPKind::F16      ? fp::f16NegativeMax()
                                                         : fp::f32NegativeMax();
    const double posMin = kind == fp::FPKind::Abstract  ? fp::f64PositiveMin()
                          : kind == fp::FPKind::F16      ? fp::f16PositiveMin()
                                                         : fp::f32PositiveMin();
    std::vector<double> l = fp::linearRange(negMax, posMin, 10);
    v.insert(v.end(), l.begin(), l.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto r = atan2Range(fp::FPKind::Abstract);
    auto cases = fp::generateScalarPairToIntervalCases(
        fp::FPKind::Abstract, r, r, /*finite=*/true,
        [](double y, double x) { return fp::atan2Interval(fp::FPKind::F32, y, x); });
    run(t, builtin("atan2"), {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto r = atan2Range(fp::FPKind::F32);
    auto cases = fp::generateScalarPairToIntervalCases(
        fp::FPKind::F32, r, r, /*finite=*/isConst,
        [](double y, double x) { return fp::atan2Interval(fp::FPKind::F32, y, x); });
    run(t, builtin("atan2"), {scalarType(ScalarKind::F32), scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto r = atan2Range(fp::FPKind::F16);
    auto cases = fp::generateScalarPairToIntervalCases(
        fp::FPKind::F16, r, r, /*finite=*/isConst,
        [](double y, double x) { return fp::atan2Interval(fp::FPKind::F16, y, x); });
    run(t, builtin("atan2"), {scalarType(ScalarKind::F16), scalarType(ScalarKind::F16)},
        scalarType(ScalarKind::F16), cfgInputSource(t), cfgVectorize(t), cases);
});
