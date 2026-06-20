// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/length.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'length' builtin function. length(e) = abs(e) (scalar) or
// sqrt(e[0]^2 + ...) (vector); result is a scalar of the element type. Inherited accuracy (abstract
// as accurate as f32). f16 deferred (no Metal oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,length",
    "Execution tests for the 'length' builtin function");

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

} // namespace

CTS_TEST(g, "abstract_float").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthScalarCases(fp::FPKind::Abstract, fp::scalarF64Range(), true);
    run(t, builtin("length"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(2), true);
    run(t, builtin("length"), {vecAF(2)}, scalarType(ScalarKind::AbstractFloat), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(3), true);
    run(t, builtin("length"), {vecAF(3)}, scalarType(ScalarKind::AbstractFloat), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(4), true);
    run(t, builtin("length"), {vecAF(4)}, scalarType(ScalarKind::AbstractFloat), InputSource::Const, 0, cases);
});

CTS_TEST(g, "f32").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthScalarCases(fp::FPKind::F32, fp::scalarF32Range(), false);
    run(t, builtin("length"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthVectorCases(fp::FPKind::F32, fp::vectorF32Range(2), isConst(t));
    run(t, builtin("length"), {vecF32(2)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthVectorCases(fp::FPKind::F32, fp::vectorF32Range(3), isConst(t));
    run(t, builtin("length"), {vecF32(3)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateLengthVectorCases(fp::FPKind::F32, fp::vectorF32Range(4), isConst(t));
    run(t, builtin("length"), {vecF32(4)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "f16").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateLengthScalarCases(fp::FPKind::F16, fp::scalarF16Range(), false);
    run(t, builtin("length"), {scalarType(ScalarKind::F16)}, scalarType(ScalarKind::F16),
        cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateLengthVectorCases(fp::FPKind::F16, fp::vectorF16Range(2), isConst(t));
    run(t, builtin("length"), {vecF16(2)}, scalarType(ScalarKind::F16), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateLengthVectorCases(fp::FPKind::F16, fp::vectorF16Range(3), isConst(t));
    run(t, builtin("length"), {vecF16(3)}, scalarType(ScalarKind::F16), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateLengthVectorCases(fp::FPKind::F16, fp::vectorF16Range(4), isConst(t));
    run(t, builtin("length"), {vecF16(4)}, scalarType(ScalarKind::F16), cfgInputSource(t), 0, cases);
});
