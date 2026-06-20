// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/modf.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'modf' builtin function. modf(e) returns a result_struct {fract, whole};
// the tests read back .fract / .whole separately. fract = correctlyRounded(e % 1.0),
// whole = correctlyRounded(e - (e % 1.0)). f16 deferred (no Metal oracle).

#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,modf",
    "Execution tests for the 'modf' builtin function");

ParamsBuilder allSources(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder constOnly(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}

// modf(v).fract / modf(v).whole expression builders.
ExpressionBuilder fractBuilder() {
    return [](const std::vector<std::string>& v) { return "modf(" + v[0] + ").fract"; };
}
ExpressionBuilder wholeBuilder() {
    return [](const std::vector<std::string>& v) { return "modf(" + v[0] + ").whole"; };
}

ExprType vecAF(int dim) { return vecType(dim, ScalarKind::AbstractFloat); }
ExprType vecF32(int dim) { return vecType(dim, ScalarKind::F32); }
ExprType vecF16(int dim) { return vecType(dim, ScalarKind::F16); }
ExprType afScalar() { return scalarType(ScalarKind::AbstractFloat); }
ExprType f32Scalar() { return scalarType(ScalarKind::F32); }
ExprType f16Scalar() { return scalarType(ScalarKind::F16); }

} // namespace

// --- f32 ---
CTS_TEST(g, "f32_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfScalarCases(fp::FPKind::F32, fp::scalarF32Range(), false);
    run(t, fractBuilder(), {f32Scalar()}, f32Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfScalarCases(fp::FPKind::F32, fp::scalarF32Range(), true);
    run(t, wholeBuilder(), {f32Scalar()}, f32Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec2_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::F32, fp::vectorF32Range(2), false);
    run(t, fractBuilder(), {vecF32(2)}, vecF32(2), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec2_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::F32, fp::vectorF32Range(2), true);
    run(t, wholeBuilder(), {vecF32(2)}, vecF32(2), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec3_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::F32, fp::vectorF32Range(3), false);
    run(t, fractBuilder(), {vecF32(3)}, vecF32(3), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec3_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::F32, fp::vectorF32Range(3), true);
    run(t, wholeBuilder(), {vecF32(3)}, vecF32(3), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec4_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::F32, fp::vectorF32Range(4), false);
    run(t, fractBuilder(), {vecF32(4)}, vecF32(4), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec4_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::F32, fp::vectorF32Range(4), true);
    run(t, wholeBuilder(), {vecF32(4)}, vecF32(4), cfgInputSource(t), 0, cases);
});

// --- f16 ---
CTS_TEST(g, "f16_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfScalarCases(fp::FPKind::F16, fp::scalarF16Range(), false);
    run(t, fractBuilder(), {f16Scalar()}, f16Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfScalarCases(fp::FPKind::F16, fp::scalarF16Range(), true);
    run(t, wholeBuilder(), {f16Scalar()}, f16Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec2_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfVectorCases(fp::FPKind::F16, fp::vectorF16Range(2), false);
    run(t, fractBuilder(), {vecF16(2)}, vecF16(2), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec2_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfVectorCases(fp::FPKind::F16, fp::vectorF16Range(2), true);
    run(t, wholeBuilder(), {vecF16(2)}, vecF16(2), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec3_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfVectorCases(fp::FPKind::F16, fp::vectorF16Range(3), false);
    run(t, fractBuilder(), {vecF16(3)}, vecF16(3), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec3_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfVectorCases(fp::FPKind::F16, fp::vectorF16Range(3), true);
    run(t, wholeBuilder(), {vecF16(3)}, vecF16(3), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec4_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfVectorCases(fp::FPKind::F16, fp::vectorF16Range(4), false);
    run(t, fractBuilder(), {vecF16(4)}, vecF16(4), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f16_vec4_whole").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = fp::generateModfVectorCases(fp::FPKind::F16, fp::vectorF16Range(4), true);
    run(t, wholeBuilder(), {vecF16(4)}, vecF16(4), cfgInputSource(t), 0, cases);
});

// --- abstract_float ---
CTS_TEST(g, "abstract_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfScalarCases(fp::FPKind::Abstract, fp::scalarF64Range(), false);
    run(t, fractBuilder(), {afScalar()}, afScalar(), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_whole").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfScalarCases(fp::FPKind::Abstract, fp::scalarF64Range(), true);
    run(t, wholeBuilder(), {afScalar()}, afScalar(), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_vec2_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(2), false);
    run(t, fractBuilder(), {vecAF(2)}, vecAF(2), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_vec2_whole").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(2), true);
    run(t, wholeBuilder(), {vecAF(2)}, vecAF(2), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_vec3_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(3), false);
    run(t, fractBuilder(), {vecAF(3)}, vecAF(3), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_vec3_whole").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(3), true);
    run(t, wholeBuilder(), {vecAF(3)}, vecAF(3), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_vec4_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(4), false);
    run(t, fractBuilder(), {vecAF(4)}, vecAF(4), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_vec4_whole").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateModfVectorCases(fp::FPKind::Abstract, fp::vectorF64Range(4), true);
    run(t, wholeBuilder(), {vecAF(4)}, vecAF(4), InputSource::Const, 0, cases);
});
