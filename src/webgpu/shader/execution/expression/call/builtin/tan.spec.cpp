// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/tan.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'tan' builtin function. tan = sin(n) / cos(n) (inherited accuracy);
// abstract uses the f32 tanInterval. f16 is deferred (no Metal oracle).

// MSVC does not define M_PI unless _USE_MATH_DEFINES precedes <cmath> (benign on clang/gcc).
#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,tan",
    "Execution tests for the 'tan' builtin function");

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

std::vector<double> tanRange(fp::FPKind kind) {
    std::vector<double> v = fp::linearRange(-M_PI, M_PI, 100);
    const std::vector<double> base = fp::scalarRangeForKind(kind);
    v.insert(v.end(), base.begin(), base.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // tan.cache uses 'unfiltered' for all traits.
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, tanRange(fp::FPKind::Abstract), /*finite=*/false,
        [](double n) { return fp::tanInterval(fp::FPKind::F32, n); });
    run(t, builtin("tan"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, tanRange(fp::FPKind::F32), /*finite=*/false,
        [](double n) { return fp::tanInterval(fp::FPKind::F32, n); });
    run(t, builtin("tan"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F16, tanRange(fp::FPKind::F16), /*finite=*/false,
        [](double n) { return fp::tanInterval(fp::FPKind::F16, n); });
    run(t, builtin("tan"), {scalarType(ScalarKind::F16)}, scalarType(ScalarKind::F16),
        cfgInputSource(t), cfgVectorize(t), cases);
});
