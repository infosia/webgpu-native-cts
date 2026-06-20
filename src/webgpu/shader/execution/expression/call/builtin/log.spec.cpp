// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/log.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'log' builtin function. log has abs-error 2^-21 on [0.5,2], else ULP(3)
// for f32, over the >0 domain; abstract uses the f32 logInterval. f16 is deferred (no Metal oracle).

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,log",
    "Execution tests for the 'log' builtin function");

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

// linearRange(positive.min, 0.5, 20) ++ linearRange(0.5, 2.0, 20) ++
// biasedRange(2.0, 2^32, 1000) ++ scalarRange().
std::vector<double> logRange(fp::FPKind kind) {
    const double posMin =
        kind == fp::FPKind::Abstract ? fp::f64PositiveMin() : fp::f32PositiveMin();
    std::vector<double> v = fp::linearRange(posMin, 0.5, 20);
    std::vector<double> a = fp::linearRange(0.5, 2.0, 20);
    v.insert(v.end(), a.begin(), a.end());
    std::vector<double> b = fp::biasedRange(2.0, 4294967296.0, 1000);
    v.insert(v.end(), b.begin(), b.end());
    const std::vector<double>& base =
        kind == fp::FPKind::Abstract ? fp::scalarF64Range() : fp::scalarF32Range();
    v.insert(v.end(), base.begin(), base.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, logRange(fp::FPKind::Abstract), /*finite=*/true,
        [](double n) { return fp::logInterval(fp::FPKind::F32, n); });
    run(t, builtin("log"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, logRange(fp::FPKind::F32), /*finite=*/isConst,
        [](double n) { return fp::logInterval(fp::FPKind::F32, n); });
    run(t, builtin("log"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
