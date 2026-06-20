// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/sign.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'sign' builtin function. f32/abstract_float use the correctly-rounded
// signInterval; i32/abstract_int are bit-exact (sign function over the full integer range).
// f16 is deferred (no Metal oracle).

#include <cmath>
#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,sign",
    "Execution tests for the 'sign' builtin function");

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

// fullI32Range: biasedRange(min,-1,50) ++ [0] ++ biasedRange(1,max,50), truncated.
std::vector<int32_t> fullI32Range() {
    std::vector<int32_t> out;
    for (double v : fp::biasedRange(static_cast<double>(INT32_MIN), -1.0, 50)) {
        out.push_back(static_cast<int32_t>(std::trunc(v)));
    }
    out.push_back(0);
    for (double v : fp::biasedRange(1.0, static_cast<double>(INT32_MAX), 50)) {
        out.push_back(static_cast<int32_t>(std::trunc(v)));
    }
    return out;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, fp::scalarF64Range(), /*finite=*/false,
        [](double n) { return fp::signInterval(fp::FPKind::Abstract, n); });
    run(t, builtin("sign"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_int").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int64_t e : fp::fullI64Range()) {
        const int64_t s = e < 0 ? -1 : (e > 0 ? 1 : 0);
        cases.push_back(Case({CaseValue(abstractInt64(e))}, CaseValue(abstractInt64(s))));
    }
    run(t, builtin("sign"), {scalarType(ScalarKind::AbstractInt)}, scalarType(ScalarKind::AbstractInt),
        InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "i32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t e : fullI32Range()) {
        const int32_t s = e < 0 ? -1 : (e > 0 ? 1 : 0);
        cases.push_back(Case({CaseValue(i32(e))}, CaseValue(i32(s))));
    }
    run(t, builtin("sign"), {scalarType(ScalarKind::I32)}, scalarType(ScalarKind::I32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, fp::scalarF32Range(), /*finite=*/false,
        [](double n) { return fp::signInterval(fp::FPKind::F32, n); });
    run(t, builtin("sign"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F16, fp::scalarRangeForKind(fp::FPKind::F16), /*finite=*/false,
        [](double n) { return fp::signInterval(fp::FPKind::F16, n); });
    run(t, builtin("sign"), {scalarType(ScalarKind::F16)}, scalarType(ScalarKind::F16),
        cfgInputSource(t), cfgVectorize(t), cases);
});
