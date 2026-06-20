// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/inverseSqrt.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'inverseSqrt' builtin function. inverseSqrt = 1/sqrt(e) with ULP(2)
// accuracy over the >0 domain for f32; abstract uses the f32 inverseSqrtInterval over a wider
// (f64) input range. f16 is deferred (no Metal oracle).

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,inverseSqrt",
    "Execution tests for the 'inverseSqrt' builtin function");

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

// f32: linearRange(f32.positive.min, 1, 100) ++ biasedRange(1, 2^32, 1000).
std::vector<double> f32Inputs() {
    std::vector<double> v = fp::linearRange(fp::f32PositiveMin(), 1.0, 100);
    std::vector<double> b = fp::biasedRange(1.0, 4294967296.0, 1000);
    v.insert(v.end(), b.begin(), b.end());
    return v;
}
// abstract: linearRange(f64.positive.min, 1, 100) ++ biasedRange(1, 2^64, 100).
std::vector<double> abstractInputs() {
    std::vector<double> v = fp::linearRange(fp::f64PositiveMin(), 1.0, 100);
    std::vector<double> b = fp::biasedRange(1.0, 18446744073709551616.0, 100);
    v.insert(v.end(), b.begin(), b.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, abstractInputs(), /*finite=*/true,
        [](double n) { return fp::inverseSqrtInterval(fp::FPKind::F32, n); });
    run(t, builtin("inverseSqrt"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, f32Inputs(), /*finite=*/false,
        [](double n) { return fp::inverseSqrtInterval(fp::FPKind::F32, n); });
    run(t, builtin("inverseSqrt"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
