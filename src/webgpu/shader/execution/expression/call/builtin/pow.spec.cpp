// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/pow.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'pow' builtin function. pow(x, y) = exp2(y * log2(x)) (inherited
// accuracy); abstract uses the f32 powInterval. f16 is deferred (no Metal oracle).

#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,pow",
    "Execution tests for the 'pow' builtin function");

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
std::vector<double> scalarRange(fp::FPKind kind) {
    return fp::scalarRangeForKind(kind);
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto r = scalarRange(fp::FPKind::Abstract);
    auto cases = fp::generateScalarPairToIntervalCases(
        fp::FPKind::Abstract, r, r, /*finite=*/true,
        [](double x, double y) { return fp::powInterval(fp::FPKind::F32, x, y); });
    run(t, builtin("pow"), {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto r = scalarRange(fp::FPKind::F32);
    auto cases = fp::generateScalarPairToIntervalCases(
        fp::FPKind::F32, r, r, /*finite=*/isConst,
        [](double x, double y) { return fp::powInterval(fp::FPKind::F32, x, y); });
    run(t, builtin("pow"), {scalarType(ScalarKind::F32), scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    auto r = scalarRange(fp::FPKind::F16);
    auto cases = fp::generateScalarPairToIntervalCases(
        fp::FPKind::F16, r, r, /*finite=*/isConst,
        [](double x, double y) { return fp::powInterval(fp::FPKind::F16, x, y); });
    run(t, builtin("pow"), {scalarType(ScalarKind::F16), scalarType(ScalarKind::F16)},
        scalarType(ScalarKind::F16), cfgInputSource(t), cfgVectorize(t), cases);
});
