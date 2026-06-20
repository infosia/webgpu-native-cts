// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/frexp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'frexp' builtin function. frexp(e) returns a result_struct {fract, exp};
// the tests read back .fract (correctly-rounded float in [0.5,1)) / .exp (exact i32 / abstract-int)
// separately. Non-zero subnormal inputs are skipped (impl-defined). f16 deferred (no Metal oracle).

#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,frexp",
    "Execution tests for the 'frexp' builtin function");

ParamsBuilder allSources(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder constOnly(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}

ExpressionBuilder fractBuilder() {
    return [](const std::vector<std::string>& v) { return "frexp(" + v[0] + ").fract"; };
}
ExpressionBuilder expBuilder() {
    return [](const std::vector<std::string>& v) { return "frexp(" + v[0] + ").exp"; };
}

ExprType vecAF(int dim) { return vecType(dim, ScalarKind::AbstractFloat); }
ExprType vecF32(int dim) { return vecType(dim, ScalarKind::F32); }
ExprType vecI32(int dim) { return vecType(dim, ScalarKind::I32); }
ExprType vecAI(int dim) { return vecType(dim, ScalarKind::AbstractInt); }
ExprType afScalar() { return scalarType(ScalarKind::AbstractFloat); }
ExprType aiScalar() { return scalarType(ScalarKind::AbstractInt); }
ExprType f32Scalar() { return scalarType(ScalarKind::F32); }
ExprType i32Scalar() { return scalarType(ScalarKind::I32); }

} // namespace

// --- abstract_float ---
CTS_TEST(g, "abstract_float_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpScalarFractCases(fp::FPKind::Abstract, fp::scalarF64Range());
    run(t, fractBuilder(), {afScalar()}, afScalar(), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_exp").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpScalarExpCases(fp::FPKind::Abstract, fp::scalarF64Range());
    run(t, expBuilder(), {afScalar()}, aiScalar(), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec2_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorFractCases(fp::FPKind::Abstract, fp::vectorF64Range(2));
    run(t, fractBuilder(), {vecAF(2)}, vecAF(2), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec2_exp").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorExpCases(fp::FPKind::Abstract, fp::vectorF64Range(2));
    run(t, expBuilder(), {vecAF(2)}, vecAI(2), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec3_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorFractCases(fp::FPKind::Abstract, fp::vectorF64Range(3));
    run(t, fractBuilder(), {vecAF(3)}, vecAF(3), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec3_exp").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorExpCases(fp::FPKind::Abstract, fp::vectorF64Range(3));
    run(t, expBuilder(), {vecAF(3)}, vecAI(3), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec4_fract").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorFractCases(fp::FPKind::Abstract, fp::vectorF64Range(4));
    run(t, fractBuilder(), {vecAF(4)}, vecAF(4), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec4_exp").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorExpCases(fp::FPKind::Abstract, fp::vectorF64Range(4));
    run(t, expBuilder(), {vecAF(4)}, vecAI(4), InputSource::Const, 0, cases);
});

// --- f32 ---
CTS_TEST(g, "f32_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpScalarFractCases(fp::FPKind::F32, fp::scalarF32Range());
    run(t, fractBuilder(), {f32Scalar()}, f32Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpScalarExpCases(fp::FPKind::F32, fp::scalarF32Range());
    run(t, expBuilder(), {f32Scalar()}, i32Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec2_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorFractCases(fp::FPKind::F32, fp::vectorF32Range(2));
    run(t, fractBuilder(), {vecF32(2)}, vecF32(2), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec2_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorExpCases(fp::FPKind::F32, fp::vectorF32Range(2));
    run(t, expBuilder(), {vecF32(2)}, vecI32(2), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec3_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorFractCases(fp::FPKind::F32, fp::vectorF32Range(3));
    run(t, fractBuilder(), {vecF32(3)}, vecF32(3), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec3_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorExpCases(fp::FPKind::F32, fp::vectorF32Range(3));
    run(t, expBuilder(), {vecF32(3)}, vecI32(3), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec4_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorFractCases(fp::FPKind::F32, fp::vectorF32Range(4));
    run(t, fractBuilder(), {vecF32(4)}, vecF32(4), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec4_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateFrexpVectorExpCases(fp::FPKind::F32, fp::vectorF32Range(4));
    run(t, expBuilder(), {vecF32(4)}, vecI32(4), cfgInputSource(t), 0, cases);
});

// --- f16 (deferred) ---
CTS_TEST(g, "f16_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec2_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec2_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec3_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec3_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec4_fract").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec4_exp").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
