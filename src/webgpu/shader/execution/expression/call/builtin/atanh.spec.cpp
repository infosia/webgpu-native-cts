// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atanh.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'atanh' builtin function. atanh = log((1+x)/(1-x)) * 0.5 (inherited
// accuracy); abstract uses the f32 atanhInterval. f16 is deferred (no Metal oracle).

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atanh",
    "Execution tests for the 'atanh' builtin function");

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

// biasedRange(negative.less_than_one, -0.9, 20) ++ [-1] ++
// biasedRange(positive.less_than_one, 0.9, 20) ++ [1] ++ scalarRange().
std::vector<double> atanhRange(fp::FPKind kind) {
    std::vector<double> v = fp::biasedRange(fp::negativeLessThanOne(kind), -0.9, 20);
    v.push_back(-1.0);
    std::vector<double> p = fp::biasedRange(fp::positiveLessThanOne(kind), 0.9, 20);
    v.insert(v.end(), p.begin(), p.end());
    v.push_back(1.0);
    const std::vector<double>& base =
        kind == fp::FPKind::Abstract ? fp::scalarF64Range() : fp::scalarF32Range();
    v.insert(v.end(), base.begin(), base.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, atanhRange(fp::FPKind::Abstract), /*finite=*/true,
        [](double n) { return fp::atanhInterval(fp::FPKind::F32, n); });
    run(t, builtin("atanh"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, atanhRange(fp::FPKind::F32), /*finite=*/isConst,
        [](double n) { return fp::atanhInterval(fp::FPKind::F32, n); });
    run(t, builtin("atanh"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
