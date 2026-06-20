// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/acosh.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'acosh' builtin function. acosh is accepted by anyOf two formulations:
// acosh_alt = log(x + sqrt((x+1)*(x-1))) and acosh_primary = log(x + sqrt(x*x - 1)) (inherited
// accuracy); abstract uses the f32 formulations. f16 is deferred (no Metal oracle).

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,acosh",
    "Execution tests for the 'acosh' builtin function");

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

// biasedRange(1, 2, 100) ++ scalarRange().
std::vector<double> acoshRange(fp::FPKind kind) {
    std::vector<double> v = fp::biasedRange(1.0, 2.0, 100);
    const std::vector<double> base = fp::scalarRangeForKind(kind);
    v.insert(v.end(), base.begin(), base.end());
    return v;
}

// The cache uses FP[trait!=='abstract'?trait:'f32'].acoshIntervals, so abstract uses F32 and f16
// uses the genuine f16 formulations.
std::vector<fp::ScalarToInterval> acoshOps(fp::FPKind opKind) {
    return {[opKind](double n) { return fp::acoshAlternativeInterval(opKind, n); },
            [opKind](double n) { return fp::acoshPrimaryInterval(opKind, n); }};
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCasesAnyOf(
        fp::FPKind::Abstract, acoshRange(fp::FPKind::Abstract), /*finite=*/true,
        acoshOps(fp::FPKind::F32));
    run(t, builtin("acosh"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCasesAnyOf(
        fp::FPKind::F32, acoshRange(fp::FPKind::F32), /*finite=*/isConst, acoshOps(fp::FPKind::F32));
    run(t, builtin("acosh"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto cases = fp::generateScalarToIntervalCasesAnyOf(
        fp::FPKind::F16, acoshRange(fp::FPKind::F16), /*finite=*/isConst, acoshOps(fp::FPKind::F16));
    run(t, builtin("acosh"), {scalarType(ScalarKind::F16)}, scalarType(ScalarKind::F16),
        cfgInputSource(t), cfgVectorize(t), cases);
});
