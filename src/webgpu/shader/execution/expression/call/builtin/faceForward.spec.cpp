// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/faceForward.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'faceForward' builtin function. faceForward(e1,e2,e3) = select(-e1, e1,
// dot(e2,e3) < 0). Returns an anyOf of the candidate result vectors (the sign at dot==0 is impl-
// defined). Inherited accuracy (abstract as accurate as f32; restricted to 20 cases). f16 deferred.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,faceForward",
    "Execution tests for the 'faceForward' builtin function");

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

std::vector<Case> abstractCases(int dim) {
    auto cases = fp::generateFaceForwardCases(
        fp::FPKind::Abstract, fp::sparseVectorF64Range(dim), fp::sparseVectorF64Range(dim),
        fp::sparseVectorF64Range(dim), true);
    return fp::selectNCases("faceForward", 20, cases);
}
std::vector<Case> f32Cases(int dim, bool constStage) {
    return fp::generateFaceForwardCases(fp::FPKind::F32, fp::sparseVectorF32Range(dim),
                                        fp::sparseVectorF32Range(dim), fp::sparseVectorF32Range(dim),
                                        constStage);
}
std::vector<Case> f16Cases(int dim, bool constStage) {
    return fp::generateFaceForwardCases(fp::FPKind::F16, fp::sparseVectorF16Range(dim),
                                        fp::sparseVectorF16Range(dim), fp::sparseVectorF16Range(dim),
                                        constStage);
}

} // namespace

CTS_TEST(g, "abstract_float_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("faceForward"), {vecAF(2), vecAF(2), vecAF(2)}, vecAF(2), InputSource::Const, 0,
        abstractCases(2));
});
CTS_TEST(g, "abstract_float_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("faceForward"), {vecAF(3), vecAF(3), vecAF(3)}, vecAF(3), InputSource::Const, 0,
        abstractCases(3));
});
CTS_TEST(g, "abstract_float_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("faceForward"), {vecAF(4), vecAF(4), vecAF(4)}, vecAF(4), InputSource::Const, 0,
        abstractCases(4));
});

CTS_TEST(g, "f32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("faceForward"), {vecF32(2), vecF32(2), vecF32(2)}, vecF32(2), cfgInputSource(t), 0,
        f32Cases(2, isConst(t)));
});
CTS_TEST(g, "f32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("faceForward"), {vecF32(3), vecF32(3), vecF32(3)}, vecF32(3), cfgInputSource(t), 0,
        f32Cases(3, isConst(t)));
});
CTS_TEST(g, "f32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    run(t, builtin("faceForward"), {vecF32(4), vecF32(4), vecF32(4)}, vecF32(4), cfgInputSource(t), 0,
        f32Cases(4, isConst(t)));
});

CTS_TEST(g, "f16_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    run(t, builtin("faceForward"), {vecF16(2), vecF16(2), vecF16(2)}, vecF16(2), cfgInputSource(t), 0,
        f16Cases(2, isConst(t)));
});
CTS_TEST(g, "f16_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    run(t, builtin("faceForward"), {vecF16(3), vecF16(3), vecF16(3)}, vecF16(3), cfgInputSource(t), 0,
        f16Cases(3, isConst(t)));
});
CTS_TEST(g, "f16_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    run(t, builtin("faceForward"), {vecF16(4), vecF16(4), vecF16(4)}, vecF16(4), cfgInputSource(t), 0,
        f16Cases(4, isConst(t)));
});
