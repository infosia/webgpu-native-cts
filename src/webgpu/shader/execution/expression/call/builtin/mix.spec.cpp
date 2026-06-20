// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/mix.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'mix' builtin function. mix accepts EITHER x+(y-x)*z (imprecise) OR
// x*(1-z)+y*z (precise), tested via anyOf. Matching (all scalar) and non-matching (vecN args with
// scalar blend factor) forms. Inherited accuracy (abstract as accurate as f32). f16 deferred.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,mix",
    "Execution tests for the 'mix' builtin function");

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
ParamsBuilder sourceOnly(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder constOnly(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// mixIntervals order: imprecise, precise. Inherited accuracy: abstract uses f32 math, f16 uses f16.
std::vector<fp::ScalarTripleToInterval> mixOps(fp::FPKind kind) {
    const fp::FPKind mathKind = kind == fp::FPKind::F16 ? fp::FPKind::F16 : fp::FPKind::F32;
    return {[mathKind](double x, double y, double z) { return fp::mixImpreciseInterval(mathKind, x, y, z); },
            [mathKind](double x, double y, double z) { return fp::mixPreciseInterval(mathKind, x, y, z); }};
}

std::vector<Case> scalarCases(fp::FPKind kind, bool constStage) {
    const std::vector<double>& r = fp::sparseScalarRange(kind);
    std::vector<Case> cases =
        fp::generateScalarTripleToIntervalCases(kind, r, r, r, /*finite=*/constStage, mixOps(kind));
    const size_t n = kind == fp::FPKind::Abstract ? 50 : cases.size();
    return fp::selectNCases("mix_scalar", n, cases);
}

std::vector<Case> vecCases(fp::FPKind kind, int dim, bool constStage) {
    const std::vector<std::vector<double>> vr = fp::sparseVectorRange(kind, dim);
    const std::vector<double>& sr = fp::sparseScalarRange(kind);
    std::vector<Case> cases = fp::generateVectorPairScalarToVectorComponentWiseCase(
        kind, vr, vr, sr, /*finite=*/constStage, mixOps(kind));
    const size_t n = kind == fp::FPKind::Abstract ? 50 : cases.size();
    return fp::selectNCases("mix_vector", n, cases);
}

ExprType vecAF(int dim) { return vecType(dim, ScalarKind::AbstractFloat); }
ExprType vecF32(int dim) { return vecType(dim, ScalarKind::F32); }
ExprType vecF16(int dim) { return vecType(dim, ScalarKind::F16); }

} // namespace

// --- abstract_float ---
CTS_TEST(g, "abstract_float_matching").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = scalarCases(fp::FPKind::Abstract, /*constStage=*/true);
    run(t, builtin("mix"),
        {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractFloat),
         scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_float_nonmatching_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = vecCases(fp::FPKind::Abstract, 2, /*constStage=*/true);
    run(t, builtin("mix"), {vecAF(2), vecAF(2), scalarType(ScalarKind::AbstractFloat)}, vecAF(2),
        InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_nonmatching_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = vecCases(fp::FPKind::Abstract, 3, /*constStage=*/true);
    run(t, builtin("mix"), {vecAF(3), vecAF(3), scalarType(ScalarKind::AbstractFloat)}, vecAF(3),
        InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_nonmatching_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = vecCases(fp::FPKind::Abstract, 4, /*constStage=*/true);
    run(t, builtin("mix"), {vecAF(4), vecAF(4), scalarType(ScalarKind::AbstractFloat)}, vecAF(4),
        InputSource::Const, 0, cases);
});

// --- f32 ---
CTS_TEST(g, "f32_matching").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = scalarCases(fp::FPKind::F32, isConst(t));
    run(t, builtin("mix"),
        {scalarType(ScalarKind::F32), scalarType(ScalarKind::F32), scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f32_nonmatching_vec2").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = vecCases(fp::FPKind::F32, 2, isConst(t));
    run(t, builtin("mix"), {vecF32(2), vecF32(2), scalarType(ScalarKind::F32)}, vecF32(2),
        cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_nonmatching_vec3").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = vecCases(fp::FPKind::F32, 3, isConst(t));
    run(t, builtin("mix"), {vecF32(3), vecF32(3), scalarType(ScalarKind::F32)}, vecF32(3),
        cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_nonmatching_vec4").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = vecCases(fp::FPKind::F32, 4, isConst(t));
    run(t, builtin("mix"), {vecF32(4), vecF32(4), scalarType(ScalarKind::F32)}, vecF32(4),
        cfgInputSource(t), 0, cases);
});

// --- f16 ---
CTS_TEST(g, "f16_matching").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = scalarCases(fp::FPKind::F16, isConst(t));
    run(t, builtin("mix"),
        {scalarType(ScalarKind::F16), scalarType(ScalarKind::F16), scalarType(ScalarKind::F16)},
        scalarType(ScalarKind::F16), cfgInputSource(t), cfgVectorize(t), cases);
});
CTS_TEST(g, "f16_nonmatching_vec2").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = vecCases(fp::FPKind::F16, 2, isConst(t));
    run(t, builtin("mix"), {vecF16(2), vecF16(2), scalarType(ScalarKind::F16)}, vecF16(2),
        cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_nonmatching_vec3").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = vecCases(fp::FPKind::F16, 3, isConst(t));
    run(t, builtin("mix"), {vecF16(3), vecF16(3), scalarType(ScalarKind::F16)}, vecF16(3),
        cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_nonmatching_vec4").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = vecCases(fp::FPKind::F16, 4, isConst(t));
    run(t, builtin("mix"), {vecF16(4), vecF16(4), scalarType(ScalarKind::F16)}, vecF16(4),
        cfgInputSource(t), 0, cases);
});
