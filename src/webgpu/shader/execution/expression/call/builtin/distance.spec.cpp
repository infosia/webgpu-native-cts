// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/distance.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'distance' builtin function. distance(a,b) = length(a-b); result is a
// scalar. Inherited accuracy (abstract as accurate as f32). f16 deferred (no Metal oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,distance",
    "Execution tests for the 'distance' builtin function");

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
ExprType afScalar() { return scalarType(ScalarKind::AbstractFloat); }
ExprType f32Scalar() { return scalarType(ScalarKind::F32); }

} // namespace

CTS_TEST(g, "abstract_float").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceScalarCases(fp::FPKind::Abstract, fp::scalarF64Range(),
                                                 fp::scalarF64Range(), true);
    run(t, builtin("distance"), {afScalar(), afScalar()}, afScalar(), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceVectorCases(fp::FPKind::Abstract, fp::sparseVectorF64Range(2),
                                                 fp::sparseVectorF64Range(2), true);
    run(t, builtin("distance"), {vecAF(2), vecAF(2)}, afScalar(), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceVectorCases(fp::FPKind::Abstract, fp::sparseVectorF64Range(3),
                                                 fp::sparseVectorF64Range(3), true);
    run(t, builtin("distance"), {vecAF(3), vecAF(3)}, afScalar(), InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceVectorCases(fp::FPKind::Abstract, fp::sparseVectorF64Range(4),
                                                 fp::sparseVectorF64Range(4), true);
    run(t, builtin("distance"), {vecAF(4), vecAF(4)}, afScalar(), InputSource::Const, 0, cases);
});

CTS_TEST(g, "f32").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceScalarCases(fp::FPKind::F32, fp::scalarF32Range(),
                                                 fp::scalarF32Range(), isConst(t));
    run(t, builtin("distance"), {f32Scalar(), f32Scalar()}, f32Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceVectorCases(fp::FPKind::F32, fp::sparseVectorF32Range(2),
                                                 fp::sparseVectorF32Range(2), isConst(t));
    run(t, builtin("distance"), {vecF32(2), vecF32(2)}, f32Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceVectorCases(fp::FPKind::F32, fp::sparseVectorF32Range(3),
                                                 fp::sparseVectorF32Range(3), isConst(t));
    run(t, builtin("distance"), {vecF32(3), vecF32(3)}, f32Scalar(), cfgInputSource(t), 0, cases);
});
CTS_TEST(g, "f32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDistanceVectorCases(fp::FPKind::F32, fp::sparseVectorF32Range(4),
                                                 fp::sparseVectorF32Range(4), isConst(t));
    run(t, builtin("distance"), {vecF32(4), vecF32(4)}, f32Scalar(), cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "f16").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
