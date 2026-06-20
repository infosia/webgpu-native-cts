// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f32_multiplication.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for non-matrix f32 multiplication (x * y). Accuracy: correctly rounded.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f32_multiplication",
    "Execution Tests for non-matrix f32 multiplication expression");

ParamsBuilder sourceOnly(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder sourceVectorize(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))});
}
ParamsBuilder sourceVectorizeUndef(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder sourceDim(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("dim", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                         Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// FP.f32.multiplicationInterval(x, y).
fp::FPInterval mulOp(double x, double y) { return fp::multiplicationInterval(fp::FPKind::F32, x, y); }

std::vector<Case> scalarCases(bool nonConst) {
    return fp::generateScalarPairToIntervalCases(fp::FPKind::F32, fp::sparseScalarF32Range(),
                                                 fp::sparseScalarF32Range(), /*finite=*/!nonConst,
                                                 mulOp);
}

const ExprType F32 = scalarType(ScalarKind::F32);

} // namespace

CTS_TEST(g, "scalar").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = scalarCases(!isConst(t));
    run(t, binaryOp("*"), {F32, F32}, F32, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "vector").params(sourceVectorize).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = scalarCases(!isConst(t));
    run(t, binaryOp("*"), {F32, F32}, F32, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "scalar_compound").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = scalarCases(!isConst(t));
    runCompound(t, "*=", {F32, F32}, F32, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "vector_scalar").params(sourceDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::F32);
    auto cases = fp::generateVectorScalarToVectorCases(
        fp::FPKind::F32, fp::sparseVectorF32Range(dim), fp::sparseScalarF32Range(),
        /*finite=*/isConst(t), mulOp);
    run(t, binaryOp("*"), {vt, F32}, vt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "vector_scalar_compound").params(sourceDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::F32);
    auto cases = fp::generateVectorScalarToVectorCases(
        fp::FPKind::F32, fp::sparseVectorF32Range(dim), fp::sparseScalarF32Range(),
        /*finite=*/isConst(t), mulOp);
    runCompound(t, "*=", {vt, F32}, vt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "scalar_vector").params(sourceDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::F32);
    auto cases = fp::generateScalarVectorToVectorCases(
        fp::FPKind::F32, fp::sparseScalarF32Range(), fp::sparseVectorF32Range(dim),
        /*finite=*/isConst(t), mulOp);
    run(t, binaryOp("*"), {F32, vt}, vt, cfgInputSource(t), 0, cases);
});
