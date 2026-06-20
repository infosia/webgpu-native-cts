// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/sin.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'sin' builtin function. sin has an inherited accuracy (absolute-error
// 2^-11 for f32 over the [-pi, pi] domain); abstract uses the f32 sinInterval. f16 is deferred.

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,sin",
    "Execution tests for the 'sin' builtin function");

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

// linearRange(-pi, pi, 100) ++ scalarRange().
std::vector<double> sinRange(fp::FPKind kind) {
    std::vector<double> v = fp::linearRange(-M_PI, M_PI, 100);
    const std::vector<double>& base =
        kind == fp::FPKind::Abstract ? fp::scalarF64Range() : fp::scalarF32Range();
    v.insert(v.end(), base.begin(), base.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, sinRange(fp::FPKind::Abstract), /*finite=*/true,
        [](double n) { return fp::sinInterval(fp::FPKind::F32, n); });
    run(t, builtin("sin"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, sinRange(fp::FPKind::F32), /*finite=*/false,
        [](double n) { return fp::sinInterval(fp::FPKind::F32, n); });
    run(t, builtin("sin"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
