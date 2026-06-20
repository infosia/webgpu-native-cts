// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/exp2.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'exp2' builtin function. exp2 = 2^n with ULP(3 + 2*|n|) accuracy for f32;
// abstract uses the f32 exp2Interval over the f32 input range. f16 is deferred (no Metal oracle).

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,exp2",
    "Execution tests for the 'exp2' builtin function");

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

// f32_inputs (same range is reused by abstract).
std::vector<double> exp2Inputs() {
    std::vector<double> v = {0.0, -128.0, fp::f32NegativeMin()};
    std::vector<double> a = fp::biasedRange(fp::f32NegativeMax(), -127.0, 100);
    v.insert(v.end(), a.begin(), a.end());
    std::vector<double> b = fp::biasedRange(fp::f32PositiveMin(), 127.0, 100);
    v.insert(v.end(), b.begin(), b.end());
    std::vector<double> c = fp::linearRange(128.0, 1023.0, 10);
    v.insert(v.end(), c.begin(), c.end());
    return v;
}

// f16_inputs (exp2.cache.ts).
std::vector<double> exp2F16Inputs() {
    std::vector<double> v = {0.0, -16.0, fp::f16NegativeMin()};
    std::vector<double> a = fp::biasedRange(fp::f16NegativeMax(), -15.0, 100);
    v.insert(v.end(), a.begin(), a.end());
    std::vector<double> b = fp::biasedRange(fp::f16PositiveMin(), 15.0, 100);
    v.insert(v.end(), b.begin(), b.end());
    std::vector<double> c = fp::linearRange(16.0, 1023.0, 10);
    v.insert(v.end(), c.begin(), c.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, exp2Inputs(), /*finite=*/true,
        [](double n) { return fp::exp2Interval(fp::FPKind::F32, n); });
    run(t, builtin("exp2"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, exp2Inputs(), /*finite=*/isConst,
        [](double n) { return fp::exp2Interval(fp::FPKind::F32, n); });
    run(t, builtin("exp2"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F16, exp2F16Inputs(), /*finite=*/isConst,
        [](double n) { return fp::exp2Interval(fp::FPKind::F16, n); });
    run(t, builtin("exp2"), {scalarType(ScalarKind::F16)}, scalarType(ScalarKind::F16),
        cfgInputSource(t), cfgVectorize(t), cases);
});
