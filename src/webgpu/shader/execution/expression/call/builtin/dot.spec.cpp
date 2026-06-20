// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/dot.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'dot' builtin function. dot(e1, e2) = sum of e1[i]*e2[i]. The f32 +
// abstract_float variants use the order-independent sum-of-products acceptance interval (inherited
// accuracy). The i32/u32/abstract_int variants are bit-exact and depend on the integer vector range
// machinery (vectorI32/U32/I64Range), which belongs to the integer-builtins batch; they are deferred
// here. f16 deferred (no Metal oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,dot",
    "Execution tests for the 'dot' builtin function");

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

// dot uses the dense vectorRange for vec2 and the sparse range for vec3/vec4 (per the cache).
std::vector<std::vector<double>> dotRange(fp::FPKind kind, int dim) {
    return dim == 2 ? fp::vectorRange(kind, 2) : fp::sparseVectorRange(kind, dim);
}

} // namespace

// --- abstract_int / i32 / u32 (deferred: integer vector ranges belong to the integer batch) ---
CTS_TEST(g, "abstract_int_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorI64Range (integer-builtins batch)");
});
CTS_TEST(g, "abstract_int_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorI64Range (integer-builtins batch)");
});
CTS_TEST(g, "abstract_int_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorI64Range (integer-builtins batch)");
});
CTS_TEST(g, "i32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorI32Range (integer-builtins batch)");
});
CTS_TEST(g, "i32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorI32Range (integer-builtins batch)");
});
CTS_TEST(g, "i32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorI32Range (integer-builtins batch)");
});
CTS_TEST(g, "u32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorU32Range (integer-builtins batch)");
});
CTS_TEST(g, "u32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorU32Range (integer-builtins batch)");
});
CTS_TEST(g, "u32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("integer dot deferred: needs vectorU32Range (integer-builtins batch)");
});

// --- abstract_float ---
CTS_TEST(g, "abstract_float_vec2").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::Abstract, dotRange(fp::FPKind::Abstract, 2),
                                      dotRange(fp::FPKind::Abstract, 2), true);
    run(t, builtin("dot"), {vecAF(2), vecAF(2)}, scalarType(ScalarKind::AbstractFloat),
        InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec3").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::Abstract, dotRange(fp::FPKind::Abstract, 3),
                                      dotRange(fp::FPKind::Abstract, 3), true);
    run(t, builtin("dot"), {vecAF(3), vecAF(3)}, scalarType(ScalarKind::AbstractFloat),
        InputSource::Const, 0, cases);
});
CTS_TEST(g, "abstract_float_vec4").params(constOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::Abstract, dotRange(fp::FPKind::Abstract, 4),
                                      dotRange(fp::FPKind::Abstract, 4), true);
    run(t, builtin("dot"), {vecAF(4), vecAF(4)}, scalarType(ScalarKind::AbstractFloat),
        InputSource::Const, 0, cases);
});

// --- f32 ---
CTS_TEST(g, "f32_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::F32, dotRange(fp::FPKind::F32, 2),
                                      dotRange(fp::FPKind::F32, 2), isConst(t));
    run(t, builtin("dot"), {vecF32(2), vecF32(2)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0,
        cases);
});
CTS_TEST(g, "f32_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::F32, dotRange(fp::FPKind::F32, 3),
                                      dotRange(fp::FPKind::F32, 3), isConst(t));
    run(t, builtin("dot"), {vecF32(3), vecF32(3)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0,
        cases);
});
CTS_TEST(g, "f32_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateDotCases(fp::FPKind::F32, dotRange(fp::FPKind::F32, 4),
                                      dotRange(fp::FPKind::F32, 4), isConst(t));
    run(t, builtin("dot"), {vecF32(4), vecF32(4)}, scalarType(ScalarKind::F32), cfgInputSource(t), 0,
        cases);
});

// --- f16 (deferred) ---
CTS_TEST(g, "f16_vec2").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec3").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
CTS_TEST(g, "f16_vec4").params(allSources).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
