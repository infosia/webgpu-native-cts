// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/refract.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'refract' builtin function. Signature (vecN, vecN, scalar) -> vecN. Full
// upstream refractInterval (k<0 -> 0 vector; else e3*e1 - (e3*dot(e2,e1)+sqrt(k))*e2). Inherited
// accuracy (abstract as accurate as f32; restricted to 20 cases). f16 deferred (no Metal oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,refract",
    "Execution tests for the 'refract' builtin function");

ParamsBuilder allSources(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder constOnly(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }

ExprType vecAF(int dim) { return vecType(dim, ScalarKind::AbstractFloat); }
ExprType vecF32(int dim) { return vecType(dim, ScalarKind::F32); }
ExprType vecF16(int dim) { return vecType(dim, ScalarKind::F16); }
ExprType afScalar() { return scalarType(ScalarKind::AbstractFloat); }
ExprType f32Scalar() { return scalarType(ScalarKind::F32); }
ExprType f16Scalar() { return scalarType(ScalarKind::F16); }

std::vector<Case> abstractCases(int dim) {
    auto cases = fp::generateRefractCases(fp::FPKind::Abstract, fp::sparseVectorF64Range(dim),
                                          fp::sparseVectorF64Range(dim), fp::sparseScalarF64Range(),
                                          true);
    return fp::selectNCases("refract", 20, cases);
}
std::vector<Case> f32Cases(int dim, bool constStage) {
    return fp::generateRefractCases(fp::FPKind::F32, fp::sparseVectorF32Range(dim),
                                    fp::sparseVectorF32Range(dim), fp::sparseScalarF32Range(),
                                    constStage);
}
std::vector<Case> f16Cases(int dim, bool constStage) {
    return fp::generateRefractCases(fp::FPKind::F16, fp::sparseVectorF16Range(dim),
                                    fp::sparseVectorF16Range(dim), fp::sparseScalarF16Range(),
                                    constStage);
}

} // namespace

CTS_TEST(g, "abstract_float_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("refract"), {vecAF(2), vecAF(2), afScalar()}, vecAF(2), InputSource::Const, 0,
        abstractCases(2));
});
CTS_TEST(g, "abstract_float_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("refract"), {vecAF(3), vecAF(3), afScalar()}, vecAF(3), InputSource::Const, 0,
        abstractCases(3));
});
CTS_TEST(g, "abstract_float_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("refract"), {vecAF(4), vecAF(4), afScalar()}, vecAF(4), InputSource::Const, 0,
        abstractCases(4));
});

CTS_TEST(g, "f32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("refract"), {vecF32(2), vecF32(2), f32Scalar()}, vecF32(2), cfgInputSource(t), 0,
        f32Cases(2, isConst(t)));
});
CTS_TEST(g, "f32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("refract"), {vecF32(3), vecF32(3), f32Scalar()}, vecF32(3), cfgInputSource(t), 0,
        f32Cases(3, isConst(t)));
});
CTS_TEST(g, "f32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("refract"), {vecF32(4), vecF32(4), f32Scalar()}, vecF32(4), cfgInputSource(t), 0,
        f32Cases(4, isConst(t)));
});

CTS_TEST(g, "f16_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    run(t, builtin("refract"), {vecF16(2), vecF16(2), f16Scalar()}, vecF16(2), cfgInputSource(t), 0,
        f16Cases(2, isConst(t)));
});
CTS_TEST(g, "f16_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    run(t, builtin("refract"), {vecF16(3), vecF16(3), f16Scalar()}, vecF16(3), cfgInputSource(t), 0,
        f16Cases(3, isConst(t)));
});
CTS_TEST(g, "f16_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    run(t, builtin("refract"), {vecF16(4), vecF16(4), f16Scalar()}, vecF16(4), cfgInputSource(t), 0,
        f16Cases(4, isConst(t)));
});
